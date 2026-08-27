(() => {
  'use strict';

  const STORAGE_KEY = 'online_exercises_session_v5';
  const DAILY_PACK_FILE = '__daily_math__.yaml';

  function getTodayDateKey() {
    const now = new Date();
    const year = now.getFullYear();
    const month = String(now.getMonth() + 1).padStart(2, '0');
    const day = String(now.getDate()).padStart(2, '0');
    return `${year}-${month}-${day}`;
  }

  function hashString(input) {
    let hash = 2166136261;
    for (let index = 0; index < input.length; index++) {
      hash ^= input.charCodeAt(index);
      hash = Math.imul(hash, 16777619);
    }
    return hash >>> 0;
  }

  function createSeededRandom(seedText) {
    let state = hashString(seedText) || 1;
    return () => {
      state += 0x6D2B79F5;
      let value = state;
      value = Math.imul(value ^ (value >>> 15), value | 1);
      value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
      return ((value ^ (value >>> 14)) >>> 0) / 4294967296;
    };
  }

  function randomInt(random, min, max) {
    return Math.floor(random() * (max - min + 1)) + min;
  }

  function createMixedOperationQuestion(random, index) {
    let prompt = '';
    let answer = 0;
    if (index % 2 === 0) {
      const a = randomInt(random, 18, 42);
      const b = randomInt(random, 4, 12);
      const c = randomInt(random, 5, 14);
      answer = a + b * c;
      prompt = `计算：$${a} + ${b} \\times ${c}$`;
    } else {
      const base = randomInt(random, 8, 22);
      const add = randomInt(random, 9, 26);
      const factor = randomInt(random, 3, 8);
      answer = (base + add) * factor;
      prompt = `计算：$(${base} + ${add}) \\times ${factor}$`;
    }
    return {
      type: 'number',
      title: `每日混合运算 ${index + 1}`,
      prompt,
      answer
    };
  }

  function createDivisionQuestion(random, index) {
    const answer = randomInt(random, 6, 24);
    const divisor = randomInt(random, 3, 14);
    const dividend = answer * divisor;
    return {
      type: 'number',
      title: `每日计算 ${index + 1}`,
      prompt: `计算：$${dividend} \\div ${divisor}$`,
      answer
    };
  }

  function createEquationQuestion(random, index) {
    const mode = index % 3;
    const x = randomInt(random, 4, 24);
    let prompt = '';
    if (mode === 0) {
      const add = randomInt(random, 6, 18);
      prompt = `解方程：$x + ${add} = ${x + add}$`;
    } else if (mode === 1) {
      const minus = randomInt(random, 4, 16);
      prompt = `解方程：$x - ${minus} = ${x - minus}$`;
    } else {
      const factor = randomInt(random, 3, 11);
      prompt = `解方程：$${factor}x = ${factor * x}$`;
    }
    return {
      type: 'number',
      title: `每日方程 ${index + 1}`,
      prompt,
      answer: x
    };
  }

  function generateDailyMathPack(dateKey = getTodayDateKey()) {
    const random = createSeededRandom(`daily-math:${dateKey}`);
    return {
      file: DAILY_PACK_FILE,
      name: '每日数学挑战',
      description: `固定题目日期：${dateKey}`,
      daily: true,
      dateKey,
      questions: [
        createMixedOperationQuestion(random, 0),
        createMixedOperationQuestion(random, 1),
        createDivisionQuestion(random, 2),
        createEquationQuestion(random, 0),
        createEquationQuestion(random, 1),
        createEquationQuestion(random, 2)
      ]
    };
  }

  function escapeHTML(value = '') {
    return String(value).replace(/[&<>'"]/g, char => ({
      '&': '&amp;',
      '<': '&lt;',
      '>': '&gt;',
      "'": '&#39;',
      '"': '&quot;'
    })[char]);
  }

  function highlightCode(code, language = '') {
    const lang = language.toLowerCase().replace(/javascript.*/, 'js').replace(/python.*/, 'py').replace(/c\+\+.*/, 'cpp');
    const keywords = {
      js: new Set('as async await break case catch class const continue debugger default delete do else export extends finally for from function get if import in instanceof let new of return set static super switch this throw try typeof var void while with yield'.split(' ')),
      py: new Set('and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield'.split(' ')),
      cpp: new Set('alignas alignof auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend if inline int long namespace new nullptr operator private protected public register return short signed sizeof static struct switch template this throw true try typedef typename union unsigned using virtual void volatile wchar_t while'.split(' '))
    };
    const words = keywords[lang] || keywords.js;
    const types = {
      js: new Set('Array BigInt Boolean Date Error Function Map Number Object Promise RegExp Set String Symbol WeakMap WeakSet'.split(' ')),
      py: new Set('bool bytes dict float frozenset int list object range set str tuple type'.split(' ')),
      cpp: new Set('bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t string vector map set pair queue deque stack'.split(' '))
    }[lang] || new Set();
    const builtins = {
      js: new Set('console document fetch JSON Math parseFloat parseInt setInterval setTimeout window'.split(' ')),
      py: new Set('abs all any bin chr enumerate filter format input len map max min open ord print reversed round sorted sum zip'.split(' ')),
      cpp: new Set('begin cerr end getline lower_bound make_pair max min printf reverse scanf sort swap upper_bound'.split(' '))
    }[lang] || new Set();
    const constants = new Set(lang === 'py' ? ['True', 'False', 'None'] : ['true', 'false', 'null', 'nullptr', 'undefined']);
    const localNames = new Map();
    function addLocalName(name) {
      if (!name || localNames.has(name) || words.has(name) || types.has(name) || builtins.has(name) || constants.has(name)) return;
      localNames.set(name, localNames.size % 6);
    }
    const localPatterns = [
      /\b(?:const|let|var|auto|bool|char|short|int|long|float|double|string|size_t|unsigned|signed|[A-Z][\w:$<>]*)\s+([A-Za-z_$][\w$]*)\b(?!\s*\()/g,
      /(?:^|[\n;,])\s*([A-Za-z_$][\w$]*)\s*=(?!=)/g
    ];
    localPatterns.forEach(pattern => {
      for (const match of code.matchAll(pattern)) addLocalName(match[1]);
    });
    for (const match of code.matchAll(/\(([^()\n;]*)\)\s*(?:const\s*)?(?:->\s*[\w:<>,\s*&]+)?\s*\{/g)) {
      match[1].split(',').forEach(parameter => {
        const names = parameter.trim().match(/[A-Za-z_$][\w$]*/g);
        if (names?.length) addLocalName(names[names.length - 1]);
      });
    }
    const tokenPattern = /(\/\/[^\n]*|#\s*[A-Za-z_]\w*|\/\*[\s\S]*?\*\/|'''[\s\S]*?'''|"""[\s\S]*?"""|<[^>\n]+>|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|\b(?:0[xX][\da-fA-F]+|\d+(?:\.\d+)?)\b|\b[A-Za-z_$][\w$]*\b|(?:===|!==|==|!=|<=|>=|=>|<<|>>|&&|\|\||\+\+|--|\+=|-=|\*=|\/=|%=|[-+*/%=!<>&|^~?:]+)|[{}()[\],.;])/g;
    const rainbowBrackets = ['red', 'peach', 'yellow', 'green', 'sapphire', 'lavender'];
    const bracketStack = [];
    const bracketPairs = { ')': '(', ']': '[', '}': '{' };
    let output = '';
    let cursor = 0;
    for (const match of code.matchAll(tokenPattern)) {
      output += escapeHTML(code.slice(cursor, match.index));
      const token = match[0];
      let kind = '';
      if (token.startsWith('#')) kind = lang === 'cpp' ? 'macro' : 'comment';
      else if (lang === 'cpp' && /^<[^>\n]+>$/.test(token) && code.slice(0, match.index).split('\n').pop().trimStart().startsWith('#include')) kind = 'string';
      else if (/^(\/\/|\/\*|'''|""")/.test(token)) kind = 'comment';
      else if (/^['"]/.test(token)) {
        const stringHTML = escapeHTML(token).replace(/(\\(?:x[\da-fA-F]{2}|u[\da-fA-F]{4}|.|$))/g, '<span class="tok-escape">$1</span>');
        output += `<span class="tok-string">${stringHTML}</span>`;
        cursor = match.index + token.length;
        continue;
      }
      else if (/^\d/.test(token)) kind = 'number';
      else if (constants.has(token) || /^[A-Z][A-Z\d_]+$/.test(token)) kind = 'constant';
      else if (words.has(token)) kind = 'keyword';
      else if (types.has(token)) kind = 'type';
      else if (builtins.has(token)) kind = 'builtin';
      else if (/^[A-Za-z_$]/.test(token) && /^\s*\(/.test(code.slice(match.index + token.length))) kind = 'function';
      else if (localNames.has(token)) kind = `local-${localNames.get(token) + 1}`;
      else if (/^[([{]$/.test(token)) {
        const color = rainbowBrackets[bracketStack.length % rainbowBrackets.length];
        bracketStack.push(token);
        output += `<span class="tok-rainbow-${color}">${escapeHTML(token)}</span>`;
        cursor = match.index + token.length;
        continue;
      }
      else if (/^[)\]}]$/.test(token)) {
        const depth = Math.max(0, bracketStack.length - 1);
        const color = rainbowBrackets[depth % rainbowBrackets.length];
        if (bracketStack[bracketStack.length - 1] === bracketPairs[token]) bracketStack.pop();
        else if (bracketStack.length) bracketStack.pop();
        output += `<span class="tok-rainbow-${color}">${escapeHTML(token)}</span>`;
        cursor = match.index + token.length;
        continue;
      }
      else if (/^[,.;]$/.test(token)) kind = 'delimiter';
      else if (/^(?:===|!==|==|!=|<=|>=|=>|<<|>>|&&|\|\||\+\+|--|\+=|-=|\*=|\/=|%=|[-+*/%=!<>&|^~?:]+)$/.test(token)) kind = 'operator';
      output += kind ? `<span class="tok-${kind}">${escapeHTML(token)}</span>` : escapeHTML(token);
      cursor = match.index + token.length;
    }
    return output + escapeHTML(code.slice(cursor));
  }

  function renderMarkdown(source = '') {
    const blocks = [];
    let text = String(source).replace(/\r\n?/g, '\n').replace(/```\s*([\w+#-]*)\n([\s\S]*?)```/g, (_, lang, code) => {
      const language = lang || 'code';
      const token = `@@CODEBLOCK_${blocks.length}@@`;
      blocks.push(`<div class="code-block"><span class="code-lang">${escapeHTML(language)}</span><pre><code>${highlightCode(code.replace(/\n$/, ''), language)}</code></pre></div>`);
      return `\n\n${token}\n\n`;
    });

    text = escapeHTML(text)
      .replace(/``([^\n]+?)``/g, '<code>$1</code>')
      .replace(/(^|[^`])`([^`\n]+?)`(?!`)/g, '$1<code>$2</code>')
      .replace(/\*\*([^\n]+?)\*\*/g, '<strong>$1</strong>');

    return text.split(/\n{2,}/).map(part => {
      const trimmed = part.trim();
      const blockMatch = trimmed.match(/^@@CODEBLOCK_(\d+)@@$/);
      if (blockMatch) return blocks[Number(blockMatch[1])];
      return trimmed ? `<p>${trimmed.replace(/\n/g, '<br>')}</p>` : '';
    }).join('');
  }

  function renderMath(root) {
    if (typeof window.renderMathInElement !== 'function') return;
    window.renderMathInElement(root, {
      delimiters: [
        { left: '$$', right: '$$', display: false },
        { left: '$', right: '$', display: false }
      ],
      throwOnError: false,
      strict: false
    });
  }

  function normalizeOption(option, index) {
    return typeof option === 'object'
      ? { id: String(option.id ?? index), text: String(option.text ?? option.label ?? option.id ?? '') }
      : { id: String(index), text: String(option) };
  }

  function normalizeKnowledgeItem(item, index, chapterIndex) {
    if (typeof item === 'string') {
      return {
        id: `k-${chapterIndex + 1}-${index + 1}`,
        title: `知识点 ${index + 1}`,
        content: item
      };
    }

    return {
      id: String(item.id ?? `k-${chapterIndex + 1}-${index + 1}`),
      title: String(item.title ?? item.name ?? item.label ?? `知识点 ${index + 1}`),
      content: String(item.content ?? item.body ?? item.text ?? item.markdown ?? '')
    };
  }

  function normalizeKnowledgeChapters(pack) {
    const directKnowledge = Array.isArray(pack.knowledge) ? pack.knowledge : [];
    const directChapters = Array.isArray(pack.knowledgeChapters) ? pack.knowledgeChapters
      : Array.isArray(pack.chapters) ? pack.chapters
      : [];

    let chapters = [];

    if (directChapters.length) {
      chapters = directChapters.map((chapter, chapterIndex) => {
        const itemsSource = Array.isArray(chapter.items) ? chapter.items
          : Array.isArray(chapter.knowledge) ? chapter.knowledge
          : Array.isArray(chapter.points) ? chapter.points
          : [];
        return {
          id: String(chapter.id ?? `chapter-${chapterIndex + 1}`),
          title: String(chapter.title ?? chapter.name ?? `章节 ${chapterIndex + 1}`),
          items: itemsSource.map((item, itemIndex) => normalizeKnowledgeItem(item, itemIndex, chapterIndex)).filter(item => item.content.trim())
        };
      }).filter(chapter => chapter.items.length);
    } else if (directKnowledge.length && directKnowledge.some(entry =>
      Array.isArray(entry?.items) || Array.isArray(entry?.knowledge) || Array.isArray(entry?.points)
    )) {
      chapters = directKnowledge.map((chapter, chapterIndex) => {
        const itemsSource = Array.isArray(chapter.items) ? chapter.items
          : Array.isArray(chapter.knowledge) ? chapter.knowledge
          : Array.isArray(chapter.points) ? chapter.points
          : [];
        return {
          id: String(chapter.id ?? `chapter-${chapterIndex + 1}`),
          title: String(chapter.title ?? chapter.chapter ?? chapter.name ?? `章节 ${chapterIndex + 1}`),
          items: itemsSource.map((item, itemIndex) => normalizeKnowledgeItem(item, itemIndex, chapterIndex)).filter(item => item.content.trim())
        };
      }).filter(chapter => chapter.items.length);
    } else if (directKnowledge.length) {
      chapters = [{
        id: 'chapter-1',
        title: String(pack.knowledgeTitle || '知识点'),
        items: directKnowledge.map((item, itemIndex) => normalizeKnowledgeItem(item, itemIndex, 0)).filter(item => item.content.trim())
      }].filter(chapter => chapter.items.length);
    }

    return chapters;
  }

  function getKnowledgeOutline(pack) {
    const chapters = normalizeKnowledgeChapters(pack);
    const items = chapters.flatMap((chapter, chapterIndex) => chapter.items.map((item, itemIndex) => ({
      ...item,
      chapterId: chapter.id,
      chapterTitle: chapter.title,
      chapterIndex,
      itemIndex
    })));
    return { chapters, items };
  }

  function packHasKnowledge(pack) {
    return getKnowledgeOutline(pack).items.length > 0;
  }

  function countKnowledgeItems(pack) {
    return getKnowledgeOutline(pack).items.length;
  }

  function normalizeQuestion(question, index) {
    const typeAliases = {
      numeric: 'number',
      'fill-number': 'number',
      letter: 'text',
      'fill-text': 'text',
      sentence: 'ordering',
      'word-order': 'ordering'
    };
    const rawType = String(question.type || 'single').toLowerCase();
    const normalized = { ...question, id: String(question.id ?? index), type: typeAliases[rawType] || rawType };
    normalized.title = String(question.title || `第 ${index + 1} 题`);
    normalized.prompt = String(question.prompt || question.question || '');
    normalized.explanation = String(question.explanation || '');
    if (normalized.type === 'judge' && !question.options) {
      normalized.options = [{ id: 'true', text: '正确' }, { id: 'false', text: '错误' }];
    } else {
      normalized.options = (question.options || []).map(normalizeOption);
    }
    normalized.left = (question.left || []).map(normalizeOption);
    normalized.right = (question.right || []).map(normalizeOption);
    normalized.words = (question.words || []).map((word, wordIndex) => typeof word === 'object'
      ? { id: Number(word.id ?? wordIndex + 1), text: String(word.text ?? word.label ?? '') }
      : { id: wordIndex + 1, text: String(word) });
    return normalized;
  }

  function expandQuestion(question) {
    const groupType = String(question.type || '').toLowerCase();
    if (groupType !== 'cloze' && groupType !== 'reading') return [question];
    const items = question.questions || question.blanks || [];
    return items.map((item, index) => ({
      ...item,
      id: `${question.id || groupType}-${item.id || index + 1}`,
      type: groupType,
      title: question.title || (groupType === 'cloze' ? '完形填空' : '阅读题'),
      article: String(question.article || question.passage || ''),
      prompt: String(item.prompt || `请完成第 ${index + 1} 题`),
      clozeGroupId: String(question.id || groupType),
      clozeIndex: index + 1,
      clozeTotal: items.length
    }));
  }

  function blankAnswer() {
    return {
      selected: new Set(),
      matches: {},
      input: '',
      order: [],
      pendingDigits: '',
      correct: false,
      attempted: false,
      outcome: null
    };
  }

  function getExpectedIds(question) {
    return (question.answers ?? [question.answer]).flat().filter(value => value !== undefined).map(String);
  }

  function isInputType(question) {
    return question.type === 'number' || question.type === 'text';
  }

  function isCorrect(question, answer) {
    if (question.type === 'matching') {
      const expected = question.answers || question.answer || {};
      const entries = Object.entries(expected).map(([key, value]) => [String(key), String(value)]);
      return entries.length > 0
        && entries.every(([key, value]) => answer.matches[key] === value)
        && Object.keys(answer.matches).length === entries.length;
    }
    if (isInputType(question)) {
      const expected = getExpectedIds(question);
      const actual = answer.input.trim();
      if (!actual || !expected.length) return false;
      if (question.type === 'number') {
        const actualNumber = Number(actual);
        return Number.isFinite(actualNumber) && expected.some(value => Number(value) === actualNumber);
      }
      const normalize = value => question.caseSensitive ? String(value).trim() : String(value).trim().toLocaleLowerCase();
      return expected.some(value => normalize(value) === normalize(actual));
    }
    if (question.type === 'ordering') {
      const expected = getExpectedIds(question).map(Number);
      return expected.length === answer.order.length && expected.every((number, index) => number === answer.order[index]);
    }
    const expected = getExpectedIds(question).sort();
    const selected = [...answer.selected].sort();
    return expected.length > 0 && expected.length === selected.length && expected.every((value, index) => value === selected[index]);
  }

  function getGrade(accuracy) {
    if (accuracy >= 90) return 'A+';
    if (accuracy >= 80) return 'A';
    if (accuracy >= 70) return 'A-';
    if (accuracy >= 60) return 'B+';
    if (accuracy >= 50) return 'B';
    if (accuracy >= 40) return 'B-';
    if (accuracy >= 30) return 'C+';
    if (accuracy >= 20) return 'C';
    if (accuracy >= 10) return 'C-';
    return 'D+';
  }

  function serializeAnswer(answer) {
    return { ...answer, selected: [...answer.selected] };
  }

  function deserializeAnswer(saved) {
    return {
      ...blankAnswer(),
      ...saved,
      selected: new Set(saved?.selected || []),
      matches: saved?.matches || {}
    };
  }

  function defaultStore() {
    return { lastLocation: { page: 'index' }, packs: {}, dailyChallenges: {} };
  }

  function readStore() {
    try {
      return { ...defaultStore(), ...(JSON.parse(localStorage.getItem(STORAGE_KEY)) || {}) };
    } catch {
      return defaultStore();
    }
  }

  function writeStore(store) {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(store));
  }

  function updateStore(mutator) {
    const store = readStore();
    mutator(store);
    writeStore(store);
    return store;
  }

  function saveLastLocation(location) {
    updateStore(store => {
      store.lastLocation = location?.packFile === DAILY_PACK_FILE
        ? { ...location, dateKey: getTodayDateKey() }
        : location;
    });
  }

  function readLastLocation() {
    const lastLocation = readStore().lastLocation || { page: 'index' };
    if (lastLocation.packFile === DAILY_PACK_FILE && lastLocation.dateKey !== getTodayDateKey()) {
      return { page: 'index' };
    }
    return lastLocation;
  }

  function getProgressPackKey(packFile) {
    return packFile === DAILY_PACK_FILE ? `${DAILY_PACK_FILE}:${getTodayDateKey()}` : packFile;
  }

  function savePackProgress(packFile, progress) {
    const progressKey = getProgressPackKey(packFile);
    updateStore(store => {
      store.packs ||= {};
      store.packs[progressKey] = {
        ...(store.packs[progressKey] || {}),
        ...progress
      };
      if (packFile === DAILY_PACK_FILE) delete store.packs[DAILY_PACK_FILE];
    });
  }

  function readPackProgress(packFile) {
    return readStore().packs?.[getProgressPackKey(packFile)] || null;
  }

  function readKnowledgeProgress(packFile) {
    const saved = readPackProgress(packFile)?.knowledge || {};
    return {
      currentItemId: saved.currentItemId || '',
      readIds: Array.isArray(saved.readIds) ? saved.readIds : [],
      collapsedChapterIds: Array.isArray(saved.collapsedChapterIds) ? saved.collapsedChapterIds : [],
      completed: Boolean(saved.completed)
    };
  }

  function saveKnowledgeProgress(packFile, knowledgeProgress) {
    savePackProgress(packFile, {
      knowledge: {
        currentItemId: knowledgeProgress.currentItemId || '',
        readIds: [...new Set(knowledgeProgress.readIds || [])],
        collapsedChapterIds: [...new Set(knowledgeProgress.collapsedChapterIds || [])],
        completed: Boolean(knowledgeProgress.completed)
      }
    });
  }

  function clearPackProgress(packFile) {
    updateStore(store => {
      if (store.packs) {
        delete store.packs[getProgressPackKey(packFile)];
        if (packFile === DAILY_PACK_FILE) delete store.packs[DAILY_PACK_FILE];
      }
      if (packFile === DAILY_PACK_FILE && store.dailyChallenges) {
        delete store.dailyChallenges[getTodayDateKey()];
      }
      if (store.lastLocation?.packFile === packFile) {
        store.lastLocation = { page: 'index' };
      }
    });
  }

  function clearAllProgress() {
    writeStore(defaultStore());
  }

  function markDailyChallengeComplete(dateKey, summary = {}) {
    updateStore(store => {
      store.dailyChallenges ||= {};
      store.dailyChallenges[dateKey] = {
        completed: true,
        completedAt: new Date().toISOString(),
        accuracy: summary.accuracy ?? null
      };
    });
  }

  function readDailyChallenges() {
    return readStore().dailyChallenges || {};
  }

  function getMonthCalendarData(referenceDate = new Date()) {
    const year = referenceDate.getFullYear();
    const month = referenceDate.getMonth();
    const firstDay = new Date(year, month, 1);
    const lastDay = new Date(year, month + 1, 0);
    const leading = firstDay.getDay();
    const totalDays = lastDay.getDate();
    const completions = readDailyChallenges();
    const cells = [];

    for (let index = 0; index < leading; index++) cells.push({ type: 'empty' });

    for (let day = 1; day <= totalDays; day++) {
      const dateKey = `${year}-${String(month + 1).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
      cells.push({
        type: 'day',
        day,
        dateKey,
        today: dateKey === getTodayDateKey(),
        completed: Boolean(completions[dateKey]?.completed)
      });
    }

    return {
      title: `${year} 年 ${month + 1} 月`,
      cells
    };
  }

  function encodePackRef(packFile) {
    if (packFile === DAILY_PACK_FILE) return 'daily';
    return String(packFile || '')
      .replace(/\.ya?ml$/i, '')
      .replace(/[^a-zA-Z0-9_-]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .toLowerCase() || 'pack';
  }

  function resolvePackFile(packRef, packs = []) {
    if (!packRef) return '';
    if (packRef === DAILY_PACK_FILE || packRef === 'daily') return DAILY_PACK_FILE;
    const direct = packs.find(pack => pack.file === packRef);
    if (direct) return direct.file;
    const byRef = packs.find(pack => encodePackRef(pack.file) === packRef);
    return byRef?.file || packRef;
  }

  function buildQuizUrl(packFile, questionIndex = 0) {
    const params = new URLSearchParams({ pack: encodePackRef(packFile), q: String(questionIndex) });
    return `quiz.html?${params.toString()}`;
  }

  function buildLearnUrl(packFile, itemId = '') {
    const params = new URLSearchParams({ pack: encodePackRef(packFile), view: 'learn' });
    if (itemId) params.set('k', itemId);
    return `quiz.html?${params.toString()}`;
  }

  function buildResultsUrl(packFile) {
    const params = new URLSearchParams({ pack: encodePackRef(packFile) });
    return `results.html?${params.toString()}`;
  }

  function buildReviewUrl(packFile, questionIndex = 0) {
    const params = new URLSearchParams({ pack: encodePackRef(packFile), view: 'review', q: String(questionIndex) });
    return `quiz.html?${params.toString()}`;
  }

  async function fetchYaml(path) {
    const response = await fetch(path, { cache: 'no-store' });
    if (!response.ok) throw new Error(`${path}（HTTP ${response.status}）`);
    if (!window.jsyaml) throw new Error('YAML 解析器未能加载');
    return window.jsyaml.load(await response.text()) || {};
  }

  async function loadLibrary() {
    const manifest = await fetchYaml('problems/index.yaml');
    const files = Array.isArray(manifest.files) ? manifest.files : [];
    if (!files.length) throw new Error('problems/index.yaml 中没有 files 列表');
    const yamlPacks = await Promise.all(files.map(async file => ({ ...(await fetchYaml(`problems/${file}`)), file })));
    const packs = [generateDailyMathPack(), ...yamlPacks];
    for (const pack of packs) {
      if (!pack.name) throw new Error(`${pack.file} 必须以 name: 记录名称`);
      if (!Array.isArray(pack.questions) || !pack.questions.length) throw new Error(`${pack.file} 中没有可用题目`);
    }
    return packs;
  }

  function countPackQuestions(pack) {
    return pack.questions.reduce((total, question) => {
      const type = String(question.type || '').toLowerCase();
      if (type !== 'cloze' && type !== 'reading') return total + 1;
      return total + (question.questions || question.blanks || []).length;
    }, 0);
  }

  function getQuestions(pack) {
    return pack.questions.flatMap(expandQuestion).map(normalizeQuestion);
  }

  function computeResults(questions, answersMap) {
    const outcomes = questions.map((_, index) => answersMap.get(index)?.outcome || 'skipped');
    const correct = outcomes.filter(value => value === 'correct').length;
    const wrong = outcomes.filter(value => value === 'wrong').length;
    const skipped = outcomes.filter(value => value === 'skipped').length;
    const accuracy = questions.length ? Math.round((correct / questions.length) * 100) : 0;
    return { correct, wrong, skipped, accuracy, grade: getGrade(accuracy) };
  }

  function installRipple(root = document) {
    root.addEventListener('pointerdown', event => {
      const target = event.target.closest('button, .ripple-button');
      if (!target || target.disabled) return;
      const rect = target.getBoundingClientRect();
      const size = Math.max(rect.width, rect.height) * 1.8;
      const ripple = document.createElement('span');
      ripple.className = 'ripple';
      ripple.style.width = ripple.style.height = `${size}px`;
      ripple.style.left = `${event.clientX - rect.left - size / 2}px`;
      ripple.style.top = `${event.clientY - rect.top - size / 2}px`;
      target.appendChild(ripple);
      ripple.addEventListener('animationend', () => ripple.remove(), { once: true });
    });
  }

  window.OnlineExercises = {
    STORAGE_KEY,
    DAILY_PACK_FILE,
    blankAnswer,
    buildLearnUrl,
    buildQuizUrl,
    buildResultsUrl,
    buildReviewUrl,
    clearAllProgress,
    clearPackProgress,
    computeResults,
    countKnowledgeItems,
    countPackQuestions,
    deserializeAnswer,
    escapeHTML,
    resolvePackFile,
    fetchYaml,
    generateDailyMathPack,
    getExpectedIds,
    getGrade,
    getKnowledgeOutline,
    getMonthCalendarData,
    getQuestions,
    getTodayDateKey,
    highlightCode,
    installRipple,
    isCorrect,
    isInputType,
    loadLibrary,
    markDailyChallengeComplete,
    packHasKnowledge,
    readDailyChallenges,
    readKnowledgeProgress,
    readLastLocation,
    readPackProgress,
    renderMarkdown,
    renderMath,
    encodePackRef,
    saveKnowledgeProgress,
    saveLastLocation,
    savePackProgress,
    serializeAnswer
  };
})();
