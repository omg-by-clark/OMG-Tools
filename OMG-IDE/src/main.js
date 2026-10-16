// OMG-IDE - An IDE for the OMG programming language.
// Copyright (C) 2026 Chi (Clark) Zhang. GPL-3.0-or-later.

import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import completionTemplatesYaml from "../omg-completions.yaml?raw";
import { OmgEditor } from "./custom-editor.js";
import "./style.css";

const icon = name => {
  const paths = {
    file: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/>',
    folder: '<path d="M3 7h5l2 2h11v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="M3 7V5a2 2 0 0 1 2-2h4l2 2h4"/>',
    save: '<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><path d="M17 21v-8H7v8M7 3v5h8"/>',
    magic: '<path d="m15 4 5 5L8 21H3v-5zM12 7l5 5M6 3v4M4 5h4M19 16v4M17 18h4"/>',
    play: '<path d="m8 5 11 7-11 7z" fill="currentColor" stroke="none"/>',
    terminal: '<path d="m4 17 6-5-6-5M12 19h8"/>',
    code: '<path d="m8 9-4 3 4 3M16 9l4 3-4 3M14 5l-4 14"/>',
    panel: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 15h18"/>',
    close: '<path d="m6 6 12 12M18 6 6 18"/>',
    chevron: '<path d="m9 18 6-6-6-6"/>',
    sidebar: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M9 3v18"/>',
    sparkle: '<path d="m12 3 1.4 3.6L17 8l-3.6 1.4L12 13l-1.4-3.6L7 8l3.6-1.4zM19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8zM5 13l.8 2.2L8 16l-2.2.8L5 19l-.8-2.2L2 16l2.2-.8z"/>',
    trash: '<path d="M4 7h16M9 11v6M15 11v6M6 7l1 14h10l1-14M9 7V4h6v3"/>'
  };
  return `<svg viewBox="0 0 24 24" aria-hidden="true">${paths[name]}</svg>`;
};

const app = document.querySelector("#app");
app.innerHTML = `
<div class="app-shell">
  <header class="titlebar" data-tauri-drag-region>
    <div class="brand" data-tauri-drag-region>
      <span class="brand-mark">O</span>
      <span class="brand-name">OMG <b>IDE</b></span>
    </div>
    <div class="command-bar">
      <button class="icon-button" data-action="sidebar" title="切换侧栏">${icon("sidebar")}</button>
      <span class="command-divider"></span>
      <button data-action="new" title="新建文件 Ctrl+N">${icon("file")}<span>新建</span></button>
      <button data-action="open" title="打开文件 Ctrl+O">${icon("folder")}<span>打开</span></button>
      <button data-action="save" title="保存文件 Ctrl+S">${icon("save")}<span>保存</span></button>
      <button data-action="format" title="格式化文档">${icon("magic")}<span>格式化</span></button>
    </div>
    <div class="titlebar-drag" data-tauri-drag-region></div>
    <button class="run-button" data-action="run" title="运行 Ctrl+Enter">${icon("play")}<span>运行</span><kbd>Ctrl+Enter</kbd></button>
    <div class="window-actions" aria-label="窗口控制">
      <button data-action="minimize" title="最小化" aria-label="最小化"><span class="minimize-glyph"></span></button>
      <button data-action="maximize" title="最大化" aria-label="最大化"><span class="maximize-glyph"></span></button>
      <button class="close-window" data-action="close" title="关闭" aria-label="关闭">${icon("close")}</button>
    </div>
  </header>

  <main class="app-main">
    <aside class="sidebar">
      <div class="sidebar-heading"><span>资源管理器</span><button data-action="new" title="新建文件">＋</button></div>
      <div class="sidebar-section-title">已打开的编辑器</div>
      <button class="file-entry active" data-action="focus-editor">
        <span class="file-type">OMG</span><span id="sidebar-filename">Untitled.omg</span>
      </button>
      <div class="folder-state">未打开文件夹</div>
    </aside>

    <section class="workbench">
      <section class="workspace">
        <div class="editor-tabs">
          <div class="editor-tab active">
            <span class="file-type">OMG</span><span id="filename">Untitled.omg</span>
          </div>
          <div class="editor-actions">
            <button data-action="toggle-panel" title="显示或隐藏底部面板">${icon("panel")}</button>
          </div>
        </div>
        <div class="editor-stage">
          <div id="editor"></div>
          <aside id="preview" hidden>
            <div class="preview-header"><span>OML 预览</span><span class="live-pill"><i></i>实时</span></div>
            <article id="oml-preview"></article>
          </aside>
        </div>
      </section>

      <div class="panel-resizer" aria-hidden="true"></div>
      <section class="bottom-panel">
        <nav class="panel-tabs">
          <button class="active" data-tab="output">输出</button>
          <button data-tab="terminal">终端</button>
          <span class="panel-spacer"></span>
          <button class="panel-icon" data-action="clear-panel" title="清空">${icon("trash")}</button>
          <button class="panel-icon" data-action="toggle-panel" title="关闭面板">${icon("close")}</button>
        </nav>
        <div id="output-pane" class="output-pane"><pre id="output"><span class="empty-output">运行程序后，结果会显示在这里。</span></pre></div>
        <div id="terminal" hidden>
          <pre id="terminal-output"><span class="terminal-welcome">OMG Terminal · PowerShell</span>\n</pre>
          <label class="terminal-command"><span>›</span><input id="terminal-input" placeholder="输入命令并按 Enter" autocomplete="off" /></label>
        </div>
      </section>
    </section>
  </main>

  <footer class="statusbar">
    <div><span class="status-indicator"></span><span id="status">就绪</span></div>
    <div class="statusbar-right"><span id="selection-status">Ln 1, Col 1</span><span>空格: 2</span><span>UTF-8</span><span class="language-pill">OMG</span></div>
  </footer>
</div>
<input id="file-input" type="file" accept=".omg,.oml,.txt" hidden />`;

let fileName = "Untitled.omg";
let dirty = false;
let activeTab = "output";
let programRunning = false;
let runtimeInput = null;
const isTauri = Boolean(window.__TAURI_INTERNALS__);

const status = document.querySelector("#status");
const output = document.querySelector("#output");
const outputPane = document.querySelector("#output-pane");
const preview = document.querySelector("#preview");
const shell = document.querySelector(".app-shell");
const runButton = document.querySelector(".run-button");

function parseCompletionTemplates(source) {
  const templates = [];
  let trigger = "";
  let textLines = [];
  let collectingText = false;
  const finishTemplate = () => {
    if (trigger && textLines.length) templates.push({ trigger, text: textLines.join("\n").replace(/\s+$/, "") });
    trigger = "";
    textLines = [];
    collectingText = false;
  };
  for (const line of source.replace(/\r/g, "").split("\n")) {
    const triggerMatch = line.match(/^\s*-\s+trigger:\s*(.+?)\s*$/);
    if (triggerMatch) {
      finishTemplate();
      trigger = triggerMatch[1];
      continue;
    }
    if (/^\s+text:\s*\|-?\s*$/.test(line)) {
      collectingText = true;
      continue;
    }
    if (collectingText) {
      const textMatch = line.match(/^\s{6}(.*)$/);
      if (textMatch) textLines.push(textMatch[1]);
      else if (line.trim() === "") textLines.push("");
      else collectingText = false;
    }
  }
  finishTemplate();
  return templates;
}

const editor = new OmgEditor(document.querySelector("#editor"), {
  templates: parseCompletionTemplates(completionTemplatesYaml)
});
editor.focus();

function setStatus(message, state = "ready") {
  status.textContent = message;
  document.querySelector(".status-indicator").dataset.state = state;
}

function updateTitle() {
  document.querySelector("#filename").textContent = fileName;
  document.querySelector("#sidebar-filename").textContent = fileName;
  document.title = `${dirty ? "● " : ""}${fileName} — OMG IDE`;
}

function renderOml(text) {
  return text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
    .replace(/\\t\[([^\]]*)\]/g, "<h1>$1</h1>")
    .replace(/\\subt\[([^\]]*)\]/g, "<h2>$1</h2>")
    .replace(/\\b\[([^\]]*)\]/g, "<b>$1</b>")
    .replace(/\\i\[([^\]]*)\]/g, "<i>$1</i>")
    .replace(/\\code\[([^\]]*)\]/g, "<code>$1</code>")
    .replace(/\n/g, "<br>");
}

editor.onDidChangeModelContent(() => {
  dirty = true;
  updateTitle();
  setStatus("已修改", "dirty");
  if (fileName.endsWith(".oml")) document.querySelector("#oml-preview").innerHTML = renderOml(editor.getValue());
});

document.querySelector("#editor").addEventListener("editor-position", event => {
  const { lineNumber, column } = event.detail;
  const selection = editor.getSelectionInfo();
  document.querySelector("#selection-status").textContent = selection.length
    ? `已选择 ${selection.length} 个字符`
    : `Ln ${lineNumber}, Col ${column}`;
});

function setOutputTab() {
  activeTab = "output";
  outputPane.hidden = false;
  document.querySelector("#terminal").hidden = true;
  document.querySelectorAll(".panel-tabs [data-tab]").forEach(button => button.classList.toggle("active", button.dataset.tab === "output"));
  shell.classList.remove("panel-collapsed");
}

function hideRuntimeInput() {
  if (runtimeInput) runtimeInput.hidden = true;
}

function showRuntimeInput() {
  if (!runtimeInput) {
    runtimeInput = document.createElement("label");
    runtimeInput.id = "runtime-input";
    runtimeInput.innerHTML = '<span>程序正在等待输入</span><b>›</b><input aria-label="程序输入" autocomplete="off" placeholder="输入后按 Enter" />';
    const field = runtimeInput.querySelector("input");
    field.addEventListener("keydown", async event => {
      if (event.key !== "Enter" || !programRunning) return;
      event.preventDefault();
      const value = field.value;
      field.value = "";
      try {
        await invoke("send_omg_input", { value });
      } catch (error) {
        output.textContent += `\n${String(error)}\n`;
        finishRun(false);
      }
    });
    outputPane.append(runtimeInput);
  }
  runtimeInput.hidden = false;
  runtimeInput.querySelector("input").focus();
}

function finishRun(success) {
  programRunning = false;
  runButton.classList.remove("is-running");
  runButton.querySelector("span").textContent = "运行";
  hideRuntimeInput();
  setStatus(success ? "运行完成" : "运行失败", success ? "success" : "error");
}

const outputEventsReady = isTauri ? Promise.all([
  listen("omg-output", event => {
    if (output.querySelector(".empty-output")) output.textContent = "";
    output.textContent += event.payload;
    output.scrollTop = output.scrollHeight;
  }),
  listen("omg-finished", event => finishRun(Boolean(event.payload)))
]) : Promise.resolve();

async function runProgram() {
  if (programRunning) return;
  await outputEventsReady;
  const source = editor.getValue();
  programRunning = true;
  runButton.classList.add("is-running");
  runButton.querySelector("span").textContent = "运行中";
  setOutputTab();
  output.textContent = "";
  setStatus("正在运行…", "running");
  if (/\binput(?:_without_display)?\s*\(/.test(source)) showRuntimeInput();
  else hideRuntimeInput();
  if (!isTauri) {
    output.textContent = "浏览器预览模式无法启动 OMG 运行时。\n请在桌面应用中运行代码。";
    finishRun(false);
    return;
  }
  try {
    await invoke("start_omg", { source, name: fileName });
  } catch (error) {
    output.textContent = String(error);
    finishRun(false);
  }
}

async function handleAction(action) {
  if (action === "focus-editor") return editor.focus();
  if (action === "new") {
    editor.setValue("", { silent: true });
    fileName = "Untitled.omg";
    dirty = false;
    preview.hidden = true;
    updateTitle();
    setStatus("已新建文件");
    return editor.focus();
  }
  if (action === "open") return document.querySelector("#file-input").click();
  if (action === "save") {
    const url = URL.createObjectURL(new Blob([editor.getValue()], { type: "text/plain" }));
    Object.assign(document.createElement("a"), { href: url, download: fileName }).click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    dirty = false;
    updateTitle();
    return setStatus("已保存", "success");
  }
  if (action === "format") {
    editor.format();
    setStatus("格式化完成", "success");
    return editor.focus();
  }
  if (action === "run") return runProgram();
  if (action === "sidebar") return shell.classList.toggle("sidebar-collapsed");
  if (action === "toggle-panel") return shell.classList.toggle("panel-collapsed");
  if (action === "clear-panel") {
    if (activeTab === "output") output.innerHTML = '<span class="empty-output">输出已清空。</span>';
    else document.querySelector("#terminal-output").innerHTML = '<span class="terminal-welcome">OMG Terminal · PowerShell</span>\n';
    return;
  }
  const windowCommands = { minimize: "minimize_window", maximize: "toggle_maximize_window", close: "close_window" };
  if (windowCommands[action] && isTauri) {
    try { await invoke(windowCommands[action]); }
    catch (error) { setStatus(`窗口错误：${String(error)}`, "error"); }
  }
}

document.querySelectorAll("[data-action]").forEach(button => {
  button.addEventListener("pointerdown", event => event.stopPropagation());
  button.addEventListener("click", event => {
    event.preventDefault();
    handleAction(event.currentTarget.dataset.action);
  });
});

document.querySelector("#file-input").addEventListener("change", async event => {
  const file = event.target.files[0];
  if (!file) return;
  fileName = file.name;
  editor.setValue(await file.text(), { silent: true });
  dirty = false;
  preview.hidden = !fileName.endsWith(".oml");
  document.querySelector("#oml-preview").innerHTML = renderOml(editor.getValue());
  updateTitle();
  setStatus("文件已打开", "success");
  editor.focus();
  event.target.value = "";
});

document.querySelectorAll(".panel-tabs [data-tab]").forEach(button => button.addEventListener("click", () => {
  activeTab = button.dataset.tab;
  outputPane.hidden = activeTab !== "output";
  document.querySelector("#terminal").hidden = activeTab !== "terminal";
  document.querySelectorAll(".panel-tabs [data-tab]").forEach(item => item.classList.toggle("active", item === button));
  shell.classList.remove("panel-collapsed");
  if (activeTab === "terminal") document.querySelector("#terminal-input").focus();
}));

document.querySelector("#terminal-input").addEventListener("keydown", async event => {
  if (event.key !== "Enter" || !event.target.value.trim()) return;
  const command = event.target.value;
  const terminalOutput = document.querySelector("#terminal-output");
  terminalOutput.textContent += `› ${command}\n`;
  event.target.value = "";
  if (!isTauri) {
    terminalOutput.textContent += "终端仅在桌面应用中可用。\n";
    return;
  }
  try { terminalOutput.textContent += `${await invoke("run_terminal", { command })}\n`; }
  catch (error) { terminalOutput.textContent += `${String(error)}\n`; }
  terminalOutput.scrollTop = terminalOutput.scrollHeight;
});

document.addEventListener("keydown", event => {
  const modifier = event.ctrlKey || event.metaKey;
  if (modifier && event.key.toLowerCase() === "s") { event.preventDefault(); handleAction("save"); }
  if (modifier && event.key.toLowerCase() === "n") { event.preventDefault(); handleAction("new"); }
  if (modifier && event.key.toLowerCase() === "o") { event.preventDefault(); handleAction("open"); }
  if ((modifier && event.key === "Enter") || event.key === "F5") { event.preventDefault(); runProgram(); }
});

const resizer = document.querySelector(".panel-resizer");
resizer.addEventListener("pointerdown", event => {
  event.preventDefault();
  resizer.setPointerCapture(event.pointerId);
  const startY = event.clientY;
  const startHeight = document.querySelector(".bottom-panel").getBoundingClientRect().height;
  const move = moveEvent => {
    const height = Math.max(120, Math.min(window.innerHeight * .55, startHeight + startY - moveEvent.clientY));
    document.querySelector(".workbench").style.setProperty("--panel-height", `${height}px`);
  };
  const stop = () => {
    resizer.removeEventListener("pointermove", move);
    resizer.removeEventListener("pointerup", stop);
  };
  resizer.addEventListener("pointermove", move);
  resizer.addEventListener("pointerup", stop);
});

updateTitle();
