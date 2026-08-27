(() => {
  'use strict';

  const { escapeHTML, highlightCode, renderMarkdown, renderMath, installRipple } = window.OnlineExercises;
  const $ = selector => document.querySelector(selector);
  const params = new URLSearchParams(location.search);
  const trainingId = params.get('trainingId') || '';
  const problemId = params.get('problemId') || '';
  const AC_STORAGE_KEY = 'online_exercises_programming_ac_v1';
  const DRAFT_PREFIX = 'online_exercises_programming_draft_v1';
  const PENDING_STATUSES = new Set([5, 6, 7, 9]);
  const STATUS = {
    '-10': '没有提交', '-5': '结果不可用', '-4': '已取消', '-3': '格式错误', '-2': '编译错误', '-1': '答案错误',
    0: 'Accepted', 1: '时间超限', 2: '内存超限', 3: '运行错误', 4: '系统错误',
    5: '等待评测', 6: '正在编译', 7: '正在评测', 8: '部分正确', 9: '正在提交', 10: '提交失败'
  };
  const LANGUAGE_WORDS = {
    cpp: {
      keyword: 'alignas alignof asm auto break case catch class const constexpr consteval constinit continue co_await co_return co_yield decltype default delete do else enum explicit export extern false for friend goto if inline mutable namespace new noexcept nullptr operator private protected public register requires return sizeof static static_assert struct switch template this thread_local throw true try typedef typename union using virtual volatile while'.split(' '),
      type: 'bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t string vector map set unordered_map unordered_set pair queue deque stack priority_queue'.split(' '),
      function: 'begin end sort reverse lower_bound upper_bound min max swap make_pair move forward cin cout cerr getline printf scanf'.split(' ')
    },
    python: {
      keyword: 'and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield'.split(' '),
      type: 'bool bytes dict float frozenset int list object range set str tuple type'.split(' '),
      function: 'abs all any bin chr enumerate filter format input len map max min open ord print reversed round sorted sum zip'.split(' ')
    },
    js: {
      keyword: 'async await break case catch class const continue debugger default delete do else export extends false finally for from function get if import in instanceof let new null of return set static super switch this throw true try typeof undefined var void while with yield'.split(' '),
      type: 'Array BigInt Boolean Date Error Function Map Number Object Promise RegExp Set String Symbol WeakMap WeakSet'.split(' '),
      function: 'clearInterval clearTimeout decodeURI encodeURI fetch parseFloat parseInt setInterval setTimeout'.split(' ')
    }
  };

  let problem = null;
  let languages = [];
  let templates = {};
  let draftTimer = 0;
  let snackbarTimer = 0;
  let pollGeneration = 0;
  let completionState = { items: [], index: 0, start: 0, end: 0, query: '' };

  async function request(path, options = {}) {
    const response = await fetch(path, { cache: 'no-store', headers: { 'Content-Type': 'application/json', ...(options.headers || {}) }, ...options });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      const error = new Error(payload.error || `请求失败（HTTP ${response.status}）`);
      error.status = response.status;
      throw error;
    }
    return payload;
  }

  function showSnackbar(message) {
    const snackbar = $('#snackbar');
    snackbar.textContent = message;
    snackbar.classList.add('show');
    clearTimeout(snackbarTimer);
    snackbarTimer = setTimeout(() => snackbar.classList.remove('show'), 2800);
  }

  function problemKey() { return `${trainingId}:${problemId}`; }

  function markAccepted(source = 'judge') {
    let records = {};
    try { records = JSON.parse(localStorage.getItem(AC_STORAGE_KEY) || '{}'); } catch (_) {}
    records[problemKey()] = { at: Date.now(), source };
    localStorage.setItem(AC_STORAGE_KEY, JSON.stringify(records));
    $('#acBadge').classList.remove('hidden');
  }

  function isAccepted() {
    try { return Boolean(JSON.parse(localStorage.getItem(AC_STORAGE_KEY) || '{}')[problemKey()]); } catch (_) { return false; }
  }

  function markdownInto(element, value) {
    element.innerHTML = renderMarkdown(String(value || ''));
    renderMath(element);
  }

  function renderOptionalSection(sectionSelector, bodySelector, value) {
    if (!String(value || '').trim()) return;
    $(sectionSelector).classList.remove('hidden');
    markdownInto($(bodySelector), value);
  }

  function normalizeExamples(value) {
    let examples = value;
    if (typeof examples === 'string') {
      try { examples = JSON.parse(examples); } catch (_) { return []; }
    }
    if (!Array.isArray(examples)) return [];
    return examples.map(item => ({
      input: String(item?.input ?? item?.inputText ?? ''),
      output: String(item?.output ?? item?.outputText ?? '')
    }));
  }

  function renderExamples(value) {
    const examples = normalizeExamples(value);
    if (!examples.length) return;
    $('#examplesSection').classList.remove('hidden');
    $('#examples').innerHTML = examples.map((example, index) => `<div class="example-grid">
      <div class="example-block"><span>输入${examples.length > 1 ? ` ${index + 1}` : ''}</span><pre>${escapeHTML(example.input)}</pre></div>
      <div class="example-block"><span>输出${examples.length > 1 ? ` ${index + 1}` : ''}</span><pre>${escapeHTML(example.output)}</pre></div>
    </div>`).join('');
  }

  function templateFor(language) {
    const value = templates?.[language];
    if (typeof value === 'string') return value;
    return String(value?.code ?? value?.template ?? '');
  }

  function draftKey(language = $('#languageSelect').value) {
    return `${DRAFT_PREFIX}:${trainingId}:${problemId}:${language}`;
  }

  function updateLineNumbers() {
    const editor = $('#codeInput');
    const lines = Math.max(1, editor.value.split('\n').length);
    $('#lineNumbers').textContent = Array.from({ length: lines }, (_, index) => index + 1).join('\n');
    $('#codeLength').textContent = `${editor.value.length} / 65535`;
    updateCodeHighlight();
  }

  function syntaxLanguage() {
    const language = String($('#languageSelect').value || '').toLowerCase();
    if (language.includes('c++') || language === 'c' || language.includes('gcc')) return 'cpp';
    if (language.includes('python') || language === 'py') return 'python';
    return 'js';
  }

  function updateCodeHighlight() {
    const editor = $('#codeInput');
    const code = editor.value || ' ';
    $('#codeHighlight code').innerHTML = highlightCode(code, syntaxLanguage()) + (code.endsWith('\n') ? ' ' : '');
    syncEditorScroll();
  }

  function syncEditorScroll() {
    const editor = $('#codeInput');
    const highlight = $('#codeHighlight');
    highlight.scrollTop = editor.scrollTop;
    highlight.scrollLeft = editor.scrollLeft;
    $('#lineNumbers').scrollTop = editor.scrollTop;
    if (!$('#completionPopup').classList.contains('hidden')) positionCompletionPopup();
  }

  function loadLanguage(language) {
    const draft = localStorage.getItem(draftKey(language));
    $('#codeInput').value = draft === null ? templateFor(language) : draft;
    updateLineNumbers();
    $('#draftState').textContent = draft === null ? '已载入模板' : '已恢复草稿';
  }

  function saveDraft() {
    localStorage.setItem(draftKey(), $('#codeInput').value);
    $('#draftState').textContent = '已保存';
  }

  function renderProblem(payload) {
    problem = payload.problem || payload;
    languages = Array.isArray(payload.languages) ? payload.languages.map(String) : [];
    templates = payload.codeTemplate || payload.codeTemplates || {};
    if (!languages.length) languages = Object.keys(templates);
    if (!languages.length) languages = ['C++'];

    document.title = `${problem.problemId || problemId} ${problem.title || '编程题'} · Online Exercises`;
    $('#problemNumber').textContent = problem.problemId || problemId;
    $('#problemTitle').textContent = problem.title || '未命名题目';
    if (isAccepted()) $('#acBadge').classList.remove('hidden');
    const meta = [];
    if (problem.timeLimit !== undefined && problem.timeLimit !== null) meta.push(`时间 ${problem.timeLimit} ms`);
    if (problem.memoryLimit !== undefined && problem.memoryLimit !== null) meta.push(`内存 ${problem.memoryLimit} MB`);
    if (problem.difficulty !== undefined && problem.difficulty !== null) meta.push(`难度 ${problem.difficulty}`);
    $('#problemMeta').innerHTML = meta.map(item => `<span class="meta-chip">${escapeHTML(item)}</span>`).join('');

    markdownInto($('#description'), problem.description || '暂无题目描述。');
    renderOptionalSection('#inputSection', '#inputDescription', problem.input);
    renderOptionalSection('#outputSection', '#outputDescription', problem.output);
    renderExamples(problem.examples);
    renderOptionalSection('#hintSection', '#hint', problem.hint);
    renderOptionalSection('#sourceSection', '#source', problem.source);

    $('#languageSelect').innerHTML = languages.map(language => `<option value="${escapeHTML(language)}">${escapeHTML(language)}</option>`).join('');
    loadLanguage(languages[0]);
  }

  function formatMemory(value) {
    if (value === undefined || value === null || value === '') return '—';
    const number = Number(value);
    if (!Number.isFinite(number)) return String(value);
    return number >= 1024 ? `${(number / 1024).toFixed(2)} MB` : `${number} KB`;
  }

  function formatTime(value) {
    if (value === undefined || value === null || value === '') return '—';
    return `${value} ms`;
  }

  function renderJudge(submission, submitId, pending = false) {
    const card = $('#judgeCard');
    const status = Number(submission?.status ?? 5);
    card.classList.remove('hidden', 'accepted', 'failed');
    if (!pending) card.classList.add(status === 0 ? 'accepted' : 'failed');
    $('#judgeStatus').textContent = STATUS[status] || `状态 ${status}`;
    $('#judgeDetails').innerHTML = `
      <div class="judge-detail"><span>提交 ID</span><strong>${escapeHTML(submitId || submission?.submitId || '—')}</strong></div>
      <div class="judge-detail"><span>得分</span><strong>${escapeHTML(submission?.score ?? '—')}</strong></div>
      <div class="judge-detail"><span>OI Rank</span><strong>${escapeHTML(submission?.oiRankScore ?? '—')}</strong></div>
      <div class="judge-detail"><span>时间</span><strong>${escapeHTML(formatTime(submission?.time))}</strong></div>
      <div class="judge-detail"><span>内存</span><strong>${escapeHTML(formatMemory(submission?.memory))}</strong></div>
      <div class="judge-detail"><span>语言</span><strong>${escapeHTML(submission?.language || $('#languageSelect').value || '—')}</strong></div>`;
    const message = submission?.errorMessage || submission?.message || '';
    $('#judgeMessage').textContent = message;
    $('#judgeMessage').classList.toggle('hidden', !message);
  }

  async function pollSubmission(submitId, generation) {
    for (let count = 0; count < 300 && generation === pollGeneration; count += 1) {
      const payload = await request(`/api/programming/submission?submitId=${encodeURIComponent(submitId)}`);
      const submission = payload.submission || {};
      const status = Number(submission.status ?? 5);
      const pending = !Object.keys(submission).length || PENDING_STATUSES.has(status);
      renderJudge(submission, submitId, pending);
      if (!pending) {
        if (status === 0) {
          markAccepted('judge');
          showSnackbar('Accepted，已记入本地 AC 记录');
        }
        return;
      }
      await new Promise(resolve => setTimeout(resolve, 2000));
    }
    if (generation === pollGeneration) showSnackbar('评测仍在进行，可以稍后回到目标站查看');
  }

  async function submitCode() {
    const code = $('#codeInput').value;
    if (!code.trim()) { showSnackbar('代码还是空的'); return; }
    if (code.length > 65535) { showSnackbar('代码超过 65535 字符'); return; }
    saveDraft();
    const button = $('#submitButton');
    button.disabled = true;
    button.textContent = '正在提交…';
    pollGeneration += 1;
    const generation = pollGeneration;
    try {
      const payload = await request('/api/programming/submit', {
        method: 'POST',
        body: JSON.stringify({ trainingId, problemId, language: $('#languageSelect').value, code, isRemote: Boolean(problem?.isRemote) })
      });
      renderJudge({ status: 9, language: $('#languageSelect').value }, payload.submitId, true);
      button.textContent = '评测中…';
      await pollSubmission(payload.submitId, generation);
    } catch (error) {
      if (error.status === 401) showSnackbar('登录已失效，请回到编程题目页重新连接');
      else showSnackbar(error.message);
    } finally {
      if (generation === pollGeneration) { button.disabled = false; button.textContent = '提交代码'; }
    }
  }

  function completionIcon(kind) {
    const icons = {
      keyword: '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="8" cy="12" r="3.2"/><path d="M11.2 12H21m-3 0v3m-3-3v2"/></svg>',
      variable: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 7.5 12 4l7 3.5v9L12 20l-7-3.5zM5 7.5l7 3.5 7-3.5M12 11v9"/></svg>',
      function: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 4H7.8C6.6 4 6 4.8 5.7 6L3.5 18c-.3 1.2-.9 2-2.1 2H1m2.8-9H10m3.5-3.5 7 9m0-9-7 9"/></svg>',
      type: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 6h16M8 6v14m8-14v14M5 20h6m2 0h6"/></svg>'
    };
    return icons[kind] || icons.variable;
  }

  function fuzzyMatch(name, query) {
    const target = name.toLowerCase();
    const needle = query.toLowerCase();
    const indexes = [];
    let cursor = 0;
    for (const character of needle) {
      const found = target.indexOf(character, cursor);
      if (found < 0) return null;
      indexes.push(found);
      cursor = found + 1;
    }
    const prefix = target.startsWith(needle) ? 0 : 1;
    const spread = indexes.length ? indexes[indexes.length - 1] - indexes[0] + 1 - indexes.length : 0;
    return { indexes, score: prefix * 1000 + spread * 20 + indexes[0] * 4 + name.length };
  }

  function highlightedCompletion(name, indexes) {
    const matched = new Set(indexes);
    return Array.from(name).map((character, index) => matched.has(index) ? `<strong>${escapeHTML(character)}</strong>` : escapeHTML(character)).join('');
  }

  function dynamicCompletions(source) {
    const found = new Map();
    const add = (name, kind) => {
      if (!name || name.length > 80 || LANGUAGE_WORDS[syntaxLanguage()]?.keyword.includes(name)) return;
      const key = `${kind}:${name}`;
      if (!found.has(key)) found.set(key, { name, kind });
    };
    const patterns = [
      { kind: 'type', regex: /\b(?:class|struct|enum|interface|type)\s+([A-Za-z_$][\w$]*)/g },
      { kind: 'function', regex: /\b(?:def|function)\s+([A-Za-z_$][\w$]*)\s*\(/g },
      { kind: 'function', regex: /\b(?:void|bool|char|short|int|long|float|double|string|auto|const|unsigned|signed|[A-Z][\w:$<>]*)\s+([A-Za-z_$][\w$]*)\s*\(/g },
      { kind: 'variable', regex: /\b(?:const|let|var|auto|bool|char|short|int|long|float|double|string|size_t|unsigned|signed|[A-Z][\w:$<>]*)\s+([A-Za-z_$][\w$]*)\b(?!\s*\()/g },
      { kind: 'variable', regex: /(?:^|[\n;,])\s*([A-Za-z_$][\w$]*)\s*=(?!=)/g }
    ];
    patterns.forEach(({ kind, regex }) => {
      for (const match of source.matchAll(regex)) add(match[1], kind);
    });
    for (const match of source.matchAll(/\(([^()\n]*)\)/g)) {
      match[1].split(',').forEach(parameter => {
        const names = parameter.trim().match(/[A-Za-z_$][\w$]*/g);
        if (names?.length) add(names[names.length - 1], 'variable');
      });
    }
    return [...found.values()];
  }

  function completionContext() {
    const editor = $('#codeInput');
    const end = editor.selectionStart;
    const before = editor.value.slice(0, end);
    const match = before.match(/[A-Za-z_$][\w$]*$/);
    return match ? { query: match[0], start: end - match[0].length, end } : null;
  }

  function closeCompletions() {
    $('#completionPopup').classList.add('hidden');
    completionState = { items: [], index: 0, start: 0, end: 0, query: '' };
    $('#codeInput').removeAttribute('aria-activedescendant');
  }

  function positionCompletionPopup() {
    const editor = $('#codeInput');
    const stack = editor.parentElement;
    const before = editor.value.slice(0, editor.selectionStart);
    const lines = before.split('\n');
    const row = lines.length - 1;
    const column = lines[lines.length - 1].length;
    const lineHeight = 23.1;
    let top = 16 + (row + 1) * lineHeight - editor.scrollTop;
    const left = Math.max(8, Math.min(16 + column * 8.45 - editor.scrollLeft, stack.clientWidth - 370));
    if (top > stack.clientHeight - 150) top = Math.max(8, 16 + row * lineHeight - editor.scrollTop - 230);
    stack.style.setProperty('--completion-top', `${Math.max(8, top)}px`);
    stack.style.setProperty('--completion-left', `${left}px`);
  }

  function renderCompletions() {
    const popup = $('#completionPopup');
    popup.innerHTML = completionState.items.map((item, index) => `<button class="completion-item ripple-button${index === completionState.index ? ' is-selected' : ''}" id="completion-${index}" type="button" role="option" aria-selected="${index === completionState.index}" data-index="${index}">
      <span class="completion-icon ${item.kind}">${completionIcon(item.kind)}</span>
      <span class="completion-name">${highlightedCompletion(item.name, item.indexes)}</span>
      <span class="completion-kind">${item.kind === 'keyword' ? 'Keyword' : item.kind === 'function' ? 'Function' : item.kind === 'type' ? 'Type' : 'Variable'}</span>
    </button>`).join('');
    popup.classList.remove('hidden');
    positionCompletionPopup();
    const selected = popup.children[completionState.index];
    selected?.scrollIntoView({ block: 'nearest' });
    $('#codeInput').setAttribute('aria-activedescendant', `completion-${completionState.index}`);
  }

  function updateCompletions() {
    const context = completionContext();
    if (!context?.query) { closeCompletions(); return; }
    const words = LANGUAGE_WORDS[syntaxLanguage()] || LANGUAGE_WORDS.js;
    const candidates = [
      ...words.keyword.map(name => ({ name, kind: 'keyword' })),
      ...words.type.map(name => ({ name, kind: 'type' })),
      ...words.function.map(name => ({ name, kind: 'function' })),
      ...dynamicCompletions($('#codeInput').value)
    ];
    const unique = new Map();
    candidates.forEach(candidate => {
      const match = fuzzyMatch(candidate.name, context.query);
      if (!match || candidate.name === context.query || unique.has(candidate.name)) return;
      unique.set(candidate.name, { ...candidate, ...match });
    });
    const items = [...unique.values()].sort((a, b) => a.score - b.score || a.name.localeCompare(b.name)).slice(0, 40);
    if (!items.length) { closeCompletions(); return; }
    completionState = { items, index: 0, ...context };
    renderCompletions();
  }

  function selectCompletion(delta) {
    const length = completionState.items.length;
    if (!length) return;
    completionState.index = (completionState.index + delta + length) % length;
    renderCompletions();
  }

  function acceptCompletion(index = completionState.index) {
    const item = completionState.items[index];
    if (!item) return false;
    const editor = $('#codeInput');
    editor.setRangeText(item.name, completionState.start, completionState.end, 'end');
    closeCompletions();
    editor.dispatchEvent(new Event('input'));
    return true;
  }

  function replaceEditorRange(text, start, end, caretOffset = text.length) {
    const editor = $('#codeInput');
    editor.setRangeText(text, start, end, 'start');
    editor.selectionStart = editor.selectionEnd = start + caretOffset;
    editor.dispatchEvent(new Event('input'));
  }

  function installEditor() {
    const editor = $('#codeInput');
    editor.addEventListener('input', () => {
      updateLineNumbers();
      updateCompletions();
      $('#draftState').textContent = '保存中…';
      clearTimeout(draftTimer);
      draftTimer = setTimeout(saveDraft, 450);
    });
    editor.addEventListener('scroll', syncEditorScroll);
    editor.addEventListener('keydown', event => {
      if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') {
        event.preventDefault();
        closeCompletions();
        submitCode();
        return;
      }

      const popupOpen = !$('#completionPopup').classList.contains('hidden');
      if (popupOpen && (event.key === 'ArrowDown' || event.key === 'ArrowUp')) {
        event.preventDefault();
        selectCompletion(event.key === 'ArrowDown' ? 1 : -1);
        return;
      }
      if (popupOpen && (event.key === 'Enter' || event.key === 'Tab')) {
        event.preventDefault();
        acceptCompletion();
        return;
      }
      if (popupOpen && event.key === 'Escape') {
        event.preventDefault();
        closeCompletions();
        return;
      }

      const start = editor.selectionStart;
      const end = editor.selectionEnd;
      const value = editor.value;
      const selected = value.slice(start, end);

      if (event.key === 'Backspace' && start === end) {
        if (value.slice(start - 2, start) === '/*' && value.slice(start, start + 2) === '*/') {
          event.preventDefault();
          replaceEditorRange('', start - 2, start + 2, 0);
          return;
        }
        const matching = { '(': ')', '[': ']', '{': '}', '"': '"', "'": "'", '`': '`' };
        if (matching[value[start - 1]] === value[start]) {
          event.preventDefault();
          replaceEditorRange('', start - 1, start + 1, 0);
          return;
        }
      }

      if (event.key === '*' && start === end && value[start - 1] === '/') {
        event.preventDefault();
        replaceEditorRange('**/', start, start, 1);
        return;
      }

      const pairs = { '(': ')', '[': ']', '{': '}', '"': '"', "'": "'", '`': '`' };
      if (pairs[event.key] && !event.ctrlKey && !event.metaKey && !event.altKey) {
        if (start === end && value[start] === event.key && ['"', "'", '`'].includes(event.key)) {
          event.preventDefault();
          editor.selectionStart = editor.selectionEnd = start + 1;
          closeCompletions();
          return;
        }
        event.preventDefault();
        replaceEditorRange(`${event.key}${selected}${pairs[event.key]}`, start, end, 1 + selected.length);
        return;
      }

      if ([')', ']', '}'].includes(event.key) && start === end && value[start] === event.key) {
        event.preventDefault();
        editor.selectionStart = editor.selectionEnd = start + 1;
        closeCompletions();
        return;
      }

      if (event.key === 'Enter') {
        event.preventDefault();
        const lineStart = value.lastIndexOf('\n', start - 1) + 1;
        const indent = value.slice(lineStart, start).match(/^ */)?.[0] || '';
        const opener = value[start - 1];
        const closer = value[start];
        const splitPair = ({ '(': ')', '[': ']', '{': '}' }[opener] === closer) || (value.slice(start - 2, start) === '/*' && value.slice(start, start + 2) === '*/');
        if (splitPair) {
          const insertion = `\n${indent}  \n${indent}`;
          replaceEditorRange(insertion, start, end, 1 + indent.length + 2);
        } else {
          replaceEditorRange(`\n${indent}`, start, end);
        }
        return;
      }

      if (event.key === 'Tab') {
        event.preventDefault();
        if (start !== end && value.slice(start, end).includes('\n')) {
          const blockStart = value.lastIndexOf('\n', start - 1) + 1;
          const block = value.slice(blockStart, end);
          const replacement = event.shiftKey ? block.replace(/^ {1,2}/gm, '') : block.replace(/^/gm, '  ');
          replaceEditorRange(replacement, blockStart, end, replacement.length);
        } else if (event.shiftKey) {
          const remove = Math.min(2, (value.slice(0, start).match(/ +$/)?.[0].length || 0));
          if (remove) replaceEditorRange('', start - remove, start, 0);
        } else replaceEditorRange('  ', start, end);
        return;
      }
    });
    editor.addEventListener('keyup', event => {
      if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) updateCompletions();
    });
    editor.addEventListener('click', updateCompletions);
    editor.addEventListener('blur', () => setTimeout(closeCompletions, 120));
    $('#completionPopup').addEventListener('pointerdown', event => event.preventDefault());
    $('#completionPopup').addEventListener('click', event => {
      const item = event.target.closest('.completion-item');
      if (!item) return;
      acceptCompletion(Number(item.dataset.index));
      editor.focus();
    });
    $('#languageSelect').addEventListener('change', event => { closeCompletions(); loadLanguage(event.target.value); });
    $('#submitButton').addEventListener('click', submitCode);
  }

  async function boot() {
    installRipple(document);
    installEditor();
    if (!trainingId || !problemId) {
      $('#loadingPanel').classList.add('hidden');
      $('#errorPanel').classList.remove('hidden');
      $('#errorText').textContent = '题目地址缺少训练编号或题号。';
      return;
    }
    try {
      const payload = await request(`/api/programming/problem?trainingId=${encodeURIComponent(trainingId)}&problemId=${encodeURIComponent(problemId)}`);
      renderProblem(payload);
      $('#loadingPanel').classList.add('hidden');
      $('#workspace').classList.remove('hidden');
    } catch (error) {
      $('#loadingPanel').classList.add('hidden');
      $('#errorPanel').classList.remove('hidden');
      $('#errorText').textContent = error.status === 401 ? '登录已失效，请回到“编程题目”重新连接目标站账号。' : error.message;
    }
  }

  window.addEventListener('load', () => document.querySelectorAll('.markdown-body').forEach(renderMath));
  boot();
})();
