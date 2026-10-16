import { invoke } from "@tauri-apps/api/core";
import "./style.css";

const app = document.querySelector("#app");

const icon = (color, paths) => `<svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="${color}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${paths}</svg>`;

const tools = [
  { id: "exposure", label: "曝光", key: "E", min: -2, max: 2, step: .05, value: 0, unit: "EV", icon: icon("#89b4fa", `<circle cx="12" cy="12" r="8.4"/><circle cx="12" cy="12" r="2.7" fill="#89b4fa" fill-opacity=".25"/><path d="m12 3.6 4 2.3-2 3.4h-4L8 5.9l4-2.3ZM20.4 12l-2.3 4-3.4-2v-4l3.4-2 2.3 4ZM12 20.4l-4-2.3 2-3.4h4l2 3.4-4 2.3ZM3.6 12l2.3-4 3.4 2v4l-3.4 2-2.3-4Z"/>`) },
  { id: "vibrance", label: "鲜明度", key: "V", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#cba6f7", `<path d="m12 3 1.55 5.45L19 10l-5.45 1.55L12 17l-1.55-5.45L5 10l5.45-1.55L12 3Z"/><path d="m18.5 16 .6 2.1 2.1.6-2.1.6-.6 2.1-.6-2.1-2.1-.6 2.1-.6.6-2.1Z"/>`) },
  { id: "highlights", label: "高光", key: "H", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#fab387", `<circle cx="12" cy="12" r="3.6" fill="#fab387" fill-opacity=".24"/><path d="M12 2.6v3M12 18.4v3M2.6 12h3M18.4 12h3M5.3 5.3l2.1 2.1M16.6 16.6l2.1 2.1M18.7 5.3l-2.1 2.1M7.4 16.6l-2.1 2.1"/><path d="m12 7.8 1.05 3.15 3.15 1.05-3.15 1.05L12 16.2l-1.05-3.15L7.8 12l3.15-1.05L12 7.8Z" fill="#fab387" stroke="none"/>`) },
  { id: "shadows", label: "阴影", key: "S", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#89b4fa", `<path d="M15.6 3.8a7.9 7.9 0 1 0 4.6 14.4A8.8 8.8 0 1 1 15.6 3.8Z" fill="#89b4fa" fill-opacity=".24"/><path d="M4.2 19.4h9.6M4.2 16.5h6.4"/>`) },
  { id: "brightness", label: "亮度", key: "L", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#f9e2af", `<circle cx="12" cy="12" r="4.1" fill="#f9e2af" fill-opacity=".22"/><path d="M12 2.5v2.4M12 19.1v2.4M2.5 12h2.4M19.1 12h2.4M5.3 5.3 7 7M17 17l1.7 1.7M18.7 5.3 17 7M7 17l-1.7 1.7"/>`) },
  { id: "contrast", label: "对比度", key: "C", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#94e2d5", `<path d="M12 3.5a8.5 8.5 0 1 1 0 17V3.5Z" fill="#94e2d5" fill-opacity=".75"/><circle cx="12" cy="12" r="8.5"/><path d="M12 3.5v17"/>`) },
  { id: "saturation", label: "饱和度", key: "A", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#f5c2e7", `<path d="M12.1 4.1c-5 0-8.6 3.3-8.6 7.6 0 4 3.1 7.3 7.3 7.3h2.1c1.2 0 2-.9 2-2 0-.6-.3-1.1-.6-1.5-.4-.5-.1-1.2.5-1.2h1.1c2.7 0 4.6-2.1 4.6-4.8 0-3.1-3.3-5.4-8.4-5.4Z" fill="#f5c2e7" fill-opacity=".15"/><circle cx="7.9" cy="10.5" r="1" fill="#f38ba8" stroke="none"/><circle cx="11.2" cy="7.7" r="1" fill="#f9e2af" stroke="none"/><circle cx="15" cy="9.4" r="1" fill="#89b4fa" stroke="none"/><circle cx="8.9" cy="14.1" r="1" fill="#a6e3a1" stroke="none"/>`) },
  { id: "temperature", label: "色温", key: "T", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#f38ba8", `<path d="M10 4a2 2 0 1 1 4 0v9.1a4 4 0 1 1-4 0V4Z"/><path d="M12 8v7M17.5 6.5l1.3-1.3M18.8 11H21"/>`) },
  { id: "tint", label: "色调", key: "I", min: -100, max: 100, step: 1, value: 0, unit: "", icon: icon("#89dceb", `<path d="M12 3.5 5.5 12A6.5 6.5 0 0 0 18.5 12L12 3.5Z"/><path d="M8.6 13.4c.7 1.3 1.8 1.9 3.4 1.9"/><path d="m18.5 4 .6 2.1 2.1.6-2.1.6-.6 2.1-.6-2.1-2.1-.6 2.1-.6.6-2.1Z"/>`) }
];

const state = {
  active: "exposure", editorFocused: false, menuOpen: false, navArmed: false,
  source: null, imageName: "Untitled", sourcePath: null, sourceDataUrl: null, before: false, rendering: false, dragId: null, reordering: false,
  crop: { x: 0, y: 0, width: 1, height: 1 }, cropping: false, history: [], historyIndex: -1, cropDrag: null
};

app.innerHTML = `
  <header class="titlebar">
    <div class="brand" data-tauri-drag-region><span class="brand-mark">P</span><span>Photo<span>Shop</span></span></div>
    <nav class="top-nav" aria-label="主菜单">
      <button id="file-tab" class="nav-tab" aria-expanded="false">File <kbd>F</kbd></button>
      <button class="nav-tab disabled" disabled>Edit</button>
      <button class="nav-tab disabled" disabled>View</button>
    </nav>
    <div class="title-drag" data-tauri-drag-region></div>
    <div class="window-controls">
      <button data-window="minimize" aria-label="最小化">−</button>
      <button data-window="maximize" aria-label="最大化">□</button>
      <button data-window="close" class="close" aria-label="关闭">×</button>
    </div>
    <div id="file-menu" class="file-menu" role="menu" hidden>
      <button data-action="open" role="menuitem"><span class="menu-icon">↗</span><span>打开文件（夹）</span><kbd>O</kbd></button>
      <button data-action="new" role="menuitem"><span class="menu-icon">＋</span><span>新建文件</span><kbd>N</kbd></button>
    </div>
  </header>
  <main>
    <section class="workspace" aria-label="照片画布">
      <div class="workspace-header">
        <div class="breadcrumb"><span class="dot"></span><strong id="image-name">Untitled</strong><span id="image-meta">准备开始创作</span></div>
        <div class="canvas-actions">
          <button id="fit-button" class="ghost">⌗ 适合画面</button>
          <button id="compare-button" class="compare" title="按住 Ctrl+Shift+O 查看原图"><span>◐</span> 按住查看原图 <kbd>Ctrl+Shift+O</kbd></button>
          <button id="export-button" class="primary">导出 <span>↓</span></button>
        </div>
      </div>
      <div id="canvas-stage" class="canvas-stage">
        <div class="checker"></div>
        <div id="empty-state" class="empty-state">
          <div class="empty-logo"><span>✦</span></div>
          <h1>从一张照片开始</h1>
          <p>拖入图像，或从 File 菜单打开文件。</p>
          <div class="empty-actions"><button class="primary" data-action="open">打开照片 <kbd>O</kbd></button><button class="ghost" data-action="new">新建画布</button></div>
          <small>支持 JPG、PNG、WebP、GIF、BMP</small>
        </div>
        <div id="canvas-wrap" class="canvas-wrap" hidden>
          <canvas id="photo-canvas"></canvas>
          <div id="before-label" class="before-label" hidden>原图</div>
          <div id="crop-overlay" class="crop-overlay" hidden>
            <div id="crop-selection" class="crop-selection">
              <button class="crop-edge top" data-crop-handle="top" aria-label="裁剪顶部"></button><button class="crop-edge right" data-crop-handle="right" aria-label="裁剪右侧"></button><button class="crop-edge bottom" data-crop-handle="bottom" aria-label="裁剪底部"></button><button class="crop-edge left" data-crop-handle="left" aria-label="裁剪左侧"></button>
              <button class="crop-corner top-left" data-crop-handle="top-left" aria-label="裁剪左上"></button><button class="crop-corner top-right" data-crop-handle="top-right" aria-label="裁剪右上"></button><button class="crop-corner bottom-left" data-crop-handle="bottom-left" aria-label="裁剪左下"></button><button class="crop-corner bottom-right" data-crop-handle="bottom-right" aria-label="裁剪右下"></button>
            </div>
          </div>
        </div>
        <div id="drop-overlay" class="drop-overlay" hidden><div>松开以打开图像</div></div>
      </div>
    </section>
  </main>
  <section id="filmstrip" class="filmstrip" aria-label="编辑工具">
    <div class="filmstrip-top"><h2>编辑栏 <kbd>Ctrl+E</kbd></h2><div class="edit-bar-actions"><span id="focus-indicator" class="focus-indicator">未聚焦</span><button id="reorder-button" class="reorder-button" title="启用工具排序" aria-label="启用工具排序"><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i></button></div></div>
    <div class="filmstrip-body"><div class="tools-scroll"><div id="tools-list" class="tools-list"></div></div><div id="active-control" class="active-control"></div></div>
  </section>
  <input id="file-input" type="file" accept=".jpg,.jpeg,.png,.webp,.gif,.bmp,image/jpeg,image/png,image/webp,image/gif,image/bmp" hidden />
  <input id="folder-input" type="file" accept=".jpg,.jpeg,.png,.webp,.gif,.bmp,image/jpeg,image/png,image/webp,image/gif,image/bmp" webkitdirectory directory multiple hidden />
  <div id="toast" class="toast" role="status"></div>
`;

const $ = (selector) => document.querySelector(selector);
const canvas = $("#photo-canvas");
const ctx = canvas.getContext("2d", { willReadFrequently: true });
let toolList = tools;

function formatValue(tool) {
  const value = Number(tool.value);
  if (tool.unit === "EV") return `${value > 0 ? "+" : ""}${value.toFixed(2)} ${tool.unit}`;
  return `${value > 0 ? "+" : ""}${value}`;
}

function renderTools() {
  const list = $("#tools-list");
  list.innerHTML = toolList.map(tool => `
    <article class="tool-card ${tool.id === state.active ? "selected" : ""} ${state.reordering ? "reorder-ready" : ""}" data-tool="${tool.id}" draggable="${state.reordering}">
      <button class="tool-select" data-select="${tool.id}" title="${tool.label}（${tool.key}）" aria-label="${tool.label}"><span class="tool-icon">${tool.icon}</span></button>
    </article>`).join("");
  bindToolEvents();
}

function bindToolEvents() {
  document.querySelectorAll("[data-select]").forEach(button => button.addEventListener("click", () => selectTool(button.dataset.select, true)));
  document.querySelectorAll(".tool-card").forEach(card => {
    card.addEventListener("dragstart", (event) => {
      if (!state.reordering) { event.preventDefault(); return; }
      state.dragId = card.dataset.tool;
      event.dataTransfer.effectAllowed = "move";
      card.classList.add("dragging");
    });
    card.addEventListener("dragend", () => {
      state.dragId = null;
      document.querySelectorAll(".tool-card").forEach(card => card.classList.remove("dragging", "drop-target"));
    });
    card.addEventListener("dragover", (event) => { if (state.dragId && card.dataset.tool !== state.dragId) { event.preventDefault(); card.classList.add("drop-target"); } });
    card.addEventListener("dragleave", () => card.classList.remove("drop-target"));
    card.addEventListener("drop", (event) => {
      event.preventDefault();
      const destination = card.dataset.tool;
      if (!state.dragId || destination === state.dragId) return;
      const sourceIndex = toolList.findIndex(tool => tool.id === state.dragId);
      const destinationIndex = toolList.findIndex(tool => tool.id === destination);
      const [moved] = toolList.splice(sourceIndex, 1);
      toolList.splice(destinationIndex, 0, moved);
      renderTools();
      showToast(`“${moved.label}” 已重新排序`);
    });
  });
}

function selectTool(id, focusEditor = false) {
  state.active = id;
  if (focusEditor) {
    state.editorFocused = true;
    $("#filmstrip").classList.add("focused");
    $("#focus-indicator").textContent = "已聚焦";
  }
  const tool = toolList.find(item => item.id === id);
  $("#active-control").innerHTML = `
    <div class="control-title"><span>${tool.label}</span><output>${formatValue(tool)}</output></div>
    <input id="active-range" aria-label="调整${tool.label}" type="range" min="${tool.min}" max="${tool.max}" step="${tool.step}" value="${tool.value}" />
    <div class="range-minmax"><span>${tool.min}${tool.unit}</span><button id="control-reset" title="重置 ${tool.label}">设为 0</button><span>+${tool.max}${tool.unit}</span></div>`;
  const range = $("#active-range");
  range.addEventListener("input", () => { tool.value = Number(range.value); $(".control-title output").textContent = formatValue(tool); renderTools(); scheduleRender(); });
  range.addEventListener("change", () => recordChange("adjust"));
  $("#control-reset").addEventListener("click", () => { tool.value = 0; selectTool(id); scheduleRender(); recordChange("adjust"); });
  renderTools();
}

function adjustActiveTool(direction) {
  const tool = toolList.find(item => item.id === state.active);
  const next = Math.min(tool.max, Math.max(tool.min, Number((tool.value + direction * tool.step).toFixed(2))));
  if (next === tool.value) return;
  tool.value = next;
  selectTool(tool.id);
  scheduleRender();
  recordChange("adjust");
}

function clamp(value) { return Math.max(0, Math.min(255, value)); }

function effects() { return Object.fromEntries(toolList.map(tool => [tool.id, Number(tool.value)])); }

function cropSource() {
  const source = state.source, width = source.naturalWidth || source.width, height = source.naturalHeight || source.height;
  if (state.cropping) return { x: 0, y: 0, width, height };
  return { x: Math.round(state.crop.x * width), y: Math.round(state.crop.y * height), width: Math.max(1, Math.round(state.crop.width * width)), height: Math.max(1, Math.round(state.crop.height * height)) };
}

function prepareCanvas() {
  const source = state.source, crop = cropSource(), maxDimension = 1920;
  const scale = Math.min(1, maxDimension / Math.max(crop.width, crop.height));
  canvas.width = Math.max(1, Math.round(crop.width * scale)); canvas.height = Math.max(1, Math.round(crop.height * scale));
  ctx.drawImage(source, crop.x, crop.y, crop.width, crop.height, 0, 0, canvas.width, canvas.height);
}

function processImage() {
  if (!state.source || state.before) return drawOriginal();
  prepareCanvas();
  const data = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const pixels = data.data;
  const fx = effects();
  const exposure = Math.pow(2, fx.exposure);
  const brightness = fx.brightness * 1.7;
  const contrast = (100 + fx.contrast) / 100;
  const saturation = 1 + fx.saturation / 100;
  const temperature = fx.temperature * .42;
  const tint = fx.tint * .28;
  const highlight = fx.highlights / 100;
  const shadow = fx.shadows / 100;
  for (let index = 0; index < pixels.length; index += 4) {
    let r = pixels[index] * exposure, g = pixels[index + 1] * exposure, b = pixels[index + 2] * exposure;
    const lum = (r * .2126 + g * .7152 + b * .0722) / 255;
    const shadowWeight = Math.pow(1 - lum, 2) * shadow;
    const highlightWeight = Math.pow(lum, 2) * highlight;
    r += shadowWeight * 92 - highlightWeight * 90;
    g += shadowWeight * 92 - highlightWeight * 90;
    b += shadowWeight * 92 - highlightWeight * 90;
    r += brightness; g += brightness; b += brightness;
    r = (r - 128) * contrast + 128; g = (g - 128) * contrast + 128; b = (b - 128) * contrast + 128;
    const gray = r * .299 + g * .587 + b * .114;
    let vibrance = 1 + (fx.vibrance / 100) * (1 - (Math.max(r, g, b) - Math.min(r, g, b)) / 255);
    r = gray + (r - gray) * saturation * vibrance;
    g = gray + (g - gray) * saturation * vibrance;
    b = gray + (b - gray) * saturation * vibrance;
    pixels[index] = clamp(r + temperature + tint);
    pixels[index + 1] = clamp(g - Math.abs(temperature) * .13);
    pixels[index + 2] = clamp(b - temperature - tint);
  }
  ctx.putImageData(data, 0, 0);
}

function drawOriginal() {
  if (!state.source) return;
  prepareCanvas();
}

let renderQueued = false;
function scheduleRender() {
  if (!state.source || renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(() => { renderQueued = false; processImage(); });
}

function snapshot(type) { return { type, values: effects(), crop: { ...state.crop } }; }
function recordChange(type) {
  state.history = state.history.slice(0, state.historyIndex + 1); state.history.push(snapshot(type)); state.historyIndex = state.history.length - 1; queueAutoSave();
}
function restoreHistory(index) {
  state.historyIndex = index; const entry = state.history[index];
  toolList.forEach(tool => tool.value = entry ? entry.values[tool.id] : 0); state.crop = entry ? { ...entry.crop } : { x: 0, y: 0, width: 1, height: 1 };
  selectTool(state.active); scheduleRender(); queueAutoSave();
}
function ocgText() {
  return `version: 1\nsource_path: ${state.sourcePath || ""}\nchanges: ${JSON.stringify(state.history, null, 2)}\ncurrently_at: ${state.historyIndex + 1}\n`;
}
function loadOcg(text) {
  try { const body = text.match(/changes:\s*([\s\S]*?)\ncurrently_at:/)?.[1]; const at = Number(text.match(/currently_at:\s*(\d+)/)?.[1] ?? 0); state.history = JSON.parse(body || "[]"); restoreHistory(Math.min(at - 1, state.history.length - 1)); } catch { state.history = []; state.historyIndex = -1; }
}
function persistSession() {
  if (state.sourcePath) invoke("persist_session", { session: { sourcePath: state.sourcePath, ocg: ocgText() } }).catch(() => {});
}
let autoSaveTimer;
function queueAutoSave() {
  persistSession();
  clearTimeout(autoSaveTimer);
  autoSaveTimer = setTimeout(() => saveProject(false), 350);
}
async function saveProject(notify = true) {
  if (!state.sourcePath || !state.sourceDataUrl) return;
  try { const target = await invoke("save_project", { input: { sourcePath: state.sourcePath, originalDataUrl: state.sourceDataUrl, ocg: ocgText() } }); if (notify) showToast(`已保存 ${target.split(/[\\/]/).pop()}`); } catch (error) { if (notify) showToast(`保存失败：${error}`); }
}

function loadImage(url, name, sourcePath = null, ocg = null) {
  const image = new Image();
  image.onload = () => {
    state.source = image; state.imageName = name; state.sourcePath = sourcePath; state.sourceDataUrl = url; state.crop = { x: 0, y: 0, width: 1, height: 1 }; state.history = []; state.historyIndex = -1;
    $("#image-name").textContent = name;
    $("#image-meta").textContent = `${image.naturalWidth} × ${image.naturalHeight} px`;
    $("#empty-state").hidden = true; $("#canvas-wrap").hidden = false;
    $("#export-button").disabled = false;
    if (ocg) loadOcg(ocg); else processImage();
    queueAutoSave();
    showToast("照片已准备好，可以开始编辑");
  };
  image.onerror = () => showToast("无法读取这张图片，请选择其他文件");
  image.src = url;
}

function isSupportedImage(file) {
  return Boolean(file) && (file.type.startsWith("image/") || /\.(jpe?g|png|webp|gif|bmp)$/i.test(file.name));
}

function loadFile(file) {
  if (!isSupportedImage(file)) { showToast("请选择 JPG、PNG、WebP、GIF 或 BMP 图片"); return; }
  const reader = new FileReader();
  reader.onload = () => loadImage(reader.result, file.name.replace(/\.[^.]+$/, ""));
  reader.readAsDataURL(file);
}

async function openProject(path = null, restoredOcg = null) {
  try {
    const selected = path || await invoke("open_file_dialog"); if (!selected) return;
    const project = await invoke("read_project", { path: selected });
    loadImage(project.dataUrl, project.name, project.sourcePath, restoredOcg ?? project.ocg);
  } catch (error) { showToast(`无法打开：${error}`); }
}

function newCanvas() {
  const demo = document.createElement("canvas"); demo.width = 1600; demo.height = 1067;
  const d = demo.getContext("2d");
  const sky = d.createLinearGradient(0, 0, 0, demo.height); sky.addColorStop(0, "#7ec4dc"); sky.addColorStop(.42, "#f2b18b"); sky.addColorStop(.65, "#8e6c73"); sky.addColorStop(1, "#162b36"); d.fillStyle = sky; d.fillRect(0, 0, demo.width, demo.height);
  d.fillStyle = "rgba(255,225,174,.65)"; d.beginPath(); d.arc(1160, 270, 105, 0, Math.PI * 2); d.fill();
  d.fillStyle = "#43585b"; d.beginPath(); d.moveTo(0, 550); d.lineTo(420, 280); d.lineTo(710, 550); d.lineTo(1030, 215); d.lineTo(1600, 610); d.lineTo(1600, 740); d.lineTo(0, 740); d.fill();
  d.fillStyle = "#263f44"; d.beginPath(); d.moveTo(0, 680); d.lineTo(350, 450); d.lineTo(670, 675); d.lineTo(990, 430); d.lineTo(1600, 680); d.lineTo(1600, 805); d.lineTo(0, 805); d.fill();
  const water = d.createLinearGradient(0, 700, 0, 1067); water.addColorStop(0, "#477c86"); water.addColorStop(1, "#102b37"); d.fillStyle = water; d.fillRect(0, 700, 1600, 367);
  for (let y = 720; y < 1030; y += 20) { d.strokeStyle = `rgba(255, ${170 + (y % 60)}, 145, ${.22 - (y - 720) / 2600})`; d.lineWidth = 3; d.beginPath(); d.moveTo(270 + (y % 70), y); d.lineTo(1280 - (y % 130), y); d.stroke(); }
  loadImage(demo.toDataURL("image/jpeg", .92), "New creation");
}

function setBefore(enabled) {
  if (!state.source || state.before === enabled) return;
  state.before = enabled;
  $("#before-label").hidden = !enabled;
  $("#canvas-wrap").classList.toggle("showing-before", enabled);
  processImage();
}

function updateCropOverlay() {
  const selection = $("#crop-selection"), crop = state.crop;
  selection.style.left = `${crop.x * 100}%`; selection.style.top = `${crop.y * 100}%`; selection.style.width = `${crop.width * 100}%`; selection.style.height = `${crop.height * 100}%`;
}
function setCropping(enabled) {
  if (!state.source) return;
  state.cropping = enabled; $("#crop-overlay").hidden = !enabled; updateCropOverlay(); processImage();
  showToast(enabled ? "拖动边或角裁剪；Enter 确认，Esc 取消" : "裁剪已确认");
}
function beginCropDrag(event) {
  const handle = event.currentTarget.dataset.cropHandle; event.preventDefault(); event.currentTarget.setPointerCapture(event.pointerId);
  state.cropDrag = { handle, initial: { ...state.crop } };
}
function moveCropDrag(event) {
  if (!state.cropDrag) return;
  const bounds = canvas.getBoundingClientRect(), minX = 10 / Math.max(bounds.width, 1), minY = 10 / Math.max(bounds.height, 1);
  const x = Math.max(0, Math.min(1, (event.clientX - bounds.left) / bounds.width)), y = Math.max(0, Math.min(1, (event.clientY - bounds.top) / bounds.height));
  const crop = { ...state.crop }, h = state.cropDrag.handle;
  if (h.includes("left")) { const right = crop.x + crop.width; crop.x = Math.min(x, right - minX); crop.width = right - crop.x; }
  if (h.includes("right")) crop.width = Math.max(minX, x - crop.x);
  if (h.includes("top")) { const bottom = crop.y + crop.height; crop.y = Math.min(y, bottom - minY); crop.height = bottom - crop.y; }
  if (h.includes("bottom")) crop.height = Math.max(minY, y - crop.y);
  if (h === "left") { const right = crop.x + crop.width; crop.x = Math.min(x, right - minX); crop.width = right - crop.x; }
  if (h === "right") crop.width = Math.max(minX, x - crop.x);
  if (h === "top") { const bottom = crop.y + crop.height; crop.y = Math.min(y, bottom - minY); crop.height = bottom - crop.y; }
  if (h === "bottom") crop.height = Math.max(minY, y - crop.y);
  state.crop = crop; updateCropOverlay();
}
function endCropDrag() { if (!state.cropDrag) return; state.cropDrag = null; recordChange("crop"); }

function toggleMenu(force) {
  state.menuOpen = force ?? !state.menuOpen;
  $("#file-menu").hidden = !state.menuOpen;
  $("#file-tab").setAttribute("aria-expanded", String(state.menuOpen));
  $("#file-tab").classList.toggle("active", state.menuOpen || state.navArmed);
  if (state.menuOpen) $("#file-menu button").focus();
}

let toastTimer;
function showToast(message) {
  const toast = $("#toast"); toast.textContent = message; toast.classList.add("visible"); clearTimeout(toastTimer); toastTimer = setTimeout(() => toast.classList.remove("visible"), 2200);
}

$("#file-tab").addEventListener("click", () => toggleMenu());
document.querySelectorAll("[data-action]").forEach(button => button.addEventListener("click", () => {
  const action = button.dataset.action;
  toggleMenu(false);
  if (action === "open") openProject();
  if (action === "new") newCanvas();
}));
$("#file-input").addEventListener("change", event => { loadFile(event.target.files[0]); event.target.value = ""; });
$("#folder-input").addEventListener("change", event => { const image = [...event.target.files].find(isSupportedImage); if (image) { loadFile(image); showToast(`已从文件夹中打开 ${image.name}`); } event.target.value = ""; });
$("#canvas-stage").addEventListener("dragover", event => { event.preventDefault(); $("#drop-overlay").hidden = false; });
$("#canvas-stage").addEventListener("dragleave", event => { if (!event.currentTarget.contains(event.relatedTarget)) $("#drop-overlay").hidden = true; });
$("#canvas-stage").addEventListener("drop", event => { event.preventDefault(); $("#drop-overlay").hidden = true; loadFile(event.dataTransfer.files[0]); });
$("#compare-button").addEventListener("pointerdown", () => setBefore(true));
$("#compare-button").addEventListener("pointerup", () => setBefore(false));
$("#compare-button").addEventListener("pointerleave", () => setBefore(false));
$("#fit-button").addEventListener("click", () => { $("#canvas-wrap").classList.add("fit-pulse"); setTimeout(() => $("#canvas-wrap").classList.remove("fit-pulse"), 350); });
document.querySelectorAll("[data-crop-handle]").forEach(handle => handle.addEventListener("pointerdown", beginCropDrag));
window.addEventListener("pointermove", moveCropDrag); window.addEventListener("pointerup", endCropDrag);
$("#reorder-button").addEventListener("click", () => {
  state.reordering = !state.reordering;
  $("#reorder-button").classList.toggle("active", state.reordering);
  $("#reorder-button").title = state.reordering ? "拖动图标以排序" : "启用工具排序";
  renderTools();
  showToast(state.reordering ? "拖动图标以调整顺序" : "已退出工具排序");
});
$("#export-button").addEventListener("click", () => { if (!state.source) return; const link = document.createElement("a"); link.download = `${state.imageName || "photo"}-edited.jpg`; link.href = canvas.toDataURL("image/jpeg", .94); link.click(); showToast("已导出 JPG 图片"); });
document.querySelectorAll("[data-window]").forEach(button => button.addEventListener("click", () => invoke(`${button.dataset.window}_window`).catch(() => window.close())));

document.addEventListener("keydown", event => {
  const key = event.key.toUpperCase();
  if (event.ctrlKey && key === "S") { event.preventDefault(); saveProject(); return; }
  if (event.ctrlKey && key === "R") { event.preventDefault(); setCropping(!state.cropping); return; }
  if (event.ctrlKey && key === "Z") { event.preventDefault(); if (state.historyIndex >= 0) restoreHistory(state.historyIndex - 1); return; }
  if (event.ctrlKey && (key === "Y" || (event.shiftKey && key === "Z"))) { event.preventDefault(); if (state.historyIndex < state.history.length - 1) restoreHistory(state.historyIndex + 1); return; }
  if (state.cropping && event.key === "Enter") { event.preventDefault(); setCropping(false); return; }
  if (state.cropping && event.key === "Escape") { event.preventDefault(); setCropping(false); return; }
  if (event.ctrlKey && event.shiftKey && key === "O") { event.preventDefault(); setBefore(true); return; }
  if (event.ctrlKey && key === "T") { event.preventDefault(); state.navArmed = true; $("#file-tab").classList.add("active"); showToast("菜单导航：按 F 选择 File，再按 Enter 打开"); return; }
  if (state.navArmed && key === "F") { event.preventDefault(); $("#file-tab").focus(); return; }
  if (state.navArmed && key === "ENTER") { event.preventDefault(); state.navArmed = false; toggleMenu(true); return; }
  if (event.ctrlKey && key === "E") { event.preventDefault(); state.editorFocused = true; $("#filmstrip").classList.add("focused"); $("#focus-indicator").textContent = "已聚焦"; $("#active-range").focus(); showToast("编辑栏已聚焦"); return; }
  if (state.menuOpen && !event.ctrlKey && !event.altKey) {
    if (key === "O") { event.preventDefault(); toggleMenu(false); openProject(); return; }
    if (key === "N") { event.preventDefault(); toggleMenu(false); newCanvas(); return; }
    if (key === "ESCAPE") { toggleMenu(false); return; }
  }
  if (state.editorFocused && !event.ctrlKey && !event.altKey && !event.metaKey) {
    if (event.key === "+" || event.key === "=") { event.preventDefault(); adjustActiveTool(1); return; }
    if (event.key === "-") { event.preventDefault(); adjustActiveTool(-1); return; }
    const tool = toolList.find(item => item.key === key);
    if (tool) { event.preventDefault(); selectTool(tool.id); $("#active-range").focus(); }
  }
});
document.addEventListener("keyup", event => { if (event.ctrlKey && event.shiftKey && event.key.toUpperCase() === "O") setBefore(false); });
window.addEventListener("blur", () => setBefore(false));
window.addEventListener("beforeunload", () => { persistSession(); saveProject(false); });

invoke("register_oimg_association").catch(() => {});
Promise.all([invoke("launch_target"), invoke("load_last_session")]).then(([target, session]) => {
  if (target) return openProject(target);
  if (session?.sourcePath) openProject(session.sourcePath, session.ocg);
});

renderTools();
selectTool("exposure");
$("#export-button").disabled = true;
