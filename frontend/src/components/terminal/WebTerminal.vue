<template>
  <div ref="mountElement" class="web-terminal__mount" :style="{ backgroundColor: props.background }" />
</template>

<script setup lang="ts">
import "@xterm/xterm/css/xterm.css";
import { FitAddon } from "@xterm/addon-fit";
import { SearchAddon } from "@xterm/addon-search";
import { SerializeAddon } from "@xterm/addon-serialize";
import { WebLinksAddon } from "@xterm/addon-web-links";
import { WebglAddon } from "@xterm/addon-webgl";
import { Terminal, type IDisposable } from "@xterm/xterm";
import { onBeforeUnmount, onMounted, ref } from "vue";
import type {
  TerminalRenderer,
  WebTerminalHandle,
  WebTerminalReadyEvent,
  WebTerminalResizeEvent,
  WebTerminalSearchResult,
} from "./WebTerminal.types";

const props = withDefaults(defineProps<{
  scrollback?: number;
  fontSize?: number;
  lineHeight?: number;
  letterSpacing?: number;
  fontFamily?: string;
  restoreBuffer?: string;
  background?: string;
  foreground?: string;
  cursor?: string;
  selectionBackground?: string;
  searchHighlightLimit?: number;
  autofocus?: boolean;
  enableWebgl?: boolean;
}>(), {
  scrollback: 50000,
  fontSize: 14,
  lineHeight: 1.2,
  letterSpacing: 0,
  fontFamily: '"SFMono-Regular", Consolas, "Liberation Mono", monospace',
  restoreBuffer: "",
  background: "#101418",
  foreground: "#d8dee9",
  cursor: "#8fbcbb",
  selectionBackground: "#d96820",
  searchHighlightLimit: 1000,
  autofocus: true,
  enableWebgl: true,
});

const emit = defineEmits<{
  ready: [event: WebTerminalReadyEvent];
  data: [data: string];
  resize: [event: WebTerminalResizeEvent];
  renderer: [renderer: TerminalRenderer];
  searchResults: [event: WebTerminalSearchResult];
}>();

const mountElement = ref<HTMLElement>();
let terminal: Terminal | undefined;
let fitAddon: FitAddon | undefined;
let searchAddon: SearchAddon | undefined;
let serializeAddon: SerializeAddon | undefined;
let webglAddon: WebglAddon | undefined;
let resizeObserver: ResizeObserver | undefined;
let resizeFrame = 0;
let fitStabilizeFrame = 0;
let appearanceFrame = 0;
let touchCleanup: (() => void) | undefined;
let searchWorker: Worker | undefined;
let searchCountTimer = 0;
let searchCountRequestId = 0;
let currentSearchTerm = "";
let currentSearchCaseSensitive = false;
let currentSearchWholeWord = false;
let currentSearchRegex = false;
let currentSearchIndex = -1;
let exactSearchCount: number | undefined;
let addonSearchIndex = -1;
let addonSearchCount = 0;
let pendingSearchAction: "reset" | "next" | "previous" | undefined;
let searchSnapshotReadyId = 0;
let pendingJumpIndex: number | undefined;
let jumpDecorations: IDisposable[] = [];
let nearbyDecorations: IDisposable[] = [];

type SearchWorkerResponse =
  | { type: "result"; id: number; count: number }
  | { type: "location"; id: number; index: number; row: number; offset: number; length: number }
  | { type: "window"; id: number; locations: Array<[index: number, row: number, offset: number, length: number]> };

const searchDecorations = {
  matchBackground: "#756719",
  matchBorder: "#e1ca4d",
  matchOverviewRuler: "#c3ad37",
  activeMatchBackground: "#d85d1c",
  activeMatchBorder: "#ffe39a",
  activeMatchColorOverviewRuler: "#f18a4f",
};

let reportedColumns = 0;
let reportedRows = 0;

function reportSize() {
  if (!terminal) return;
  if (terminal.cols === reportedColumns && terminal.rows === reportedRows) return;
  reportedColumns = terminal.cols;
  reportedRows = terminal.rows;
  emit("resize", { columns: terminal.cols, rows: terminal.rows });
}

function fitNow() {
  if (!terminal || !fitAddon) return;
  fitAddon.fit();
  reportSize();
}

function fit() {
  fitNow();
  // Removing a sibling panel changes the available grid/flex height. A second
  // frame lets xterm apply its new canvas dimensions before refreshing the
  // final row, avoiding a half-clipped line after the command panel is hidden.
  window.cancelAnimationFrame(fitStabilizeFrame);
  fitStabilizeFrame = window.requestAnimationFrame(() => {
    fitNow();
    terminal?.refresh(0, Math.max(0, terminal.rows - 1));
  });
}

function write(data: string | Uint8Array, callback?: () => void) {
  terminal?.write(data, callback);
}

function writeln(data: string) {
  terminal?.writeln(data);
}

function focus() {
  terminal?.focus();
}

function disposeDecorations(items: IDisposable[]) {
  items.splice(0).forEach((item) => item.dispose());
}

function bufferColumnFromStringOffset(row: number, stringOffset: number) {
  const line = terminal?.buffer.active.getLine(row);
  if (!line) return -1;
  let consumed = 0;
  for (let column = 0; column < line.length; column += 1) {
    const cell = line.getCell(column);
    if (!cell || cell.getWidth() === 0) continue;
    const charsLength = cell.getCode() === 0 ? 1 : Math.max(1, cell.getChars().length);
    if (consumed + charsLength > stringOffset) return column;
    consumed += charsLength;
  }
  return Math.min(line.length - 1, consumed === stringOffset ? line.length : -1);
}

function matchBufferSize(row: number, column: number, textLength: number) {
  const instance = terminal;
  if (!instance) return 1;
  let currentRow = row;
  let currentColumn = column;
  let remaining = Math.max(1, textLength);
  const start = row * instance.cols + column;
  while (currentRow < instance.buffer.active.length) {
    const line = instance.buffer.active.getLine(currentRow);
    if (!line) break;
    while (currentColumn < line.length) {
      const cell = line.getCell(currentColumn);
      if (!cell) break;
      const width = cell.getWidth();
      if (width === 0) {
        currentColumn += 1;
        continue;
      }
      remaining -= cell.getCode() === 0 ? 1 : Math.max(1, cell.getChars().length);
      currentColumn += Math.max(1, width);
      if (remaining <= 0) return Math.max(1, currentRow * instance.cols + currentColumn - start);
    }
    currentRow += 1;
    currentColumn = 0;
  }
  return Math.max(1, textLength);
}

function decorateMatch(row: number, stringOffset: number, textLength: number, active: boolean, target: IDisposable[]) {
  const instance = terminal;
  if (!instance || !currentSearchTerm) return;
  const column = bufferColumnFromStringOffset(row, stringOffset);
  if (column < 0 || column >= instance.cols) return;
  const size = matchBufferSize(row, column, textLength);
  let remaining = size;
  let decorationRow = row;
  let decorationColumn = column;
  while (remaining > 0) {
    const availableWidth = instance.cols - decorationColumn;
    if (availableWidth <= 0) break;
    const width = Math.min(remaining, availableWidth);
    const marker = instance.registerMarker(decorationRow - instance.buffer.active.baseY - instance.buffer.active.cursorY);
    const decoration = instance.registerDecoration({
      marker,
      x: decorationColumn,
      width,
      backgroundColor: active ? searchDecorations.activeMatchBackground : searchDecorations.matchBackground,
      layer: "top",
    });
    target.push(marker);
    if (decoration) target.push(decoration);
    remaining -= width;
    decorationRow += 1;
    decorationColumn = 0;
  }
  if (active) {
    instance.select(column, row, size);
    instance.scrollToLine(Math.max(0, row - Math.floor(instance.rows / 2)));
    instance.focus();
  }
}

function requestNearbyHighlights() {
  if (!searchWorker || searchSnapshotReadyId !== searchCountRequestId || currentSearchIndex < props.searchHighlightLimit) {
    disposeDecorations(nearbyDecorations);
    return;
  }
  const start = Math.max(props.searchHighlightLimit, currentSearchIndex - 100);
  const end = Math.min(exactSearchCount ?? currentSearchIndex + 101, currentSearchIndex + 101);
  searchWorker.postMessage({ type: "window", id: searchCountRequestId, start, end });
}

function emitSearchResult() {
  const count = exactSearchCount ?? addonSearchCount;
  if (count <= 0) currentSearchIndex = -1;
  else if (currentSearchIndex < 0 && addonSearchIndex >= 0) currentSearchIndex = addonSearchIndex;
  emit("searchResults", {
    resultIndex: currentSearchIndex,
    resultCount: count,
    limited: exactSearchCount === undefined && addonSearchCount >= props.searchHighlightLimit,
  });
}

function nextMainThreadSlice() {
  return new Promise<void>((resolve) => window.setTimeout(resolve, 0));
}

async function buildSearchSnapshot(requestId: number, term: string) {
  const instance = terminal;
  const worker = searchWorker;
  if (!instance || !worker || requestId !== searchCountRequestId || term !== currentSearchTerm) return;
  worker.postMessage({
    type: "start",
    id: requestId,
    query: term,
    caseSensitive: currentSearchCaseSensitive,
    wholeWord: currentSearchWholeWord,
    regex: currentSearchRegex,
  });
  const buffer = instance.buffer.active;
  let segments: Array<[row: number, text: string, wrapsToNext: boolean]> = [];
  let sliceStartedAt = performance.now();
  for (let row = 0; row < buffer.length; row += 1) {
    if (requestId !== searchCountRequestId || term !== currentSearchTerm) return;
    const line = buffer.getLine(row);
    if (line) {
      const nextLine = buffer.getLine(row + 1);
      const nextWraps = nextLine?.isWrapped === true;
      let value = line.translateToString(!nextWraps);
      // Match SearchAddon's handling of a wide character wrapping at the last column.
      const lastCell = line.getCell(line.length - 1);
      if (nextWraps && lastCell?.getCode() === 0 && lastCell.getWidth() === 1 && nextLine?.getCell(0)?.getWidth() === 2) {
        value = value.slice(0, -1);
      }
      segments.push([row, value, nextWraps]);
    }
    if (segments.length >= 512 || performance.now() - sliceStartedAt >= 4) {
      if (segments.length) worker.postMessage({ type: "chunk", id: requestId, segments });
      segments = [];
      await nextMainThreadSlice();
      sliceStartedAt = performance.now();
    }
  }
  if (segments.length) worker.postMessage({ type: "chunk", id: requestId, segments });
  if (requestId === searchCountRequestId && term === currentSearchTerm) {
    worker.postMessage({ type: "finish", id: requestId });
  }
}

function scheduleExactSearchCount(term: string, delay = 0, resetKnownCount = false) {
  window.clearTimeout(searchCountTimer);
  const requestId = ++searchCountRequestId;
  searchSnapshotReadyId = 0;
  if (resetKnownCount) exactSearchCount = undefined;
  searchCountTimer = window.setTimeout(() => {
    void buildSearchSnapshot(requestId, term);
  }, delay);
}

function search(
  term: string,
  previous = false,
  incremental = false,
  caseSensitive = false,
  wholeWord = false,
  regex = false,
) {
  if (!searchAddon || !term) return false;
  const queryChanged = term !== currentSearchTerm;
  const optionsChanged = caseSensitive !== currentSearchCaseSensitive
    || wholeWord !== currentSearchWholeWord
    || regex !== currentSearchRegex;
  const termChanged = queryChanged || optionsChanged;
  if (!termChanged && incremental) return addonSearchCount > 0;
  if (termChanged) {
    disposeDecorations(jumpDecorations);
    disposeDecorations(nearbyDecorations);
    if (optionsChanged) {
      // SearchAddon can navigate with new options while retaining decorations
      // and result counts created with the previous options. Clearing its
      // cached term forces a complete highlight/result rebuild.
      searchAddon.clearDecorations();
    }
    currentSearchTerm = term;
    currentSearchCaseSensitive = caseSensitive;
    currentSearchWholeWord = wholeWord;
    currentSearchRegex = regex;
    currentSearchIndex = -1;
    addonSearchIndex = -1;
    addonSearchCount = 0;
    terminal?.clearSelection();
    pendingSearchAction = "reset";
    scheduleExactSearchCount(term, 0, true);
  } else {
    disposeDecorations(jumpDecorations);
    pendingSearchAction = previous ? "previous" : "next";
  }
  const options = {
    caseSensitive,
    wholeWord,
    regex,
    incremental: optionsChanged ? false : incremental,
    decorations: searchDecorations,
  };
  let found = false;
  try {
    found = previous ? searchAddon.findPrevious(term, options) : searchAddon.findNext(term, options);
  } catch {
    // Invalid regular expressions are treated as zero matches. The search
    // worker follows the same rule and will publish the exact zero count.
    searchAddon.clearDecorations();
    addonSearchIndex = -1;
    addonSearchCount = 0;
    currentSearchIndex = -1;
    emitSearchResult();
  }
  pendingSearchAction = undefined;
  return found;
}

function jumpToSearchIndex(index: number) {
  const target = Math.trunc(index);
  const count = exactSearchCount ?? (addonSearchCount < props.searchHighlightLimit ? addonSearchCount : undefined);
  if (!currentSearchTerm || !Number.isFinite(target) || target < 0 || (count !== undefined && target >= count) || !searchWorker) return false;
  pendingJumpIndex = target;
  if (searchSnapshotReadyId === searchCountRequestId) {
    searchWorker.postMessage({ type: "locate", id: searchCountRequestId, index: target });
  } else {
    scheduleExactSearchCount(currentSearchTerm, 0);
  }
  return true;
}

function clearSearch() {
  window.clearTimeout(searchCountTimer);
  searchCountRequestId += 1;
  currentSearchTerm = "";
  currentSearchCaseSensitive = false;
  currentSearchWholeWord = false;
  currentSearchRegex = false;
  currentSearchIndex = -1;
  exactSearchCount = undefined;
  addonSearchIndex = -1;
  addonSearchCount = 0;
  pendingSearchAction = undefined;
  pendingJumpIndex = undefined;
  searchSnapshotReadyId = 0;
  disposeDecorations(jumpDecorations);
  disposeDecorations(nearbyDecorations);
  searchAddon?.clearDecorations();
}

function setAppearance(options: { fontSize?: number; lineHeight?: number; letterSpacing?: number }) {
  if (!terminal) return;
  if (options.fontSize !== undefined) terminal.options.fontSize = options.fontSize;
  if (options.lineHeight !== undefined) terminal.options.lineHeight = options.lineHeight;
  if (options.letterSpacing !== undefined) terminal.options.letterSpacing = options.letterSpacing;
  // xterm recalculates character metrics asynchronously. Fitting in the same
  // frame can retain the old row height and leave the final row half clipped.
  window.cancelAnimationFrame(appearanceFrame);
  appearanceFrame = window.requestAnimationFrame(() => {
    terminal?.refresh(0, Math.max(0, terminal.rows - 1));
    fit();
  });
}

function getTerminal() {
  return terminal;
}

function getElement() {
  return mountElement.value;
}

const terminalHandle: WebTerminalHandle = {
  write,
  writeln,
  fit,
  focus,
  search,
  jumpToSearchIndex,
  clearSearch,
  setAppearance,
  getTerminal,
  getElement,
};

function setupTouchScrolling(element: HTMLElement, instance: Terminal) {
  let startY: number | null = null;
  let lastY: number | null = null;
  let remainder = 0;
  let scrolling = false;
  const touchStart = (event: TouchEvent) => {
    startY = event.touches.length === 1 ? event.touches[0].clientY : null;
    lastY = startY;
    remainder = 0;
    scrolling = false;
  };
  const touchMove = (event: TouchEvent) => {
    if (lastY === null || event.touches.length !== 1) return;
    const currentY = event.touches[0].clientY;
    if (!scrolling && startY !== null && Math.abs(currentY - startY) < 6) return;
    scrolling = true;
    remainder += lastY - currentY;
    lastY = currentY;
    const lineHeight = Math.max(12, element.clientHeight / Math.max(1, instance.rows));
    const lines = Math.trunc(remainder / lineHeight);
    if (lines !== 0) {
      instance.scrollLines(lines);
      remainder -= lines * lineHeight;
    }
    event.preventDefault();
  };
  const resetTouch = () => {
    startY = null;
    lastY = null;
    remainder = 0;
    scrolling = false;
  };
  const touchEnd = () => {
    // xterm's hidden textarea is not focused reliably by a canvas tap in iOS
    // Safari. Focusing while the touch gesture is still active also lets the
    // software keyboard open. Do not steal focus after an intentional scroll.
    if (!scrolling) instance.focus();
    resetTouch();
  };
  const pointerDown = (event: PointerEvent) => {
    if (event.pointerType !== "touch") instance.focus();
  };
  element.addEventListener("pointerdown", pointerDown, { passive: true, capture: true });
  element.addEventListener("touchstart", touchStart, { passive: true, capture: true });
  element.addEventListener("touchmove", touchMove, { passive: false, capture: true });
  element.addEventListener("touchend", touchEnd, { passive: true, capture: true });
  element.addEventListener("touchcancel", resetTouch, { passive: true, capture: true });
  touchCleanup = () => {
    element.removeEventListener("pointerdown", pointerDown, true);
    element.removeEventListener("touchstart", touchStart, true);
    element.removeEventListener("touchmove", touchMove, true);
    element.removeEventListener("touchend", touchEnd, true);
    element.removeEventListener("touchcancel", resetTouch, true);
  };
}

onMounted(() => {
  const element = mountElement.value;
  if (!element) return;
  terminal = new Terminal({
    cursorBlink: true,
    fontFamily: props.fontFamily,
    fontSize: props.fontSize,
    lineHeight: props.lineHeight,
    letterSpacing: props.letterSpacing,
    scrollback: props.scrollback,
    // SearchAddon uses xterm's decoration API to highlight all matches.
    allowProposedApi: true,
    theme: {
      background: props.background,
      foreground: props.foreground,
      cursor: props.cursor,
      selectionBackground: props.selectionBackground,
      selectionInactiveBackground: props.selectionBackground,
      selectionForeground: "#ffffff",
    },
  });
  fitAddon = new FitAddon();
  searchAddon = new SearchAddon({ highlightLimit: props.searchHighlightLimit });
  searchWorker = new Worker(new URL("./terminalSearch.worker.ts", import.meta.url), { type: "module" });
  searchWorker.onmessage = (event: MessageEvent<SearchWorkerResponse>) => {
    const response = event.data;
    if (response.id !== searchCountRequestId) return;
    if (response.type === "result") {
      searchSnapshotReadyId = response.id;
      exactSearchCount = response.count;
      if (exactSearchCount <= 0) currentSearchIndex = -1;
      else if (currentSearchIndex < 0) currentSearchIndex = Math.max(0, addonSearchIndex);
      else currentSearchIndex %= exactSearchCount;
      emitSearchResult();
      if (pendingJumpIndex !== undefined) {
        searchWorker?.postMessage({ type: "locate", id: response.id, index: pendingJumpIndex });
      } else requestNearbyHighlights();
      return;
    }
    if (response.type === "location") {
      pendingJumpIndex = undefined;
      currentSearchIndex = response.index;
      searchAddon?.clearActiveDecoration();
      disposeDecorations(jumpDecorations);
      decorateMatch(response.row, response.offset, response.length, true, jumpDecorations);
      emitSearchResult();
      requestNearbyHighlights();
      return;
    }
    disposeDecorations(nearbyDecorations);
    for (const [index, row, offset, length] of response.locations) {
      if (index !== currentSearchIndex) decorateMatch(row, offset, length, false, nearbyDecorations);
    }
  };
  serializeAddon = new SerializeAddon();
  terminal.loadAddon(fitAddon);
  terminal.loadAddon(new WebLinksAddon());
  terminal.loadAddon(searchAddon);
  terminal.loadAddon(serializeAddon);
  terminal.open(element);
  searchAddon.onDidChangeResults(({ resultIndex, resultCount }) => {
    addonSearchIndex = resultIndex;
    addonSearchCount = resultCount;
    if (resultCount < props.searchHighlightLimit) {
      window.clearTimeout(searchCountTimer);
      searchCountRequestId += 1;
      exactSearchCount = resultCount;
    }
    if (pendingSearchAction === "reset") {
      currentSearchIndex = resultCount > 0 ? Math.max(0, resultIndex) : -1;
    } else if (pendingSearchAction === "next" || pendingSearchAction === "previous") {
      const knownCount = exactSearchCount ?? (resultCount < props.searchHighlightLimit ? resultCount : undefined);
      if (currentSearchIndex < 0) currentSearchIndex = resultIndex >= 0 ? resultIndex : 0;
      else if (knownCount && knownCount > 0) {
        const delta = pendingSearchAction === "previous" ? -1 : 1;
        currentSearchIndex = (currentSearchIndex + delta + knownCount) % knownCount;
      } else if (resultIndex >= 0) currentSearchIndex = resultIndex;
      else currentSearchIndex = Math.max(0, currentSearchIndex + (pendingSearchAction === "previous" ? -1 : 1));
    } else if (resultIndex >= 0) currentSearchIndex = resultIndex;
    emitSearchResult();
    requestNearbyHighlights();
  });
  if (props.enableWebgl) {
    try {
      webglAddon = new WebglAddon();
      webglAddon.onContextLoss(() => {
        webglAddon?.dispose();
        webglAddon = undefined;
        emit("renderer", "canvas");
      });
      terminal.loadAddon(webglAddon);
      emit("renderer", "webgl");
    } catch {
      webglAddon = undefined;
      emit("renderer", "canvas");
    }
  } else emit("renderer", "canvas");
  if (props.restoreBuffer) terminal.write(props.restoreBuffer);
  terminal.onData((data) => emit("data", data));
  terminal.onWriteParsed(() => {
    if (currentSearchTerm) scheduleExactSearchCount(currentSearchTerm, 400);
  });
  setupTouchScrolling(element, terminal);
  resizeObserver = new ResizeObserver(() => {
    window.cancelAnimationFrame(resizeFrame);
    resizeFrame = window.requestAnimationFrame(fit);
  });
  resizeObserver.observe(element);
  fit();
  if (props.autofocus) terminal.focus();
  emit("ready", { terminal, element, handle: terminalHandle });
});

onBeforeUnmount(() => {
  window.cancelAnimationFrame(resizeFrame);
  window.cancelAnimationFrame(fitStabilizeFrame);
  window.cancelAnimationFrame(appearanceFrame);
  window.clearTimeout(searchCountTimer);
  searchCountRequestId += 1;
  searchWorker?.terminate();
  searchWorker = undefined;
  resizeObserver?.disconnect();
  touchCleanup?.();
  webglAddon?.dispose();
  terminal?.dispose();
  terminal = undefined;
});

defineExpose(terminalHandle);
</script>

<style scoped>
.web-terminal__mount { width: 100%; height: 100%; min-width: 0; min-height: 0; }
.web-terminal__mount :deep(.xterm) {
  box-sizing: border-box;
  height: 100%;
  padding: 4px 5px 5px;
  background-color: inherit;
}
.web-terminal__mount :deep(.xterm) { touch-action: pan-y; }
.web-terminal__mount :deep(.xterm-viewport) {
  background-color: inherit !important;
  overflow-y: auto !important;
  overscroll-behavior-y: contain;
  touch-action: pan-y;
  -webkit-overflow-scrolling: touch;
}
</style>
