#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use chrono::{Local, NaiveDateTime};
use serde::{Deserialize, Serialize};
#[cfg(target_os = "windows")]
use std::os::windows::process::CommandExt;
use std::{
    collections::{BTreeMap, HashMap, HashSet},
    fs,
    path::{Path, PathBuf},
    process::Command,
    sync::Mutex,
    thread,
    time::{Duration, Instant},
};
use tauri::{
    menu::{Menu, MenuItem},
    tray::{TrayIconBuilder, TrayIconEvent},
    AppHandle, Emitter, Manager, State, WindowEvent,
};

#[cfg(target_os = "windows")]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

const APP_NAME: &str = "OMG Network Monitor";
const FIREWALL_RULE_PREFIX: &str = "OMG_NetworkMonitor_";
const FIREWALL_RULE_GROUP: &str = "OMG Network Monitor";
const DEFAULT_CONFIG: &str = r#"# OMG Network Monitor configuration
start_with_windows: true
poll_interval_ms: 1000
keep_closed_seconds: 90
retention_days: 30
upload_alert_mbps: 20
log_connection_events: true
log_minute_metrics: true
start_minimized_with_windows: true
address_display: ip
"#;

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct Config {
    start_with_windows: bool,
    poll_interval_ms: u64,
    keep_closed_seconds: i64,
    retention_days: i64,
    upload_alert_mbps: f64,
    log_connection_events: bool,
    log_minute_metrics: bool,
    start_minimized_with_windows: bool,
    address_display: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            start_with_windows: true,
            poll_interval_ms: 1000,
            keep_closed_seconds: 90,
            retention_days: 30,
            upload_alert_mbps: 20.0,
            log_connection_events: true,
            log_minute_metrics: true,
            start_minimized_with_windows: true,
            address_display: "ip".into(),
        }
    }
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct UiSettings {
    address_display: String,
    poll_interval_seconds: u64,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct Connection {
    id: String,
    protocol: String,
    pid: u32,
    process: String,
    process_path: String,
    local: String,
    remote: String,
    state: String,
    review: bool,
    first_seen: String,
    last_seen: String,
    active: bool,
    blocked: bool,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct AdapterStat {
    name: String,
    received: u64,
    sent: u64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct Snapshot {
    connections: Vec<Connection>,
    adapters: Vec<AdapterStat>,
    download_bps: f64,
    upload_bps: f64,
    total_received: u64,
    total_sent: u64,
    down_history: Vec<f64>,
    up_history: Vec<f64>,
    paused: bool,
    ready: bool,
    status: String,
}

struct MonitorState {
    config: Config,
    connections: BTreeMap<String, Connection>,
    process_cache: HashMap<u32, (String, String)>,
    blocked: HashSet<String>,
    adapters: Vec<AdapterStat>,
    previous_received: u64,
    previous_sent: u64,
    last_sample: Option<Instant>,
    down_history: Vec<f64>,
    up_history: Vec<f64>,
    download_bps: f64,
    upload_bps: f64,
    paused: bool,
    initialized: bool,
    status: String,
    last_metric_minute: i64,
}

impl MonitorState {
    fn new(config: Config, blocked: HashSet<String>) -> Self {
        Self {
            config,
            connections: BTreeMap::new(),
            process_cache: HashMap::new(),
            blocked,
            adapters: Vec::new(),
            previous_received: 0,
            previous_sent: 0,
            last_sample: None,
            down_history: vec![0.0; 120],
            up_history: vec![0.0; 120],
            download_bps: 0.0,
            upload_bps: 0.0,
            paused: false,
            initialized: false,
            status: "正在启动系统级网络监控…".into(),
            last_metric_minute: -1,
        }
    }

    fn snapshot(&self) -> Snapshot {
        let mut connections: Vec<_> = self.connections.values().cloned().collect();
        connections.sort_by(|a, b| {
            b.active
                .cmp(&a.active)
                .then_with(|| b.review.cmp(&a.review))
                .then_with(|| b.last_seen.cmp(&a.last_seen))
        });
        Snapshot {
            connections,
            adapters: self.adapters.clone(),
            download_bps: self.download_bps,
            upload_bps: self.upload_bps,
            total_received: self.previous_received,
            total_sent: self.previous_sent,
            down_history: self.down_history.clone(),
            up_history: self.up_history.clone(),
            paused: self.paused,
            ready: self.initialized,
            status: self.status.clone(),
        }
    }
}

struct AppState {
    data_dir: PathBuf,
    log_dir: PathBuf,
    config_path: PathBuf,
    state: Mutex<MonitorState>,
}

fn data_dir() -> PathBuf {
    dirs_next::data_local_dir()
        .unwrap_or_else(std::env::temp_dir)
        .join(APP_NAME)
}

fn config_path(data_dir: &Path) -> PathBuf {
    data_dir.join("network-monitor.yaml")
}
fn blocklist_path(data_dir: &Path) -> PathBuf {
    data_dir.join("blocked-connections.txt")
}

fn read_config(path: &Path) -> Config {
    let mut config = Config::default();
    let Ok(text) = fs::read_to_string(path) else {
        return config;
    };
    for line in text.lines() {
        let line = line.split('#').next().unwrap_or("").trim();
        let Some((key, value)) = line.split_once(':') else {
            continue;
        };
        let value = value.trim();
        let boolean = |fallback| match value.to_ascii_lowercase().as_str() {
            "true" | "yes" | "1" => true,
            "false" | "no" | "0" => false,
            _ => fallback,
        };
        match key.trim() {
            "start_with_windows" => config.start_with_windows = boolean(config.start_with_windows),
            "poll_interval_ms" => {
                config.poll_interval_ms = value
                    .parse()
                    .map(|v: u64| v.clamp(1_000, 20_000))
                    .unwrap_or(config.poll_interval_ms)
            }
            "keep_closed_seconds" => {
                config.keep_closed_seconds = value
                    .parse()
                    .map(|v: i64| v.clamp(0, 3600))
                    .unwrap_or(config.keep_closed_seconds)
            }
            "retention_days" => {
                config.retention_days = value
                    .parse()
                    .map(|v: i64| v.clamp(1, 3650))
                    .unwrap_or(config.retention_days)
            }
            "upload_alert_mbps" => {
                config.upload_alert_mbps = value
                    .parse::<f64>()
                    .unwrap_or(config.upload_alert_mbps)
                    .max(0.0)
            }
            "log_connection_events" => {
                config.log_connection_events = boolean(config.log_connection_events)
            }
            "log_minute_metrics" => config.log_minute_metrics = boolean(config.log_minute_metrics),
            "start_minimized_with_windows" => {
                config.start_minimized_with_windows = boolean(config.start_minimized_with_windows)
            }
            "address_display" => {
                config.address_display = if value.eq_ignore_ascii_case("url") {
                    "url".into()
                } else {
                    "ip".into()
                }
            }
            _ => {}
        }
    }
    config
}

fn save_config(path: &Path, config: &Config) -> Result<(), String> {
    let text = format!(
        "# OMG Network Monitor configuration\n\
start_with_windows: {}\n\
poll_interval_ms: {}\n\
keep_closed_seconds: {}\n\
retention_days: {}\n\
upload_alert_mbps: {}\n\
log_connection_events: {}\n\
log_minute_metrics: {}\n\
start_minimized_with_windows: {}\n\
address_display: {}\n",
        config.start_with_windows,
        config.poll_interval_ms,
        config.keep_closed_seconds,
        config.retention_days,
        config.upload_alert_mbps,
        config.log_connection_events,
        config.log_minute_metrics,
        config.start_minimized_with_windows,
        config.address_display,
    );
    fs::write(path, text).map_err(|e| e.to_string())
}

fn load_blocklist(path: &Path) -> HashSet<String> {
    fs::read_to_string(path)
        .unwrap_or_default()
        .lines()
        .filter(|line| !line.is_empty())
        .map(str::to_owned)
        .collect()
}

fn save_blocklist(path: &Path, blocklist: &HashSet<String>) -> Result<(), String> {
    let mut keys: Vec<_> = blocklist.iter().collect();
    keys.sort();
    fs::write(
        path,
        keys.into_iter()
            .map(|s| format!("{s}\n"))
            .collect::<String>(),
    )
    .map_err(|e| e.to_string())
}

fn now() -> String {
    Local::now().format("%Y-%m-%dT%H:%M:%S").to_string()
}
fn daily_log_path(dir: &Path) -> PathBuf {
    dir.join(format!("events-{}.jsonl", Local::now().format("%Y-%m-%d")))
}
fn append_log(dir: &Path, value: serde_json::Value) {
    if let Ok(line) = serde_json::to_string(&value) {
        let _ = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(daily_log_path(dir))
            .and_then(|mut file| {
                std::io::Write::write_all(&mut file, format!("{line}\n").as_bytes())
            });
    }
}

fn review_port(remote: &str) -> bool {
    [":1337", ":4444", ":5555", ":6667", ":31337"]
        .iter()
        .any(|port| remote.ends_with(port))
}

fn remote_host(remote: &str) -> Option<&str> {
    let host = remote
        .strip_prefix('[')
        .and_then(|value| value.split(']').next())
        .or_else(|| remote.rsplit_once(':').map(|value| value.0))
        .unwrap_or(remote)
        .trim();
    (!host.is_empty() && host != "*" && host != "0.0.0.0" && host != "::").then_some(host)
}

fn connection_key(process_path: &str, remote: &str) -> String {
    let host = remote_host(remote).unwrap_or("*");
    format!(
        "{}|{}",
        process_path.to_ascii_lowercase(),
        host.to_ascii_lowercase()
    )
}

fn key_belongs_to_program(key: &str, process_path: &str) -> bool {
    if process_path.is_empty() {
        return false;
    }
    key.starts_with(&format!("{}|", process_path.to_ascii_lowercase()))
}

fn is_blocked(blocklist: &HashSet<String>, process_path: &str, remote: &str) -> bool {
    if process_path.is_empty() {
        blocklist.contains(&connection_key(process_path, remote))
    } else {
        blocklist
            .iter()
            .any(|key| key_belongs_to_program(key, process_path))
    }
}

fn firewall_rule_name(key: &str) -> String {
    format!("{FIREWALL_RULE_PREFIX}{:x}", fxhash(key))
}

fn background_command(program: &str) -> Command {
    let mut command = Command::new(program);
    #[cfg(target_os = "windows")]
    command.creation_flags(CREATE_NO_WINDOW);
    command
}

fn command_text(program: &str, args: &[&str]) -> Result<String, String> {
    background_command(program)
        .args(args)
        .output()
        .map_err(|e| e.to_string())
        .and_then(|out| {
            if out.status.success() {
                Ok(String::from_utf8_lossy(&out.stdout).to_string())
            } else {
                Err(String::from_utf8_lossy(&out.stderr).trim().to_string())
            }
        })
}

fn process_info(cache: &mut HashMap<u32, (String, String)>) {
    let script = "Get-Process | Select-Object Id,ProcessName,Path | ConvertTo-Json -Compress";
    let Ok(value) = command_text(
        "powershell",
        &["-NoProfile", "-NonInteractive", "-Command", script],
    ) else {
        return;
    };
    let Ok(json) = serde_json::from_str::<serde_json::Value>(&value) else {
        return;
    };
    let items = json.as_array().cloned().unwrap_or_else(|| vec![json]);
    for item in items {
        let Some(pid) = item.get("Id").and_then(|v| v.as_u64()).map(|v| v as u32) else {
            continue;
        };
        let name = item
            .get("ProcessName")
            .and_then(|v| v.as_str())
            .unwrap_or("Unknown")
            .to_string();
        let path = item
            .get("Path")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();
        cache.insert(pid, (name, path));
    }
}

fn parse_netstat(
    cache: &mut HashMap<u32, (String, String)>,
    blocklist: &HashSet<String>,
) -> Vec<Connection> {
    process_info(cache);
    let Ok(output) = command_text("netstat", &["-ano"]) else {
        return Vec::new();
    };
    let timestamp = now();
    output
        .lines()
        .filter_map(|line| {
            let pieces: Vec<_> = line.split_whitespace().collect();
            let protocol = *pieces.first()?;
            if !protocol.eq_ignore_ascii_case("tcp") && !protocol.eq_ignore_ascii_case("udp") {
                return None;
            }
            let (local, remote, state, pid) = if protocol.eq_ignore_ascii_case("tcp") {
                if pieces.len() < 5 {
                    return None;
                }
                (pieces[1], pieces[2], pieces[3], pieces[4].parse().ok()?)
            } else {
                if pieces.len() < 4 {
                    return None;
                }
                (pieces[1], pieces[2], "LISTEN", pieces[3].parse().ok()?)
            };
            let (process, path) = cache
                .get(&pid)
                .cloned()
                .unwrap_or_else(|| ("Unknown".into(), String::new()));
            let key = format!("{}|{}|{}|{}", protocol.to_uppercase(), pid, local, remote);
            let blocked = is_blocked(blocklist, &path, remote);
            Some(Connection {
                id: key,
                protocol: protocol.to_uppercase(),
                pid,
                process,
                process_path: path.clone(),
                local: local.into(),
                remote: remote.into(),
                state: state.into(),
                review: review_port(remote)
                    || (path.is_empty() && pid > 4 && state.eq_ignore_ascii_case("ESTABLISHED")),
                first_seen: timestamp.clone(),
                last_seen: timestamp.clone(),
                active: true,
                blocked,
            })
        })
        .collect()
}

fn adapter_stats() -> Vec<AdapterStat> {
    let script = "Get-NetAdapterStatistics -ErrorAction SilentlyContinue | Select-Object Name,ReceivedBytes,SentBytes | ConvertTo-Json -Compress";
    let Ok(text) = command_text(
        "powershell",
        &["-NoProfile", "-NonInteractive", "-Command", script],
    ) else {
        return Vec::new();
    };
    let Ok(json) = serde_json::from_str::<serde_json::Value>(&text) else {
        return Vec::new();
    };
    json.as_array()
        .cloned()
        .unwrap_or_else(|| vec![json])
        .into_iter()
        .filter_map(|item| {
            Some(AdapterStat {
                name: item.get("Name")?.as_str()?.to_string(),
                received: item.get("ReceivedBytes")?.as_u64()?,
                sent: item.get("SentBytes")?.as_u64()?,
            })
        })
        .collect()
}

fn cleanup_logs(dir: &Path, retention_days: i64) {
    let cutoff = std::time::SystemTime::now()
        .checked_sub(Duration::from_secs(retention_days.max(1) as u64 * 86_400));
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            if let (Some(cutoff), Ok(meta)) = (cutoff, entry.metadata()) {
                if meta.is_file() && meta.modified().map(|time| time < cutoff).unwrap_or(false) {
                    let _ = fs::remove_file(entry.path());
                }
            }
        }
    }
}

fn refresh(state: &AppState) -> Snapshot {
    // System inspection can take several seconds on Windows. Do it without holding
    // the shared state lock so UI commands never make the window look hung.
    let (blocked, mut process_cache) = {
        let mut monitor = state.state.lock().expect("monitor state poisoned");
        if monitor.paused {
            return monitor.snapshot();
        }
        (
            monitor.blocked.clone(),
            std::mem::take(&mut monitor.process_cache),
        )
    };
    let current = parse_netstat(&mut process_cache, &blocked);
    let adapters = adapter_stats();
    let received: u64 = adapters.iter().map(|a| a.received).sum();
    let sent: u64 = adapters.iter().map(|a| a.sent).sum();
    let mut monitor = state.state.lock().expect("monitor state poisoned");
    monitor.process_cache = process_cache;
    let elapsed = monitor
        .last_sample
        .map(|instant| instant.elapsed().as_secs_f64())
        .unwrap_or(0.0);
    monitor.download_bps = if elapsed > 0.0 && received >= monitor.previous_received {
        (received - monitor.previous_received) as f64 / elapsed
    } else {
        0.0
    };
    monitor.upload_bps = if elapsed > 0.0 && sent >= monitor.previous_sent {
        (sent - monitor.previous_sent) as f64 / elapsed
    } else {
        0.0
    };
    monitor.previous_received = received;
    monitor.previous_sent = sent;
    monitor.last_sample = Some(Instant::now());
    let stamp = now();
    let mut active = HashSet::new();
    for mut connection in current {
        active.insert(connection.id.clone());
        if let Some(previous) = monitor.connections.get_mut(&connection.id) {
            connection.first_seen = previous.first_seen.clone();
            *previous = connection;
        } else {
            if monitor.config.log_connection_events {
                append_log(
                    &state.log_dir,
                    serde_json::json!({"time":stamp,"event":"connection_open","connection":connection}),
                );
            }
            monitor
                .connections
                .insert(connection.id.clone(), connection);
        }
    }
    let log_connection_events = monitor.config.log_connection_events;
    for connection in monitor.connections.values_mut() {
        if connection.active && !active.contains(&connection.id) {
            connection.active = false;
            connection.last_seen = stamp.clone();
            if log_connection_events {
                append_log(
                    &state.log_dir,
                    serde_json::json!({"time":stamp,"event":"connection_close","connection":connection}),
                );
            }
        }
    }
    let cutoff = (Local::now() - chrono::Duration::seconds(monitor.config.keep_closed_seconds))
        .naive_local();
    monitor.connections.retain(|_, connection| {
        connection.active
            || connection.blocked
            || NaiveDateTime::parse_from_str(&connection.last_seen, "%Y-%m-%dT%H:%M:%S")
                .map(|time| time >= cutoff)
                .unwrap_or(true)
    });
    monitor.adapters = adapters;
    let download_bps = monitor.download_bps;
    let upload_bps = monitor.upload_bps;
    monitor.down_history.rotate_left(1);
    if let Some(v) = monitor.down_history.last_mut() {
        *v = download_bps;
    }
    monitor.up_history.rotate_left(1);
    if let Some(v) = monitor.up_history.last_mut() {
        *v = upload_bps;
    }
    let minute = Local::now().timestamp() / 60;
    if minute != monitor.last_metric_minute && monitor.config.log_minute_metrics {
        monitor.last_metric_minute = minute;
        append_log(
            &state.log_dir,
            serde_json::json!({"time":stamp,"event":"minute_metrics","download_bps":monitor.download_bps,"upload_bps":monitor.upload_bps,"total_received":received,"total_sent":sent,"adapters":monitor.adapters}),
        );
    }
    monitor.initialized = true;
    monitor.status = format!("正在监控 {} 个连接", active.len());
    monitor.snapshot()
}

fn emit_refresh(app: &AppHandle) {
    let snapshot = refresh(app.state::<AppState>().inner());
    let _ = app.emit("monitor-update", snapshot);
}

fn start_monitor(app: AppHandle) {
    thread::spawn(move || loop {
        emit_refresh(&app);
        let interval = app
            .state::<AppState>()
            .state
            .lock()
            .map(|m| m.config.poll_interval_ms)
            .unwrap_or(1000);
        thread::sleep(Duration::from_millis(interval));
    });
}

fn configure_autostart(enabled: bool) -> Result<(), String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    if enabled {
        let value = format!("\"{}\" --autostart", exe.display());
        let status = background_command("reg")
            .args([
                "add",
                r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run",
                "/v",
                "OMG Network Monitor",
                "/t",
                "REG_SZ",
                "/d",
                &value,
                "/f",
            ])
            .status()
            .map_err(|e| e.to_string())?;
        if !status.success() {
            return Err("无法写入 Windows 开机启动设置。".into());
        }
    } else {
        let _ = background_command("reg")
            .args([
                "delete",
                r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run",
                "/v",
                "OMG Network Monitor",
                "/f",
            ])
            .status();
    }
    Ok(())
}

#[tauri::command]
fn get_snapshot(state: State<AppState>) -> Snapshot {
    state
        .state
        .lock()
        .expect("monitor state poisoned")
        .snapshot()
}

#[tauri::command]
fn show_main_window(app: AppHandle, state: State<AppState>) -> Result<bool, String> {
    let launched_by_windows = std::env::args_os().any(|arg| arg == "--autostart");
    let start_minimized = state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?
        .config
        .start_minimized_with_windows;
    if launched_by_windows && start_minimized {
        return Ok(false);
    }
    let window = app
        .get_webview_window("main")
        .ok_or_else(|| "主窗口不可用".to_string())?;
    window.unminimize().map_err(|error| error.to_string())?;
    window.center().map_err(|error| error.to_string())?;
    window.show().map_err(|error| error.to_string())?;
    window.set_focus().map_err(|error| error.to_string())?;
    Ok(true)
}

fn ui_settings(config: &Config) -> UiSettings {
    UiSettings {
        address_display: config.address_display.clone(),
        poll_interval_seconds: (config.poll_interval_ms / 1_000).clamp(1, 20),
    }
}

#[tauri::command]
fn get_settings(state: State<AppState>) -> Result<UiSettings, String> {
    let monitor = state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?;
    Ok(ui_settings(&monitor.config))
}

#[tauri::command]
fn update_settings(
    state: State<AppState>,
    address_display: String,
    poll_interval_seconds: u64,
) -> Result<UiSettings, String> {
    let address_display = address_display.to_ascii_lowercase();
    if address_display != "ip" && address_display != "url" {
        return Err("网址显示设置无效。".into());
    }
    if !(1..=20).contains(&poll_interval_seconds) {
        return Err("检测间隔必须在 1 到 20 秒之间。".into());
    }

    let mut config = state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?
        .config
        .clone();
    config.address_display = address_display;
    config.poll_interval_ms = poll_interval_seconds * 1_000;
    save_config(&state.config_path, &config)?;
    state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?
        .config = config.clone();
    Ok(ui_settings(&config))
}

#[tauri::command]
async fn reverse_lookup(ip: String) -> Result<Option<String>, String> {
    let address = ip
        .parse::<std::net::IpAddr>()
        .map_err(|_| "无效的 IP 地址".to_string())?;
    if address.is_unspecified() || address.is_multicast() {
        return Ok(None);
    }
    let original = address.to_string();
    let result = tauri::async_runtime::spawn_blocking(move || dns_lookup::lookup_addr(&address))
        .await
        .map_err(|e| e.to_string())?;
    Ok(result.ok().and_then(|hostname| {
        let hostname = hostname.trim_end_matches('.').to_string();
        (!hostname.is_empty() && !hostname.eq_ignore_ascii_case(&original)).then_some(hostname)
    }))
}

#[tauri::command]
fn set_paused(state: State<AppState>, paused: bool) -> Snapshot {
    let mut monitor = state.state.lock().expect("monitor state poisoned");
    monitor.paused = paused;
    monitor.status = if paused {
        "监控已暂停".into()
    } else {
        "正在恢复监控…".into()
    };
    monitor.snapshot()
}

#[tauri::command]
fn reload_config(state: State<AppState>) -> Result<Config, String> {
    let config = read_config(&state.config_path);
    configure_autostart(config.start_with_windows)?;
    cleanup_logs(&state.log_dir, config.retention_days);
    state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?
        .config = config.clone();
    Ok(config)
}

#[tauri::command]
fn open_config(state: State<AppState>) -> Result<(), String> {
    background_command("notepad.exe")
        .arg(&state.config_path)
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
fn open_logs(state: State<AppState>) -> Result<(), String> {
    background_command("explorer.exe")
        .arg(&state.log_dir)
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
fn copy_connection(connection: Connection) -> String {
    format!(
        "进程: {} (PID {})\n路径: {}\n协议: {}\n本地: {}\n远端: {}\n状态: {}\n首次发现: {}",
        connection.process,
        connection.pid,
        if connection.process_path.is_empty() {
            "不可用"
        } else {
            &connection.process_path
        },
        connection.protocol,
        connection.local,
        connection.remote,
        connection.state,
        connection.first_seen
    )
}

#[tauri::command]
fn export_csv(state: State<AppState>) -> Result<String, String> {
    let snapshot = state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?
        .snapshot();
    let desktop = dirs_next::desktop_dir().unwrap_or_else(|| state.data_dir.clone());
    let path = desktop.join(format!(
        "network-snapshot-{}.csv",
        Local::now().format("%Y%m%d-%H%M%S")
    ));
    let mut writer = csv::WriterBuilder::new()
        .has_headers(true)
        .from_path(&path)
        .map_err(|e| e.to_string())?;
    for item in snapshot.connections {
        writer.serialize(item).map_err(|e| e.to_string())?;
    }
    writer.flush().map_err(|e| e.to_string())?;
    Ok(path.display().to_string())
}

#[tauri::command]
fn open_remote(remote: String) -> Result<(), String> {
    let host = remote
        .strip_prefix('[')
        .and_then(|v| v.split(']').next())
        .unwrap_or_else(|| remote.rsplit_once(':').map(|v| v.0).unwrap_or(&remote));
    let url = format!("http://{host}");
    background_command("explorer.exe")
        .arg(url)
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
fn toggle_firewall(state: State<AppState>, connection: Connection) -> Result<bool, String> {
    let key = connection_key(&connection.process_path, &connection.remote);
    let blocking = {
        let monitor = state
            .state
            .lock()
            .map_err(|_| "监控状态不可用".to_string())?;
        !is_blocked(
            &monitor.blocked,
            &connection.process_path,
            &connection.remote,
        )
    };

    if blocking && connection.process_path.is_empty() {
        return Err("无法读取程序路径，为避免影响其他程序，不能创建阻断规则。".into());
    }
    let host = remote_host(&connection.remote);
    if blocking && host.is_none() {
        return Err("该项目没有具体的远端地址，请选择一个活动的远端连接。".into());
    }

    if !blocking {
        remove_program_firewall_rules(&connection.process_path)?;
        let mut monitor = state
            .state
            .lock()
            .map_err(|_| "监控状态不可用".to_string())?;
        monitor
            .blocked
            .retain(|item| !key_belongs_to_program(item, &connection.process_path));
        for item in monitor.connections.values_mut() {
            if item
                .process_path
                .eq_ignore_ascii_case(&connection.process_path)
            {
                item.blocked = false;
            }
        }
        save_blocklist(&blocklist_path(&state.data_dir), &monitor.blocked)?;
        return Ok(false);
    }

    let rule_name = firewall_rule_name(&key);
    let arguments = vec![
        "advfirewall".into(),
        "firewall".into(),
        "add".into(),
        "rule".into(),
        format!("name={rule_name}"),
        "dir=out".into(),
        "action=block".into(),
        "enable=yes".into(),
        "profile=any".into(),
        "protocol=any".into(),
        format!("group={FIREWALL_RULE_GROUP}"),
        format!("program={}", connection.process_path),
        format!("remoteip={}", host.expect("host checked above")),
    ];
    let result = background_command("netsh.exe")
        .args(&arguments)
        .output()
        .map_err(|e| e.to_string())?;
    if !result.status.success() {
        let detail = String::from_utf8_lossy(&result.stderr).trim().to_string();
        return Err(if detail.is_empty() {
            "防火墙操作失败。请以管理员身份启动应用后重试。".into()
        } else {
            format!("防火墙操作失败：{detail}")
        });
    }

    let mut monitor = state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?;
    monitor.blocked.insert(key);
    for item in monitor.connections.values_mut() {
        if item
            .process_path
            .eq_ignore_ascii_case(&connection.process_path)
        {
            item.blocked = true;
        }
    }
    save_blocklist(&blocklist_path(&state.data_dir), &monitor.blocked)?;
    Ok(true)
}

fn remove_program_firewall_rules(process_path: &str) -> Result<(), String> {
    if process_path.is_empty() {
        return Err("无法读取程序路径，不能安全恢复该程序的联网。".into());
    }
    let script = format!(
        "$ErrorActionPreference = 'Stop'; \
         $target = [Environment]::GetEnvironmentVariable('OMG_TARGET_PROGRAM'); \
         Get-NetFirewallRule -ErrorAction Stop | \
         Where-Object {{ $_.DisplayName -like '{}*' }} | \
         Where-Object {{ (($_ | Get-NetFirewallApplicationFilter).Program) -ieq $target }} | \
         Remove-NetFirewallRule -ErrorAction Stop",
        FIREWALL_RULE_PREFIX
    );
    let output = background_command("powershell.exe")
        .env("OMG_TARGET_PROGRAM", process_path)
        .args(["-NoProfile", "-NonInteractive", "-Command", &script])
        .output()
        .map_err(|error| error.to_string())?;
    if output.status.success() {
        Ok(())
    } else {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        Err(if stderr.is_empty() {
            "恢复联网失败。请以管理员身份启动应用后重试。".into()
        } else {
            format!("恢复联网失败：{stderr}")
        })
    }
}

#[tauri::command]
fn restore_all_firewall(state: State<AppState>) -> Result<(), String> {
    let script = format!(
        "$ErrorActionPreference = 'Stop'; \
         Get-NetFirewallRule -ErrorAction Stop | \
         Where-Object {{ $_.DisplayName -like '{}*' }} | \
         Remove-NetFirewallRule -ErrorAction Stop",
        FIREWALL_RULE_PREFIX
    );
    let output = background_command("powershell.exe")
        .args(["-NoProfile", "-NonInteractive", "-Command", &script])
        .output()
        .map_err(|error| error.to_string())?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        return Err(if stderr.is_empty() {
            "恢复联网失败。请以管理员身份启动应用后重试。".into()
        } else {
            format!("恢复联网失败：{stderr}")
        });
    }

    let mut monitor = state
        .state
        .lock()
        .map_err(|_| "监控状态不可用".to_string())?;
    monitor.blocked.clear();
    for connection in monitor.connections.values_mut() {
        connection.blocked = false;
    }
    save_blocklist(&blocklist_path(&state.data_dir), &monitor.blocked)
}

fn fxhash(value: &str) -> u64 {
    value.bytes().fold(1469598103934665603u64, |hash, byte| {
        (hash ^ byte as u64).wrapping_mul(1099511628211)
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extracts_only_specific_remote_hosts() {
        assert_eq!(remote_host("1.2.3.4:443"), Some("1.2.3.4"));
        assert_eq!(remote_host("[2402:4e00::1]:443"), Some("2402:4e00::1"));
        assert_eq!(remote_host("*:*"), None);
        assert_eq!(remote_host("[::]:0"), None);
        assert_eq!(remote_host("0.0.0.0:0"), None);
    }

    #[test]
    fn one_blocked_endpoint_marks_the_whole_program_recoverable() {
        let program = r"C:\Program Files\Tencent\Weixin\Weixin.exe";
        let mut blocklist = HashSet::new();
        blocklist.insert(connection_key(program, "1.2.3.4:443"));

        assert!(is_blocked(&blocklist, program, "5.6.7.8:443"));
        assert!(!is_blocked(
            &blocklist,
            r"C:\Program Files\Other\Other.exe",
            "1.2.3.4:443"
        ));
    }
}

#[tauri::command]
fn exit_app(app: AppHandle) {
    app.exit(0);
}

fn main() {
    let data_dir = data_dir();
    let _ = fs::create_dir_all(&data_dir);
    let log_dir = data_dir.join("logs");
    let _ = fs::create_dir_all(&log_dir);
    let config_path = config_path(&data_dir);
    if !config_path.exists() {
        let _ = fs::write(&config_path, DEFAULT_CONFIG);
    }
    let config = read_config(&config_path);
    let _ = configure_autostart(config.start_with_windows);
    cleanup_logs(&log_dir, config.retention_days);
    tauri::Builder::default()
        .manage(AppState {
            data_dir: data_dir.clone(),
            log_dir,
            config_path,
            state: Mutex::new(MonitorState::new(
                config,
                load_blocklist(&blocklist_path(&data_dir)),
            )),
        })
        .setup(|app| {
            let show = MenuItem::with_id(app, "show", "显示窗口", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "退出", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show, &quit])?;
            TrayIconBuilder::with_id("monitor-tray")
                .tooltip(APP_NAME)
                .icon(app.default_window_icon().expect("application icon").clone())
                .menu(&menu)
                .on_menu_event(|handle, event| match event.id().as_ref() {
                    "show" => {
                        if let Some(window) = handle.get_webview_window("main") {
                            let _ = window.show();
                            let _ = window.set_focus();
                        }
                    }
                    "quit" => handle.exit(0),
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if matches!(event, TrayIconEvent::DoubleClick { .. }) {
                        if let Some(window) = tray.app_handle().get_webview_window("main") {
                            let _ = window.show();
                            let _ = window.set_focus();
                        }
                    }
                })
                .build(app)?;
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.hide();
            }
            let handle = app.handle().clone();
            start_monitor(handle);
            Ok(())
        })
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                let _ = window.hide();
                api.prevent_close();
            }
        })
        .invoke_handler(tauri::generate_handler![
            get_snapshot,
            show_main_window,
            get_settings,
            update_settings,
            reverse_lookup,
            set_paused,
            reload_config,
            open_config,
            open_logs,
            copy_connection,
            export_csv,
            open_remote,
            toggle_firewall,
            restore_all_firewall,
            exit_app
        ])
        .run(tauri::generate_context!())
        .expect("error while running OMG Network Monitor");
}
