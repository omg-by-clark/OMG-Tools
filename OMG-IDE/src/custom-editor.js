import { builtins, containerTypes, highlightOmg, keywords, stringMethods } from "./omg-language.js";

const PAIRS = { "(": ")", "[": "]", "{": "}", '"': '"', "'": "'" };
const CLOSERS = new Set(Object.values(PAIRS));

export class OmgEditor {
  constructor(host, options = {}) {
    this.host = host;
    this.templates = options.templates ?? [];
    this.changeListeners = new Set();
    this.typeListeners = new Set();
    this.suggestions = [];
    this.suggestionIndex = 0;

    host.classList.add("omg-editor");
    host.innerHTML = `
      <div class="editor-gutter" aria-hidden="true"><div class="editor-line-numbers"></div></div>
      <div class="editor-viewport">
        <pre class="editor-highlight" aria-hidden="true"></pre>
        <textarea class="editor-input" aria-label="OMG code editor" autocomplete="off" autocapitalize="off" spellcheck="false" wrap="off"></textarea>
        <div class="editor-suggestions" role="listbox" hidden></div>
      </div>`;

    this.input = host.querySelector(".editor-input");
    this.highlight = host.querySelector(".editor-highlight");
    this.lineNumbers = host.querySelector(".editor-line-numbers");
    this.popup = host.querySelector(".editor-suggestions");

    this.input.addEventListener("input", event => this.handleInput(event));
    this.input.addEventListener("scroll", () => this.syncScroll(true));
    this.input.addEventListener("keydown", event => this.handleKeydown(event));
    this.input.addEventListener("keyup", () => this.render());
    this.input.addEventListener("click", () => { this.render(); this.hideSuggestions(); });
    this.input.addEventListener("blur", () => setTimeout(() => this.hideSuggestions(), 120));
    this.popup.addEventListener("pointerdown", event => {
      const item = event.target.closest("button[data-index]");
      if (!item) return;
      event.preventDefault();
      this.suggestionIndex = Number(item.dataset.index);
      this.acceptSuggestion();
    });
    this.render();
  }

  getValue() { return this.input.value; }

  setValue(value, { silent = false } = {}) {
    this.input.value = value ?? "";
    this.hideSuggestions();
    this.render();
    if (!silent) this.emitChange();
  }

  focus() { this.input.focus(); }

  onDidChangeModelContent(listener) {
    this.changeListeners.add(listener);
    return { dispose: () => this.changeListeners.delete(listener) };
  }

  onDidType(listener) {
    this.typeListeners.add(listener);
    return { dispose: () => this.typeListeners.delete(listener) };
  }

  getPosition() {
    const before = this.input.value.slice(0, this.input.selectionStart);
    const lines = before.split("\n");
    return { lineNumber: lines.length, column: lines.at(-1).length + 1 };
  }

  getSelectionInfo() {
    const start = this.input.selectionStart;
    const end = this.input.selectionEnd;
    return { start, end, length: end - start };
  }

  emitChange() {
    this.changeListeners.forEach(listener => listener());
    this.host.dispatchEvent(new CustomEvent("editor-position", { detail: this.getPosition() }));
  }

  handleInput(event) {
    this.render();
    this.emitChange();
    if (event.data) this.typeListeners.forEach(listener => listener(event.data));
    if (event.inputType?.startsWith("insert") && /[A-Za-z_]/.test(event.data ?? "")) this.showSuggestions();
    else if (event.inputType?.startsWith("delete")) this.showSuggestions();
    else this.hideSuggestions();
  }

  handleKeydown(event) {
    if (!this.popup.hidden) {
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        const direction = event.key === "ArrowDown" ? 1 : -1;
        this.suggestionIndex = (this.suggestionIndex + direction + this.suggestions.length) % this.suggestions.length;
        this.renderSuggestions();
        return;
      }
      if (event.key === "Enter" || event.key === "Tab") {
        event.preventDefault();
        this.acceptSuggestion();
        return;
      }
      if (event.key === "Escape") {
        event.preventDefault();
        this.hideSuggestions();
        return;
      }
    }

    if ((event.ctrlKey || event.metaKey) && event.key === " ") {
      event.preventDefault();
      this.showSuggestions(true);
      return;
    }
    if ((event.ctrlKey || event.metaKey) && event.key === "/") {
      event.preventDefault();
      this.toggleLineComment();
      return;
    }
    if (event.key === "Backspace" && this.input.selectionStart === this.input.selectionEnd) {
      const cursor = this.input.selectionStart;
      const open = this.input.value[cursor - 1];
      const close = this.input.value[cursor];
      if (cursor > 0 && PAIRS[open] === close) {
        event.preventDefault();
        this.replaceRange(cursor - 1, cursor + 1, "", cursor - 1);
        return;
      }
    }
    if (event.key === "Tab") {
      event.preventDefault();
      this.indentSelection(event.shiftKey);
      return;
    }
    if (event.key === "Enter") {
      event.preventDefault();
      this.insertNewLine();
      return;
    }
    if (PAIRS[event.key]) {
      event.preventDefault();
      this.insertPair(event.key, PAIRS[event.key]);
      return;
    }
    if (CLOSERS.has(event.key) && this.input.selectionStart === this.input.selectionEnd && this.input.value[this.input.selectionStart] === event.key) {
      event.preventDefault();
      const next = this.input.selectionStart + 1;
      this.input.setSelectionRange(next, next);
      this.render();
    }
  }

  replaceRange(start, end, text, selectionStart = start + text.length, selectionEnd = selectionStart) {
    this.input.setRangeText(text, start, end, "end");
    this.input.setSelectionRange(selectionStart, selectionEnd);
    this.hideSuggestions();
    this.render();
    this.emitChange();
  }

  insertPair(open, close) {
    const { selectionStart: start, selectionEnd: end, value } = this.input;
    const selected = value.slice(start, end);
    this.replaceRange(start, end, `${open}${selected}${close}`, start + 1, start + 1 + selected.length);
  }

  insertNewLine() {
    const { selectionStart: start, selectionEnd: end, value } = this.input;
    const lineStart = value.lastIndexOf("\n", start - 1) + 1;
    const leading = value.slice(lineStart, start).match(/^\s*/)?.[0] ?? "";
    const before = value.slice(lineStart, start).trimEnd();
    const next = value.slice(end).match(/^\s*([}\])])/)?.[1];
    const opensBlock = /[{[(]$/.test(before);
    const indent = opensBlock ? `${leading}  ` : leading;
    if (opensBlock && next) {
      const inserted = `\n${indent}\n${leading}`;
      this.replaceRange(start, end, inserted, start + 1 + indent.length);
    } else {
      this.replaceRange(start, end, `\n${indent}`);
    }
  }

  indentSelection(outdent = false) {
    const { selectionStart: start, selectionEnd: end, value } = this.input;
    if (start === end && !outdent) {
      this.replaceRange(start, end, "  ");
      return;
    }
    const blockStart = value.lastIndexOf("\n", start - 1) + 1;
    const blockEndCandidate = value.indexOf("\n", end);
    const blockEnd = blockEndCandidate === -1 ? value.length : blockEndCandidate;
    const original = value.slice(blockStart, blockEnd);
    const replacement = original.split("\n").map(line => outdent ? line.replace(/^ {1,2}|^\t/, "") : `  ${line}`).join("\n");
    this.replaceRange(blockStart, blockEnd, replacement, blockStart, blockStart + replacement.length);
  }

  toggleLineComment() {
    const { selectionStart: start, selectionEnd: end, value } = this.input;
    const blockStart = value.lastIndexOf("\n", start - 1) + 1;
    const blockEndCandidate = value.indexOf("\n", end);
    const blockEnd = blockEndCandidate === -1 ? value.length : blockEndCandidate;
    const lines = value.slice(blockStart, blockEnd).split("\n");
    const allCommented = lines.filter(Boolean).every(line => /^\s*\/\//.test(line));
    const replacement = lines.map(line => allCommented ? line.replace(/^(\s*)\/\/ ?/, "$1") : line.replace(/^(\s*)/, "$1// ")).join("\n");
    this.replaceRange(blockStart, blockEnd, replacement, blockStart, blockStart + replacement.length);
  }

  format() {
    let depth = 0;
    const formatted = this.input.value.replace(/\r/g, "").split("\n").map(line => {
      const content = line.trim();
      if (!content) return "";
      const startsClosing = /^[}\])]/.test(content);
      if (startsClosing) depth = Math.max(0, depth - 1);
      const result = `${"  ".repeat(depth)}${content}`;
      const opens = (content.match(/[{[(]/g) ?? []).length;
      const closes = (content.match(/[}\])]/g) ?? []).length;
      depth = Math.max(0, depth + opens - closes + (startsClosing ? 1 : 0));
      return result;
    }).join("\n");
    this.setValue(formatted);
  }

  currentWord() {
    const cursor = this.input.selectionStart;
    const match = this.input.value.slice(0, cursor).match(/[A-Za-z_][\w]*$/);
    return { value: match?.[0] ?? "", start: cursor - (match?.[0].length ?? 0), end: cursor };
  }

  getAllSuggestions() {
    const simple = [
      ...Array.from(keywords).map(label => ({ label, insert: label, type: "keyword", detail: "关键字" })),
      ...Array.from(builtins).map(label => ({ label: `${label}()`, match: label, insert: `${label}()`, caret: -1, type: "function", detail: "内置函数" })),
      ...Array.from(containerTypes).map(label => ({ label, insert: label, type: "type", detail: "容器类型" })),
      ...stringMethods.map(label => ({ label: `${label}()`, match: label, insert: `${label}()`, caret: -1, type: "method", detail: "字符串方法" }))
    ];
    return [
      ...this.templates.map(item => ({ label: item.trigger, insert: item.text, type: "snippet", detail: "代码模板" })),
      ...simple
    ];
  }

  showSuggestions(force = false) {
    const word = this.currentWord();
    if (!force && !word.value) return this.hideSuggestions();
    const query = word.value.toLowerCase();
    this.suggestions = this.getAllSuggestions()
      .filter(item => force || (item.match ?? item.label).toLowerCase().startsWith(query))
      .filter(item => force || (item.match ?? item.label).toLowerCase() !== query)
      .slice(0, 9);
    if (!this.suggestions.length) return this.hideSuggestions();
    this.suggestionIndex = 0;
    this.popup.hidden = false;
    const position = this.getCaretCoordinates();
    this.popup.style.left = `${Math.max(8, Math.min(position.left, this.host.clientWidth - 310))}px`;
    this.popup.style.top = `${Math.max(8, Math.min(position.top, this.host.clientHeight - 280))}px`;
    this.renderSuggestions();
  }

  renderSuggestions() {
    this.popup.innerHTML = this.suggestions.map((item, index) => `
      <button type="button" role="option" data-index="${index}" class="suggestion${index === this.suggestionIndex ? " is-selected" : ""}">
        <span class="suggestion-icon is-${item.type}">${item.type === "snippet" ? "S" : item.type === "function" || item.type === "method" ? "ƒ" : "K"}</span>
        <span class="suggestion-copy"><strong>${item.label}</strong><small>${item.detail}</small></span>
      </button>`).join("");
    this.popup.querySelector(".is-selected")?.scrollIntoView({ block: "nearest" });
  }

  acceptSuggestion() {
    const item = this.suggestions[this.suggestionIndex];
    if (!item) return;
    const word = this.currentWord();
    const caret = word.start + item.insert.length + (item.caret ?? 0);
    this.replaceRange(word.start, word.end, item.insert, caret);
    this.hideSuggestions();
    this.focus();
  }

  hideSuggestions() { this.popup.hidden = true; }

  getCaretCoordinates() {
    const { lineNumber, column } = this.getPosition();
    const style = getComputedStyle(this.input);
    const probe = document.createElement("canvas").getContext("2d");
    probe.font = `${style.fontSize} ${style.fontFamily}`;
    const characterWidth = probe.measureText("M").width;
    return {
      left: 12 + (column - 1) * characterWidth - this.input.scrollLeft,
      top: 14 + lineNumber * parseFloat(style.lineHeight) - this.input.scrollTop
    };
  }

  syncScroll(hideSuggestions = false) {
    this.highlight.style.transform = `translate(${-this.input.scrollLeft}px, ${-this.input.scrollTop}px)`;
    this.lineNumbers.style.transform = `translateY(${-this.input.scrollTop}px)`;
    if (hideSuggestions) this.hideSuggestions();
  }

  render() {
    const position = this.getPosition();
    const lineCount = Math.max(1, this.input.value.split("\n").length);
    this.highlight.innerHTML = highlightOmg(this.input.value, position.lineNumber);
    this.lineNumbers.innerHTML = Array.from({ length: lineCount }, (_, index) => `<span class="${index + 1 === position.lineNumber ? "is-active" : ""}">${index + 1}</span>`).join("");
    this.syncScroll(false);
    this.host.dispatchEvent(new CustomEvent("editor-position", { detail: position }));
  }
}
