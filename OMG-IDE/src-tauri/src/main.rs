#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

use std::{
    fs,
    io::{Read, Write},
    process::{ChildStdin, Command, Stdio},
    sync::{Arc, Mutex},
    thread,
};
use tauri::{AppHandle, Emitter, Manager, State, Window};

#[cfg(target_os = "windows")]
fn configure_portable_webview2() {
    let Ok(executable) = std::env::current_exe() else {
        return;
    };
    let Some(directory) = executable.parent() else {
        return;
    };
    let runtime = directory.join("WebView2Runtime");
    if runtime.join("msedgewebview2.exe").is_file() {
        std::env::set_var("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", runtime);
    }
}

struct RunningProgram {
    stdin: ChildStdin,
}

struct ProgramState(Arc<Mutex<Option<RunningProgram>>>);

fn emit_stream<R: Read>(mut stream: R, app: AppHandle) {
    let mut buffer = [0; 4096];
    while let Ok(count) = stream.read(&mut buffer) {
        if count == 0 {
            break;
        }
        let _ = app.emit("omg-output", String::from_utf8_lossy(&buffer[..count]).to_string());
    }
}

#[tauri::command]
fn start_omg(
    app: AppHandle,
    state: State<'_, ProgramState>,
    source: String,
    name: String,
) -> Result<(), String> {
    let dir = std::env::temp_dir().join("omg-ide");
    fs::create_dir_all(&dir).map_err(|error| error.to_string())?;
    let file = dir.join(name);
    fs::write(&file, source).map_err(|error| error.to_string())?;

    let bundled = app.path().resource_dir().ok().map(|dir| dir.join("resources").join("omg.exe"));
    let development = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("resources").join("omg.exe");
    let executable = bundled.filter(|path| path.exists()).unwrap_or(development);

    let mut child = Command::new(executable)
        .arg(file)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|error| error.to_string())?;
    let stdin = child.stdin.take().ok_or("could not open program input")?;
    let stdout = child.stdout.take().ok_or("could not open program output")?;
    let stderr = child.stderr.take().ok_or("could not open program errors")?;
    let sessions = Arc::clone(&state.0);
    {
        let mut active = sessions.lock().map_err(|_| "program input lock failed")?;
        if active.is_some() {
            return Err("a program is already running".into());
        }
        *active = Some(RunningProgram { stdin });
    }

    let output_app = app.clone();
    let error_app = app.clone();
    thread::spawn(move || {
        let output_thread = thread::spawn(move || emit_stream(stdout, output_app));
        let error_thread = thread::spawn(move || emit_stream(stderr, error_app));
        let success = child.wait().map(|status| status.success()).unwrap_or(false);
        let _ = output_thread.join();
        let _ = error_thread.join();
        if let Ok(mut active) = sessions.lock() {
            *active = None;
        }
        let _ = app.emit("omg-finished", success);
    });
    Ok(())
}

#[tauri::command]
fn send_omg_input(state: State<'_, ProgramState>, value: String) -> Result<(), String> {
    let mut active = state.0.lock().map_err(|_| "program input lock failed")?;
    let program = active.as_mut().ok_or("no program is waiting for input")?;
    writeln!(program.stdin, "{value}").map_err(|error| error.to_string())?;
    program.stdin.flush().map_err(|error| error.to_string())
}
#[tauri::command]
fn run_terminal(command: String) -> String {
    Command::new("powershell.exe")
        .args(["-NoProfile", "-Command", &command])
        .output()
        .map(|output| format!("{}{}", String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr)))
        .unwrap_or_else(|error| error.to_string())
}

#[tauri::command]
fn minimize_window(window: Window) -> Result<(), String> {
    window.minimize().map_err(|error| error.to_string())
}

#[tauri::command]
fn toggle_maximize_window(window: Window) -> Result<(), String> {
    if window.is_maximized().map_err(|error| error.to_string())? {
        window.unmaximize().map_err(|error| error.to_string())
    } else {
        window.maximize().map_err(|error| error.to_string())
    }
}

#[tauri::command]
fn close_window(window: Window) -> Result<(), String> {
    window.close().map_err(|error| error.to_string())
}

fn main() {
    #[cfg(target_os = "windows")]
    configure_portable_webview2();

    tauri::Builder::default()
        .manage(ProgramState(Arc::new(Mutex::new(None))))
        .invoke_handler(tauri::generate_handler![
            start_omg,
            send_omg_input,
            run_terminal,
            minimize_window,
            toggle_maximize_window,
            close_window
        ])
        .run(tauri::generate_context!())
        .expect("Tauri error");
}
