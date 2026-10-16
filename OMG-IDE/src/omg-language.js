export const keywords = new Set([
  "define", "as", "func", "var", "val", "bool", "if", "else", "for", "in", "to",
  "while", "repeat", "until", "try", "return", "exit", "with", "throwing", "error",
  "break", "continue", "import", "class", "and", "or", "not", "is", "detected"
]);

export const builtins = new Set([
  "print", "println", "input", "input_without_display", "calc", "to_digit", "to_char",
  "to_string", "type_of", "size_of", "push", "pop", "top_of", "front_of", "swap"
]);

export const containerTypes = new Set([
  "dict", "array", "queue", "stack", "pqueue", "pquue", "greater_pqueue", "gpqueue"
]);

export const constants = new Set(["true", "false", "null"]);

export const stringMethods = [
  "position_of", "can_find", "starts_with", "ends_with", "is_empty", "sub", "split",
  "replace", "to_upper", "to_lower", "trim"
];

export function escapeHtml(value) {
  return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function token(type, value) {
  return `<span class="tok-${type}">${escapeHtml(value)}</span>`;
}

function highlightLine(line, state) {
  let html = "";
  let index = 0;

  while (index < line.length) {
    if (state.blockComment) {
      const end = line.indexOf("*/", index);
      if (end === -1) return html + token("comment", line.slice(index));
      html += token("comment", line.slice(index, end + 2));
      index = end + 2;
      state.blockComment = false;
      continue;
    }

    if (line.startsWith("//", index)) {
      html += token("comment", line.slice(index));
      break;
    }
    if (line.startsWith("/*", index)) {
      const end = line.indexOf("*/", index + 2);
      if (end === -1) {
        state.blockComment = true;
        html += token("comment", line.slice(index));
        break;
      }
      html += token("comment", line.slice(index, end + 2));
      index = end + 2;
      continue;
    }

    const character = line[index];
    if (character === '"' || character === "'") {
      const quote = character;
      let end = index + 1;
      while (end < line.length) {
        if (line[end] === "\\") end += 2;
        else if (line[end++] === quote) break;
      }
      html += token("string", line.slice(index, end));
      index = end;
      continue;
    }

    const rest = line.slice(index);
    const number = rest.match(/^\d+(?:\.\d+)?/);
    if (number) {
      html += token("number", number[0]);
      index += number[0].length;
      continue;
    }

    const identifier = rest.match(/^[A-Za-z_][\w]*/);
    if (identifier) {
      const value = identifier[0];
      const type = keywords.has(value) ? "keyword"
        : builtins.has(value) ? "function"
          : containerTypes.has(value) ? "type"
            : constants.has(value) ? "constant" : "identifier";
      html += type === "identifier" ? escapeHtml(value) : token(type, value);
      index += value.length;
      continue;
    }

    if (/[+\-*/%=!<>|&]/.test(character)) html += token("operator", character);
    else if (/[{}[\]()]/.test(character)) html += token("bracket", character);
    else if (/[,.:;]/.test(character)) html += token("punctuation", character);
    else html += escapeHtml(character);
    index += 1;
  }
  return html || " ";
}

export function highlightOmg(source, activeLine = 1) {
  const state = { blockComment: false };
  return source.replace(/\r/g, "").split("\n").map((line, index) => {
    const active = index + 1 === activeLine ? " is-active" : "";
    return `<span class="editor-line${active}">${highlightLine(line, state)}</span>`;
  }).join("");
}
