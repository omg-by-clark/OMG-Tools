const state = {
  trainings: []
};

const els = {
  refreshButton: document.querySelector("#refreshButton"),
  trainingCount: document.querySelector("#trainingCount"),
  publicCount: document.querySelector("#publicCount"),
  privateCount: document.querySelector("#privateCount"),
  statusText: document.querySelector("#statusText"),
  summaryCard: document.querySelector("#summaryCard"),
  trainingList: document.querySelector("#trainingList"),
  trainingTemplate: document.querySelector("#trainingTemplate")
};

async function api(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

function renderMetrics() {
  const publicCount = state.trainings.filter((item) => String(item.auth).toLowerCase() === "public").length;
  const privateCount = state.trainings.filter((item) => String(item.auth).toLowerCase() !== "public").length;
  els.trainingCount.textContent = state.trainings.length;
  els.publicCount.textContent = publicCount;
  els.privateCount.textContent = privateCount;
}

function renderSummary() {
  const privateCount = state.trainings.filter((item) => String(item.auth).toLowerCase() !== "public").length;
  if (!state.trainings.length) {
    els.summaryCard.textContent = "没有拿到训练目录。";
    return;
  }
  els.summaryCard.innerHTML = [
    `已读取到 <strong>${state.trainings.length}</strong> 个训练。`,
    `其中 <strong>${privateCount}</strong> 个是私有训练。`,
    `所以现在还不能安全地直接筛出 “OI Rank Score ≥ 10” 的题目；要继续，需要你明确授权去读取这些私有训练的题目内容。`
  ].join("<br>");
}

function renderTrainings() {
  if (!state.trainings.length) {
    els.trainingList.className = "training-list empty";
    els.trainingList.textContent = "没有拿到训练列表。";
    return;
  }

  const fragment = document.createDocumentFragment();
  state.trainings.forEach((training) => {
    const node = els.trainingTemplate.content.firstElementChild.cloneNode(true);
    node.querySelector(".training-title").textContent = training.title ?? "未命名训练";
    node.querySelector(".training-meta").textContent =
      `ID ${training.id} · ${training.problemCount ?? 0} 题 · rank ${training.rank ?? "--"}`;
    node.querySelector(".auth-chip").textContent = training.auth ?? "Unknown";
    fragment.appendChild(node);
  });

  els.trainingList.className = "training-list";
  els.trainingList.replaceChildren(fragment);
}

async function loadTrainings() {
  els.refreshButton.disabled = true;
  els.statusText.textContent = "正在刷新训练列表…";
  try {
    const payload = await api("/api/trainings");
    state.trainings = payload.trainings ?? [];
    renderMetrics();
    renderSummary();
    renderTrainings();
    els.statusText.textContent = `已加载 ${state.trainings.length} 个训练。`;
  } catch (error) {
    els.statusText.textContent = `加载失败：${error.message}`;
    els.trainingList.className = "training-list empty";
    els.trainingList.textContent = "加载失败。";
  } finally {
    els.refreshButton.disabled = false;
  }
}

els.refreshButton.addEventListener("click", loadTrainings);

loadTrainings();
