const endpoint = process.argv[2] ?? "http://127.0.0.1:9223";
const pages = await fetch(`${endpoint}/json/list`).then(response => response.json());
const page = pages.find(item => item.type === "page" && item.url.includes("127.0.0.1:1420"));
if (!page) throw new Error("OMG IDE preview page was not found");

const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, { once: true });
  socket.addEventListener("error", reject, { once: true });
});

let sequence = 0;
const pending = new Map();
socket.addEventListener("message", event => {
  const message = JSON.parse(event.data);
  if (!message.id || !pending.has(message.id)) return;
  pending.get(message.id)(message);
  pending.delete(message.id);
});

function request(method, params = {}) {
  const id = ++sequence;
  socket.send(JSON.stringify({ id, method, params }));
  return new Promise(resolve => pending.set(id, resolve));
}

await request("Page.enable");
await request("Page.reload", { ignoreCache: true });
await new Promise(resolve => setTimeout(resolve, 1500));

const expression = `(() => {
  const input = document.querySelector('.editor-input');
  input.focus();
  input.value = 'func main() {\\n  pri\\n}';
  input.setSelectionRange(input.value.indexOf('pri') + 3, input.value.indexOf('pri') + 3);
  input.dispatchEvent(new InputEvent('input', { bubbles: true, data: 'i', inputType: 'insertText' }));
  input.dispatchEvent(new KeyboardEvent('keyup', { key: 'i', bubbles: true }));
  const renderedLines = Array.from(document.querySelectorAll('.editor-line'));
  const gutterLines = Array.from(document.querySelectorAll('.editor-line-numbers span'));
  const results = {
    lineNumbers: gutterLines.length,
    highlightedKeyword: document.querySelectorAll('.tok-keyword').length,
    completionVisible: !document.querySelector('.editor-suggestions').hidden,
    completionLabels: Array.from(document.querySelectorAll('.suggestion strong')).map(node => node.textContent),
    textareaAccessible: input.getAttribute('aria-label') === 'OMG code editor',
    lineHeightCorrect: Math.round(renderedLines[1].getBoundingClientRect().top - renderedLines[0].getBoundingClientRect().top) === 24,
    layersAligned: Math.abs(renderedLines[1].getBoundingClientRect().top - gutterLines[1].getBoundingClientRect().top) < 1
  };
  input.value = '{}';
  input.setSelectionRange(1, 1);
  input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Backspace', bubbles: true, cancelable: true }));
  results.pairDelete = input.value === '' && input.selectionStart === 0;
  input.value = 'if';
  input.setSelectionRange(2, 2);
  input.dispatchEvent(new InputEvent('input', { bubbles: true, data: 'f', inputType: 'insertText' }));
  input.dispatchEvent(new KeyboardEvent('keydown', { key: '{', bubbles: true, cancelable: true }));
  results.structuralInputClosesCompletion = document.querySelector('.editor-suggestions').hidden;
  input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  results.blockEnterActual = input.value;
  results.blockEnter = input.value === 'if{\\n  \\n}';
  input.value = '';
  input.setSelectionRange(0, 0);
  input.dispatchEvent(new KeyboardEvent('keydown', { key: '{', bubbles: true, cancelable: true }));
  results.autoPair = input.value === '{}' && input.selectionStart === 1;
  return results;
})()`;

const response = await request("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
socket.close();
const result = response.result?.result?.value;
if (!result) throw new Error(JSON.stringify(response.result?.exceptionDetails ?? response, null, 2));

const passed = result.lineNumbers === 3
  && result.highlightedKeyword >= 1
  && result.completionVisible
  && result.completionLabels.some(label => label.startsWith("print"))
  && result.textareaAccessible
  && result.lineHeightCorrect
  && result.layersAligned
  && result.pairDelete
  && result.structuralInputClosesCompletion
  && result.blockEnter
  && result.autoPair;

console.log(JSON.stringify({ passed, ...result }, null, 2));
if (!passed) process.exitCode = 1;
