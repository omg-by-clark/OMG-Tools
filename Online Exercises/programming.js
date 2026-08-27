(() => {
  'use strict';

  const { escapeHTML, installRipple, saveLastLocation } = window.OnlineExercises;
  const $ = selector => document.querySelector(selector);

  const loginPanel = $('#loginPanel');
  const loginForm = $('#loginForm');
  const loginButton = $('#loginButton');
  const logoutButton = $('#logoutButton');
  const connectionBadge = $('#connectionBadge');
  const resultsPanel = $('#resultsPanel');
  const loadingPanel = $('#loadingPanel');
  const problemGrid = $('#problemGrid');
  const emptyState = $('#emptyState');
  const summaryRow = $('#summaryRow');
  const queryInput = $('#queryInput');
  const scoreInput = $('#scoreInput');
  const searchButton = $('#searchButton');
  const snackbar = $('#snackbar');

  const AC_STORAGE_KEY = 'online_exercises_programming_ac_v1';
  const CATALOG_CACHE_KEY = 'online_exercises_programming_catalog_v3';
  const MIN_TRAINING_LESSON_EXCLUSIVE = 20;

  let snackbarTimer = 0;
  let catalog = [];
  let catalogMeta = { trainingCount: 0, problemCount: 0 };
  let currentUsername = '';

  function showSnackbar(message) {
    snackbar.textContent = message;
    snackbar.classList.add('show');
    clearTimeout(snackbarTimer);
    snackbarTimer = window.setTimeout(() => snackbar.classList.remove('show'), 2600);
  }

  function setConnected(connected, username = '') {
    currentUsername = connected ? username : '';
    connectionBadge.classList.toggle('connected', connected);
    connectionBadge.innerHTML = `<span></span>${connected ? escapeHTML(username || '已连接') : '未连接'}`;
    logoutButton.classList.toggle('hidden', !connected);
    loginPanel.classList.toggle('hidden', connected);
    resultsPanel.classList.toggle('hidden', !connected);
  }

  async function request(path, options = {}) {
    const response = await fetch(path, {
      cache: 'no-store',
      headers: { 'Content-Type': 'application/json', ...(options.headers || {}) },
      ...options
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      const error = new Error(payload.error || `请求失败（HTTP ${response.status}）`);
      error.status = response.status;
      throw error;
    }
    return payload;
  }

  function readAcRecords() {
    try {
      return JSON.parse(localStorage.getItem(AC_STORAGE_KEY) || '{}');
    } catch (_) {
      return {};
    }
  }

  function problemKey(problem) {
    return `${problem.trainingId}:${problem.problemId}`;
  }

  function getTrainingLessonNumber(problem) {
    const title = String(problem.trainingTitle || '');
    const match = title.match(/(?:\u7b2c\s*)?(\d+)\s*(?:\u8bfe|\u8b1b|\u8bb2|lesson)|(?:\u8bfe|\u8b1b|\u8bb2|lesson)\s*(\d+)/i);
    if (!match) return null;
    return Number(match[1] || match[2]);
  }

  function isAfterLessonTwenty(problem) {
    const lessonNumber = getTrainingLessonNumber(problem);
    return Number.isFinite(lessonNumber) && lessonNumber > MIN_TRAINING_LESSON_EXCLUSIVE;
  }

  function setAc(problem, accepted) {
    const records = readAcRecords();
    const key = problemKey(problem);
    if (accepted) records[key] = { at: Date.now(), source: 'manual' };
    else delete records[key];
    localStorage.setItem(AC_STORAGE_KEY, JSON.stringify(records));
    renderResults();
    showSnackbar(accepted ? '已设为 AC' : '已取消 AC 标记');
  }

  function filteredProblems() {
    const minScore = Math.max(0, Number(scoreInput.value) || 0);
    const query = queryInput.value.trim().toLocaleLowerCase('zh-CN');
    const ac = readAcRecords();
    return catalog.filter(problem => {
      if (!isAfterLessonTwenty(problem)) return false;
      if (Number(problem.oiRankScore) < minScore) return false;
      const text = `${problem.problemId} ${problem.title} ${problem.trainingTitle}`.toLocaleLowerCase('zh-CN');
      return !query || text.includes(query);
    }).sort((a, b) => {
      const acDifference = Number(Boolean(ac[problemKey(a)])) - Number(Boolean(ac[problemKey(b)]));
      return acDifference || Number(b.oiRankScore) - Number(a.oiRankScore) || String(a.problemId).localeCompare(String(b.problemId));
    });
  }

  function renderResults() {
    const records = filteredProblems();
    const ac = readAcRecords();
    const acceptedCount = records.filter(problem => ac[problemKey(problem)]).length;
    summaryRow.innerHTML = [
      `<span class="summary-chip accent">${records.length} 道符合条件</span>`,
      `<span class="summary-chip">第 20 课之后</span>`,
      `<span class="summary-chip">${catalogMeta.trainingCount} 个训练</span>`,
      `<span class="summary-chip">${catalogMeta.problemCount} 道训练题</span>`,
      `<span class="summary-chip">${acceptedCount} 道已 AC</span>`
    ].join('');

    problemGrid.innerHTML = records.map((problem, index) => {
      const isAccepted = Boolean(ac[problemKey(problem)]);
      const href = `programming-problem.html?trainingId=${encodeURIComponent(problem.trainingId)}&problemId=${encodeURIComponent(problem.problemId)}`;
      return `<article class="problem-card-shell${isAccepted ? ' is-ac' : ''}" data-index="${index}">
        <a class="problem-card ripple-button" href="${href}">
          <span class="problem-top"><span class="problem-id">${escapeHTML(problem.problemId)}</span><span class="rank-score">${escapeHTML(problem.oiRankScore)} <small>OI RANK</small></span></span>
          <h2 title="${escapeHTML(problem.title)}">${escapeHTML(problem.title)}</h2>
          <span class="problem-bottom"><span class="problem-training">${escapeHTML(problem.trainingTitle)}</span><span class="problem-score">${isAccepted ? '✓ 已 AC' : `原始分 ${escapeHTML(problem.score ?? '-')}`}</span></span>
        </a>
        <button class="problem-menu-toggle ripple-button" type="button" aria-label="${escapeHTML(problem.title)} 的更多操作" aria-expanded="false">•••</button>
        <div class="problem-card-menu" role="menu"><button class="menu-ac-action ripple-button" type="button" role="menuitem">${isAccepted ? '取消已 AC' : '设为已 AC'}</button></div>
      </article>`;
    }).join('');
    emptyState.classList.toggle('hidden', records.length > 0);
  }

  async function loadCatalog() {
    resultsPanel.classList.add('hidden');
    loadingPanel.classList.remove('hidden');
    try {
      const payload = await request('/api/programming/search?minScore=0&page=1&limit=2000');
      catalog = payload.records || [];
      catalogMeta = { trainingCount: payload.trainingCount || 0, problemCount: payload.problemCount || 0 };
      sessionStorage.setItem(CATALOG_CACHE_KEY, JSON.stringify({ username: currentUsername, records: catalog, meta: catalogMeta }));
      renderResults();
      resultsPanel.classList.remove('hidden');
    } catch (error) {
      if (error.status === 401) {
        setConnected(false);
        showSnackbar('连接已失效，请重新登录');
      } else {
        resultsPanel.classList.remove('hidden');
        showSnackbar(error.message);
      }
    } finally {
      loadingPanel.classList.add('hidden');
    }
  }

  loginForm.addEventListener('submit', async event => {
    event.preventDefault();
    const username = $('#usernameInput').value.trim();
    const password = $('#passwordInput').value;
    if (!username || !password) return;

    loginButton.disabled = true;
    loginButton.textContent = '正在连接...';
    try {
      const payload = await request('/api/programming/login', {
        method: 'POST',
        body: JSON.stringify({ username, password })
      });
      $('#passwordInput').value = '';
      setConnected(true, payload.username);
      await loadCatalog();
    } catch (error) {
      showSnackbar(error.message);
    } finally {
      loginButton.disabled = false;
      loginButton.textContent = '连接并搜题';
    }
  });

  logoutButton.addEventListener('click', async () => {
    try {
      await request('/api/programming/logout', { method: 'POST', body: '{}' });
    } catch (_) {}
    setConnected(false);
    sessionStorage.removeItem(CATALOG_CACHE_KEY);
    catalog = [];
    problemGrid.innerHTML = '';
    showSnackbar('已断开目标站账号');
  });

  searchButton.addEventListener('click', renderResults);
  queryInput.addEventListener('input', renderResults);
  scoreInput.addEventListener('input', renderResults);

  problemGrid.addEventListener('click', event => {
    const toggle = event.target.closest('.problem-menu-toggle');
    const action = event.target.closest('.menu-ac-action');
    if (!toggle && !action) return;

    event.preventDefault();
    event.stopPropagation();
    const shell = event.target.closest('.problem-card-shell');
    if (toggle) {
      const wasOpen = shell.classList.contains('menu-open');
      problemGrid.querySelectorAll('.menu-open').forEach(card => {
        card.classList.remove('menu-open');
        card.querySelector('.problem-menu-toggle')?.setAttribute('aria-expanded', 'false');
      });
      shell.classList.toggle('menu-open', !wasOpen);
      toggle.setAttribute('aria-expanded', String(!wasOpen));
      return;
    }

    const problem = filteredProblems()[Number(shell.dataset.index)];
    if (problem) setAc(problem, !readAcRecords()[problemKey(problem)]);
  });

  document.addEventListener('click', event => {
    if (!event.target.closest('.problem-card-shell')) {
      problemGrid.querySelectorAll('.menu-open').forEach(card => card.classList.remove('menu-open'));
    }
  });

  async function boot() {
    saveLastLocation({ page: 'index' });
    installRipple(document);
    sessionStorage.removeItem('online_exercises_programming_catalog_v1');
    sessionStorage.removeItem('online_exercises_programming_catalog_v2');
    try {
      const status = await request('/api/programming/status');
      setConnected(status.authenticated, status.username);
      if (status.authenticated) {
        let cached = null;
        try {
          cached = JSON.parse(sessionStorage.getItem(CATALOG_CACHE_KEY) || 'null');
        } catch (_) {}
        if (cached?.username === status.username && Array.isArray(cached.records)) {
          catalog = cached.records;
          catalogMeta = cached.meta || catalogMeta;
          renderResults();
        } else {
          await loadCatalog();
        }
      }
    } catch (_) {
      setConnected(false);
      showSnackbar('本地搜题服务未启动');
    }
  }

  boot();
})();
