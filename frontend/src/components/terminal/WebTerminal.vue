<template>
  <div
    class="web-terminal__root"
    :style="{ backgroundColor: effectiveTheme.background, '--terminal-gutter-foreground': effectiveTheme.foreground }"
  >
    <div
      v-if="gutterVisible"
      ref="gutterElement"
      class="web-terminal__gutter"
      :style="{ fontFamily: props.fontFamily, fontSize: `${props.fontSize}px` }"
      :aria-hidden="props.showLineNumbers ? undefined : 'true'"
      :aria-label="props.showLineNumbers ? '终端行号' : undefined"
    >
      <div class="web-terminal__gutter-rows" :style="{ transform: `translateY(${gutterOffsetTop}px)` }">
        <div
          v-for="row in gutterRows"
          :key="row.key"
          class="web-terminal__gutter-row"
          :class="{
            'web-terminal__gutter-row--wrapped': row.wrapped,
            'web-terminal__gutter-row--selected': isGutterLineSelected(row.number),
          }"
          :style="{ height: `${gutterRowHeight}px`, lineHeight: `${gutterRowHeight}px` }"
        >
          <button
            v-if="props.showLineNumbers && row.visible"
            class="web-terminal__line-number"
            type="button"
            :aria-label="`选择第 ${row.number} 行`"
            :title="`选择第 ${row.number} 行；拖动或 Shift+单击选择多行`"
            @pointerdown="beginGutterLineSelection($event, row.number)"
            @keydown.enter.prevent="selectGutterLineFromKeyboard($event, row.number)"
            @keydown.space.prevent="selectGutterLineFromKeyboard($event, row.number)"
          >{{ row.number }}</button>
          <span v-else-if="props.showLineNumbers" class="web-terminal__line-number" />
          <span v-if="props.showLineTimestamps" class="web-terminal__line-time">{{ row.visible ? row.timestamp : "" }}</span>
        </div>
      </div>
    </div>
    <div ref="mountElement" class="web-terminal__mount" />
    <div
      v-if="gotoLineOpen"
      class="web-terminal__goto-line-backdrop"
      @pointerdown.self="closeGotoLine"
    >
      <form class="web-terminal__goto-line" role="dialog" aria-label="跳转或选择指定行" @submit.prevent="submitGotoLine">
        <label :for="gotoLineInputId">跳转或选择行</label>
        <input
          :id="gotoLineInputId"
          ref="gotoLineInput"
          v-model="gotoLineValue"
          type="text"
          inputmode="text"
          autocomplete="off"
          placeholder="例如 120 或 120-180"
          @input="gotoLineError = ''"
          @keydown.esc.prevent.stop="closeGotoLine"
        />
        <span class="web-terminal__goto-line-range">共 {{ gotoLineMax }} 行</span>
        <span v-if="gotoLineError" class="web-terminal__goto-line-error">{{ gotoLineError }}</span>
        <div class="web-terminal__goto-line-actions">
          <button type="button" @click="closeGotoLine">取消</button>
          <button type="button" @click="selectAndCopyGotoLine">选择并复制</button>
          <button type="submit" class="primary">跳转/选择</button>
        </div>
      </form>
    </div>
    <div
      v-if="mobileSelectionToolbar"
      class="web-terminal__selection-toolbar"
      :style="{ left: `${mobileSelectionToolbar.left}px`, top: `${mobileSelectionToolbar.top}px` }"
      role="toolbar"
      aria-label="终端文本选择"
      @pointerdown.stop
      @touchstart.stop
    >
      <button type="button" @click="copyMobileSelection">{{ mobileCopyLabel }}</button>
      <button type="button" @click="clearMobileSelection">取消</button>
    </div>
  </div>
</template>

<script setup lang="ts">
import "@xterm/xterm/css/xterm.css";
import { FitAddon } from "@xterm/addon-fit";
import { SearchAddon } from "@xterm/addon-search";
import { SerializeAddon } from "@xterm/addon-serialize";
import { WebLinksAddon } from "@xterm/addon-web-links";
import { WebglAddon } from "@xterm/addon-webgl";
import { Terminal, type IDisposable, type IMarker, type ITheme } from "@xterm/xterm";
import { computed, nextTick, onBeforeUnmount, onMounted, ref, useId, watch } from "vue";
import { translateText } from "@/i18n";
import { writeClipboard } from "@/utils/clipboard";
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
  theme?: ITheme;
  searchHighlightLimit?: number;
  autofocus?: boolean;
  enableWebgl?: boolean;
  showLineNumbers?: boolean;
  showLineTimestamps?: boolean;
  copyOnSelect?: boolean;
  visible?: boolean;
}>(), {
  scrollback: 50000,
  fontSize: 13,
  lineHeight: 1,
  letterSpacing: 0,
  fontFamily: '"DejaVu Sans Mono", "Courier New", Courier, monospace',
  restoreBuffer: "",
  background: "#101418",
  foreground: "#d8dee9",
  cursor: "#8fbcbb",
  selectionBackground: "#b34713",
  searchHighlightLimit: 1000,
  autofocus: true,
  enableWebgl: true,
  showLineNumbers: false,
  showLineTimestamps: false,
  copyOnSelect: false,
  visible: true,
});

const emit = defineEmits<{
  ready: [event: WebTerminalReadyEvent];
  data: [data: string];
  resize: [event: WebTerminalResizeEvent];
  renderer: [renderer: TerminalRenderer];
  searchResults: [event: WebTerminalSearchResult];
  userSelectionStart: [];
  copyError: [];
}>();

const mountElement = ref<HTMLElement>();
const gutterElement = ref<HTMLElement>();
const effectiveTheme = computed<ITheme>(() => {
  const theme = props.theme ?? {
    background: props.background,
    foreground: props.foreground,
    cursor: props.cursor,
    cursorAccent: props.background,
    selectionBackground: props.selectionBackground,
    selectionForeground: "#ffffff",
  };
  return {
    ...theme,
    selectionInactiveBackground: theme.selectionInactiveBackground ?? theme.selectionBackground,
  };
});
const gutterVisible = computed(() => props.showLineNumbers || props.showLineTimestamps);
const gutterRows = ref<Array<{
  key: string;
  number: number;
  timestamp: string;
  wrapped: boolean;
  visible: boolean;
}>>([]);
const gutterOffsetTop = ref(0);
const gutterRowHeight = ref(0);
const mobileSelectionToolbar = ref<{ left: number; top: number }>();
const mobileCopyLabel = ref("复制");
const gotoLineOpen = ref(false);
const gotoLineInputId = useId();
const gotoLineValue = ref("");
const gotoLineMax = ref(1);
const gotoLineError = ref("");
const gotoLineInput = ref<HTMLInputElement>();
let terminal: Terminal | undefined;
let fitAddon: FitAddon | undefined;
let searchAddon: SearchAddon | undefined;
let serializeAddon: SerializeAddon | undefined;
let webglAddon: WebglAddon | undefined;
let resizeObserver: ResizeObserver | undefined;
let fitFrame = 0;
let fitStabilizeFrame = 0;
let appearanceFrame = 0;
let gutterFrame = 0;
let touchCleanup: (() => void) | undefined;
let focusBoundaryCleanup: (() => void) | undefined;
let mobileCopyLabelTimer = 0;
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
let protectUserSelection = false;
let userSelectionPointerActive = false;
let userSelectionEventActive = false;
let restoringUserSelection = false;
let searchSelectionRestoreTimer = 0;
let protectedSelection: { startX: number; startY: number; endX: number; endY: number } | undefined;
let lineSelectionMode = false;
let lineSelectionAnchor: number | undefined;
let gutterSelectionAnchor: number | undefined;
let gutterSelectionPointerId: number | undefined;
let gutterSelectionPointerType = "";
const selectedLineStart = ref<number>();
const selectedLineEnd = ref<number>();

// Leave enough time for xterm's second click to turn an empty first-click
// selection into a word selection before restoring the active search match.
const searchSelectionRestoreDelay = 600;

type BufferType = "normal" | "alternate";
interface LineTimestamp {
  marker: IMarker;
  value: string;
}
const lineTimestamps: Record<BufferType, LineTimestamp[]> = { normal: [], alternate: [] };
const timestampOrderDirty: Record<BufferType, boolean> = { normal: false, alternate: false };

type SearchAddonInternals = SearchAddon & {
  _highlightTimeout?: { clear: () => void };
  _updateMatches?: () => void;
};

type SearchWorkerResponse =
  | { type: "result"; id: number; count: number }
  | { type: "location"; id: number; index: number; row: number; offset: number; length: number }
  | { type: "window"; id: number; locations: Array<[index: number, row: number, offset: number, length: number]> };

const darkSearchDecorations = {
  matchBackground: "#3e3507",
  matchBorder: "#f0d86b",
  matchOverviewRuler: "#d6bd4e",
  activeMatchBackground: "#a84412",
  activeMatchBorder: "#ffd3a8",
  activeMatchColorOverviewRuler: "#dd6b2f",
};
const lightSearchDecorations = {
  matchBackground: "#f6dc72",
  matchBorder: "#8a6700",
  matchOverviewRuler: "#a97900",
  activeMatchBackground: "#f0a35d",
  activeMatchBorder: "#8f3d00",
  activeMatchColorOverviewRuler: "#b84d00",
};

function relativeLuminance(color: string | undefined) {
  const match = color?.match(/^#([\da-f]{6})$/i);
  if (!match) return 1;
  const channels = match[1].match(/../g)?.map((value) => {
    const channel = Number.parseInt(value, 16) / 255;
    return channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
  });
  return channels ? 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2] : 1;
}

function searchDecorations() {
  const textColor = effectiveTheme.value.selectionForeground ?? effectiveTheme.value.foreground;
  return relativeLuminance(textColor) < 0.35 ? lightSearchDecorations : darkSearchDecorations;
}

let reportedColumns = 0;
let reportedRows = 0;

// xterm counts wrapped screen rows, so a narrow terminal can otherwise turn a
// configured 50,000-line history into only a few thousand newline-delimited
// output lines. Keep approximately the same text capacity at every width by
// treating the preference as rows at a conventional 120-column terminal.
const scrollbackReferenceColumns = 120;

function scrollbackForColumns(columns: number) {
  const safeColumns = Math.max(1, Math.trunc(columns));
  return Math.ceil(props.scrollback * Math.max(1, scrollbackReferenceColumns / safeColumns));
}

function preserveScrollbackForColumns(columns: number) {
  if (!terminal) return;
  const requiredScrollback = scrollbackForColumns(columns);
  // Never reduce a live buffer: widening a terminal must not discard history
  // that was retained while it was narrow.
  if ((terminal.options.scrollback ?? 0) < requiredScrollback) {
    terminal.options.scrollback = requiredScrollback;
  }
}

function reportSize() {
  if (!terminal) return;
  if (terminal.cols === reportedColumns && terminal.rows === reportedRows) return;
  reportedColumns = terminal.cols;
  reportedRows = terminal.rows;
  emit("resize", { columns: terminal.cols, rows: terminal.rows });
}

function fitNow() {
  if (!terminal || !fitAddon) return;
  const proposedColumns = fitAddon.proposeDimensions()?.cols;
  if (proposedColumns) preserveScrollbackForColumns(proposedColumns);
  fitAddon.fit();
  preserveScrollbackForColumns(terminal.cols);
  reportSize();
  scheduleGutterUpdate();
}

function fit() {
  // Visibility, grid and sidebar changes can all report the same resize. Merge
  // them into one fit cycle so switching a tab does not synchronously reflow
  // xterm several times before the new pane can paint.
  if (fitFrame) return;
  fitFrame = window.requestAnimationFrame(() => {
    fitFrame = 0;
    fitNow();
    // Removing a sibling panel changes the available grid/flex height. A second
    // frame lets xterm apply its new canvas dimensions before refreshing the
    // final row, avoiding a half-clipped line after the command panel is hidden.
    window.cancelAnimationFrame(fitStabilizeFrame);
    fitStabilizeFrame = window.requestAnimationFrame(() => {
      fitNow();
      terminal?.refresh(0, Math.max(0, terminal.rows - 1));
    });
  });
}

function formatLineTimestamp(value = new Date()) {
  const pad = (part: number) => String(part).padStart(2, "0");
  return `${pad(value.getHours())}:${pad(value.getMinutes())}:${pad(value.getSeconds())}`;
}

function addLineTimestamp(instance = terminal) {
  if (!instance || !props.showLineTimestamps) return;
  const type = instance.buffer.active.type as BufferType;
  const entries = lineTimestamps[type];
  while (entries[entries.length - 1]?.marker.isDisposed) entries.pop();
  const previous = entries[entries.length - 1];
  const targetLine = instance.buffer.active.baseY + instance.buffer.active.cursorY;
  if (previous?.marker.line === targetLine) return;
  const marker = instance.registerMarker(0);
  if (previous && !previous.marker.isDisposed && marker.line < previous.marker.line) {
    timestampOrderDirty[type] = true;
  }
  entries.push({ marker, value: formatLineTimestamp() });
}

function clearLineTimestamps() {
  for (const type of ["normal", "alternate"] as const) {
    lineTimestamps[type].forEach(({ marker }) => marker.dispose());
    lineTimestamps[type] = [];
    timestampOrderDirty[type] = false;
  }
}

function activeLineTimestamps(type: BufferType) {
  const entries = lineTimestamps[type];
  while (entries[0]?.marker.isDisposed) entries.shift();
  if (timestampOrderDirty[type]) {
    lineTimestamps[type] = entries
      .filter(({ marker }) => !marker.isDisposed && marker.line >= 0)
      .sort((left, right) => left.marker.line - right.marker.line);
    timestampOrderDirty[type] = false;
  }
  return lineTimestamps[type];
}

function timestampForBufferRow(entries: LineTimestamp[], row: number, wrapped: boolean) {
  let low = 0;
  let high = entries.length - 1;
  let found: LineTimestamp | undefined;
  while (low <= high) {
    const middle = (low + high) >> 1;
    const candidate = entries[middle];
    if (candidate.marker.line <= row) {
      found = candidate;
      low = middle + 1;
    } else high = middle - 1;
  }
  if (!found || (found.marker.line !== row && !wrapped)) return "--:--:--";
  return found.value;
}

function updateGutter() {
  gutterFrame = 0;
  const instance = terminal;
  const element = mountElement.value;
  if (!instance || !element || !gutterVisible.value) {
    gutterRows.value = [];
    return;
  }
  const screen = element.querySelector<HTMLElement>(".xterm-screen");
  const screenBounds = screen?.getBoundingClientRect();
  const elementBounds = element.getBoundingClientRect();
  const measuredHeight = screenBounds?.height ?? 0;
  gutterOffsetTop.value = Math.max(0, (screenBounds?.top ?? elementBounds.top) - elementBounds.top);
  gutterRowHeight.value = measuredHeight > 0
    ? measuredHeight / Math.max(1, instance.rows)
    : props.fontSize * props.lineHeight;

  const buffer = instance.buffer.active;
  const timestamps = props.showLineTimestamps ? activeLineTimestamps(buffer.type as BufferType) : [];
  const lastCursorRow = buffer.baseY + buffer.cursorY;
  const rows = [];
  for (let viewportRow = 0; viewportRow < instance.rows; viewportRow += 1) {
    const bufferRow = buffer.viewportY + viewportRow;
    const line = buffer.getLine(bufferRow);
    const wrapped = line?.isWrapped === true;
    const visible = bufferRow <= lastCursorRow || wrapped || Boolean(line?.translateToString(true));
    rows.push({
      key: `${buffer.type}:${bufferRow}`,
      number: bufferRow + 1,
      timestamp: props.showLineTimestamps ? timestampForBufferRow(timestamps, bufferRow, wrapped) : "",
      wrapped,
      visible,
    });
  }
  gutterRows.value = rows;
}

function scheduleGutterUpdate() {
  if (gutterFrame || !gutterVisible.value) return;
  gutterFrame = window.requestAnimationFrame(updateGutter);
}

function write(data: string | Uint8Array, callback?: () => void) {
  terminal?.write(typeof data === "string" ? translateText(data) : data, callback);
}

function writeln(data: string) {
  terminal?.writeln(translateText(data));
}

function focus() {
  if (document.hasFocus()) terminal?.focus();
}

function lastNumberedLine(instance: Terminal) {
  const buffer = instance.buffer.active;
  const cursorLine = buffer.baseY + buffer.cursorY;
  for (let row = buffer.length - 1; row > cursorLine; row -= 1) {
    const line = buffer.getLine(row);
    if (line?.isWrapped || line?.translateToString(true)) return row + 1;
  }
  return Math.max(1, cursorLine + 1);
}

function isGutterLineSelected(line: number) {
  return lineSelectionMode
    && selectedLineStart.value !== undefined
    && selectedLineEnd.value !== undefined
    && line >= Math.min(selectedLineStart.value, selectedLineEnd.value)
    && line <= Math.max(selectedLineStart.value, selectedLineEnd.value);
}

function updateSelectedLineRange(instance: Terminal) {
  if (!lineSelectionMode) return;
  const range = instance.getSelectionPosition();
  if (!range) {
    selectedLineStart.value = undefined;
    selectedLineEnd.value = undefined;
    return;
  }
  selectedLineStart.value = range.start.y + 1;
  selectedLineEnd.value = range.end.y + 1;
}

function applyLineSelection(startLine: number, endLine: number, scroll = true, begin = true) {
  const instance = terminal;
  if (!instance) return false;
  const max = lastNumberedLine(instance);
  const start = Math.max(1, Math.min(max, Math.trunc(startLine)));
  const end = Math.max(1, Math.min(max, Math.trunc(endLine)));
  const first = Math.min(start, end);
  const last = Math.max(start, end);
  if (begin) beginUserSelection(instance, false);
  lineSelectionMode = true;
  selectedLineStart.value = first;
  selectedLineEnd.value = last;
  userSelectionEventActive = true;
  try {
    instance.selectLines(first - 1, last - 1);
    captureUserSelection(instance);
  } finally {
    userSelectionEventActive = false;
  }
  if (scroll) {
    const target = last - first + 1 > instance.rows
      ? first - 1
      : Math.floor((first + last) / 2) - 1 - Math.floor(instance.rows / 2);
    instance.scrollToLine(Math.max(0, target));
    scheduleGutterUpdate();
  }
  return true;
}

function parseGotoLineValue(instance: Terminal) {
  gotoLineMax.value = lastNumberedLine(instance);
  const match = gotoLineValue.value.match(/^\s*(\d+)\s*(?:[-–—:]\s*(\d+)\s*)?$/u);
  if (!match) {
    gotoLineError.value = "请输入单个行号或范围，例如 120-180";
    return undefined;
  }
  const start = Number(match[1]);
  const end = match[2] ? Number(match[2]) : start;
  if (start < 1 || start > gotoLineMax.value || end < 1 || end > gotoLineMax.value) {
    gotoLineError.value = `请输入 1 到 ${gotoLineMax.value} 之间的行号`;
    return undefined;
  }
  return { start, end, range: match[2] !== undefined };
}

function showMobileSelectionToolbarAt(clientX: number, clientY: number) {
  const element = mountElement.value;
  if (!element) return;
  const bounds = element.getBoundingClientRect();
  const rootBounds = element.parentElement?.getBoundingClientRect() ?? bounds;
  const left = Math.max(58, Math.min(rootBounds.width - 58, clientX - rootBounds.left));
  const pointY = clientY - bounds.top;
  const top = pointY >= 58 ? pointY - 48 : Math.min(bounds.height - 40, pointY + 24);
  mobileSelectionToolbar.value = { left, top: Math.max(8, top) };
}

async function copyCurrentSelection() {
  const selection = terminal?.getSelection() ?? "";
  if (!selection) return undefined;
  return writeClipboard(selection);
}

function gutterLineFromPoint(clientY: number) {
  const instance = terminal;
  const gutter = gutterElement.value;
  if (!instance || !gutter || gutterRowHeight.value <= 0) return undefined;
  const bounds = gutter.getBoundingClientRect();
  const viewportRow = Math.max(0, Math.min(
    instance.rows - 1,
    Math.floor((clientY - bounds.top - gutterOffsetTop.value) / gutterRowHeight.value),
  ));
  return Math.min(lastNumberedLine(instance), instance.buffer.active.viewportY + viewportRow + 1);
}

function finishGutterLineSelection(event: PointerEvent, canceled = false) {
  if (gutterSelectionPointerId === undefined || event.pointerId !== gutterSelectionPointerId) return;
  document.removeEventListener("pointermove", moveGutterLineSelection, true);
  document.removeEventListener("pointerup", finishGutterLineSelection, true);
  document.removeEventListener("pointercancel", cancelGutterLineSelection, true);
  gutterSelectionPointerId = undefined;
  gutterSelectionAnchor = undefined;
  finishUserSelection();
  if (!canceled && gutterSelectionPointerType === "touch" && !props.copyOnSelect) {
    showMobileSelectionToolbarAt(event.clientX, event.clientY);
  }
  gutterSelectionPointerType = "";
  if (!canceled && props.copyOnSelect) {
    void copyCurrentSelection().then((success) => { if (success === false) emit("copyError"); });
  }
  terminal?.focus();
}

function cancelGutterLineSelection(event: PointerEvent) {
  finishGutterLineSelection(event, true);
}

function moveGutterLineSelection(event: PointerEvent) {
  if (gutterSelectionPointerId === undefined || event.pointerId !== gutterSelectionPointerId || gutterSelectionAnchor === undefined) return;
  event.preventDefault();
  const line = gutterLineFromPoint(event.clientY);
  if (line !== undefined) applyLineSelection(gutterSelectionAnchor, line, false, false);
}

function beginGutterLineSelection(event: PointerEvent, line: number) {
  if (event.button !== 0 || gutterSelectionPointerId !== undefined || !terminal) return;
  event.preventDefault();
  event.stopPropagation();
  mobileSelectionToolbar.value = undefined;
  const anchor = event.shiftKey && lineSelectionAnchor !== undefined ? lineSelectionAnchor : line;
  if (!event.shiftKey) lineSelectionAnchor = line;
  gutterSelectionAnchor = anchor;
  gutterSelectionPointerId = event.pointerId;
  gutterSelectionPointerType = event.pointerType;
  applyLineSelection(anchor, line, false);
  document.addEventListener("pointermove", moveGutterLineSelection, { passive: false, capture: true });
  document.addEventListener("pointerup", finishGutterLineSelection, true);
  document.addEventListener("pointercancel", cancelGutterLineSelection, true);
}

function selectGutterLineFromKeyboard(event: KeyboardEvent, line: number) {
  const anchor = event.shiftKey && lineSelectionAnchor !== undefined ? lineSelectionAnchor : line;
  if (!event.shiftKey) lineSelectionAnchor = line;
  applyLineSelection(anchor, line);
  finishUserSelection();
  if (props.copyOnSelect) {
    void copyCurrentSelection().then((success) => { if (success === false) emit("copyError"); });
  }
  terminal?.focus();
}

function openGotoLine() {
  const instance = terminal;
  if (!instance || !props.showLineNumbers) return false;
  gotoLineMax.value = lastNumberedLine(instance);
  gotoLineValue.value = "";
  gotoLineError.value = "";
  gotoLineOpen.value = true;
  void nextTick(() => gotoLineInput.value?.focus());
  return true;
}

function closeGotoLine() {
  gotoLineOpen.value = false;
  gotoLineError.value = "";
  void nextTick(focus);
}

function submitGotoLine() {
  const instance = terminal;
  if (!instance) return;
  const target = parseGotoLineValue(instance);
  if (!target) {
    gotoLineInput.value?.focus();
    return;
  }
  if (target.range) {
    lineSelectionAnchor = target.start;
    applyLineSelection(target.start, target.end);
    finishUserSelection();
  } else {
    instance.scrollToLine(Math.max(0, target.start - 1 - Math.floor(instance.rows / 2)));
    scheduleGutterUpdate();
  }
  gotoLineOpen.value = false;
  gotoLineError.value = "";
  void nextTick(focus);
}

async function selectAndCopyGotoLine() {
  const instance = terminal;
  if (!instance) return;
  const target = parseGotoLineValue(instance);
  if (!target) {
    gotoLineInput.value?.focus();
    return;
  }
  lineSelectionAnchor = target.start;
  applyLineSelection(target.start, target.end);
  finishUserSelection();
  const copied = await copyCurrentSelection();
  if (copied === undefined) {
    gotoLineError.value = "所选行没有可复制的文本";
    gotoLineInput.value?.focus();
    return;
  }
  if (!copied) {
    gotoLineError.value = "复制失败，请检查浏览器剪贴板权限";
    gotoLineInput.value?.focus();
    emit("copyError");
    return;
  }
  gotoLineOpen.value = false;
  gotoLineError.value = "";
  void nextTick(focus);
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
  const decorations = searchDecorations();
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
      backgroundColor: active ? decorations.activeMatchBackground : decorations.matchBackground,
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

function captureUserSelection(instance = terminal) {
  const range = instance?.getSelectionPosition();
  protectedSelection = range
    ? { startX: range.start.x, startY: range.start.y, endX: range.end.x, endY: range.end.y }
    : undefined;
}

function cancelAutomaticSearchRefresh() {
  // SearchAddon 0.16 refreshes matches 200ms after every parsed write. That
  // refresh calls terminal.select(), even though it uses the addon's no-scroll
  // mode, so a continuously writing terminal steals an in-progress or finished
  // mouse selection. The addon does not expose cancellation publicly; clear its
  // disposable timer while a user selection is protected. Explicit search
  // actions call allowSearchSelection() and enable automatic refresh again.
  const addon = searchAddon as SearchAddonInternals | undefined;
  addon?._highlightTimeout?.clear();
}

function guardAutomaticSearchRefresh(addon: SearchAddon) {
  const internals = addon as SearchAddonInternals;
  const updateMatches = internals._updateMatches?.bind(addon);
  if (!updateMatches) return;
  internals._updateMatches = () => {
    if (protectUserSelection) {
      internals._highlightTimeout?.clear();
      return;
    }
    updateMatches();
  };
}

function cancelSearchSelectionRestore() {
  window.clearTimeout(searchSelectionRestoreTimer);
  searchSelectionRestoreTimer = 0;
}

function beginUserSelection(instance: Terminal, resetLineAnchor = true) {
  // A second press must be allowed to complete xterm's double-click word
  // selection before an empty first press can navigate back to the match.
  cancelSearchSelectionRestore();
  protectUserSelection = true;
  userSelectionPointerActive = true;
  pendingJumpIndex = undefined;
  cancelAutomaticSearchRefresh();
  // Remove only the active search marker. Keeping the passive match
  // decorations lets an empty click restore the previous result immediately
  // without rebuilding every highlighted match.
  searchAddon?.clearActiveDecoration();
  disposeDecorations(jumpDecorations);
  disposeDecorations(nearbyDecorations);
  protectedSelection = undefined;
  lineSelectionMode = false;
  if (resetLineAnchor) lineSelectionAnchor = undefined;
  selectedLineStart.value = undefined;
  selectedLineEnd.value = undefined;
  instance.clearSelection();
  emit("userSelectionStart");
}

function finishUserSelection() {
  if (!userSelectionPointerActive) return;
  userSelectionPointerActive = false;
  userSelectionEventActive = false;
  captureUserSelection();
  const hasSelection = protectedSelection
    && (protectedSelection.startX !== protectedSelection.endX || protectedSelection.startY !== protectedSelection.endY);
  if (!hasSelection && currentSearchTerm) {
    const restoreIndex = currentSearchIndex;
    const restoreTerm = currentSearchTerm;
    searchSelectionRestoreTimer = window.setTimeout(() => {
      searchSelectionRestoreTimer = 0;
      if (userSelectionPointerActive || currentSearchTerm !== restoreTerm || terminal?.hasSelection()) return;
      allowSearchSelection();
      if (restoreIndex < 0 || !jumpToSearchIndex(restoreIndex)) {
        search(
          restoreTerm,
          true,
          false,
          currentSearchCaseSensitive,
          currentSearchWholeWord,
          currentSearchRegex,
        );
      }
    }, searchSelectionRestoreDelay);
  }
}

function allowSearchSelection() {
  cancelSearchSelectionRestore();
  protectUserSelection = false;
  userSelectionPointerActive = false;
  userSelectionEventActive = false;
  protectedSelection = undefined;
  lineSelectionMode = false;
  lineSelectionAnchor = undefined;
  selectedLineStart.value = undefined;
  selectedLineEnd.value = undefined;
}

function preserveUserSelection(instance: Terminal) {
  if (!protectUserSelection || restoringUserSelection) return;
  // xterm extends a selection from its own auto-scroll timer when the pointer
  // is held above or below the screen. Those selection changes do not happen
  // inside a mousemove event, so accept every change while the pointer remains
  // down instead of restoring an older range and fighting xterm's scrolling.
  if (userSelectionPointerActive || userSelectionEventActive) {
    captureUserSelection(instance);
    return;
  }
  const current = instance.getSelectionPosition();
  const expected = protectedSelection;
  const unchanged = current && expected
    ? current.start.x === expected.startX
      && current.start.y === expected.startY
      && current.end.x === expected.endX
      && current.end.y === expected.endY
    : !current && !expected;
  if (unchanged) return;
  restoringUserSelection = true;
  if (expected) {
    const length = (expected.endY - expected.startY) * instance.cols + expected.endX - expected.startX;
    instance.select(expected.startX, expected.startY, Math.max(1, length));
  } else instance.clearSelection();
  restoringUserSelection = false;
}

function targetsTerminalScreen(event: PointerEvent | MouseEvent, element: HTMLElement) {
  const screen = element.querySelector<HTMLElement>(".xterm-screen");
  const target = event.target;
  return Boolean(screen && target instanceof Node && screen.contains(target));
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
  if (!searchAddon || !term || userSelectionPointerActive) return false;
  allowSearchSelection();
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
    decorations: searchDecorations(),
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
  if (userSelectionPointerActive || !currentSearchTerm || !Number.isFinite(target) || target < 0 || (count !== undefined && target >= count) || !searchWorker) return false;
  allowSearchSelection();
  pendingJumpIndex = target;
  if (searchSnapshotReadyId === searchCountRequestId) {
    searchWorker.postMessage({ type: "locate", id: searchCountRequestId, index: target });
  } else {
    scheduleExactSearchCount(currentSearchTerm, 0);
  }
  return true;
}

function clearSearch() {
  cancelSearchSelectionRestore();
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

function setAppearance(options: { fontFamily?: string; fontSize?: number; lineHeight?: number; letterSpacing?: number; theme?: ITheme }) {
  if (!terminal) return;
  if (options.fontFamily !== undefined) terminal.options.fontFamily = options.fontFamily;
  if (options.fontSize !== undefined) terminal.options.fontSize = options.fontSize;
  if (options.lineHeight !== undefined) terminal.options.lineHeight = options.lineHeight;
  if (options.letterSpacing !== undefined) terminal.options.letterSpacing = options.letterSpacing;
  if (options.theme !== undefined) terminal.options.theme = {
    ...options.theme,
    selectionInactiveBackground: options.theme.selectionInactiveBackground ?? options.theme.selectionBackground,
  };
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

function getBufferText() {
  const instance = terminal;
  if (!instance) return "";
  // Export the normal shell buffer even when a full-screen program currently
  // owns the alternate buffer. xterm has already parsed colors, cursor moves
  // and other escape sequences, so only readable cell text remains here.
  const buffer = instance.buffer.normal;
  const cursorLine = Math.max(0, buffer.baseY + buffer.cursorY);
  let lastLine = Math.min(buffer.length - 1, cursorLine);
  for (let row = buffer.length - 1; row > lastLine; row -= 1) {
    const line = buffer.getLine(row);
    if (line?.isWrapped || line?.translateToString(true)) {
      lastLine = row;
      break;
    }
  }

  let result = "";
  for (let row = 0; row <= lastLine; row += 1) {
    const line = buffer.getLine(row);
    if (!line) continue;
    result += line.translateToString(true);
    if (row < lastLine && !buffer.getLine(row + 1)?.isWrapped) result += "\n";
  }
  return result.replace(/\n+$/u, "");
}

function clearMobileSelection() {
  window.clearTimeout(mobileCopyLabelTimer);
  mobileCopyLabel.value = "复制";
  mobileSelectionToolbar.value = undefined;
  terminal?.clearSelection();
}

async function copyMobileSelection() {
  const selection = terminal?.getSelection() ?? "";
  if (!selection) {
    clearMobileSelection();
    return;
  }
  const copied = await writeClipboard(selection);
  mobileCopyLabel.value = copied ? "已复制" : "复制失败";
  window.clearTimeout(mobileCopyLabelTimer);
  mobileCopyLabelTimer = window.setTimeout(() => {
    mobileCopyLabel.value = "复制";
  }, 1200);
}

const terminalHandle: WebTerminalHandle = {
  write,
  writeln,
  fit,
  focus,
  search,
  jumpToSearchIndex,
  openGotoLine,
  getBufferText,
  clearSearch,
  setAppearance,
  getTerminal,
  getElement,
};

function setupTouchScrolling(element: HTMLElement, instance: Terminal) {
  const longPressDelay = 480;
  let startY: number | null = null;
  let startX: number | null = null;
  let lastY: number | null = null;
  let lastX: number | null = null;
  let lastMoveAt = 0;
  let remainder = 0;
  let velocityY = 0;
  let scrolling = false;
  let selecting = false;
  let selectionStart = 0;
  let selectionEnd = 0;
  let longPressTimer = 0;
  let momentumFrame = 0;

  const cancelMomentum = () => {
    window.cancelAnimationFrame(momentumFrame);
    momentumFrame = 0;
    velocityY = 0;
  };
  const cancelLongPress = () => {
    window.clearTimeout(longPressTimer);
    longPressTimer = 0;
  };
  const bufferCellFromPoint = (clientX: number, clientY: number) => {
    const screen = element.querySelector<HTMLElement>(".xterm-screen");
    const bounds = screen?.getBoundingClientRect() ?? element.getBoundingClientRect();
    if (bounds.width <= 0 || bounds.height <= 0) return undefined;
    const column = Math.max(0, Math.min(instance.cols - 1, Math.floor((clientX - bounds.left) / bounds.width * instance.cols)));
    const viewportRow = Math.max(0, Math.min(instance.rows - 1, Math.floor((clientY - bounds.top) / bounds.height * instance.rows)));
    return { column, row: instance.buffer.active.viewportY + viewportRow };
  };
  const hasTextAt = (row: number, column: number) => {
    const line = instance.buffer.active.getLine(row);
    let textColumn = column;
    let cell = line?.getCell(textColumn);
    // A double-width CJK/emoji cell is followed by a zero-width continuation
    // cell. Treat both columns as text so a long press selects the whole word.
    while (textColumn > 0 && cell?.getWidth() === 0) {
      textColumn -= 1;
      cell = line?.getCell(textColumn);
    }
    return Boolean(cell?.getChars().trim());
  };
  const wordRangeAt = (clientX: number, clientY: number) => {
    const position = bufferCellFromPoint(clientX, clientY);
    if (!position) return undefined;
    let column = position.column;
    const line = instance.buffer.active.getLine(position.row);
    while (column > 0 && line?.getCell(column)?.getWidth() === 0) column -= 1;
    let firstColumn = column;
    let lastColumn = column;
    if (hasTextAt(position.row, column)) {
      while (firstColumn > 0 && hasTextAt(position.row, firstColumn - 1)) firstColumn -= 1;
      while (lastColumn + 1 < instance.cols && hasTextAt(position.row, lastColumn + 1)) lastColumn += 1;
    }
    const rowOffset = position.row * instance.cols;
    return { start: rowOffset + firstColumn, end: rowOffset + lastColumn };
  };
  const selectRange = (start: number, end: number) => {
    const first = Math.min(start, end);
    const last = Math.max(start, end);
    userSelectionEventActive = true;
    try {
      instance.select(first % instance.cols, Math.floor(first / instance.cols), last - first + 1);
      captureUserSelection(instance);
    } finally {
      userSelectionEventActive = false;
    }
  };
  const beginSelection = (clientX: number, clientY: number) => {
    const range = wordRangeAt(clientX, clientY);
    if (!range) return;
    selecting = true;
    scrolling = false;
    velocityY = 0;
    selectionStart = range.start;
    selectionEnd = range.end;
    selectRange(selectionStart, selectionEnd);
    showMobileSelectionToolbarAt(clientX, clientY);
  };
  const extendSelection = (clientX: number, clientY: number) => {
    const position = bufferCellFromPoint(clientX, clientY);
    if (!position) return;
    const current = position.row * instance.cols + position.column;
    selectRange(current < selectionStart ? selectionEnd : selectionStart, current);
    mobileSelectionToolbar.value = undefined;
  };
  const scrollPixels = (pixels: number) => {
    remainder += pixels;
    const lineHeight = Math.max(12, element.clientHeight / Math.max(1, instance.rows));
    const lines = Math.trunc(remainder / lineHeight);
    if (lines === 0) return true;
    const previousViewportY = instance.buffer.active.viewportY;
    instance.scrollLines(lines);
    remainder -= lines * lineHeight;
    if (instance.buffer.active.viewportY !== previousViewportY) return true;
    // Do not keep animating against the top or bottom of the scrollback buffer.
    remainder = 0;
    return false;
  };
  const startMomentum = () => {
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      remainder = 0;
      velocityY = 0;
      return;
    }
    velocityY = Math.max(-2.4, Math.min(2.4, velocityY));
    if (Math.abs(velocityY) < 0.04) {
      remainder = 0;
      velocityY = 0;
      return;
    }
    let previousFrameAt = performance.now();
    const step = (now: number) => {
      const elapsed = Math.min(34, Math.max(1, now - previousFrameAt));
      previousFrameAt = now;
      const moved = scrollPixels(velocityY * elapsed);
      const speed = Math.max(0, Math.abs(velocityY) - 0.0035 * elapsed);
      velocityY = Math.sign(velocityY) * speed;
      if (!moved || speed < 0.02) {
        momentumFrame = 0;
        remainder = 0;
        velocityY = 0;
        return;
      }
      momentumFrame = window.requestAnimationFrame(step);
    };
    momentumFrame = window.requestAnimationFrame(step);
  };
  const touchStart = (event: TouchEvent) => {
    cancelMomentum();
    cancelLongPress();
    if (mobileSelectionToolbar.value) clearMobileSelection();
    const touch = event.touches.length === 1 ? event.touches[0] : undefined;
    startX = touch?.clientX ?? null;
    startY = touch?.clientY ?? null;
    lastX = startX;
    lastY = startY;
    lastMoveAt = performance.now();
    remainder = 0;
    scrolling = false;
    selecting = false;
    if (touch && window.matchMedia("(max-width: 760px)").matches) {
      const touchX = touch.clientX;
      const touchY = touch.clientY;
      longPressTimer = window.setTimeout(() => beginSelection(touchX, touchY), longPressDelay);
    }
  };
  const touchMove = (event: TouchEvent) => {
    if (lastY === null || lastX === null || event.touches.length !== 1) return;
    const currentX = event.touches[0].clientX;
    const currentY = event.touches[0].clientY;
    if (selecting) {
      lastX = currentX;
      lastY = currentY;
      extendSelection(currentX, currentY);
      event.preventDefault();
      return;
    }
    const now = performance.now();
    const deltaY = lastY - currentY;
    const elapsed = Math.min(64, Math.max(1, now - lastMoveAt));
    lastX = currentX;
    lastY = currentY;
    lastMoveAt = now;
    if (!scrolling) {
      const distance = startX === null || startY === null ? 0 : Math.hypot(currentX - startX, currentY - startY);
      if (distance < 6) return;
      cancelLongPress();
      scrolling = true;
    }
    const sampledVelocity = deltaY / elapsed;
    velocityY = velocityY !== 0 && Math.sign(sampledVelocity) === Math.sign(velocityY)
      ? velocityY * 0.65 + sampledVelocity * 0.35
      : sampledVelocity;
    scrollPixels(deltaY);
    event.preventDefault();
  };
  const resetTouch = () => {
    cancelMomentum();
    cancelLongPress();
    if (selecting) clearMobileSelection();
    startX = null;
    startY = null;
    lastX = null;
    lastY = null;
    lastMoveAt = 0;
    remainder = 0;
    scrolling = false;
    selecting = false;
  };
  const touchEnd = () => {
    cancelLongPress();
    // xterm's hidden textarea is not focused reliably by a canvas tap in iOS
    // Safari. Focusing while the touch gesture is still active also lets the
    // software keyboard open. Do not steal focus after an intentional scroll.
    if (selecting) {
      if (lastX !== null && lastY !== null) showMobileSelectionToolbarAt(lastX, lastY);
      startX = null;
      startY = null;
      lastX = null;
      lastY = null;
      selecting = false;
      return;
    }
    if (!scrolling) {
      instance.focus();
      resetTouch();
      return;
    }
    const continueScrolling = performance.now() - lastMoveAt < 100;
    startX = null;
    startY = null;
    lastX = null;
    lastY = null;
    scrolling = false;
    if (continueScrolling) startMomentum();
    else resetTouch();
  };
  const markUserSelectionEvent = () => {
    userSelectionEventActive = true;
    window.queueMicrotask(() => { userSelectionEventActive = false; });
  };
  const pointerDown = (event: PointerEvent) => {
    // Enter protection at the earliest primary-pointer event. The mousedown
    // handler below is kept as a fallback for browsers that do not emit pointer
    // events for xterm's desktop selection path.
    if (event.button !== 0 || !targetsTerminalScreen(event, element)) return;
    markUserSelectionEvent();
    beginUserSelection(instance);
    if (event.pointerType !== "touch") instance.focus();
  };
  const pointerMove = (event: PointerEvent) => {
    if (event.pointerType === "touch" && userSelectionPointerActive) markUserSelectionEvent();
  };
  const pointerUp = (event: PointerEvent) => {
    if (event.pointerType === "touch") finishUserSelection();
  };
  const pointerCancel = () => finishUserSelection();
  const mouseDown = (event: MouseEvent) => {
    if (event.button !== 0) {
      instance.focus();
      return;
    }
    if (!targetsTerminalScreen(event, element)) return;
    markUserSelectionEvent();
    if (!userSelectionPointerActive) beginUserSelection(instance);
    instance.focus();
  };
  const mouseMove = () => {
    if (userSelectionPointerActive) markUserSelectionEvent();
  };
  const mouseUp = () => {
    if (!userSelectionPointerActive) return;
    markUserSelectionEvent();
    // xterm publishes the completed selection from its own mouseup listener.
    // Keep search navigation suspended until all listeners for that event have
    // run, then store the final user selection.
    window.queueMicrotask(finishUserSelection);
  };
  element.addEventListener("pointerdown", pointerDown, { passive: true, capture: true });
  element.addEventListener("pointermove", pointerMove, { passive: true, capture: true });
  document.addEventListener("pointerup", pointerUp, true);
  document.addEventListener("pointercancel", pointerCancel, true);
  element.addEventListener("mousedown", mouseDown, { passive: true, capture: true });
  document.addEventListener("mousemove", mouseMove, true);
  document.addEventListener("mouseup", mouseUp, true);
  element.addEventListener("touchstart", touchStart, { passive: true, capture: true });
  element.addEventListener("touchmove", touchMove, { passive: false, capture: true });
  element.addEventListener("touchend", touchEnd, { passive: true, capture: true });
  element.addEventListener("touchcancel", resetTouch, { passive: true, capture: true });
  touchCleanup = () => {
    cancelMomentum();
    cancelLongPress();
    element.removeEventListener("pointerdown", pointerDown, true);
    element.removeEventListener("pointermove", pointerMove, true);
    document.removeEventListener("pointerup", pointerUp, true);
    document.removeEventListener("pointercancel", pointerCancel, true);
    element.removeEventListener("mousedown", mouseDown, true);
    document.removeEventListener("mousemove", mouseMove, true);
    document.removeEventListener("mouseup", mouseUp, true);
    element.removeEventListener("touchstart", touchStart, true);
    element.removeEventListener("touchmove", touchMove, true);
    element.removeEventListener("touchend", touchEnd, true);
    element.removeEventListener("touchcancel", resetTouch, true);
  };
}

function setupFocusBoundary(element: HTMLElement, instance: Terminal) {
  const blurWhenPointerLeavesTerminal = (event: PointerEvent) => {
    const target = event.target;
    if (target instanceof Node && !element.contains(target)) instance.blur();
  };
  const blurWhenBrowserLosesFocus = () => instance.blur();
  const blurWhenPageIsHidden = () => {
    if (document.visibilityState !== "visible") instance.blur();
  };
  document.addEventListener("pointerdown", blurWhenPointerLeavesTerminal, true);
  window.addEventListener("blur", blurWhenBrowserLosesFocus);
  document.addEventListener("visibilitychange", blurWhenPageIsHidden);
  focusBoundaryCleanup = () => {
    document.removeEventListener("pointerdown", blurWhenPointerLeavesTerminal, true);
    window.removeEventListener("blur", blurWhenBrowserLosesFocus);
    document.removeEventListener("visibilitychange", blurWhenPageIsHidden);
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
    theme: effectiveTheme.value,
  });
  fitAddon = new FitAddon();
  searchAddon = new SearchAddon({ highlightLimit: props.searchHighlightLimit });
  guardAutomaticSearchRefresh(searchAddon);
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
      if (pendingJumpIndex === undefined || response.index !== pendingJumpIndex) return;
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
  if (props.showLineTimestamps) addLineTimestamp(terminal);
  terminal.onSelectionChange(() => {
    preserveUserSelection(terminal!);
    updateSelectedLineRange(terminal!);
  });
  terminal.onLineFeed(() => {
    addLineTimestamp(terminal);
    scheduleGutterUpdate();
  });
  terminal.onRender(scheduleGutterUpdate);
  terminal.onScroll(scheduleGutterUpdate);
  terminal.onResize(() => {
    // Reflow can dispose markers in the middle of the list and move the
    // remaining markers. Re-sort only on resize, not on every output chunk.
    timestampOrderDirty.normal = true;
    timestampOrderDirty.alternate = true;
    scheduleGutterUpdate();
  });
  terminal.buffer.onBufferChange(() => {
    addLineTimestamp(terminal);
    scheduleGutterUpdate();
  });
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
    if (protectUserSelection) cancelAutomaticSearchRefresh();
    // Output parsing can clear xterm's selection before this callback runs.
    // Keep the range captured at mouseup instead of accepting that cleared
    // state as the new protected selection.
    if (terminal && protectUserSelection && !userSelectionPointerActive) preserveUserSelection(terminal);
    if (currentSearchTerm) scheduleExactSearchCount(currentSearchTerm, 400);
  });
  setupTouchScrolling(element, terminal);
  setupFocusBoundary(element, terminal);
  resizeObserver = new ResizeObserver(fit);
  resizeObserver.observe(element);
  fit();
  if (props.autofocus && document.hasFocus()) terminal.focus();
  emit("ready", { terminal, element, handle: terminalHandle });
});

watch(() => props.showLineTimestamps, async (enabled) => {
  if (terminal) {
    if (enabled) addLineTimestamp(terminal);
    else clearLineTimestamps();
  }
  if (!gutterVisible.value) gutterRows.value = [];
  await nextTick();
  fit();
  scheduleGutterUpdate();
});

watch(effectiveTheme, (theme) => setAppearance({ theme }));

watch(() => props.showLineNumbers, async () => {
  if (!props.showLineNumbers) gotoLineOpen.value = false;
  if (!gutterVisible.value) gutterRows.value = [];
  await nextTick();
  fit();
  scheduleGutterUpdate();
});

watch(() => props.visible, async (visible) => {
  if (!visible) return;
  // Pane switches only change visibility, so the terminal keeps its canvas and
  // dimensions while inactive. When a split layout also changes its bounds,
  // fit synchronously after Vue applies the new style and before the browser's
  // next paint; otherwise xterm briefly exposes the old-sized canvas.
  await nextTick();
  fitNow();
  terminal?.refresh(0, Math.max(0, terminal.rows - 1));
});

onBeforeUnmount(() => {
  window.cancelAnimationFrame(fitFrame);
  window.cancelAnimationFrame(fitStabilizeFrame);
  window.cancelAnimationFrame(appearanceFrame);
  window.cancelAnimationFrame(gutterFrame);
  window.clearTimeout(searchCountTimer);
  cancelSearchSelectionRestore();
  window.clearTimeout(mobileCopyLabelTimer);
  document.removeEventListener("pointermove", moveGutterLineSelection, true);
  document.removeEventListener("pointerup", finishGutterLineSelection, true);
  document.removeEventListener("pointercancel", cancelGutterLineSelection, true);
  searchCountRequestId += 1;
  searchWorker?.terminate();
  searchWorker = undefined;
  resizeObserver?.disconnect();
  touchCleanup?.();
  focusBoundaryCleanup?.();
  webglAddon?.dispose();
  terminal?.dispose();
  terminal = undefined;
});

defineExpose(terminalHandle);
</script>

<style scoped>
.web-terminal__root {
  position: relative;
  display: flex;
  width: 100%;
  height: 100%;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
}
.web-terminal__gutter {
  box-sizing: border-box;
  flex: 0 0 auto;
  height: 100%;
  padding: 0 6px;
  overflow: hidden;
  color: var(--terminal-gutter-foreground, #d8dee9);
  background: rgba(0, 0, 0, 0.12);
  border-right: 1px solid rgba(132, 151, 164, 0.18);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
  user-select: none;
}
.web-terminal__gutter-rows { will-change: transform; }
.web-terminal__gutter-row {
  display: flex;
  align-items: center;
  gap: 1ch;
  overflow: hidden;
}
.web-terminal__gutter-row--wrapped { font-style: italic; }
.web-terminal__gutter-row--selected { background: color-mix(in srgb, var(--terminal-gutter-foreground, #d8dee9) 22%, transparent); }
.web-terminal__line-number {
  box-sizing: content-box;
  width: 6ch;
  height: 100%;
  padding: 0;
  display: inline-block;
  color: inherit;
  background: transparent;
  border: 0;
  font: inherit;
  line-height: inherit;
  text-align: right;
  cursor: default;
  touch-action: none;
}
.web-terminal__line-number:focus-visible { color: #ffffff; outline: 1px solid #8fc8c4; outline-offset: -1px; }
.web-terminal__line-time { display: inline-block; width: 8ch; text-align: left; }
.web-terminal__mount { flex: 1 1 auto; width: 0; height: 100%; min-width: 0; min-height: 0; }
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
.web-terminal__goto-line-backdrop {
  position: absolute;
  z-index: 10;
  inset: 0;
  display: grid;
  place-items: start center;
  padding-top: min(18%, 96px);
  background: rgba(5, 10, 14, 0.32);
}
.web-terminal__goto-line {
  box-sizing: border-box;
  width: min(390px, calc(100% - 28px));
  padding: 16px;
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 8px 12px;
  color: #dce7ec;
  background: rgba(31, 42, 50, 0.98);
  border: 1px solid #526773;
  border-radius: 9px;
  box-shadow: 0 12px 36px rgba(0, 0, 0, 0.48);
}
.web-terminal__goto-line label { grid-column: 1 / -1; font-size: 14px; font-weight: 700; }
.web-terminal__goto-line input {
  box-sizing: border-box;
  min-width: 0;
  height: 34px;
  padding: 0 9px;
  color: #ecf5f7;
  background: #111a20;
  border: 1px solid #607783;
  border-radius: 5px;
  font: 13px/1 monospace;
  outline: none;
}
.web-terminal__goto-line input:focus { border-color: #8fc8c4; box-shadow: 0 0 0 2px rgba(143, 200, 196, 0.2); }
.web-terminal__goto-line-range { align-self: center; color: #9aadb7; font-size: 12px; white-space: nowrap; }
.web-terminal__goto-line-error { grid-column: 1 / -1; color: #ffad9f; font-size: 12px; }
.web-terminal__goto-line-actions { grid-column: 1 / -1; display: flex; justify-content: flex-end; gap: 8px; }
.web-terminal__goto-line button {
  min-width: 58px;
  height: 30px;
  color: #d9e3e7;
  background: #344650;
  border: 1px solid #536873;
  border-radius: 5px;
  cursor: pointer;
}
.web-terminal__goto-line button.primary { color: #102027; background: #91c5c1; border-color: #91c5c1; font-weight: 700; }
.web-terminal__selection-toolbar {
  position: absolute;
  z-index: 8;
  display: flex;
  gap: 1px;
  overflow: hidden;
  transform: translateX(-50%);
  border: 1px solid rgba(255, 255, 255, 0.16);
  border-radius: 9px;
  background: rgba(32, 38, 44, 0.96);
  box-shadow: 0 6px 20px rgba(0, 0, 0, 0.36);
  backdrop-filter: blur(10px);
}
.web-terminal__selection-toolbar button {
  min-width: 58px;
  height: 36px;
  padding: 0 12px;
  border: 0;
  color: #f4f6f8;
  background: transparent;
  font: 500 13px/1 system-ui, sans-serif;
}
.web-terminal__selection-toolbar button + button { border-left: 1px solid rgba(255, 255, 255, 0.12); }
.web-terminal__selection-toolbar button:hover,
.web-terminal__selection-toolbar button:focus { color: #ffffff; background: rgba(255, 255, 255, 0.14); outline: none; }
.web-terminal__selection-toolbar button:focus-visible { box-shadow: inset 0 0 0 2px #9bc7c4; }
.web-terminal__selection-toolbar button:active { color: #ffffff; background: rgba(255, 255, 255, 0.22); }

@media (max-width: 760px) {
  .web-terminal__mount :deep(.xterm),
  .web-terminal__mount :deep(.xterm-viewport),
  .web-terminal__mount :deep(.xterm-screen) {
    touch-action: none;
    -webkit-touch-callout: none;
  }
}
</style>
