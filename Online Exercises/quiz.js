(() => {
  'use strict';

  const {
    blankAnswer,
    buildLearnUrl,
    buildQuizUrl,
    buildReviewUrl,
    buildResultsUrl,
    clearPackProgress,
    computeResults,
    deserializeAnswer,
    resolvePackFile,
    escapeHTML,
    getExpectedIds,
    getKnowledgeOutline,
    getQuestions,
    installRipple,
    isCorrect,
    isInputType,
    loadLibrary,
    markDailyChallengeComplete,
    packHasKnowledge,
    readKnowledgeProgress,
    readLastLocation,
    readPackProgress,
    renderMarkdown,
    renderMath,
    saveKnowledgeProgress,
    saveLastLocation,
    savePackProgress,
    serializeAnswer
  } = window.OnlineExercises;

  const typeNames = {
    single: '单选题',
    multiple: '多选题',
    judge: '判断题',
    matching: '连线题',
    number: '填数字',
    text: '填字母',
    cloze: '完形填空',
    reading: '阅读题',
    ordering: '连词成句'
  };

  const host = document.querySelector('#questionHost');
  const resultView = document.querySelector('#resultView');
  const progressSection = document.querySelector('#progressSection');
  const questionNav = document.querySelector('#questionNav');
  const prevButton = document.querySelector('#prevButton');
  const resetButton = document.querySelector('#resetButton');
  const nextButton = document.querySelector('#nextButton');
  const reloadButton = document.querySelector('#reloadButton');
  const focusButton = document.querySelector('#focusButton');
  const snackbar = document.querySelector('#snackbar');
  const fireworkCanvas = document.querySelector('#fireworkCanvas');
  const fireworkContext = fireworkCanvas.getContext('2d');
  const particles = [];
  const particleColors = ['#f38ba8', '#fab387', '#f9e2af', '#a6e3a1', '#94e2d5', '#89dceb', '#74c7ec', '#89b4fa', '#b4befe', '#cba6f7', '#f5c2e7'];

  const state = {
    packs: [],
    currentPack: null,
    questions: [],
    answers: new Map(),
    index: 0,
    view: 'quiz',
    reviewMode: false,
    focusMode: false,
    activeLeft: '',
    snackbarTimer: 0,
    checkTimer: 0,
    advanceTimer: 0,
    digitTimer: 0,
    optionKeyTimer: 0,
    lastOptionKey: '',
    lastOptionKeyTime: 0,
    knowledge: {
      outline: { chapters: [], items: [] },
      currentIndex: 0,
      progress: {
        currentItemId: '',
        readIds: [],
        collapsedChapterIds: [],
        completed: false
      }
    }
  };

  function $(selector, root = document) {
    return root.querySelector(selector);
  }

  function $$(selector, root = document) {
    return [...root.querySelectorAll(selector)];
  }

  function showSnackbar(message) {
    snackbar.textContent = message;
    snackbar.classList.add('show');
    clearTimeout(state.snackbarTimer);
    state.snackbarTimer = window.setTimeout(() => snackbar.classList.remove('show'), 2200);
  }

  function setFocusMode(enabled) {
    state.focusMode = Boolean(enabled);
    document.body.classList.toggle('focus-mode', state.focusMode);
    focusButton?.setAttribute('aria-pressed', String(state.focusMode));
    focusButton?.setAttribute('aria-label', state.focusMode ? '退出专注模式' : '进入专注模式');
    focusButton?.setAttribute('title', state.focusMode ? '退出专注模式' : '专注模式');
  }

  async function enterFocusMode() {
    setFocusMode(true);
    try {
      if (!document.fullscreenElement && document.documentElement.requestFullscreen) {
        await document.documentElement.requestFullscreen();
      }
    } catch (_) {
      showSnackbar('浏览器没有允许全屏，但已进入专注布局');
    }
  }

  async function exitFocusMode() {
    setFocusMode(false);
    try {
      if (document.fullscreenElement && document.exitFullscreen) await document.exitFullscreen();
    } catch (_) {}
  }

  function setView(view) {
    state.view = view;
    const quizMode = view === 'quiz';
    const learnMode = view === 'learn';
    progressSection.classList.toggle('hidden', !quizMode);
    host.classList.toggle('hidden', false);
    questionNav.classList.toggle('hidden', !quizMode);
    resultView.classList.toggle('hidden', view !== 'results');
    host.classList.toggle('learn-mode', learnMode);
  }

  function getAnswerState(index = state.index) {
    if (!state.answers.has(index)) state.answers.set(index, blankAnswer());
    return state.answers.get(index);
  }

  function serializeAnswers() {
    return [...state.answers.entries()].map(([index, answer]) => [index, serializeAnswer(answer)]);
  }

  function restoreAnswers(entries) {
    state.answers.clear();
    if (!Array.isArray(entries)) return;
    for (const [index, answer] of entries) {
      state.answers.set(Number(index), deserializeAnswer(answer));
    }
  }

  function saveProgress(lastView = state.view) {
    if (!state.currentPack) return;
    if (state.view === 'learn') {
      saveKnowledgeState();
      return;
    }
    if (state.reviewMode) {
      saveLastLocation({ page: 'results', packFile: state.currentPack.file });
      return;
    }
    savePackProgress(state.currentPack.file, {
      index: state.index,
      answers: serializeAnswers(),
      lastView
    });
    saveLastLocation(lastView === 'results'
      ? { page: 'results', packFile: state.currentPack.file }
      : { page: 'quiz', packFile: state.currentPack.file, index: state.index });
  }

  function updateUrl() {
    if (!state.currentPack) return;
    const url = state.view === 'results'
      ? buildResultsUrl(state.currentPack.file)
      : state.view === 'learn'
        ? buildLearnUrl(state.currentPack.file, state.knowledge.outline.items[state.knowledge.currentIndex]?.id || '')
        : state.reviewMode
          ? buildReviewUrl(state.currentPack.file, state.index)
          : buildQuizUrl(state.currentPack.file, state.index);
    history.replaceState({}, '', url);
  }

  function getKnowledgeItem(itemId) {
    return state.knowledge.outline.items.find(item => item.id === itemId) || null;
  }

  function getKnowledgeReadSet() {
    return new Set(state.knowledge.progress.readIds);
  }

  function getKnowledgeChapterCompletion(chapter) {
    const readSet = getKnowledgeReadSet();
    return chapter.items.length > 0 && chapter.items.every(item => readSet.has(item.id));
  }

  function autoCollapseCompletedChapters() {
    const collapsed = new Set(state.knowledge.progress.collapsedChapterIds);
    for (const chapter of state.knowledge.outline.chapters) {
      if (getKnowledgeChapterCompletion(chapter)) collapsed.add(chapter.id);
    }
    state.knowledge.progress.collapsedChapterIds = [...collapsed];
  }

  function saveKnowledgeState() {
    if (!state.currentPack) return;
    const currentItem = state.knowledge.outline.items[state.knowledge.currentIndex];
    if (currentItem) state.knowledge.progress.currentItemId = currentItem.id;
    autoCollapseCompletedChapters();
    saveKnowledgeProgress(state.currentPack.file, state.knowledge.progress);
    saveLastLocation({
      page: 'learn',
      packFile: state.currentPack.file,
      itemId: currentItem?.id || ''
    });
  }

  function renderKnowledgeSidebar() {
    const current = state.knowledge.outline.items[state.knowledge.currentIndex];
    const readSet = getKnowledgeReadSet();
    const collapsed = new Set(state.knowledge.progress.collapsedChapterIds);
    return `<aside class="knowledge-sidebar">
      <div class="knowledge-sidebar-head">
        <strong>目录</strong>
        <small>${state.knowledge.currentIndex + 1} / ${state.knowledge.outline.items.length}</small>
      </div>
      <div class="knowledge-directory">
        ${state.knowledge.outline.chapters.map(chapter => {
          const done = getKnowledgeChapterCompletion(chapter);
          const isCollapsed = collapsed.has(chapter.id);
          return `<section class="knowledge-chapter ${done ? 'done' : ''} ${isCollapsed ? 'collapsed' : ''}">
            <button class="knowledge-chapter-head ripple-button" type="button" data-knowledge-action="toggle-chapter" data-chapter-id="${escapeHTML(chapter.id)}">
              <span class="knowledge-chapter-title">${escapeHTML(chapter.title)}</span>
              <span class="knowledge-chapter-state">${done ? '✓' : isCollapsed ? '+' : '−'}</span>
            </button>
            <div class="knowledge-chapter-items">
              ${chapter.items.map(item => {
                const active = current?.id === item.id;
                const read = readSet.has(item.id);
                return `<button class="knowledge-item-link ripple-button ${active ? 'active' : ''} ${read ? 'done' : ''}" type="button" data-knowledge-action="jump" data-item-id="${escapeHTML(item.id)}">
                  <span class="knowledge-item-text">${escapeHTML(item.title)}</span>
                  <span class="knowledge-item-check">${read ? '✓' : ''}</span>
                </button>`;
              }).join('')}
            </div>
          </section>`;
        }).join('')}
      </div>
    </aside>`;
  }

  function renderKnowledgeView() {
    const item = state.knowledge.outline.items[state.knowledge.currentIndex];
    if (!item) return;
    const isLast = state.knowledge.currentIndex === state.knowledge.outline.items.length - 1;
    const readSet = getKnowledgeReadSet();
    const readCount = readSet.size;

    host.innerHTML = `<article class="question-card knowledge-card">
      <div class="knowledge-layout">
        ${renderKnowledgeSidebar()}
        <section class="knowledge-content-panel">
          <div class="card-top knowledge-top">
            <div class="question-heading-row">
              <h2 class="question-title">${escapeHTML(item.title)}</h2>
              <span class="type-chip">知识点</span>
            </div>
            <div class="card-score"><span>${readCount}</span><small>已读</small></div>
          </div>
          <div class="knowledge-meta">
            <span>${escapeHTML(item.chapterTitle)}</span>
            <span>${state.knowledge.currentIndex + 1} / ${state.knowledge.outline.items.length}</span>
          </div>
          <div class="knowledge-content-scroll">
            <div class="knowledge-body">${renderMarkdown(item.content)}</div>
          </div>
          <div class="knowledge-footer">
            <div class="knowledge-shortcuts">Ctrl+Enter 下一页　·　Ctrl+← 上一页</div>
            <div class="knowledge-actions">
              <button class="text-button ripple-button" type="button" data-knowledge-action="prev-item" ${state.knowledge.currentIndex === 0 ? 'disabled' : ''}>上一页</button>
              <button class="filled-button ripple-button" type="button" data-knowledge-action="${isLast ? 'start-quiz' : 'next-item'}">${isLast ? '开始练习' : '看完了'}</button>
            </div>
          </div>
        </section>
      </div>
    </article>`;
    updateUrl();
    saveKnowledgeState();
    renderMath(host);
  }

  function goToKnowledgeIndex(index) {
    const nextIndex = Math.max(0, Math.min(index, state.knowledge.outline.items.length - 1));
    state.knowledge.currentIndex = nextIndex;
    const current = state.knowledge.outline.items[nextIndex];
    if (current) {
      state.knowledge.progress.currentItemId = current.id;
      const chapter = state.knowledge.outline.chapters.find(item => item.id === current.chapterId);
      if (chapter && !getKnowledgeChapterCompletion(chapter)) {
        state.knowledge.progress.collapsedChapterIds = state.knowledge.progress.collapsedChapterIds.filter(id => id !== chapter.id);
      }
    }
    setView('learn');
    renderKnowledgeView();
    window.scrollTo({ top: 0, behavior: 'smooth' });
  }

  function markKnowledgeRead(index = state.knowledge.currentIndex) {
    const item = state.knowledge.outline.items[index];
    if (!item) return;
    const readSet = getKnowledgeReadSet();
    readSet.add(item.id);
    state.knowledge.progress.readIds = [...readSet];
    state.knowledge.progress.currentItemId = item.id;
    state.knowledge.progress.completed = readSet.size >= state.knowledge.outline.items.length;
    autoCollapseCompletedChapters();
  }

  function advanceKnowledge() {
    markKnowledgeRead();
    if (state.knowledge.currentIndex >= state.knowledge.outline.items.length - 1) {
      state.knowledge.progress.completed = true;
      saveKnowledgeState();
      location.href = buildQuizUrl(state.currentPack.file, 0);
      return;
    }
    goToKnowledgeIndex(state.knowledge.currentIndex + 1);
  }

  function retreatKnowledge() {
    if (state.knowledge.currentIndex === 0) return;
    goToKnowledgeIndex(state.knowledge.currentIndex - 1);
  }

  function toggleKnowledgeChapter(chapterId) {
    const collapsed = new Set(state.knowledge.progress.collapsedChapterIds);
    if (collapsed.has(chapterId)) collapsed.delete(chapterId);
    else collapsed.add(chapterId);
    state.knowledge.progress.collapsedChapterIds = [...collapsed];
    renderKnowledgeView();
  }

  function renderClozeArticle(question) {
    const getFilledText = blankNumber => {
      const target = state.questions.find((item, index) => {
        const sameGroup = item.clozeGroupId && item.clozeGroupId === question.clozeGroupId;
        return sameGroup && Number(item.clozeIndex) === Number(blankNumber) && getAnswerState(index).attempted;
      });
      if (!target) return '';
      if (isInputType(target)) return getExpectedIds(target)[0] || '';
      const expectedId = getExpectedIds(target)[0];
      return target.options.find(option => option.id === expectedId)?.text || '';
    };

    return renderMarkdown(question.article).replace(/\{\{(\d+)\}\}/g, (_, number) => {
      const active = Number(number) === Number(question.clozeIndex) ? 'active' : '';
      const filled = getFilledText(number).trim();
      const filledClass = filled ? 'filled' : '';
      return `<span class="cloze-blank ${active} ${filledClass}">${filled ? escapeHTML(filled) : `${number}. ______`}</span>`;
    });
  }

  function renderCorrectAnswer(question) {
    let items = [];
    if (question.type === 'matching') {
      const expected = question.answers || question.answer || {};
      items = Object.entries(expected).map(([leftId, rightId]) => {
        const left = question.left.find(item => item.id === String(leftId));
        const right = question.right.find(item => item.id === String(rightId));
        return `${left?.text ?? leftId} → ${right?.text ?? rightId}`;
      });
    } else if (question.type === 'ordering') {
      items = [getExpectedIds(question).map(id => question.words.find(word => word.id === Number(id))?.text || id).join(' ')];
    } else if (isInputType(question)) {
      items = getExpectedIds(question);
    } else {
      items = getExpectedIds(question).map(id => question.options.find(option => option.id === id)?.text ?? id);
    }
    return `<div class="correct-answer-label">正确答案：</div><ul class="correct-answer-list">${items.map(item => `<li>${renderMarkdown(item)}</li>`).join('')}</ul>`;
  }

  function optionButton(option, index, answer, type) {
    const selected = answer.selected.has(option.id);
    const expected = getExpectedIds(state.questions[state.index]);
    const revealCorrect = answer.attempted && !answer.correct && expected.includes(option.id);
    const marker = type === 'judge' ? (option.id === 'true' ? '○' : '×') : String.fromCharCode(65 + index);
    return `<button class="option-button ripple-button ${selected ? 'selected' : ''} ${revealCorrect ? 'correct-answer' : ''}" type="button" data-option-id="${escapeHTML(option.id)}" aria-pressed="${selected}" ${answer.attempted ? 'disabled' : ''}>
      <span class="option-marker">${marker}</span><span class="option-content">${renderMarkdown(option.text)}</span>
    </button>`;
  }

  function compactOptionText(text) {
    return String(text || '')
      .replace(/```[\s\S]*?```/g, block => block.replace(/```[^\n]*\n?|\n?```/g, ''))
      .replace(/`([^`]*)`/g, '$1')
      .replace(/\*\*([^*]*)\*\*/g, '$1')
      .trim();
  }

  function shouldUseCompactOptions(question) {
    if (!Array.isArray(question.options) || question.options.length !== 4) return false;
    return question.options.every(option => {
      const text = compactOptionText(option.text);
      const compactLength = text.replace(/\s+/g, '').length;
      return compactLength > 0 && compactLength < 15 && /^[\d\s+\-*/%=<>(){}[\].,;:|&^!?~]+$/.test(text);
    });
  }

  function matchNode(item, side, answer) {
    const connected = side === 'left' ? Boolean(answer.matches[item.id]) : Object.values(answer.matches).includes(item.id);
    return `<button class="match-node ripple-button ${connected ? 'connected' : ''}" type="button" data-side="${side}" data-node-id="${escapeHTML(item.id)}" ${answer.attempted ? 'disabled' : ''}>${renderMarkdown(item.text)}</button>`;
  }

  function renderOrdering(question, answer) {
    const chosen = answer.order.map(number => question.words.find(word => word.id === number)).filter(Boolean);
    const locked = state.reviewMode || answer.attempted;
    return `<div class="ordering-area">
      <div class="word-bank" aria-label="词语列表">${question.words.map(word => `
        <button class="word-chip ripple-button ${answer.order.includes(word.id) ? 'used' : ''}" type="button" data-word-number="${word.id}" ${locked || answer.order.includes(word.id) ? 'disabled' : ''}>
          <span>${word.id}</span>${escapeHTML(word.text)}
        </button>`).join('')}</div>
      <div class="sentence-line" aria-label="已连接的句子">${chosen.length ? chosen.map(word => `<span class="sentence-token"><small>${word.id}</small>${escapeHTML(word.text)}</span>`).join('') : '<span class="sentence-placeholder">直接输入编号来连词成句</span>'}</div>
      <div class="order-input-row ${locked ? (answer.correct ? 'correct' : 'wrong') : ''}">
        <div class="order-ghost-input" aria-label="词语编号" aria-live="polite">${escapeHTML(answer.pendingDigits || '·')}</div>
        <span>${state.reviewMode ? '查看模式' : '直接输入数字 · Backspace 撤回 · Enter 检查'}</span>
      </div>
    </div>`;
  }

  function hasResponse(question, answer) {
    if (!question) return false;
    if (question.type === 'matching') return Object.keys(answer.matches).length > 0;
    if (question.type === 'ordering') return answer.order.length > 0;
    if (isInputType(question)) return Boolean(answer.input.trim());
    return answer.selected.size > 0;
  }

  function updateProgress() {
    const correct = [...state.answers.values()].filter(answer => answer.correct).length;
    const progressPercent = state.questions.length ? Math.round(((state.index + 1) / state.questions.length) * 100) : 0;
    $('#correctCount').textContent = correct;
    $('#progressText').textContent = `第 ${state.index + 1} / ${state.questions.length} 题`;
    $('#progressPercent').textContent = `${progressPercent}%`;
    $('#progressBar').style.width = `${progressPercent}%`;
  }

  function renderQuestion() {
    const question = state.questions[state.index];
    const answer = getAnswerState();
    const locked = state.reviewMode || answer.attempted;
    const isArticleGroup = question.type === 'cloze' || question.type === 'reading';
    let interaction = '';

    if (question.type === 'matching') {
      interaction = `<div class="matching-board" id="matchingBoard">
        <svg class="matching-lines" id="matchingLines" aria-hidden="true"></svg>
        <div class="match-column" data-side="left">${question.left.map(item => matchNode(item, 'left', { ...answer, attempted: locked })).join('')}</div>
        <div class="match-column" data-side="right">${question.right.map(item => matchNode(item, 'right', { ...answer, attempted: locked })).join('')}</div>
      </div>`;
    } else if (question.type === 'ordering') {
      interaction = renderOrdering(question, answer);
    } else if (isInputType(question)) {
      const numeric = question.type === 'number';
      interaction = `<div class="answer-field">
        <input class="answer-input ${locked ? (answer.correct ? 'correct' : 'wrong') : ''}" id="answerInput" type="text" ${numeric ? 'inputmode="decimal"' : 'inputmode="text" autocapitalize="off" spellcheck="false"'} value="${escapeHTML(answer.input)}" placeholder="${numeric ? '输入数字' : '输入字母'}" aria-label="${numeric ? '数字答案' : '字母答案'}" ${locked ? 'disabled' : ''}>
        <span class="input-hint">${state.reviewMode ? '查看模式' : '输入后按 Enter 检查'}</span>
      </div>`;
    } else {
      interaction = `<div class="options ${question.type === 'multiple' ? 'multiple' : ''} ${shouldUseCompactOptions(question) ? 'compact-options' : ''}" role="group" aria-label="答案选项">
        ${question.options.map((option, index) => optionButton(option, index, { ...answer, attempted: locked }, question.type)).join('')}
      </div>`;
    }

    const content = isArticleGroup
      ? `<div class="cloze-article ${question.type === 'reading' ? 'reading-article' : ''}">${question.type === 'cloze' ? renderClozeArticle(question) : renderMarkdown(question.article)}</div>
         <div class="cloze-question"><span>第 ${question.clozeIndex} / ${question.clozeTotal} ${question.type === 'cloze' ? '空' : '题'}</span>${renderMarkdown(question.prompt)}</div>`
      : `<div class="question-content">${renderMarkdown(question.prompt)}</div>`;

    const feedbackVisible = state.reviewMode || answer.attempted;
    const feedbackClass = feedbackVisible ? `show ${answer.correct ? 'correct' : 'wrong'}` : '';
    const feedbackTitle = state.reviewMode && answer.outcome === 'skipped'
      ? '本题已跳过'
      : answer.correct
        ? '回答正确！'
        : '回答不正确';
    const correctAnswer = feedbackVisible && !answer.correct ? renderCorrectAnswer(question) : '';

    host.innerHTML = `<article class="question-card ${isArticleGroup ? 'cloze-card' : ''} ${answer.correct ? 'is-correct' : answer.attempted ? 'is-wrong' : ''}" ${question.type === 'ordering' ? 'tabindex="-1"' : ''}>
      <div class="card-top">
        <div class="question-heading-row"><h2 class="question-title">${escapeHTML(question.title)}</h2><span class="type-chip">${typeNames[question.type] || '题目'}</span></div>
        <div class="card-score" aria-live="polite"><span id="correctCount">0</span><small>答对</small></div>
      </div>
      ${content}
      ${interaction}
      <div class="feedback ${feedbackClass}"><strong>${feedbackTitle}</strong>${correctAnswer}${question.explanation ? `<div class="explanation">${renderMarkdown(question.explanation)}</div>` : ''}</div>
    </article>`;

    prevButton.disabled = state.index === 0;
    const hasAnswer = hasResponse(question, answer);
    resetButton.textContent = state.reviewMode ? '返回记录' : hasAnswer || answer.attempted ? '重置本题' : '跳过';
    nextButton.classList.toggle('hidden', state.reviewMode ? false : !answer.attempted);
    nextButton.textContent = state.index === state.questions.length - 1 ? '查看记录' : '下一题';
    updateProgress();
    updateUrl();
    saveProgress(state.reviewMode ? 'results' : 'quiz');
    renderMath(host);
    requestAnimationFrame(drawMatchingLines);
    if (question.type === 'ordering') requestAnimationFrame(() => $('.question-card')?.focus());
  }

  function drawMatchingLines() {
    const board = $('#matchingBoard');
    const svg = $('#matchingLines');
    if (!board || !svg) return;
    const rect = board.getBoundingClientRect();
    svg.setAttribute('viewBox', `0 0 ${rect.width} ${rect.height}`);
    svg.innerHTML = '';
    for (const [leftId, rightId] of Object.entries(getAnswerState().matches)) {
      const left = board.querySelector(`[data-side="left"][data-node-id="${CSS.escape(leftId)}"]`);
      const right = board.querySelector(`[data-side="right"][data-node-id="${CSS.escape(rightId)}"]`);
      if (!left || !right) continue;
      const leftRect = left.getBoundingClientRect();
      const rightRect = right.getBoundingClientRect();
      const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      line.setAttribute('x1', leftRect.right - rect.left);
      line.setAttribute('y1', leftRect.top + leftRect.height / 2 - rect.top);
      line.setAttribute('x2', rightRect.left - rect.left);
      line.setAttribute('y2', rightRect.top + rightRect.height / 2 - rect.top);
      svg.appendChild(line);
    }
  }

  function clearAttemptFeedback(answer) {
    if (answer.correct) return;
    answer.attempted = false;
    answer.outcome = null;
  }

  function createFirework(x, y) {
    if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    for (let i = 0; i < 260; i++) {
      const angle = Math.random() * Math.PI * 2;
      const velocity = Math.random() * 9 + 2;
      particles.push({
        x,
        y,
        vx: Math.cos(angle) * velocity,
        vy: Math.sin(angle) * velocity,
        life: 1,
        decay: Math.random() * 0.018 + 0.014,
        size: Math.random() * 3 + 1,
        color: particleColors[Math.floor(Math.random() * particleColors.length)]
      });
    }
  }

  function resizeFireworkCanvas() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    fireworkCanvas.width = innerWidth * dpr;
    fireworkCanvas.height = innerHeight * dpr;
    fireworkContext.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  function animateFireworks() {
    fireworkContext.clearRect(0, 0, innerWidth, innerHeight);
    for (let i = particles.length - 1; i >= 0; i--) {
      const p = particles[i];
      p.vx *= 0.96;
      p.vy = p.vy * 0.96 + 0.14;
      p.x += p.vx;
      p.y += p.vy;
      p.life -= p.decay;
      if (p.life <= 0) {
        particles.splice(i, 1);
        continue;
      }
      fireworkContext.globalAlpha = p.life;
      fireworkContext.fillStyle = p.color;
      fireworkContext.beginPath();
      fireworkContext.arc(p.x, p.y, p.size, 0, Math.PI * 2);
      fireworkContext.fill();
    }
    fireworkContext.globalAlpha = 1;
    requestAnimationFrame(animateFireworks);
  }

  function clearQuestionTimers() {
    clearTimeout(state.checkTimer);
    clearTimeout(state.advanceTimer);
    clearTimeout(state.digitTimer);
    clearTimeout(state.optionKeyTimer);
  }

  function selectOptionById(optionId) {
    const question = state.questions[state.index];
    const answer = getAnswerState();
    if (!question || answer.attempted || state.reviewMode) return;
    if (question.type === 'multiple') {
      answer.selected.has(optionId) ? answer.selected.delete(optionId) : answer.selected.add(optionId);
    } else {
      answer.selected = new Set([optionId]);
    }
    clearAttemptFeedback(answer);
    renderQuestion();
    if (answer.selected.size) scheduleAutoCheck();
  }

  function scheduleAutoCheck() {
    clearTimeout(state.checkTimer);
    const baseDelay = state.questions[state.index]?.type === 'multiple' ? 5000 : 1000;
    const delay = state.focusMode ? Math.round(baseDelay * 2 / 3) : baseDelay;
    state.checkTimer = window.setTimeout(autoCheckAnswer, delay);
  }

  function scheduleAdvance() {
    clearTimeout(state.advanceTimer);
    state.advanceTimer = window.setTimeout(advanceQuestion, 3000);
  }

  function autoCheckAnswer() {
    const question = state.questions[state.index];
    const answer = getAnswerState();
    if (!question || answer.correct || state.reviewMode) return;
    if (isInputType(question) && !answer.input.trim()) {
      showSnackbar('请先输入答案');
      return;
    }
    answer.correct = isCorrect(question, answer);
    answer.attempted = true;
    answer.outcome = answer.correct ? 'correct' : 'wrong';
    if (answer.correct) {
      const origin = $('.option-button.selected, .match-node.connected, .answer-input, .order-ghost-input, .question-card');
      const rect = origin.getBoundingClientRect();
      createFirework(rect.left + rect.width / 2, rect.top + rect.height / 2);
    } else {
      showSnackbar('答案不正确，已直接显示正确答案');
    }
    renderQuestion();
    scheduleAdvance();
  }

  function showResults() {
    clearQuestionTimers();
    state.reviewMode = false;
    setView('results');
    updateUrl();
    const result = computeResults(state.questions, state.answers);
    if (state.currentPack?.daily && state.currentPack.dateKey) {
      markDailyChallengeComplete(state.currentPack.dateKey, result);
    }
    saveProgress('results');
    resultView.innerHTML = `<div class="view-heading result-heading"><p class="result-overline">${escapeHTML(state.currentPack.name)}</p><h1>练习记录</h1></div>
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
        ${packHasKnowledge(state.currentPack) ? '<button class="text-button ripple-button" id="knowledgeButton" type="button">查看知识点</button>' : ''}
        <button class="text-button ripple-button" id="backHomeButton" type="button">回到题目列表</button>
      </div>`;
    window.scrollTo({ top: 0, behavior: 'smooth' });
  }

  function goToQuestion(index) {
    clearQuestionTimers();
    state.index = Math.max(0, Math.min(index, state.questions.length - 1));
    setView('quiz');
    renderQuestion();
    window.scrollTo({ top: 0, behavior: 'smooth' });
  }

  function advanceQuestion() {
    if (state.index >= state.questions.length - 1) {
      showResults();
      return;
    }
    goToQuestion(state.index + 1);
  }

  function skipQuestion() {
    const answer = getAnswerState();
    answer.outcome = 'skipped';
    answer.correct = false;
    answer.attempted = false;
    answer.pendingDigits = '';
    saveProgress('quiz');
    advanceQuestion();
  }

  function addOrderWord(number, shouldRender = true) {
    const question = state.questions[state.index];
    const answer = getAnswerState();
    if (!question.words.some(word => word.id === number) || answer.order.includes(number)) {
      if (answer.order.includes(number)) showSnackbar('这个词语已经用过了');
      return;
    }
    answer.order.push(number);
    answer.pendingDigits = '';
    answer.attempted = false;
    answer.outcome = null;
    if (shouldRender) renderQuestion();
    else saveProgress('quiz');
  }

  function commitPendingOrder(shouldRender = true) {
    clearTimeout(state.digitTimer);
    const answer = getAnswerState();
    const number = Number(answer.pendingDigits);
    answer.pendingDigits = '';
    if (number) addOrderWord(number, shouldRender);
    else if (shouldRender) renderQuestion();
  }

  function undoOrderWord() {
    const answer = getAnswerState();
    clearTimeout(state.digitTimer);
    if (answer.pendingDigits) answer.pendingDigits = '';
    else answer.order.pop();
    renderQuestion();
  }

  function handleOrderDigit(digit) {
    const question = state.questions[state.index];
    const answer = getAnswerState();
    clearTimeout(state.digitTimer);
    const combined = `${answer.pendingDigits}${digit}`;
    const exact = question.words.some(word => word.id === Number(combined));
    const hasLonger = question.words.some(word => String(word.id).startsWith(combined) && String(word.id).length > combined.length);
    answer.pendingDigits = combined;
    if (hasLonger) {
      renderQuestion();
      state.digitTimer = window.setTimeout(() => commitPendingOrder(), 500);
    } else if (exact) {
      commitPendingOrder();
    } else {
      answer.pendingDigits = '';
      renderQuestion();
      showSnackbar(`没有编号为 ${combined} 的词语`);
    }
  }

  host.addEventListener('click', event => {
    if (state.view === 'learn') {
      const button = event.target.closest('[data-knowledge-action]');
      if (!button) return;
      const action = button.dataset.knowledgeAction;
      if (action === 'toggle-chapter') {
        toggleKnowledgeChapter(button.dataset.chapterId);
      } else if (action === 'jump') {
        const target = getKnowledgeItem(button.dataset.itemId);
        if (target) goToKnowledgeIndex(state.knowledge.outline.items.findIndex(item => item.id === target.id));
      } else if (action === 'prev-item') {
        retreatKnowledge();
      } else if (action === 'next-item' || action === 'start-quiz') {
        advanceKnowledge();
      }
      return;
    }

    if (state.reviewMode) return;
    const question = state.questions[state.index];
    const answer = getAnswerState();
    const wordChip = event.target.closest('.word-chip');
    if (wordChip) {
      addOrderWord(Number(wordChip.dataset.wordNumber));
      return;
    }

    const option = event.target.closest('.option-button');
    if (option) {
      selectOptionById(option.dataset.optionId);
      return;
    }

    const node = event.target.closest('.match-node');
    if (!node) return;
    const side = node.dataset.side;
    if (side === 'left') {
      state.activeLeft = node.dataset.nodeId;
      $$('.match-node[data-side="left"]').forEach(item => item.classList.toggle('active', item === node));
      return;
    }
    if (!state.activeLeft) {
      showSnackbar('请先选择左侧的一项');
      return;
    }
    for (const [key, value] of Object.entries(answer.matches)) {
      if (value === node.dataset.nodeId) delete answer.matches[key];
    }
    answer.matches[state.activeLeft] = node.dataset.nodeId;
    clearAttemptFeedback(answer);
    renderQuestion();
    if (Object.keys(answer.matches).length === question.left.length) scheduleAutoCheck();
  });

  host.addEventListener('input', event => {
    if (state.reviewMode || state.view !== 'quiz') return;
    if (!event.target.matches('.answer-input')) return;
    const answer = getAnswerState();
    answer.input = event.target.value;
    if (answer.correct) return;
    answer.attempted = false;
    answer.outcome = null;
    saveProgress('quiz');
  });

  host.addEventListener('keydown', event => {
    if (state.reviewMode || state.view !== 'quiz') return;
    if (event.target.matches('.answer-input') && event.key === 'Enter') {
      event.preventDefault();
      clearTimeout(state.checkTimer);
      autoCheckAnswer();
    }
  });

  document.addEventListener('keydown', event => {
    if (state.focusMode && event.key === 'Escape') {
      exitFocusMode();
      return;
    }
    if (state.view === 'learn') {
      if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') {
        event.preventDefault();
        advanceKnowledge();
        return;
      }
      if ((event.ctrlKey || event.metaKey) && event.key === 'ArrowLeft') {
        event.preventDefault();
        retreatKnowledge();
      }
      return;
    }

    if (state.view !== 'quiz') return;
    const question = state.questions[state.index];
    if (!question) return;
    if (state.reviewMode) {
      if (event.key === 'Enter' && state.index === state.questions.length - 1) {
        event.preventDefault();
        showResults();
      }
      return;
    }
    if (question.type === 'ordering' && !event.target.matches('.answer-input')) {
      if (event.ctrlKey || event.metaKey || event.altKey || event.repeat) return;
      if (/^\d$/.test(event.key)) {
        event.preventDefault();
        handleOrderDigit(event.key);
        return;
      }
      if (event.key === 'Backspace') {
        event.preventDefault();
        undoOrderWord();
        return;
      }
      if (event.key === 'Enter') {
        event.preventDefault();
        commitPendingOrder(false);
        if (getAnswerState().order.length) autoCheckAnswer();
        return;
      }
    }
    const isChoiceQuestion = Array.isArray(question.options) && question.options.length > 0 && !isInputType(question) && question.type !== 'matching' && question.type !== 'ordering';
    if (isChoiceQuestion && !event.target.matches('.answer-input') && !event.repeat) {
      const key = event.key.toLowerCase();
      const optionIndex = key.charCodeAt(0) - 97;
      if (optionIndex >= 0 && optionIndex < question.options.length) {
        const now = Date.now();
        if (state.lastOptionKey === key && now - state.lastOptionKeyTime <= 1000) {
          event.preventDefault();
          clearTimeout(state.optionKeyTimer);
          state.lastOptionKey = '';
          state.lastOptionKeyTime = 0;
          selectOptionById(question.options[optionIndex].id);
          return;
        }
        state.lastOptionKey = key;
        state.lastOptionKeyTime = now;
        clearTimeout(state.optionKeyTimer);
        state.optionKeyTimer = window.setTimeout(() => {
          state.lastOptionKey = '';
          state.lastOptionKeyTime = 0;
        }, 1000);
      }
    }
    if (event.key === 'Enter' && !event.target.matches('.answer-input') && !getAnswerState().attempted && hasResponse(question, getAnswerState())) {
      event.preventDefault();
      clearTimeout(state.checkTimer);
      autoCheckAnswer();
    }
  });

  prevButton.addEventListener('click', () => {
    if (state.index > 0) goToQuestion(state.index - 1);
  });

  nextButton.addEventListener('click', advanceQuestion);

  resetButton.addEventListener('click', () => {
    if (state.reviewMode) {
      showResults();
      return;
    }
    const question = state.questions[state.index];
    const answer = getAnswerState();
    clearQuestionTimers();
    if (!hasResponse(question, answer) && !answer.attempted) {
      skipQuestion();
      return;
    }
    state.answers.set(state.index, blankAnswer());
    renderQuestion();
  });

  resultView.addEventListener('click', event => {
    if (event.target.closest('#retryButton')) {
      clearPackProgress(state.currentPack.file);
      state.reviewMode = false;
      state.answers.clear();
      state.index = 0;
      if (packHasKnowledge(state.currentPack)) {
        location.href = buildLearnUrl(state.currentPack.file);
        return;
      }
      goToQuestion(0);
      return;
    }
    if (event.target.closest('#reviewButton')) {
      state.reviewMode = true;
      goToQuestion(0);
      return;
    }
    if (event.target.closest('#knowledgeButton')) {
      const saved = readKnowledgeProgress(state.currentPack.file);
      state.knowledge.progress = saved;
      state.knowledge.currentIndex = 0;
      goToKnowledgeIndex(0);
      return;
    }
    if (event.target.closest('#backHomeButton')) {
      saveLastLocation({ page: 'index' });
      location.href = 'index.html';
    }
  });

  reloadButton.addEventListener('click', () => location.reload());
  focusButton?.addEventListener('click', () => {
    state.focusMode ? exitFocusMode() : enterFocusMode();
  });
  document.addEventListener('fullscreenchange', () => {
    if (!document.fullscreenElement && state.focusMode) setFocusMode(false);
  });
  window.addEventListener('resize', () => {
    resizeFireworkCanvas();
    drawMatchingLines();
  });

  async function boot() {
    installRipple(document);
    resizeFireworkCanvas();
    animateFireworks();

    const params = new URLSearchParams(location.search);
    let packFile = params.get('pack');
    const requestedView = params.get('view');
    const requestedIndex = Number(params.get('q'));
    const requestedKnowledgeItemId = params.get('k');

    if (!packFile) {
      const last = readLastLocation();
      if (last.page === 'learn' && last.packFile) {
        location.replace(buildLearnUrl(last.packFile, last.itemId || ''));
        return;
      }
      if (last.page === 'quiz' && last.packFile) {
        location.replace(buildQuizUrl(last.packFile, last.index || 0));
        return;
      }
      if (last.page === 'results' && last.packFile) {
        location.replace(buildResultsUrl(last.packFile));
        return;
      }
      location.replace('index.html');
      return;
    }

    try {
      state.packs = await loadLibrary();
      packFile = resolvePackFile(packFile, state.packs);
      state.currentPack = state.packs.find(pack => pack.file === packFile);
      if (!state.currentPack) throw new Error(`没有找到题集 ${packFile}`);

      state.questions = getQuestions(state.currentPack);
      state.knowledge.outline = getKnowledgeOutline(state.currentPack);
      state.knowledge.progress = readKnowledgeProgress(packFile);

      const saved = readPackProgress(packFile);
      restoreAnswers(saved?.answers || []);
      state.index = Number.isFinite(requestedIndex)
        ? Math.max(0, Math.min(requestedIndex, state.questions.length - 1))
        : Math.max(0, Math.min(saved?.index || 0, state.questions.length - 1));

      if (requestedView === 'results') {
        showResults();
        return;
      }

      if (requestedView === 'review') {
        state.reviewMode = true;
        setView('quiz');
        renderQuestion();
        return;
      }

      if (requestedView === 'learn' && state.knowledge.outline.items.length) {
        const targetItemId = requestedKnowledgeItemId || state.knowledge.progress.currentItemId;
        const targetIndex = targetItemId
          ? Math.max(0, state.knowledge.outline.items.findIndex(item => item.id === targetItemId))
          : 0;
        state.knowledge.currentIndex = targetIndex < 0 ? 0 : targetIndex;
        setView('learn');
        renderKnowledgeView();
        return;
      }

      if (packHasKnowledge(state.currentPack) && !state.knowledge.progress.completed && !saved?.answers?.length && !requestedView) {
        location.replace(buildLearnUrl(state.currentPack.file, state.knowledge.progress.currentItemId || ''));
        return;
      }

      state.reviewMode = false;
      setView('quiz');
      renderQuestion();
    } catch (error) {
      host.innerHTML = `<article class="question-card error-state"><div><h2>题库加载失败</h2><p>${escapeHTML(error.message)}</p></div></article>`;
      questionNav.classList.add('hidden');
      progressSection.classList.add('hidden');
      showSnackbar('无法读取 YAML 题库');
    }
  }

  boot();
})();
