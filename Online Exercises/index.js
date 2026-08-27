(() => {
  'use strict';

  const {
    buildLearnUrl,
    buildQuizUrl,
    countKnowledgeItems,
    countPackQuestions,
    DAILY_PACK_FILE,
    clearPackProgress,
    escapeHTML,
    getMonthCalendarData,
    installRipple,
    loadLibrary,
    packHasKnowledge,
    readDailyChallenges,
    readKnowledgeProgress,
    readPackProgress,
    renderMath,
    saveLastLocation
  } = window.OnlineExercises;

  const homeView = document.querySelector('#homeView');
  const reloadButton = document.querySelector('#reloadButton');
  const snackbar = document.querySelector('#snackbar');

  let snackbarTimer = 0;
  let packsCache = [];
  let editMode = false;
  let openedMenuFile = '';
  let confirmDeleteFile = '';
  let keyBuffer = '';
  let keyTimer = 0;

  function showSnackbar(message) {
    snackbar.textContent = message;
    snackbar.classList.add('show');
    clearTimeout(snackbarTimer);
    snackbarTimer = window.setTimeout(() => snackbar.classList.remove('show'), 2200);
  }

  function clearEditMenus() {
    openedMenuFile = '';
    confirmDeleteFile = '';
  }

  function enterEditMode() {
    if (editMode) return;
    editMode = true;
    clearEditMenus();
    renderHome();
    showSnackbar('已进入 Edit 模式');
  }

  function exitEditMode() {
    if (!editMode) return;
    editMode = false;
    clearEditMenus();
    renderHome();
    showSnackbar('已退出 Edit 模式');
  }

  function renderMenu(pack) {
    const isOpen = openedMenuFile === pack.file;
    const isConfirm = confirmDeleteFile === pack.file;
    return `<div class="pack-menu ${isOpen ? 'open' : ''}" aria-hidden="${!isOpen}">
      <div class="pack-menu-label">当前题集操作</div>
      <button class="text-button ripple-button pack-menu-danger ${isConfirm ? 'hidden' : ''}" type="button" data-action="prepare-clear" data-pack-file="${escapeHTML(pack.file)}">清空数据</button>
      <button class="text-button ripple-button pack-menu-confirm ${isConfirm ? '' : 'hidden'}" type="button" data-action="confirm-clear" data-pack-file="${escapeHTML(pack.file)}">确认删除</button>
    </div>`;
  }

  function renderCalendar() {
    const calendar = getMonthCalendarData();
    return `<section class="daily-calendar-panel question-card">
      <div class="daily-calendar-head">
        <div>
          <h2>每日挑战日历</h2>
          <p>${escapeHTML(calendar.title)}</p>
        </div>
      </div>
      <div class="daily-calendar-weekdays">
        <span>S</span><span>M</span><span>T</span><span>W</span><span>T</span><span>F</span><span>S</span>
      </div>
      <div class="daily-calendar-grid">${calendar.cells.map(cell => {
        if (cell.type === 'empty') return '<span class="calendar-cell empty" aria-hidden="true"></span>';
        return `<span class="calendar-cell ${cell.completed ? 'completed' : ''} ${cell.today ? 'today' : ''}" title="${cell.dateKey}">
          <span>${cell.day}</span>
        </span>`;
      }).join('')}</div>
    </section>`;
  }

  function renderDailyChallenge(pack) {
    const progress = readPackProgress(pack.file);
    const completions = readDailyChallenges();
    const completed = Boolean(completions[pack.dateKey]?.completed);
    const total = countPackQuestions(pack);
    const subtitle = completed
      ? '今天已完成'
      : progress
        ? `今天做到第 ${Math.min((progress.index || 0) + 1, total)} / ${total} 题`
        : `今日题目 · ${pack.dateKey}`;

    return `<section class="daily-math-hero">
      <article class="daily-math-card question-card">
        <div class="daily-math-badge">Daily</div>
        <h2>每日数学挑战</h2>
        <p class="daily-math-subtitle">${escapeHTML(subtitle)}</p>
        <div class="daily-math-actions">
          <button class="filled-button ripple-button" type="button" data-pack-file="${escapeHTML(pack.file)}">${completed ? '再做一遍' : '开始挑战'}</button>
        </div>
      </article>
      ${renderCalendar()}
    </section>`;
  }

  function getPackRoute(pack) {
    const progress = readPackProgress(pack.file);
    const knowledgeProgress = readKnowledgeProgress(pack.file);
    if (packHasKnowledge(pack) && !knowledgeProgress.completed) {
      return buildLearnUrl(pack.file, knowledgeProgress.currentItemId || '');
    }
    return progress?.lastView === 'quiz'
      ? buildQuizUrl(pack.file, progress.index || 0)
      : buildQuizUrl(pack.file, 0);
  }

  function renderPackList(packs) {
    return `<div class="pack-grid">${packs.map(pack => {
      const progress = readPackProgress(pack.file);
      const total = countPackQuestions(pack);
      const knowledgeCount = countKnowledgeItems(pack);
      const knowledgeProgress = readKnowledgeProgress(pack.file);
      let note = `<small>${total} 道题</small>`;
      if (packHasKnowledge(pack) && !knowledgeProgress.completed) {
        const readCount = knowledgeProgress.readIds.length;
        note = `<small class="resume-note">知识点 ${Math.min(readCount + 1, Math.max(knowledgeCount, 1))} / ${knowledgeCount}</small>`;
      } else if (progress) {
        note = progress.lastView === 'results'
          ? '<small class="resume-note">上次已完成，点击可再做一遍</small>'
          : `<small class="resume-note">上次做到第 ${Math.min((progress.index || 0) + 1, total)} / ${total} 题</small>`;
      }

      if (!editMode) {
        return `<article class="pack-entry">
          <button class="pack-card pack-card-button ripple-button" type="button" data-pack-file="${escapeHTML(pack.file)}">
            <span class="pack-icon">${escapeHTML(pack.name.slice(0, 1))}</span>
            <span class="pack-info">
              <strong>${escapeHTML(pack.name)}</strong>
              <span class="pack-meta-line">
                ${knowledgeCount ? `<span class="mini-badge">知识点 ${knowledgeCount}</span>` : ''}
                ${pack.file === DAILY_PACK_FILE ? '<span class="mini-badge daily">Daily</span>' : ''}
              </span>
              ${note}
            </span>
            <span class="pack-card-end">
              <span class="pack-arrow" aria-hidden="true">→</span>
            </span>
          </button>
        </article>`;
      }

      return `<article class="pack-entry ${editMode ? 'editing' : ''}">
        <div class="pack-card">
          <button class="pack-open ripple-button" type="button" data-pack-file="${escapeHTML(pack.file)}" ${editMode ? 'disabled' : ''}>
            <span class="pack-icon">${escapeHTML(pack.name.slice(0, 1))}</span>
            <span class="pack-info">
              <strong>${escapeHTML(pack.name)}</strong>
              <span class="pack-meta-line">
                ${knowledgeCount ? `<span class="mini-badge">知识点 ${knowledgeCount}</span>` : ''}
                ${pack.file === DAILY_PACK_FILE ? '<span class="mini-badge daily">Daily</span>' : ''}
              </span>
              ${note}
            </span>
          </button>
          <div class="pack-card-end">
            <button class="pack-menu-toggle ripple-button" type="button" data-action="toggle-menu" data-pack-file="${escapeHTML(pack.file)}" aria-label="当前题集菜单">≡</button>
          </div>
        </div>
        ${renderMenu(pack)}
      </article>`;
    }).join('')}</div>`;
  }

  function renderHome() {
    const dailyPack = packsCache.find(pack => pack.file === DAILY_PACK_FILE);
    const normalPacks = packsCache.filter(pack => pack.file !== DAILY_PACK_FILE);

    homeView.innerHTML = `<div class="view-heading">
        <h1 id="libraryTitle">选择题目</h1>
        <p>${editMode ? 'Edit 模式中：点右侧 ≡ 可清空当前题集记录；按 3 下空格，或依次按 e x i t 退出。' : ''}</p>
      </div>
      ${dailyPack ? renderDailyChallenge(dailyPack) : ''}
      <section class="programming-section" aria-labelledby="programmingTitle">
        <div class="section-heading">
          <h2 id="programmingTitle">编程题目</h2>
        </div>
        <a class="programming-card ripple-button" href="programming.html">
          <span class="programming-icon" aria-hidden="true">&lt;/&gt;</span>
          <span class="programming-info">
            <strong>OI Rank 搜题</strong>
            <small>查看 OI Rank Score ≥ 10 的编程题</small>
          </span>
          <span class="pack-arrow" aria-hidden="true">→</span>
        </a>
      </section>
      <section class="library-section">
        <div class="section-heading">
          <h2>题集列表</h2>
        </div>
        ${renderPackList(normalPacks)}
      </section>`;
    renderMath(homeView);
  }

  function routeToPack(packFile) {
    const pack = packsCache.find(item => item.file === packFile);
    if (!pack) return;
    location.href = getPackRoute(pack);
  }

  function pushKey(key) {
    clearTimeout(keyTimer);
    keyBuffer = `${keyBuffer}${key}`.slice(-8);
    keyTimer = window.setTimeout(() => { keyBuffer = ''; }, 1000);
  }

  async function refresh() {
    saveLastLocation({ page: 'index' });
    homeView.innerHTML = '<article class="question-card skeleton-card"><span class="skeleton short"></span><span class="skeleton wide"></span><span class="skeleton wide"></span></article>';
    try {
      packsCache = await loadLibrary();
      renderHome();
    } catch (error) {
      homeView.innerHTML = `<article class="question-card error-state"><div><h2>题库加载失败</h2><p>${escapeHTML(error.message)}</p></div></article>`;
      showSnackbar('无法读取 YAML 题库');
    }
  }

  homeView.addEventListener('click', event => {
    const openButton = event.target.closest('[data-pack-file]');
    if (openButton && !editMode && !event.target.closest('[data-action]')) {
      routeToPack(openButton.dataset.packFile);
      return;
    }

    const actionButton = event.target.closest('[data-action]');
    if (!actionButton) return;
    const packFile = actionButton.dataset.packFile;
    const action = actionButton.dataset.action;

    if (action === 'toggle-menu') {
      openedMenuFile = openedMenuFile === packFile ? '' : packFile;
      confirmDeleteFile = '';
      renderHome();
      return;
    }

    if (action === 'prepare-clear') {
      openedMenuFile = packFile;
      confirmDeleteFile = packFile;
      renderHome();
      return;
    }

    if (action === 'confirm-clear') {
      clearPackProgress(packFile);
      openedMenuFile = '';
      confirmDeleteFile = '';
      renderHome();
      showSnackbar(packFile === DAILY_PACK_FILE ? '今日挑战记录已清空' : '当前题集记录已清空');
    }
  });

  document.addEventListener('keydown', event => {
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
    if (!editMode) {
      if (key === 'e') {
        pushKey('e');
        if (keyBuffer.endsWith('ee') || keyBuffer.endsWith('eee')) {
          enterEditMode();
          keyBuffer = '';
        }
      }
      return;
    }

    if (key === ' ') {
      event.preventDefault();
      pushKey(' ');
    } else if (/^[a-z]$/.test(key)) {
      pushKey(key);
    } else {
      return;
    }

    if (keyBuffer.endsWith('   ') || keyBuffer.endsWith('exit')) {
      exitEditMode();
      keyBuffer = '';
    }
  });

  reloadButton.addEventListener('click', refresh);
  installRipple(document);
  refresh();
})();
