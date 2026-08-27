(() => {
  'use strict';

  const {
    buildLearnUrl,
    buildQuizUrl,
    buildReviewUrl,
    buildResultsUrl,
    clearPackProgress,
    computeResults,
    countPackQuestions,
    resolvePackFile,
    escapeHTML,
    getKnowledgeOutline,
    getQuestions,
    installRipple,
    loadLibrary,
    readLastLocation,
    readPackProgress,
    renderMath,
    saveLastLocation
  } = window.OnlineExercises;

  const host = document.querySelector('#resultsHost');
  const reloadButton = document.querySelector('#reloadButton');
  const snackbar = document.querySelector('#snackbar');

  let snackbarTimer = 0;
  let currentPack = null;
  let currentQuestions = [];
  let currentProgress = null;

  function showSnackbar(message) {
    snackbar.textContent = message;
    snackbar.classList.add('show');
    clearTimeout(snackbarTimer);
    snackbarTimer = window.setTimeout(() => snackbar.classList.remove('show'), 2200);
  }

  function renderResults() {
    const result = computeResults(
      currentQuestions,
      new Map((currentProgress?.answers || []).map(([index, answer]) => [Number(index), window.OnlineExercises.deserializeAnswer(answer)]))
    );
    const knowledgeCount = getKnowledgeOutline(currentPack).items.length;

    host.innerHTML = `<div class="view-heading result-heading">
        <p class="result-overline">${escapeHTML(currentPack.name)}</p>
        <h1>练习记录</h1>
      </div>
      <div class="result-grid">
        <article class="result-card primary"><small>正确率</small><strong>${result.accuracy}%</strong></article>
        <article class="result-card grade"><small>评级</small><strong>${result.grade}</strong></article>
        <article class="result-card"><small>答对</small><strong class="green">${result.correct}</strong></article>
        <article class="result-card"><small>答错</small><strong class="red">${result.wrong}</strong></article>
        <article class="result-card"><small>跳过</small><strong>${result.skipped}</strong></article>
      </div>
      <div class="result-actions">
        <button class="filled-button ripple-button" id="retryButton" type="button">再做一遍</button>
        <button class="text-button ripple-button" id="reviewButton" type="button">查看题目</button>
        ${knowledgeCount ? '<button class="text-button ripple-button" id="knowledgeButton" type="button">查看知识点</button>' : ''}
        <button class="text-button ripple-button" id="backHomeButton" type="button">回到题目列表</button>
      </div>`;

    renderMath(host);
  }

  async function boot() {
    installRipple(document);

    const params = new URLSearchParams(location.search);
    let packFile = params.get('pack');

    if (!packFile) {
      const last = readLastLocation();
      if (last.page === 'results' && last.packFile) {
        location.replace(buildResultsUrl(last.packFile));
        return;
      }
      if (last.page === 'quiz' && last.packFile) {
        location.replace(buildQuizUrl(last.packFile, last.index || 0));
        return;
      }
      if (last.page === 'learn' && last.packFile) {
        location.replace(buildLearnUrl(last.packFile, last.itemId || ''));
        return;
      }
      location.replace('index.html');
      return;
    }

    try {
      const packs = await loadLibrary();
      packFile = resolvePackFile(packFile, packs);
      currentPack = packs.find(pack => pack.file === packFile);
      if (!currentPack) throw new Error(`没有找到题集 ${packFile}`);
      currentQuestions = getQuestions(currentPack);
      currentProgress = readPackProgress(packFile);
      if (!currentProgress) throw new Error('当前题集还没有练习记录');
      saveLastLocation({ page: 'results', packFile });
      renderResults();
    } catch (error) {
      host.innerHTML = `<article class="question-card error-state"><div><h2>记录加载失败</h2><p>${escapeHTML(error.message)}</p></div></article>`;
      showSnackbar('无法读取练习记录');
    }
  }

  host.addEventListener('click', event => {
    if (!currentPack) return;

    if (event.target.closest('#retryButton')) {
      clearPackProgress(currentPack.file);
      const knowledgeCount = getKnowledgeOutline(currentPack).items.length;
      location.href = knowledgeCount ? buildLearnUrl(currentPack.file) : buildQuizUrl(currentPack.file, 0);
      return;
    }

    if (event.target.closest('#reviewButton')) {
      location.href = buildReviewUrl(currentPack.file, 0);
      return;
    }

    if (event.target.closest('#knowledgeButton')) {
      location.href = buildLearnUrl(currentPack.file);
      return;
    }

    if (event.target.closest('#backHomeButton')) {
      saveLastLocation({ page: 'index' });
      location.href = 'index.html';
    }
  });

  reloadButton.addEventListener('click', boot);
  boot();
})();
