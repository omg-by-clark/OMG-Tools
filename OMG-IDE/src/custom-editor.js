import { basicSetup, EditorView } from "codemirror";
import { EditorSelection } from "@codemirror/state";
import { keymap } from "@codemirror/view";
import { indentWithTab } from "@codemirror/commands";
import { autocompletion, completeFromList } from "@codemirror/autocomplete";
import { HighlightStyle, StreamLanguage, syntaxHighlighting } from "@codemirror/language";
import { tags } from "@lezer/highlight";
import { builtins, constants, containerTypes, keywords, stringMethods } from "./omg-language.js";

const FUNCTION_COMPLETION = label => ({
  label: `${label}()`,
  displayLabel: `${label}()`,
  type: "function",
  detail: "内置函数",
  apply(view, _completion, from, to) {
    const text = `${label}()`;
    view.dispatch({
      changes: { from, to, insert: text },
      selection: EditorSelection.cursor(from + text.length - 1)
    });
  }
});

function createCompletions(templates) {
  return [
    ...templates.map(item => ({ label: item.trigger, apply: item.text, type: "text", detail: "代码模板" })),
    ...Array.from(keywords).map(label => ({ label, type: "keyword", detail: "关键字" })),
    ...Array.from(builtins).map(FUNCTION_COMPLETION),
    ...Array.from(containerTypes).map(label => ({ label, type: "type", detail: "容器类型" })),
    ...stringMethods.map(label => ({ ...FUNCTION_COMPLETION(label), detail: "字符串方法" }))
  ];
}

const omgLanguage = StreamLanguage.define({
  name: "omg",
  startState: () => ({ blockComment: false, depth: 0 }),
  copyState: state => ({ ...state }),
  token(stream, state) {
    if (state.blockComment) {
      if (stream.skipTo("*/")) {
        stream.match("*/");
        state.blockComment = false;
      } else {
        stream.skipToEnd();
      }
      return "comment";
    }

    if (stream.eatSpace()) return null;
    if (stream.match("//")) {
      stream.skipToEnd();
      return "comment";
    }
    if (stream.match("/*")) {
      if (stream.skipTo("*/")) stream.match("*/");
      else state.blockComment = true;
      return "comment";
    }

    const quote = stream.peek();
    if (quote === '"' || quote === "'") {
      stream.next();
      let escaped = false;
      while (!stream.eol()) {
        const character = stream.next();
        if (character === quote && !escaped) break;
        escaped = character === "\\" && !escaped;
        if (character !== "\\") escaped = false;
      }
      return "string";
    }

    if (stream.match(/^\d+(?:\.\d+)?/)) return "number";
    if (stream.match(/^[A-Za-z_]\w*/)) {
      const value = stream.current();
      if (keywords.has(value)) return "keyword";
      if (builtins.has(value)) return "builtin";
      if (containerTypes.has(value)) return "typeName";
      if (constants.has(value)) return "bool";
      return "variableName";
    }

    const character = stream.next();
    if (/[{[(]/.test(character)) state.depth += 1;
    else if (/[}\])]/.test(character)) state.depth = Math.max(0, state.depth - 1);
    if (/[+\-*/%=!<>|&]/.test(character)) return "operator";
    if (/[{}[\]()]/.test(character)) return "bracket";
    if (/[,.:;]/.test(character)) return "punctuation";
    return null;
  },
  indent(state, textAfter, context) {
    const closesBlock = /^\s*[}\])]/.test(textAfter);
    return Math.max(0, state.depth - (closesBlock ? 1 : 0)) * context.unit;
  },
  languageData: {
    commentTokens: { line: "//", block: { open: "/*", close: "*/" } },
    closeBrackets: { brackets: ["(", "[", "{", "'", "\""] }
  }
});

const omgHighlightStyle = HighlightStyle.define([
  { tag: tags.keyword, color: "#c49afa", fontWeight: "600" },
  { tag: [tags.function(tags.variableName), tags.standard(tags.name)], color: "#73daca" },
  { tag: tags.typeName, color: "#78b9ff" },
  { tag: [tags.bool, tags.null], color: "#f2a272" },
  { tag: tags.string, color: "#a4d77c" },
  { tag: tags.number, color: "#efb171" },
  { tag: tags.comment, color: "#626b79", fontStyle: "italic" },
  { tag: tags.operator, color: "#89a7f5" },
  { tag: tags.bracket, color: "#d1a8ff" },
  { tag: tags.punctuation, color: "#747c8c" }
]);

const omgTheme = EditorView.theme({
  "&": { height: "100%", backgroundColor: "var(--editor)", color: "#cbd0dd" },
  ".cm-scroller": {
    overflow: "auto",
    fontFamily: "var(--code-font)",
    fontSize: "15px",
    lineHeight: "24px"
  },
  ".cm-content": { padding: "14px 0 50px", caretColor: "#f0edf9" },
  ".cm-line": { minHeight: "24px", padding: "0" },
  ".cm-cursor, .cm-dropCursor": { borderLeftColor: "#f0edf9", borderLeftWidth: "1px" },
  ".cm-selectionBackground, &.cm-focused .cm-selectionBackground": { backgroundColor: "rgb(123 146 201 / 28%)" },
  ".cm-activeLine": {
    background: "linear-gradient(90deg, rgb(139 128 249 / 7%), rgb(139 128 249 / 2%) 72%, transparent)"
  },
  ".cm-gutters": {
    width: "46px",
    flex: "0 0 46px",
    color: "#444b59",
    backgroundColor: "#10131a",
    borderRight: "1px solid rgb(255 255 255 / 2.5%)"
  },
  ".cm-lineNumbers": { width: "100%" },
  ".cm-lineNumbers .cm-gutterElement": {
    boxSizing: "border-box",
    width: "45px",
    minWidth: "45px",
    padding: "0 13px 0 0",
    fontSize: "12px",
    lineHeight: "24px"
  },
  ".cm-activeLineGutter": { color: "#b7bdca", backgroundColor: "transparent" },
  ".cm-tooltip": {
    color: "#cfd3de",
    backgroundColor: "rgb(26 30 40 / 98%)",
    border: "1px solid #343a48",
    borderRadius: "9px",
    boxShadow: "var(--shadow)",
    overflow: "hidden"
  },
  ".cm-tooltip-autocomplete > ul": { maxHeight: "278px", padding: "5px" },
  ".cm-tooltip-autocomplete > ul > li": { minHeight: "30px", padding: "4px 8px", borderRadius: "6px" },
  ".cm-tooltip-autocomplete > ul > li[aria-selected]": { color: "white", backgroundColor: "rgb(139 128 249 / 15%)" },
  ".cm-completionLabel": { fontFamily: "var(--code-font)", fontSize: "12px" },
  ".cm-completionDetail": { color: "#626a79", fontSize: "10px", fontStyle: "normal" },
  ".cm-panels": { color: "var(--text)", backgroundColor: "var(--panel)" },
  ".cm-searchMatch": { backgroundColor: "rgb(233 196 106 / 22%)" },
  ".cm-matchingBracket": { color: "inherit", backgroundColor: "rgb(139 128 249 / 22%)" }
}, { dark: true });

export class OmgEditor {
  constructor(host, options = {}) {
    this.host = host;
    this.changeListeners = new Set();
    this.typeListeners = new Set();
    this.suppressChanges = false;
    const completions = createCompletions(options.templates ?? []);

    host.classList.add("omg-editor");
    host.replaceChildren();
    this.view = new EditorView({
      parent: host,
      extensions: [
        basicSetup,
        omgLanguage,
        syntaxHighlighting(omgHighlightStyle),
        omgTheme,
        keymap.of([indentWithTab]),
        autocompletion({ override: [completeFromList(completions)], activateOnTyping: true }),
        EditorView.updateListener.of(update => this.handleUpdate(update))
      ]
    });
    host.omgEditor = this;
    this.emitPosition();
  }

  getValue() { return this.view.state.doc.toString(); }

  setValue(value, { silent = false } = {}) {
    this.suppressChanges = silent;
    this.view.dispatch({
      changes: { from: 0, to: this.view.state.doc.length, insert: value ?? "" },
      selection: EditorSelection.cursor(0),
      scrollIntoView: true
    });
    this.suppressChanges = false;
  }

  focus() { this.view.focus(); }

  onDidChangeModelContent(listener) {
    this.changeListeners.add(listener);
    return { dispose: () => this.changeListeners.delete(listener) };
  }

  onDidType(listener) {
    this.typeListeners.add(listener);
    return { dispose: () => this.typeListeners.delete(listener) };
  }

  getPosition() {
    const head = this.view.state.selection.main.head;
    const line = this.view.state.doc.lineAt(head);
    return { lineNumber: line.number, column: head - line.from + 1 };
  }

  getSelectionInfo() {
    const selection = this.view.state.selection.main;
    return { start: selection.from, end: selection.to, length: selection.to - selection.from };
  }

  handleUpdate(update) {
    if (update.docChanged && !this.suppressChanges) {
      this.changeListeners.forEach(listener => listener());
      update.changes.iterChanges((_fromA, _toA, _fromB, _toB, inserted) => {
        const text = inserted.toString();
        if (text) this.typeListeners.forEach(listener => listener(text));
      });
    }
    if (update.docChanged || update.selectionSet || update.focusChanged) this.emitPosition();
  }

  emitPosition() {
    this.host.dispatchEvent(new CustomEvent("editor-position", { detail: this.getPosition() }));
  }

  format() {
    let depth = 0;
    const formatted = this.getValue().replace(/\r/g, "").split("\n").map(line => {
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

  destroy() {
    this.view.destroy();
    delete this.host.omgEditor;
  }
}
