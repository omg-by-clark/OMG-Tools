import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getCurrentWindow } from "@tauri-apps/api/window";
import "./style.css";

const app = document.querySelector("#app");
const hasTauriRuntime = "__TAURI_INTERNALS__" in window;
const appWindow = hasTauriRuntime ? getCurrentWindow() : null;
let snapshot = null;
let query = "";
let selected = null;
let toastTimer;
let settingsOpen = false;
let settings = { addressDisplay: "ip", pollIntervalSeconds: 1 };
let settingsDraft = null;
let startupPhase = "loading";

const SPLASH_INTRO_MS = 1500;
const SPLASH_LOGO_MS = 460;
const SPLASH_EXIT_MS = 260;

const dnsCache = new Map();
const dnsPending = new Set();
const dnsQueued = new Set();
const dnsQueue = [];
let dnsWorkers = 0;
let dnsRenderTimer;

document.querySelector("#window-minimize")?.addEventListener("click", () => appWindow?.minimize());
document.querySelector("#window-maximize")?.addEventListener("click", () => appWindow?.toggleMaximize());
document.querySelector("#window-close")?.addEventListener("click", () => appWindow?.close());
document.querySelector("#window-titlebar")?.addEventListener("dblclick", (event) => {
  if (!event.target.closest(".window-controls")) void appWindow?.toggleMaximize();
});

function errorText(error) {
  if (typeof error === "string") return error;
  if (error instanceof Error && error.message) return error.message;
  return "未知错误";
}

const bytes = (value, perSecond = false) => {
  const units = ["B", "KB", "MB", "GB", "TB"];
  let i = 0;
  while (value >= 1024 && i < units.length - 1) { value /= 1024; i += 1; }
  return `${value.toFixed(i === 0 || value >= 100 ? 0 : 1)} ${units[i]}${perSecond ? "/s" : ""}`;
};

const esc = (text) => String(text ?? "").replace(/[&<>'"]/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[char]);

function graph(values, color) {
  const max = Math.max(1024, ...values) * 1.15;
  const points = values.map((value, index) => `${(index / (values.length - 1)) * 100},${100 - (value / max) * 92}`).join(" ");
  return `<polyline points="${points}" fill="none" stroke="${color}" stroke-width="2.4" vector-effect="non-scaling-stroke"/>`;
}

function endpointParts(remote) {
  if (!remote || remote === "*:*" || remote === "*") return null;
  if (remote.startsWith("[")) {
    const end = remote.indexOf("]");
    if (end < 0) return null;
    return { host: remote.slice(1, end), suffix: remote.slice(end + 1) };
  }
  const separator = remote.lastIndexOf(":");
  if (separator < 1) return { host: remote, suffix: "" };
  return { host: remote.slice(0, separator), suffix: remote.slice(separator) };
}

function isIpAddress(value) {
  return /^(\d{1,3}\.){3}\d{1,3}$/.test(value) || value.includes(":");
}

function displayRemote(row) {
  if (settings.addressDisplay !== "url") return row.remote;
  const endpoint = endpointParts(row.remote);
  if (!endpoint) return row.remote;
  const hostname = dnsCache.get(endpoint.host);
  return hostname ? `${hostname}${endpoint.suffix}` : row.remote;
}

function scheduleDnsRender() {
  if (dnsRenderTimer || settings.addressDisplay !== "url") return;
  dnsRenderTimer = setTimeout(() => {
    dnsRenderTimer = null;
    if (snapshot && settings.addressDisplay === "url") {
      renderConnectionTable();
      renderSelectedPanel();
    }
  }, 120);
}

function pumpDnsQueue() {
  while (dnsWorkers < 4 && dnsQueue.length) {
    const ip = dnsQueue.shift();
    dnsQueued.delete(ip);
    dnsPending.add(ip);
    dnsWorkers += 1;
    invoke("reverse_lookup", { ip })
      .then((hostname) => {
        dnsCache.set(ip, hostname || null);
        if (hostname) scheduleDnsRender();
      })
      .catch(() => dnsCache.set(ip, null))
      .finally(() => {
        dnsPending.delete(ip);
        dnsWorkers -= 1;
        pumpDnsQueue();
      });
  }
}

function queueReverseLookups(connections) {
  if (settings.addressDisplay !== "url") return;
  for (const row of connections) {
    const endpoint = endpointParts(row.remote);
    if (!endpoint || !isIpAddress(endpoint.host)) continue;
    if (dnsCache.has(endpoint.host) || dnsPending.has(endpoint.host) || dnsQueued.has(endpoint.host)) continue;
    dnsQueued.add(endpoint.host);
    dnsQueue.push(endpoint.host);
  }
  pumpDnsQueue();
}

function filteredRows() {
  const term = query.trim().toLowerCase();
  return snapshot.connections.filter((row) => !term || [row.process, row.pid, row.protocol, row.local, row.remote, displayRemote(row), row.state, row.processPath].join(" ").toLowerCase().includes(term));
}

function trafficGraphMarkup() {
  return `<line x1="0" y1="75" x2="100" y2="75"/><line x1="0" y1="50" x2="100" y2="50"/><line x1="0" y1="25" x2="100" y2="25"/>${graph(snapshot.downHistory, "#89b4fa")}${graph(snapshot.upHistory, "#cba6f7")}`;
}

function startupAnimationMarkup() {
  return `<main class="startup-splash" aria-live="polite" aria-label="正在连接监控服务">
    <div class="splash-glow" aria-hidden="true"></div>
    <section class="splash-stage">
      <svg class="connection-loader" viewBox="0 0 760 300" role="img" aria-label="两条网络线路分别形成下载与上传箭头">
        <defs>
          <linearGradient id="download-gradient" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0" stop-color="#74c7ec" stop-opacity=".28" />
            <stop offset=".34" stop-color="#74c7ec" />
            <stop offset="1" stop-color="#89b4fa" />
          </linearGradient>
          <linearGradient id="upload-gradient" x1="1" y1="0" x2="0" y2="1">
            <stop offset="0" stop-color="#f9e2af" stop-opacity=".28" />
            <stop offset=".34" stop-color="#f9e2af" />
            <stop offset="1" stop-color="#fab387" />
          </linearGradient>
          <filter id="arrow-glow" x="-30%" y="-30%" width="160%" height="160%">
            <feGaussianBlur stdDeviation="4" result="blur" />
            <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
          </filter>
        </defs>

        <g class="ambient-network" aria-hidden="true">
          <path d="M77 100 168 126 237 82M530 83l68 44 86-31M91 214l91-31 63 39M525 220l58-38 101 27" />
          <circle cx="77" cy="100" r="3"/><circle cx="168" cy="126" r="3"/><circle cx="237" cy="82" r="3"/>
          <circle cx="530" cy="83" r="3"/><circle cx="598" cy="127" r="3"/><circle cx="684" cy="96" r="3"/>
          <circle cx="91" cy="214" r="3"/><circle cx="182" cy="183" r="3"/><circle cx="245" cy="222" r="3"/>
          <circle cx="525" cy="220" r="3"/><circle cx="583" cy="182" r="3"/><circle cx="684" cy="209" r="3"/>
        </g>
        <g class="arrow-assembly" filter="url(#arrow-glow)" aria-hidden="true">
          <path class="arrow-segment blue-route" pathLength="1" d="M24 150C124 150 206 150 267 150 291 150 304 136 304 109V82" />
          <path class="arrow-body blue-arrow" pathLength="1" d="M304 220 246 160 267 139 290 162V82H318V162L341 139 362 160Z" />
          <path class="arrow-segment yellow-route" pathLength="1" d="M736 150C636 150 554 150 493 150 469 150 456 164 456 191V218" />
          <path class="arrow-body yellow-arrow" pathLength="1" d="M456 80 514 140 493 161 470 138V218H442V138L419 161 398 140Z" />
          <path class="arrow-runner blue-runner blue-route-runner" pathLength="1" d="M24 150C124 150 206 150 267 150 291 150 304 136 304 109V82" />
          <path class="arrow-runner blue-runner blue-arrow-runner" pathLength="1" d="M304 220 246 160 267 139 290 162V82H318V162L341 139 362 160Z" />
          <path class="arrow-runner yellow-runner yellow-route-runner" pathLength="1" d="M736 150C636 150 554 150 493 150 469 150 456 164 456 191V218" />
          <path class="arrow-runner yellow-runner yellow-arrow-runner" pathLength="1" d="M456 80 514 140 493 161 470 138V218H442V138L419 161 398 140Z" />
        </g>
      </svg>
      <div class="splash-status">
        <span class="status-connecting"><i></i>正在连接监控服务</span>
        <span class="status-ready"><i></i>连接就绪</span>
      </div>
    </section>
  </main>`;
}

function connectionRowsMarkup(rows) {
  return rows.map((row) => `<tr class="${row.active ? "" : "closed"} ${row.review ? "review" : ""}" data-id="${esc(row.id)}">
    <td><div class="process"><b>${row.review ? "⚠ " : ""}${esc(row.process)}</b><span>PID ${row.pid}${row.processPath ? ` · ${esc(row.processPath)}` : ""}</span></div></td>
    <td><code>${esc(row.protocol)}</code></td><td>${esc(row.local)}</td><td class="remote-address" title="IP: ${esc(row.remote)}">${esc(displayRemote(row))}</td>
    <td><span class="state ${row.active ? "live" : ""}">${esc(row.active ? row.state : "CLOSED")}</span></td><td>${esc(row.firstSeen.slice(11))}</td>
    <td><button class="icon-button" title="操作" data-action="select" data-id="${esc(row.id)}">···</button></td>
  </tr>`).join("") || `<tr><td colspan="7" class="empty">没有符合条件的网络连接</td></tr>`;
}

function setText(selector, value) {
  const target = document.querySelector(selector);
  if (target) target.textContent = value;
}

function renderConnectionTable() {
  if (!snapshot) return;
  const rows = filteredRows();
  setText("#connection-summary", `${rows.length} 条结果 · 关闭的连接会暂存一段时间`);
  setText("#remote-heading", settings.addressDisplay === "url" ? "远端 URL" : "远端地址");
  const body = document.querySelector("#connection-rows");
  if (body) body.innerHTML = connectionRowsMarkup(rows);
  queueReverseLookups(snapshot.connections);
}

function renderSelectedPanel() {
  const current = document.querySelector(".action-panel");
  if (!selected) {
    current?.remove();
    return;
  }
  if (current) current.outerHTML = actionPanel(selected);
}

function renderLiveUpdate() {
  if (!snapshot || !document.querySelector("#connection-rows")) {
    render();
    return;
  }

  const active = snapshot.connections.filter((item) => item.active).length;
  const attention = snapshot.connections.filter((item) => item.review).length;
  setText("#download-speed", bytes(snapshot.downloadBps, true));
  setText("#download-total", `累计 ${bytes(snapshot.totalReceived)}`);
  setText("#upload-speed", bytes(snapshot.uploadBps, true));
  setText("#upload-total", `累计 ${bytes(snapshot.totalSent)}`);
  setText("#active-count", active);
  setText("#connection-count", `发现 ${snapshot.connections.length} 个连接`);
  setText("#attention-count", attention);

  const pauseButton = document.querySelector("#pause-button");
  if (pauseButton) {
    pauseButton.textContent = snapshot.paused ? "恢复监控" : "暂停监控";
    pauseButton.classList.toggle("primary", snapshot.paused);
  }
  document.querySelector("#attention-card")?.classList.toggle("attention", attention > 0);
  const chart = document.querySelector("#traffic-chart");
  if (chart) chart.innerHTML = trafficGraphMarkup();

  renderConnectionTable();
  renderSelectedPanel();
}

function settingsModal() {
  if (!settingsOpen || !settingsDraft) return "";
  const interval = settingsDraft.pollIntervalSeconds;
  const progress = ((interval - 1) / 19) * 100;
  return `<div class="settings-overlay" role="presentation">
    <section class="settings-dialog" role="dialog" aria-modal="true" aria-labelledby="settings-title">
      <header class="settings-header"><div><h2 id="settings-title">设置</h2><p>网络监控的显示与刷新选项</p></div><button class="settings-close" data-action="settings-close" aria-label="关闭设置">×</button></header>
      <div class="settings-content">
        <fieldset class="settings-section">
          <legend>网址显示设置</legend>
          <div class="radio-options">
            <button class="radio-option ${settingsDraft.addressDisplay === "ip" ? "selected" : ""}" data-action="set-address-display" data-value="ip"><span class="radio-dot"></span><span><b>IP</b><small>显示原始远端 IP 地址</small></span></button>
            <button class="radio-option ${settingsDraft.addressDisplay === "url" ? "selected" : ""}" data-action="set-address-display" data-value="url"><span class="radio-dot"></span><span><b>URL</b><small>通过反向 DNS 显示主机名</small></span></button>
          </div>
          <p class="settings-hint">无法反向解析的地址会自动回退显示 IP。</p>
        </fieldset>
        <fieldset class="settings-section interval-setting">
          <div class="setting-label"><legend>检测间隔时间</legend><output id="interval-value">${interval} 秒</output></div>
          <input id="poll-interval" type="range" min="1" max="20" step="1" value="${interval}" style="--range-progress:${progress}%" aria-label="检测间隔时间" />
          <div class="range-labels"><span>1 秒</span><span>20 秒</span></div>
        </fieldset>
      </div>
      <footer class="settings-actions"><button data-action="settings-close">取消</button><button class="primary" data-action="settings-save">保存设置</button></footer>
    </section>
  </div>`;
}

function render() {
  if (startupPhase === "loading") { app.innerHTML = startupAnimationMarkup(); return; }
  if (!snapshot) { app.innerHTML = `<main class="loading">正在连接监控服务…</main>`; return; }
  const rows = filteredRows();
  const active = snapshot.connections.filter((item) => item.active).length;
  const attention = snapshot.connections.filter((item) => item.review).length;
  const remoteHeading = settings.addressDisplay === "url" ? "远端 URL" : "远端地址";
  app.innerHTML = `
    <main>
      <header class="topbar">
        <div class="header-actions">
          <button id="pause-button" data-action="pause" class="${snapshot.paused ? "primary" : ""}">${snapshot.paused ? "恢复监控" : "暂停监控"}</button>
          <button data-action="restore-firewall" title="移除本工具创建的全部阻断规则">恢复全部联网</button>
          <button data-action="reload">重载配置</button>
          <button data-action="logs">日志</button>
          <button data-action="settings" title="设置 (Ctrl+,)">设置</button>
        </div>
      </header>
      <section class="metrics">
        <article><span>下载速度</span><strong id="download-speed">${bytes(snapshot.downloadBps, true)}</strong><small id="download-total">累计 ${bytes(snapshot.totalReceived)}</small></article>
        <article><span>上传速度</span><strong id="upload-speed">${bytes(snapshot.uploadBps, true)}</strong><small id="upload-total">累计 ${bytes(snapshot.totalSent)}</small></article>
        <article><span>活动连接</span><strong id="active-count">${active}</strong><small id="connection-count">发现 ${snapshot.connections.length} 个连接</small></article>
        <article id="attention-card" class="${attention ? "attention" : ""}"><span>建议复核</span><strong id="attention-count">${attention}</strong><small>常见端口或路径不可用</small></article>
      </section>
      <section class="chart-card">
        <div class="card-title"><div><h2>最近两分钟流量</h2><p>系统全部活动网卡的实时吞吐</p></div><div class="legend"><span><i class="down"></i>下载</span><span><i class="up"></i>上传</span></div></div>
        <svg id="traffic-chart" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="网络速度曲线">${trafficGraphMarkup()}</svg>
      </section>
      <section class="connections">
        <div class="connection-head"><div><h2>网络连接</h2><p id="connection-summary">${rows.length} 条结果 · 关闭的连接会暂存一段时间</p></div><div class="table-tools"><input id="search" value="${esc(query)}" placeholder="搜索进程、IP、URL、端口或状态" /><button data-action="export">导出 CSV</button></div></div>
        <div class="table-wrap"><table><thead><tr><th>进程</th><th>协议</th><th>本地地址</th><th id="remote-heading">${remoteHeading}</th><th>状态</th><th>首次发现</th><th></th></tr></thead>
          <tbody id="connection-rows">${connectionRowsMarkup(rows)}</tbody></table></div>
      </section>
      ${selected ? actionPanel(selected) : ""}
      ${settingsModal()}
      <div id="toast"></div>
    </main>`;
  document.querySelector("#search")?.addEventListener("input", (event) => {
    query = event.target.value;
    renderConnectionTable();
  });
  queueReverseLookups(snapshot.connections);
}

function renderStartupError(error) {
  app.innerHTML = `<main class="startup-error">
    <section><div class="error-mark">!</div><h1>界面初始化失败</h1><p>${esc(errorText(error))}</p><button data-action="retry-startup">重试</button></section>
  </main>`;
}

function actionPanel(row) {
  return `<aside class="action-panel"><div><b>${esc(row.process)} <span>PID ${row.pid}</span></b><p>${esc(displayRemote(row))} · ${esc(row.state)}</p></div><div class="panel-actions">
    <button data-action="copy">复制详情</button><button data-action="remote" ${row.remote === "*:*" ? "disabled" : ""}>打开地址</button>
    <button class="${row.blocked ? "danger-outline" : "danger"}" data-action="firewall">${row.blocked ? "恢复程序联网" : "阻断此连接"}</button><button data-action="dismiss">关闭</button>
  </div></aside>`;
}

function toast(message, error = false) {
  const target = document.querySelector("#toast");
  if (!target) return;
  target.textContent = message;
  target.className = error ? "show error" : "show";
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { target.className = ""; }, 3800);
}

async function openSettings() {
  settings = await invoke("get_settings");
  settingsDraft = { ...settings };
  settingsOpen = true;
  render();
}

document.addEventListener("input", (event) => {
  if (event.target.id !== "poll-interval" || !settingsDraft) return;
  const value = Number(event.target.value);
  settingsDraft.pollIntervalSeconds = value;
  event.target.style.setProperty("--range-progress", `${((value - 1) / 19) * 100}%`);
  const output = document.querySelector("#interval-value");
  if (output) output.textContent = `${value} 秒`;
});

document.addEventListener("click", async (event) => {
  if (event.target.classList.contains("settings-overlay")) {
    settingsOpen = false;
    render();
    return;
  }
  const actionElement = event.target.closest("[data-action]");
  const action = actionElement?.dataset.action;
  if (!action) return;
  try {
    if (action === "pause") { snapshot = await invoke("set_paused", { paused: !snapshot.paused }); render(); }
    if (action === "reload") {
      const config = await invoke("reload_config");
      settings = { addressDisplay: config.addressDisplay, pollIntervalSeconds: Math.round(config.pollIntervalMs / 1000) };
      render();
      toast("配置已重新载入");
    }
    if (action === "restore-firewall") {
      await invoke("restore_all_firewall");
      snapshot = await invoke("get_snapshot");
      selected = null;
      render();
      toast("已移除本工具的全部阻断规则");
    }
    if (action === "logs") await invoke("open_logs");
    if (action === "settings") await openSettings();
    if (action === "settings-close") { settingsOpen = false; render(); }
    if (action === "set-address-display") {
      settingsDraft.addressDisplay = actionElement.dataset.value;
      render();
    }
    if (action === "settings-save") {
      settings = await invoke("update_settings", {
        addressDisplay: settingsDraft.addressDisplay,
        pollIntervalSeconds: settingsDraft.pollIntervalSeconds,
      });
      settingsOpen = false;
      render();
      toast("设置已保存");
    }
    if (action === "export") toast(`已导出：${await invoke("export_csv")}`);
    if (action === "select") { selected = snapshot.connections.find((item) => item.id === actionElement.dataset.id); render(); }
    if (action === "dismiss") { selected = null; render(); }
    if (action === "copy") { const text = await invoke("copy_connection", { connection: selected }); await navigator.clipboard.writeText(text); toast("连接详情已复制"); }
    if (action === "remote") await invoke("open_remote", { remote: displayRemote(selected) });
    if (action === "firewall") { const blocked = await invoke("toggle_firewall", { connection: selected }); toast(blocked ? "已阻断此连接" : "已恢复该程序联网"); selected.blocked = blocked; render(); }
    if (action === "retry-startup") await initialize();
  } catch (error) {
    toast(errorText(error), true);
  }
});

window.addEventListener("keydown", (event) => {
  if (event.ctrlKey && (event.key === "," || event.code === "Comma")) {
    event.preventDefault();
    void openSettings().catch((error) => toast(errorText(error), true));
  }
  if (event.key === "Escape" && settingsOpen) {
    settingsOpen = false;
    render();
  }
}, true);

render();

let listening = false;
const wait = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
const nextFrame = () => new Promise((resolve) => requestAnimationFrame(resolve));

async function getReadySnapshot() {
  let current = await invoke("get_snapshot");
  while (!current.ready) {
    await wait(80);
    current = await invoke("get_snapshot");
  }
  return current;
}

async function initialize() {
  startupPhase = "loading";
  snapshot = null;
  render();
  try {
    if (!listening) {
      await listen("monitor-update", (event) => {
        snapshot = event.payload;
        if (selected) selected = snapshot.connections.find((item) => item.id === selected.id) ?? null;
        if (startupPhase === "ready") renderLiveUpdate();
      });
      listening = true;
    }
    [snapshot, settings] = await Promise.all([getReadySnapshot(), invoke("get_settings"), document.fonts?.ready]);

    // A hidden WebView may suspend requestAnimationFrame entirely.  Showing the
    // native window has to happen before waiting for a paint frame, otherwise
    // startup can deadlock on an empty window.
    const shown = await invoke("show_main_window");
    if (!shown) {
      startupPhase = "ready";
      render();
      return;
    }
    await nextFrame();
    document.querySelector(".startup-splash")?.classList.add("is-playing");
    await wait(SPLASH_INTRO_MS);
    document.querySelector(".startup-splash")?.classList.add("data-ready");
    await wait(SPLASH_LOGO_MS);
    document.querySelector(".startup-splash")?.classList.add("is-exiting");
    await wait(SPLASH_EXIT_MS);
    startupPhase = "ready";
    app.classList.add("interface-reveal");
    render();
    setTimeout(() => app.classList.remove("interface-reveal"), 650);
  } catch (error) {
    startupPhase = "error";
    renderStartupError(error);
  }
}

window.addEventListener("error", (event) => {
  if (!snapshot) renderStartupError(event.error ?? event.message);
});
window.addEventListener("unhandledrejection", (event) => {
  if (!snapshot) renderStartupError(event.reason);
});

if (hasTauriRuntime) {
  void initialize();
} else {
  render();
  document.querySelector(".startup-splash")?.classList.add("is-playing");
  setTimeout(() => document.querySelector(".startup-splash")?.classList.add("data-ready"), SPLASH_INTRO_MS);
}
