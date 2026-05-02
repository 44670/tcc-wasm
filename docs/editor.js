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

  const EXTRA_C_TYPES = [
    "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
    "int64_t", "uint64_t", "intptr_t", "uintptr_t", "size_t", "ssize_t"
  ];
  EXTRA_C_TYPES.forEach(type => C_TYPES.add(type));

  function completion(label, detail, hint, kind = "function") {
    return { label, insert: label, kind, detail, hint };
  }

  const STANDARD_COMPLETIONS = [
    { label: "#include", insert: "#include ", kind: "directive",
      detail: "#include <stdio.h>", hint: "preprocessor include directive" },
    { label: "#define", insert: "#define ", kind: "directive",
      detail: "#define NAME value", hint: "preprocessor macro directive" },
    completion("main", "int main(void)", "program entry point"),
    completion("sizeof", "sizeof expression", "compile-time size query", "operator"),

    completion("printf", "int printf(const char *fmt, ...)", "stdio.h formatting"),
    completion("fprintf", "int fprintf(FILE *f, const char *fmt, ...)", "stdio.h formatting"),
    completion("sprintf", "int sprintf(char *dst, const char *fmt, ...)", "stdio.h formatting"),
    completion("snprintf", "int snprintf(char *dst, size_t n, const char *fmt, ...)", "stdio.h formatting"),
    completion("scanf", "int scanf(const char *fmt, ...)", "stdio.h input parsing"),
    completion("fscanf", "int fscanf(FILE *f, const char *fmt, ...)", "stdio.h input parsing"),
    completion("sscanf", "int sscanf(const char *src, const char *fmt, ...)", "stdio.h input parsing"),
    completion("puts", "int puts(const char *s)", "stdio.h output"),
    completion("fputs", "int fputs(const char *s, FILE *f)", "stdio.h output"),
    completion("getchar", "int getchar(void)", "stdio.h input"),
    completion("putchar", "int putchar(int ch)", "stdio.h output"),
    completion("fgetc", "int fgetc(FILE *f)", "stdio.h input"),
    completion("fputc", "int fputc(int ch, FILE *f)", "stdio.h output"),
    completion("fgets", "char *fgets(char *s, int n, FILE *f)", "stdio.h input"),
    completion("fread", "size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)", "stdio.h file input"),
    completion("fwrite", "size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)", "stdio.h file output"),
    completion("fopen", "FILE *fopen(const char *path, const char *mode)", "stdio.h file"),
    completion("fclose", "int fclose(FILE *f)", "stdio.h file"),
    completion("fflush", "int fflush(FILE *f)", "stdio.h file"),
    completion("fseek", "int fseek(FILE *f, long offset, int whence)", "stdio.h file"),
    completion("ftell", "long ftell(FILE *f)", "stdio.h file"),

    completion("malloc", "void *malloc(size_t size)", "stdlib.h allocation"),
    completion("calloc", "void *calloc(size_t nmemb, size_t size)", "stdlib.h allocation"),
    completion("realloc", "void *realloc(void *ptr, size_t size)", "stdlib.h allocation"),
    completion("free", "void free(void *ptr)", "stdlib.h allocation"),
    completion("qsort", "void qsort(void *base, size_t nmemb, size_t width, int (*compar)(const void *, const void *))", "stdlib.h sorting"),
    completion("bsearch", "void *bsearch(const void *key, const void *base, size_t nmemb, size_t width, int (*compar)(const void *, const void *))", "stdlib.h binary search"),
    completion("abs", "int abs(int value)", "stdlib.h integer math"),
    completion("labs", "long labs(long value)", "stdlib.h integer math"),
    completion("llabs", "long long llabs(long long value)", "stdlib.h integer math"),
    completion("atoi", "int atoi(const char *s)", "stdlib.h conversion"),
    completion("atol", "long atol(const char *s)", "stdlib.h conversion"),
    completion("atoll", "long long atoll(const char *s)", "stdlib.h conversion"),
    completion("strtol", "long strtol(const char *s, char **endptr, int base)", "stdlib.h conversion"),
    completion("strtoul", "unsigned long strtoul(const char *s, char **endptr, int base)", "stdlib.h conversion"),
    completion("strtoll", "long long strtoll(const char *s, char **endptr, int base)", "stdlib.h conversion"),
    completion("strtoull", "unsigned long long strtoull(const char *s, char **endptr, int base)", "stdlib.h conversion"),
    completion("strtod", "double strtod(const char *s, char **endptr)", "stdlib.h conversion"),
    completion("rand", "int rand(void)", "stdlib.h pseudo-random"),
    completion("srand", "void srand(unsigned seed)", "stdlib.h pseudo-random"),
    completion("exit", "void exit(int code)", "stdlib.h program control"),

    completion("memset", "void *memset(void *dst, int c, size_t n)", "string.h memory"),
    completion("memcpy", "void *memcpy(void *dst, const void *src, size_t n)", "string.h memory"),
    completion("memmove", "void *memmove(void *dst, const void *src, size_t n)", "string.h memory"),
    completion("memcmp", "int memcmp(const void *a, const void *b, size_t n)", "string.h memory"),
    completion("memchr", "void *memchr(const void *s, int c, size_t n)", "string.h memory"),
    completion("strlen", "size_t strlen(const char *s)", "string.h string length"),
    completion("strnlen", "size_t strnlen(const char *s, size_t max)", "string.h string length"),
    completion("strcpy", "char *strcpy(char *dst, const char *src)", "string.h copy"),
    completion("strncpy", "char *strncpy(char *dst, const char *src, size_t n)", "string.h copy"),
    completion("strcat", "char *strcat(char *dst, const char *src)", "string.h append"),
    completion("strncat", "char *strncat(char *dst, const char *src, size_t n)", "string.h append"),
    completion("strdup", "char *strdup(const char *s)", "string.h allocation"),
    completion("strcmp", "int strcmp(const char *a, const char *b)", "string.h compare"),
    completion("strncmp", "int strncmp(const char *a, const char *b, size_t n)", "string.h compare"),
    completion("strchr", "char *strchr(const char *s, int c)", "string.h search"),
    completion("strrchr", "char *strrchr(const char *s, int c)", "string.h search"),
    completion("strstr", "char *strstr(const char *haystack, const char *needle)", "string.h search"),
    completion("strpbrk", "char *strpbrk(const char *s, const char *accept)", "string.h search"),
    completion("strspn", "size_t strspn(const char *s, const char *accept)", "string.h scan"),
    completion("strcspn", "size_t strcspn(const char *s, const char *reject)", "string.h scan"),
    completion("strtok", "char *strtok(char *s, const char *delim)", "string.h tokenization"),
    completion("strtok_r", "char *strtok_r(char *s, const char *delim, char **saveptr)", "string.h tokenization"),

    completion("isdigit", "int isdigit(int c)", "ctype.h classification"),
    completion("isalpha", "int isalpha(int c)", "ctype.h classification"),
    completion("isalnum", "int isalnum(int c)", "ctype.h classification"),
    completion("isspace", "int isspace(int c)", "ctype.h classification"),
    completion("isblank", "int isblank(int c)", "ctype.h classification"),
    completion("isupper", "int isupper(int c)", "ctype.h classification"),
    completion("islower", "int islower(int c)", "ctype.h classification"),
    completion("isxdigit", "int isxdigit(int c)", "ctype.h classification"),
    completion("isprint", "int isprint(int c)", "ctype.h classification"),
    completion("isgraph", "int isgraph(int c)", "ctype.h classification"),
    completion("ispunct", "int ispunct(int c)", "ctype.h classification"),
    completion("iscntrl", "int iscntrl(int c)", "ctype.h classification"),
    completion("tolower", "int tolower(int c)", "ctype.h conversion"),
    completion("toupper", "int toupper(int c)", "ctype.h conversion")
  ];

  const FALLBACK_HEADERS = [
    "assert.h", "ctype.h", "errno.h", "limits.h", "stdarg.h", "stddef.h",
    "stdint.h", "stdio.h", "stdlib.h", "string.h", "tcclib.h", "time.h"
  ];

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

  function uniqueItems(items) {
    const seen = new Set();
    const out = [];

    for (const item of items) {
      const key = item.label.toLowerCase();
      if (seen.has(key))
        continue;
      seen.add(key);
      out.push(item);
    }
    return out;
  }

  function baseIdentifierCompletions() {
    const keywords = Array.from(C_KEYWORDS, label => ({
      label,
      insert: label,
      kind: "keyword",
      detail: label,
      hint: "C keyword"
    }));
    const types = Array.from(C_TYPES, label => ({
      label,
      insert: label,
      kind: "type",
      detail: label,
      hint: "C scalar type"
    }));
    const extraTypes = EXTRA_C_TYPES.map(label => ({
      label,
      insert: label,
      kind: "type",
      detail: label,
      hint: "standard integer type"
    }));

    return uniqueItems([
      ...STANDARD_COMPLETIONS,
      ...keywords,
      ...types,
      ...extraTypes
    ]).sort((a, b) => a.label.localeCompare(b.label));
  }

  function resourceHeaders() {
    const resourceRoot = typeof globalThis !== "undefined"
      ? globalThis.TccWasmIdeResources
      : null;
    const files = resourceRoot && (resourceRoot.files || resourceRoot);
    const headers = new Set(FALLBACK_HEADERS);

    if (files) {
      for (const name of Object.keys(files)) {
        const match = name.match(/^\/include\/([^/]+\.h)$/);
        if (match)
          headers.add(match[1]);
      }
    }

    return Array.from(headers).sort().map(label => ({
      label,
      insert: label,
      kind: "header",
      detail: `/include/${label}`,
      hint: "virtual compiler header"
    }));
  }

  function collectSourceIdentifiers(source) {
    const seen = new Set();
    const items = [];
    const re = /\b[A-Za-z_][A-Za-z0-9_]*\b/g;
    let match;

    while ((match = re.exec(source))) {
      const label = match[0];
      if (label.length < 2 || C_KEYWORDS.has(label) || C_TYPES.has(label))
        continue;
      if (seen.has(label))
        continue;
      seen.add(label);
      items.push({
        label,
        insert: label,
        kind: "symbol",
        detail: label,
        hint: "source symbol"
      });
    }
    return items;
  }

  function currentLineInfo(value, pos) {
    let line = 0;
    let lineStart = 0;

    for (let i = 0; i < pos; ++i) {
      if (value[i] === "\n") {
        ++line;
        lineStart = i + 1;
      }
    }
    return {
      line,
      lineStart,
      column: visualColumn(value.slice(lineStart, pos))
    };
  }

  function visualColumn(text) {
    let column = 0;

    for (const ch of text) {
      if (ch === "\t")
        column += 4 - (column % 4);
      else
        ++column;
    }
    return column;
  }

  function identifierRangeBeforeCaret(value, pos) {
    let start = pos;

    while (start > 0 && /[A-Za-z0-9_]/.test(value[start - 1]))
      --start;
    if (start === pos) {
      if (pos > 0 && value[pos - 1] === "#")
        return {
          start: pos - 1,
          end: pos,
          prefix: "#"
        };
      return null;
    }
    if (start > 0 && value[start - 1] === "#")
      --start;
    return {
      start,
      end: pos,
      prefix: value.slice(start, pos)
    };
  }

  function includeRangeBeforeCaret(value, pos) {
    const lineStart = value.lastIndexOf("\n", Math.max(0, pos - 1)) + 1;
    const before = value.slice(lineStart, pos);
    const match = before.match(/^\s*#\s*include\s*([<"])([^>"]*)$/);

    if (!match)
      return null;
    return {
      start: pos - match[2].length,
      end: pos,
      prefix: match[2],
      delimiter: match[1]
    };
  }

  function functionCallBeforeCaret(value, pos) {
    const before = value.slice(0, pos);
    const match = before.match(/([A-Za-z_][A-Za-z0-9_]*)\s*\([^()\n]*$/);

    return match ? match[1] : "";
  }

  function parsePx(value, fallback) {
    const parsed = parseFloat(value);
    return Number.isFinite(parsed) ? parsed : fallback;
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

  function keepIndentOnEnter(textarea) {
    const value = textarea.value;
    const pos = textarea.selectionStart;
    const lineStart = value.lastIndexOf("\n", Math.max(0, pos - 1)) + 1;
    const indent = (value.slice(lineStart).match(/^[\t ]*/) || [""])[0];

    replaceSelection(textarea, `\n${indent}`, "end");
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
    const lineHighlight = document.createElement("div");
    const gutter = document.createElement("div");
    const lineNumbers = document.createElement("div");
    const pre = document.createElement("pre");
    const code = document.createElement("code");
    const popup = document.createElement("div");
    const hint = document.createElement("div");
    const measure = document.createElement("span");
    const highlighter = options.highlighter || highlightC;
    const staticIdentifierCompletions = uniqueItems([
      ...(options.completions || []),
      ...baseIdentifierCompletions()
    ]);
    const headerCompletions = options.headers || resourceHeaders();
    let completionItems = [];
    let completionIndex = 0;
    let completionRange = null;
    let lineCount = 0;
    let activeLine = -1;

    wrapper.className = "code-editor";
    lineHighlight.className = "code-editor-line-highlight";
    gutter.className = "code-editor-gutter";
    lineNumbers.className = "code-editor-line-numbers";
    pre.className = "code-editor-highlight";
    code.className = "code-editor-code";
    popup.className = "code-editor-popup";
    hint.className = "code-editor-hint";
    measure.className = "code-editor-measure";
    pre.setAttribute("aria-hidden", "true");
    gutter.setAttribute("aria-hidden", "true");
    popup.setAttribute("role", "listbox");
    popup.hidden = true;
    hint.hidden = true;
    pre.appendChild(code);
    gutter.appendChild(lineNumbers);

    textarea.parentNode.insertBefore(wrapper, textarea);
    wrapper.appendChild(lineHighlight);
    wrapper.appendChild(gutter);
    wrapper.appendChild(pre);
    wrapper.appendChild(textarea);
    wrapper.appendChild(popup);
    wrapper.appendChild(hint);
    wrapper.appendChild(measure);
    textarea.classList.add("code-editor-input");
    textarea.setAttribute("autocapitalize", "off");
    textarea.setAttribute("autocomplete", "off");
    textarea.setAttribute("autocorrect", "off");

    function refresh() {
      code.innerHTML = highlighter(textarea.value);
      refreshLineNumbers();
      syncScroll();
      updateContextHint();
    }

    function syncScroll() {
      code.style.transform =
        `translate(${-textarea.scrollLeft}px, ${-textarea.scrollTop}px)`;
      lineNumbers.style.transform = `translateY(${-textarea.scrollTop}px)`;
      refreshLineHighlight();
      positionAssist();
    }

    function refreshLineNumbers() {
      const nextLineCount = textarea.value.split("\n").length;
      const nextActiveLine = currentLineInfo(textarea.value, textarea.selectionStart).line;

      if (nextLineCount !== lineCount || nextActiveLine !== activeLine) {
        lineCount = nextLineCount;
        activeLine = nextActiveLine;
        wrapper.style.setProperty("--code-gutter-width",
          `${Math.max(46, 30 + String(lineCount).length * 8)}px`);
        lineNumbers.innerHTML = Array.from({ length: lineCount }, (_, i) => {
          const cls = i === activeLine ? " class=\"active\"" : "";
          return `<span${cls}>${i + 1}</span>`;
        }).join("");
      }
    }

    function metrics() {
      const style = getComputedStyle(textarea);
      measure.textContent = "0000000000";
      const rect = measure.getBoundingClientRect();
      const lineHeight = parsePx(style.lineHeight, rect.height || 18);

      return {
        charWidth: rect.width ? rect.width / 10 : 8,
        lineHeight,
        paddingLeft: parsePx(style.paddingLeft, 0),
        paddingTop: parsePx(style.paddingTop, 0)
      };
    }

    function caretPoint() {
      const info = currentLineInfo(textarea.value, textarea.selectionStart);
      const m = metrics();

      return {
        x: m.paddingLeft + info.column * m.charWidth - textarea.scrollLeft,
        y: m.paddingTop + info.line * m.lineHeight - textarea.scrollTop,
        lineHeight: m.lineHeight
      };
    }

    function refreshLineHighlight() {
      const point = caretPoint();
      const top = point.y;

      lineHighlight.style.height = `${point.lineHeight}px`;
      lineHighlight.style.transform = `translateY(${top}px)`;
      lineHighlight.hidden = top < -point.lineHeight || top > wrapper.clientHeight;
    }

    function positionAssist() {
      if (!popup.hidden) {
        positionPopup();
        if (!hint.hidden)
          positionHintNearPopup();
      } else if (!hint.hidden) {
        positionHintNearCaret();
      }
    }

    function clampedLeft(width, preferred) {
      const minLeft = parsePx(getComputedStyle(wrapper).getPropertyValue("--code-gutter-width"), 46) + 6;
      const maxLeft = Math.max(minLeft, wrapper.clientWidth - width - 6);
      return Math.min(Math.max(preferred, minLeft), maxLeft);
    }

    function positionPopup() {
      const point = caretPoint();
      const width = popup.offsetWidth || 310;
      const left = clampedLeft(width, point.x);
      let top = point.y + point.lineHeight + 4;

      if (top + popup.offsetHeight > wrapper.clientHeight - 6)
        top = Math.max(6, point.y - popup.offsetHeight - 4);
      popup.style.left = `${left}px`;
      popup.style.top = `${top}px`;
    }

    function positionHintNearPopup() {
      const left = parsePx(popup.style.left, caretPoint().x);
      let top = parsePx(popup.style.top, 0) + popup.offsetHeight + 4;

      if (top + hint.offsetHeight > wrapper.clientHeight - 6)
        top = Math.max(6, parsePx(popup.style.top, 0) - hint.offsetHeight - 4);
      hint.style.left = `${clampedLeft(hint.offsetWidth || 310, left)}px`;
      hint.style.top = `${top}px`;
    }

    function positionHintNearCaret() {
      const point = caretPoint();
      const width = hint.offsetWidth || 310;
      const left = clampedLeft(width, point.x);
      let top = point.y + point.lineHeight + 4;

      if (top + hint.offsetHeight > wrapper.clientHeight - 6)
        top = Math.max(6, point.y - hint.offsetHeight - 4);
      hint.style.left = `${left}px`;
      hint.style.top = `${top}px`;
    }

    function completionContext(manual) {
      const pos = textarea.selectionStart;
      const value = textarea.value;
      const include = textarea.selectionStart === textarea.selectionEnd
        ? includeRangeBeforeCaret(value, pos)
        : null;

      if (include)
        return { type: "header", ...include };
      if (textarea.selectionStart !== textarea.selectionEnd)
        return null;

      const identifier = identifierRangeBeforeCaret(value, pos);
      if (identifier)
        return { type: "identifier", ...identifier };
      if (manual)
        return { type: "identifier", start: pos, end: pos, prefix: "" };
      return null;
    }

    function matchesForContext(context, manual) {
      const prefix = context.prefix.toLowerCase();

      if (context.type === "header") {
        return headerCompletions
          .filter(item => item.label.toLowerCase().startsWith(prefix))
          .slice(0, 12)
          .map(item => {
            const closer = context.delimiter === "<" ? ">" : "\"";
            const suffix = textarea.value[context.end] === closer ? "" : closer;
            return { ...item, insert: item.label + suffix };
          });
      }

      if (!manual && context.prefix.length < 2 && !context.prefix.startsWith("#"))
        return [];

      const sourceItems = collectSourceIdentifiers(textarea.value);
      const items = uniqueItems([
        ...staticIdentifierCompletions,
        ...sourceItems
      ]);
      return items
        .filter(item => {
          const filterText = (item.filterText || item.label).toLowerCase();
          return filterText.startsWith(prefix) && item.label !== context.prefix;
        })
        .sort((a, b) => a.label.localeCompare(b.label))
        .slice(0, 12);
    }

    function openCompletion(manual) {
      const context = completionContext(manual);

      if (!context) {
        closeCompletion();
        updateContextHint();
        return;
      }
      completionItems = matchesForContext(context, manual);
      completionRange = { start: context.start, end: context.end };
      completionIndex = 0;
      if (!completionItems.length) {
        closeCompletion();
        updateContextHint();
        return;
      }
      renderCompletionPopup();
      popup.hidden = false;
      showHint(completionItems[completionIndex]);
      positionAssist();
    }

    function renderCompletionPopup() {
      popup.replaceChildren();
      completionItems.forEach((item, index) => {
        const row = document.createElement("div");
        const label = document.createElement("span");
        const kind = document.createElement("span");

        row.className = "code-editor-completion";
        row.setAttribute("role", "option");
        row.setAttribute("aria-selected", index === completionIndex ? "true" : "false");
        if (index === completionIndex)
          row.classList.add("active");
        label.className = "code-editor-completion-label";
        kind.className = "code-editor-completion-kind";
        label.textContent = item.label;
        kind.textContent = item.kind || "";
        row.appendChild(label);
        row.appendChild(kind);
        row.addEventListener("mouseenter", () => selectCompletion(index));
        row.addEventListener("mousedown", event => {
          event.preventDefault();
          selectCompletion(index);
          acceptCompletion();
        });
        popup.appendChild(row);
      });
    }

    function selectCompletion(index) {
      if (!completionItems.length)
        return;
      completionIndex = (index + completionItems.length) % completionItems.length;
      Array.from(popup.children).forEach((row, i) => {
        row.classList.toggle("active", i === completionIndex);
        row.setAttribute("aria-selected", i === completionIndex ? "true" : "false");
      });
      const active = popup.children[completionIndex];
      if (active)
        active.scrollIntoView({ block: "nearest" });
      showHint(completionItems[completionIndex]);
      positionAssist();
    }

    function acceptCompletion() {
      const item = completionItems[completionIndex];

      if (!item || !completionRange)
        return false;
      textarea.focus();
      textarea.setRangeText(item.insert, completionRange.start, completionRange.end, "end");
      closeCompletion();
      dispatchInput(textarea);
      refresh();
      return true;
    }

    function closeCompletion() {
      popup.hidden = true;
      popup.replaceChildren();
      completionItems = [];
      completionRange = null;
    }

    function showHint(item) {
      if (!item || (!item.detail && !item.hint)) {
        hideHint();
        return;
      }

      hint.replaceChildren();
      if (item.detail) {
        const signature = document.createElement("div");
        signature.className = "code-editor-hint-signature";
        signature.textContent = item.detail;
        hint.appendChild(signature);
      }
      if (item.hint) {
        const detail = document.createElement("div");
        detail.className = "code-editor-hint-detail";
        detail.textContent = item.hint;
        hint.appendChild(detail);
      }
      hint.hidden = false;
      positionAssist();
    }

    function hideHint() {
      hint.hidden = true;
      hint.replaceChildren();
    }

    function updateContextHint() {
      if (!popup.hidden) {
        showHint(completionItems[completionIndex]);
        return;
      }
      if (textarea.selectionStart !== textarea.selectionEnd) {
        hideHint();
        return;
      }

      const fn = functionCallBeforeCaret(textarea.value, textarea.selectionStart);
      const item = fn
        ? staticIdentifierCompletions.find(candidate =>
          candidate.kind === "function" && candidate.label === fn)
        : null;
      if (item)
        showHint(item);
      else
        hideHint();
    }

    function handleCursorChange() {
      refreshLineNumbers();
      refreshLineHighlight();
      if (!popup.hidden)
        openCompletion(false);
      else
        updateContextHint();
    }

    textarea.addEventListener("input", refresh);
    textarea.addEventListener("scroll", syncScroll);
    textarea.addEventListener("keydown", event => {
      if ((event.ctrlKey || event.metaKey) && event.key === " ") {
        event.preventDefault();
        openCompletion(true);
        return;
      }

      if (!popup.hidden) {
        if (event.key === "ArrowDown") {
          event.preventDefault();
          selectCompletion(completionIndex + 1);
          return;
        }
        if (event.key === "ArrowUp") {
          event.preventDefault();
          selectCompletion(completionIndex - 1);
          return;
        }
        if (event.key === "Enter" || event.key === "Tab") {
          event.preventDefault();
          acceptCompletion();
          return;
        }
        if (event.key === "Escape") {
          event.preventDefault();
          closeCompletion();
          updateContextHint();
          return;
        }
      }

      if (event.key === "Escape" && !hint.hidden) {
        event.preventDefault();
        hideHint();
        return;
      }

      if (event.key === "Tab") {
        event.preventDefault();
        if (event.shiftKey)
          outdentSelection(textarea);
        else
          indentSelection(textarea);
        return;
      }

      if (event.key === "Enter") {
        event.preventDefault();
        keepIndentOnEnter(textarea);
      }
    });
    textarea.addEventListener("keyup", event => {
      if (!popup.hidden && (event.key === "ArrowUp" || event.key === "ArrowDown"))
        return;
      if (event.key === "ArrowLeft" || event.key === "ArrowRight"
          || event.key === "ArrowUp" || event.key === "ArrowDown"
          || event.key === "Home" || event.key === "End"
          || event.key === "PageUp" || event.key === "PageDown")
        handleCursorChange();
    });
    textarea.addEventListener("mouseup", handleCursorChange);
    textarea.addEventListener("select", handleCursorChange);
    textarea.addEventListener("input", () => openCompletion(false));
    textarea.addEventListener("blur", () => {
      window.setTimeout(() => {
        if (!wrapper.contains(document.activeElement)) {
          closeCompletion();
          hideHint();
        }
      }, 100);
    });

    refresh();
    return {
      refresh,
      destroy() {
        textarea.classList.remove("code-editor-input");
        closeCompletion();
        hideHint();
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
