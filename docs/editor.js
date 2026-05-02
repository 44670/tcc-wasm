(function (root, factory) {
  root.TccWasmEditor = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  const C_KEYWORDS = new Set([
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "sizeof", "static", "struct", "switch", "typedef", "union",
    "volatile", "while", "_Bool", "_Complex", "_Imaginary"
  ]);

  const C_TYPES = new Set([
    "char", "double", "float", "int", "long", "short", "signed", "unsigned",
    "void"
  ]);

  function escapeHtml(text) {
    return text.replace(/[&<>"']/g, ch => {
      switch (ch) {
      case "&": return "&amp;";
      case "<": return "&lt;";
      case ">": return "&gt;";
      case "\"": return "&quot;";
      default: return "&#39;";
      }
    });
  }

  function span(cls, text) {
    return `<span class="${cls}">${escapeHtml(text)}</span>`;
  }

  function readString(source, i, quote) {
    let j = i + 1;
    while (j < source.length) {
      const ch = source[j++];
      if (ch === "\\") {
        if (j < source.length)
          ++j;
      } else if (ch === quote) {
        break;
      } else if (ch === "\n") {
        break;
      }
    }
    return j;
  }

  function readNumber(source, i) {
    const match = source.slice(i).match(/^(?:0[xX][0-9a-fA-F]+|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)(?:[uUlLfF]*)/);
    return match ? i + match[0].length : i + 1;
  }

  function readIdentifier(source, i) {
    let j = i + 1;
    while (j < source.length && /[A-Za-z0-9_]/.test(source[j]))
      ++j;
    return j;
  }

  function isLineStartPreprocessor(source, i) {
    let j = i - 1;
    while (j >= 0 && source[j] !== "\n") {
      if (source[j] !== " " && source[j] !== "\t")
        return false;
      --j;
    }
    return true;
  }

  function highlightC(source) {
    let out = "";
    let i = 0;

    while (i < source.length) {
      const ch = source[i];
      const next = source[i + 1];

      if (ch === "#"
          && isLineStartPreprocessor(source, i)) {
        let j = i + 1;
        while (j < source.length) {
          if (source[j] === "\n") {
            let slash = j - 1;
            while (slash >= i && (source[slash] === " " || source[slash] === "\t"))
              --slash;
            if (source[slash] !== "\\")
              break;
          }
          ++j;
        }
        out += span("tok-preprocessor", source.slice(i, j));
        i = j;
      } else if (ch === "/" && next === "/") {
        let j = i + 2;
        while (j < source.length && source[j] !== "\n")
          ++j;
        out += span("tok-comment", source.slice(i, j));
        i = j;
      } else if (ch === "/" && next === "*") {
        let j = i + 2;
        while (j + 1 < source.length && !(source[j] === "*" && source[j + 1] === "/"))
          ++j;
        j = j + 1 < source.length ? j + 2 : source.length;
        out += span("tok-comment", source.slice(i, j));
        i = j;
      } else if (ch === "\"" || ch === "'") {
        const j = readString(source, i, ch);
        out += span("tok-string", source.slice(i, j));
        i = j;
      } else if (/[0-9]/.test(ch) || (ch === "." && /[0-9]/.test(next))) {
        const j = readNumber(source, i);
        out += span("tok-number", source.slice(i, j));
        i = j;
      } else if (/[A-Za-z_]/.test(ch)) {
        const j = readIdentifier(source, i);
        const word = source.slice(i, j);
        let k = j;
        while (k < source.length && /\s/.test(source[k]))
          ++k;
        if (C_KEYWORDS.has(word))
          out += span("tok-keyword", word);
        else if (C_TYPES.has(word))
          out += span("tok-type", word);
        else if (source[k] === "(")
          out += span("tok-function", word);
        else
          out += escapeHtml(word);
        i = j;
      } else {
        out += escapeHtml(ch);
        ++i;
      }
    }

    return out || " ";
  }

  function dispatchInput(textarea) {
    textarea.dispatchEvent(new InputEvent("input", {
      bubbles: true,
      inputType: "insertText"
    }));
  }

  function replaceSelection(textarea, text, selectionMode) {
    textarea.setRangeText(text, textarea.selectionStart, textarea.selectionEnd,
                          selectionMode || "end");
    dispatchInput(textarea);
  }

  function selectedLineRange(value, start, end) {
    const first = value.lastIndexOf("\n", Math.max(0, start - 1)) + 1;
    let last = end;
    if (last > start && value[last - 1] === "\n")
      --last;
    last = value.indexOf("\n", last);
    if (last === -1)
      last = value.length;
    return { first, last };
  }

  function indentSelection(textarea) {
    const value = textarea.value;
    const start = textarea.selectionStart;
    const end = textarea.selectionEnd;

    if (start === end || !value.slice(start, end).includes("\n")) {
      replaceSelection(textarea, "\t", "end");
      return;
    }

    const range = selectedLineRange(value, start, end);
    const block = value.slice(range.first, range.last);
    const indented = block.replace(/^/gm, "\t");
    textarea.setRangeText(indented, range.first, range.last, "preserve");
    textarea.selectionStart = start + 1;
    textarea.selectionEnd = end + (indented.length - block.length);
    dispatchInput(textarea);
  }

  function outdentSelection(textarea) {
    const value = textarea.value;
    const start = textarea.selectionStart;
    const end = textarea.selectionEnd;
    const range = selectedLineRange(value, start, end);
    const block = value.slice(range.first, range.last);
    const outdented = block.replace(/^(?:\t| {1,4})/gm, "");
    const delta = block.length - outdented.length;

    if (!delta)
      return;
    textarea.setRangeText(outdented, range.first, range.last, "preserve");
    textarea.selectionStart = Math.max(range.first, start - (value[start - 1] === "\t" ? 1 : 0));
    textarea.selectionEnd = Math.max(textarea.selectionStart, end - delta);
    dispatchInput(textarea);
  }

  function createCodeEditor(textarea, options = {}) {
    const wrapper = document.createElement("div");
    const pre = document.createElement("pre");
    const code = document.createElement("code");
    const highlighter = options.highlighter || highlightC;

    wrapper.className = "code-editor";
    pre.className = "code-editor-highlight";
    code.className = "code-editor-code";
    pre.setAttribute("aria-hidden", "true");
    pre.appendChild(code);

    textarea.parentNode.insertBefore(wrapper, textarea);
    wrapper.appendChild(pre);
    wrapper.appendChild(textarea);
    textarea.classList.add("code-editor-input");
    textarea.setAttribute("autocapitalize", "off");
    textarea.setAttribute("autocomplete", "off");
    textarea.setAttribute("autocorrect", "off");

    function refresh() {
      code.innerHTML = highlighter(textarea.value);
      syncScroll();
    }

    function syncScroll() {
      code.style.transform =
        `translate(${-textarea.scrollLeft}px, ${-textarea.scrollTop}px)`;
    }

    textarea.addEventListener("input", refresh);
    textarea.addEventListener("scroll", syncScroll);
    textarea.addEventListener("keydown", event => {
      if (event.key !== "Tab")
        return;
      event.preventDefault();
      if (event.shiftKey)
        outdentSelection(textarea);
      else
        indentSelection(textarea);
    });

    refresh();
    return {
      refresh,
      destroy() {
        textarea.classList.remove("code-editor-input");
        wrapper.parentNode.insertBefore(textarea, wrapper);
        wrapper.remove();
      }
    };
  }

  return {
    createCodeEditor,
    highlightC
  };
});
