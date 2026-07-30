<template>
  <div class="text-compare-app">
    <header class="compare-toolbar">
      <div class="compare-brand">
        <router-link class="compare-home" to="/" aria-label="返回首页">SS</router-link>
        <div>
          <strong>文件对比</strong>
          <small>{{ summaryText }}</small>
        </div>
      </div>

      <div class="compare-actions" aria-label="对比操作">
        <div class="compare-kind-switch" aria-label="对比类型">
          <button type="button" :class="{ active: compareMode === 'file' }" @click="compareMode = 'file'">单文件</button>
          <button type="button" :class="{ active: compareMode === 'folder' }" @click="compareMode = 'folder'">文件夹</button>
        </div>
        <template v-if="compareMode === 'file'">
          <n-button size="small" secondary :disabled="!hasDifferences" @click="goToDifference('previous')">
            <template #icon><n-icon><ChevronUpOutline /></n-icon></template>
            上一个
          </n-button>
          <n-button size="small" secondary :disabled="!hasDifferences" @click="goToDifference('next')">
            <template #icon><n-icon><ChevronDownOutline /></n-icon></template>
            下一个
          </n-button>
          <n-button size="small" secondary @click="swapSides">
            <template #icon><n-icon><SwapHorizontalOutline /></n-icon></template>
            交换
          </n-button>
          <n-button size="small" secondary @click="loadExample">载入示例</n-button>
        </template>
      </div>

      <div v-if="compareMode === 'file' && !binaryMode" class="compare-options">
        <label>
          <span>忽略首尾空白</span>
          <n-switch v-model:value="ignoreTrimWhitespace" size="small" />
        </label>
        <label>
          <span>折叠相同内容</span>
          <n-switch v-model:value="hideUnchangedRegions" size="small" />
        </label>
      </div>
      <div v-else-if="compareMode === 'file'" class="compare-options compare-mode-indicator">
        <span>HEX</span>
        <small>二进制只读模式</small>
      </div>
      <div v-else class="compare-options compare-mode-indicator">
        <span>DIR</span>
        <small>浏览器本地严格比较</small>
      </div>
    </header>

    <div v-show="compareMode === 'file'" class="compare-file-bars">
      <section class="compare-file-bar">
        <div class="compare-side-title">
          <span class="compare-side-badge original">左</span>
          <span class="compare-file-name" :title="leftFileName">{{ leftFileName }}</span>
          <small>{{ formatSideMetric("left") }}</small>
        </div>
        <div class="compare-file-actions">
          <n-button size="tiny" tertiary @click="leftFileInput?.click()">
            <template #icon><n-icon><FolderOpenOutline /></n-icon></template>
            打开
          </n-button>
          <n-button size="tiny" tertiary :disabled="!leftSource.size" @click="downloadSide('left')">
            <template #icon><n-icon><DownloadOutline /></n-icon></template>
            下载
          </n-button>
          <n-button size="tiny" quaternary :disabled="!leftSource.size" @click="clearSide('left')">清空</n-button>
          <input ref="leftFileInput" type="file" @change="loadFile($event, 'left')" />
        </div>
      </section>

      <section class="compare-file-bar">
        <div class="compare-side-title">
          <span class="compare-side-badge modified">右</span>
          <span class="compare-file-name" :title="rightFileName">{{ rightFileName }}</span>
          <small>{{ formatSideMetric("right") }}</small>
        </div>
        <div class="compare-file-actions">
          <n-button size="tiny" tertiary @click="rightFileInput?.click()">
            <template #icon><n-icon><FolderOpenOutline /></n-icon></template>
            打开
          </n-button>
          <n-button size="tiny" tertiary :disabled="!rightSource.size" @click="downloadSide('right')">
            <template #icon><n-icon><DownloadOutline /></n-icon></template>
            下载
          </n-button>
          <n-button size="tiny" quaternary :disabled="!rightSource.size" @click="clearSide('right')">清空</n-button>
          <input ref="rightFileInput" type="file" @change="loadFile($event, 'right')" />
        </div>
      </section>
    </div>

    <main v-show="compareMode === 'file'" class="compare-editor-shell">
      <binary-compare-view
        v-if="binaryMode"
        ref="binaryCompareView"
        :left="leftSource"
        :right="rightSource"
        @stats="binaryStats = $event"
      />
      <div v-show="!binaryMode" ref="editorContainer" class="compare-editor" aria-label="双栏文本差异编辑器" />
      <div v-if="editorLoading && !binaryMode" class="compare-editor-loading">正在载入对比编辑器…</div>
    </main>

    <footer v-show="compareMode === 'file'" class="compare-status-bar">
      <span>{{ binaryMode ? "二进制文件仅在浏览器本地读取" : "左侧和右侧内容均可直接编辑" }}</span>
      <span v-if="binaryMode && binaryStats.scanning">
        正在比较 {{ formatBytes(binaryStats.processedBytes) }} / {{ formatBytes(binaryStats.totalBytes) }}
      </span>
      <span v-else-if="binaryMode && binaryStats.error">比较失败：{{ binaryStats.error }}</span>
      <span v-else-if="binaryMode && binaryStats.differenceBytes">
        {{ binaryStats.blocks.toLocaleString("zh-CN") }} 个差异区段 ·
        {{ binaryStats.differenceBytes.toLocaleString("zh-CN") }} 个差异字节 · 首个差异
        {{ formatBinaryOffset(binaryStats.firstDifference) }}
      </span>
      <span v-else-if="binaryMode">{{ binaryStats.totalBytes ? "二进制内容相同" : "打开文件开始对比" }}</span>
      <span v-else-if="hasDifferences">
        {{ differenceStats.blocks }} 处差异 · 新增 {{ differenceStats.added }} 行 · 删除
        {{ differenceStats.deleted }} 行 · 修改 {{ differenceStats.modified }} 行
      </span>
      <span v-else>{{ leftCharacters || rightCharacters ? "内容相同" : "打开文件或直接输入文本开始对比" }}</span>
    </footer>

    <folder-compare-view v-show="compareMode === 'folder'" @open-file="openFolderEntry" />
  </div>
</template>

<script setup lang="ts">
import {
  ChevronDownOutline,
  ChevronUpOutline,
  DownloadOutline,
  FolderOpenOutline,
  SwapHorizontalOutline,
} from "@vicons/ionicons5";
import EditorWorker from "monaco-editor/esm/vs/editor/editor.worker?worker";
import * as monaco from "monaco-editor/esm/vs/editor/editor.api";
import { NButton, NIcon, NSwitch, useMessage } from "naive-ui";
import { computed, nextTick, onBeforeUnmount, onMounted, ref, shallowRef, watch } from "vue";
import BinaryCompareView from "./BinaryCompareView.vue";
import FolderCompareView from "./FolderCompareView.vue";

window.MonacoEnvironment = {
  getWorker() {
    return new EditorWorker();
  },
};

type CompareSide = "left" | "right";
type DifferenceDirection = "previous" | "next";
type CompareMode = "file" | "folder";

interface DifferenceStats {
  blocks: number;
  added: number;
  deleted: number;
  modified: number;
}

interface BinaryCompareStats {
  scanning: boolean;
  processedBytes: number;
  totalBytes: number;
  differenceBytes: number;
  blocks: number;
  firstDifference: number | null;
  rangesTruncated: boolean;
  error: string;
}

interface BinaryCompareViewApi {
  goToDifference(direction: DifferenceDirection): void;
}

const maximumFileSize = 10 * 1024 * 1024;
const message = useMessage();
const editorContainer = ref<HTMLElement | null>(null);
const leftFileInput = ref<HTMLInputElement | null>(null);
const rightFileInput = ref<HTMLInputElement | null>(null);
const editorLoading = ref(true);
const compareMode = ref<CompareMode>("file");
const leftFileName = ref("左侧内容.txt");
const rightFileName = ref("右侧内容.txt");
const leftCharacters = ref(0);
const rightCharacters = ref(0);
const leftSource = shallowRef<Blob>(new Blob());
const rightSource = shallowRef<Blob>(new Blob());
const leftIsBinary = ref(false);
const rightIsBinary = ref(false);
const ignoreTrimWhitespace = ref(false);
const hideUnchangedRegions = ref(false);
const differenceStats = ref<DifferenceStats>(emptyDifferenceStats());
const binaryStats = ref<BinaryCompareStats>(emptyBinaryStats());
const binaryCompareView = ref<BinaryCompareViewApi | null>(null);

let diffEditor: monaco.editor.IStandaloneDiffEditor | null = null;
let leftModel: monaco.editor.ITextModel | null = null;
let rightModel: monaco.editor.ITextModel | null = null;
const disposables: monaco.IDisposable[] = [];
let suppressLeftModelChange = false;
let suppressRightModelChange = false;

const binaryMode = computed(() => leftIsBinary.value || rightIsBinary.value);
const hasDifferences = computed(() => binaryMode.value ? binaryStats.value.blocks > 0 : differenceStats.value.blocks > 0);
const summaryText = computed(() => {
  if (compareMode.value === "folder") return "选择两个本地文件夹进行严格比较";
  if (binaryMode.value && binaryStats.value.scanning) return "正在比较二进制文件";
  if (binaryMode.value && binaryStats.value.error) return "二进制比较失败";
  if (binaryMode.value && binaryStats.value.differenceBytes) {
    return `${binaryStats.value.differenceBytes.toLocaleString("zh-CN")} 个差异字节`;
  }
  if (binaryMode.value) return binaryStats.value.totalBytes ? "二进制内容相同" : "打开文件开始对比";
  if (!leftSource.value.size && !rightSource.value.size) return "打开文件或直接输入文本";
  if (!hasDifferences.value) return "两侧内容相同";
  return `${differenceStats.value.blocks} 处差异`;
});

watch([ignoreTrimWhitespace, hideUnchangedRegions], updateDiffOptions);
watch(binaryMode, async (enabled) => {
  if (enabled) return;
  await nextTick();
  diffEditor?.layout();
});

onMounted(async () => {
  await nextTick();
  if (!editorContainer.value) return;

  leftModel = monaco.editor.createModel("", "plaintext", monaco.Uri.parse("inmemory://space-station/compare/left.txt"));
  rightModel = monaco.editor.createModel("", "plaintext", monaco.Uri.parse("inmemory://space-station/compare/right.txt"));
  diffEditor = monaco.editor.createDiffEditor(editorContainer.value, {
    automaticLayout: true,
    diffAlgorithm: "advanced",
    fontFamily: '"SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace',
    fontSize: 13,
    ignoreTrimWhitespace: ignoreTrimWhitespace.value,
    maxComputationTime: 5000,
    maxFileSize: 10,
    minimap: { enabled: false },
    originalEditable: true,
    renderGutterMenu: true,
    renderMarginRevertIcon: true,
    renderSideBySide: true,
    scrollBeyondLastLine: false,
    smoothScrolling: true,
    useInlineViewWhenSpaceIsLimited: true,
    wordWrap: "off",
  });
  diffEditor.setModel({ original: leftModel, modified: rightModel });

  disposables.push(
    diffEditor.onDidUpdateDiff(updateDifferenceStats),
    leftModel.onDidChangeContent(() => {
      leftCharacters.value = leftModel?.getValueLength() ?? 0;
      if (!suppressLeftModelChange) {
        leftSource.value = new Blob([leftModel?.getValue() ?? ""], { type: "text/plain;charset=utf-8" });
        leftIsBinary.value = false;
      }
    }),
    rightModel.onDidChangeContent(() => {
      rightCharacters.value = rightModel?.getValueLength() ?? 0;
      if (!suppressRightModelChange) {
        rightSource.value = new Blob([rightModel?.getValue() ?? ""], { type: "text/plain;charset=utf-8" });
        rightIsBinary.value = false;
      }
    }),
  );
  editorLoading.value = false;
});

onBeforeUnmount(() => {
  disposables.splice(0).forEach((disposable) => disposable.dispose());
  diffEditor?.setModel(null);
  diffEditor?.dispose();
  leftModel?.dispose();
  rightModel?.dispose();
  diffEditor = null;
  leftModel = null;
  rightModel = null;
});

function updateDiffOptions() {
  diffEditor?.updateOptions({
    ignoreTrimWhitespace: ignoreTrimWhitespace.value,
    hideUnchangedRegions: {
      enabled: hideUnchangedRegions.value,
      contextLineCount: 3,
      minimumLineCount: 8,
      revealLineCount: 10,
    },
  });
}

function updateDifferenceStats() {
  const changes = diffEditor?.getLineChanges() ?? [];
  const nextStats = emptyDifferenceStats();
  nextStats.blocks = changes.length;

  for (const change of changes) {
    const originalLines = rangeLineCount(change.originalStartLineNumber, change.originalEndLineNumber);
    const modifiedLines = rangeLineCount(change.modifiedStartLineNumber, change.modifiedEndLineNumber);
    const sharedLines = Math.min(originalLines, modifiedLines);
    nextStats.modified += sharedLines;
    nextStats.deleted += originalLines - sharedLines;
    nextStats.added += modifiedLines - sharedLines;
  }
  differenceStats.value = nextStats;
}

function rangeLineCount(start: number, end: number) {
  return end === 0 ? 0 : Math.max(0, end - start + 1);
}

function emptyDifferenceStats(): DifferenceStats {
  return { blocks: 0, added: 0, deleted: 0, modified: 0 };
}

function emptyBinaryStats(): BinaryCompareStats {
  return {
    scanning: false,
    processedBytes: 0,
    totalBytes: 0,
    differenceBytes: 0,
    blocks: 0,
    firstDifference: null,
    rangesTruncated: false,
    error: "",
  };
}

function goToDifference(direction: DifferenceDirection) {
  if (binaryMode.value) {
    binaryCompareView.value?.goToDifference(direction);
    return;
  }
  diffEditor?.goToDiff(direction);
  diffEditor?.focus();
}

function swapSides() {
  if (!leftModel || !rightModel) return;
  const leftValue = leftModel.getValue();
  const rightValue = rightModel.getValue();
  const previousLeftSource = leftSource.value;
  const previousLeftBinary = leftIsBinary.value;
  const previousLeftName = leftFileName.value;

  suppressLeftModelChange = true;
  suppressRightModelChange = true;
  leftModel.setValue(rightValue);
  rightModel.setValue(leftValue);
  suppressLeftModelChange = false;
  suppressRightModelChange = false;
  leftSource.value = rightSource.value;
  rightSource.value = previousLeftSource;
  leftIsBinary.value = rightIsBinary.value;
  rightIsBinary.value = previousLeftBinary;
  leftFileName.value = rightFileName.value;
  rightFileName.value = previousLeftName;
}

async function loadFile(event: Event, side: CompareSide) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  input.value = "";
  if (!file) return;

  await loadSelectedFile(file, side, true);
}

async function loadSelectedFile(file: File, side: CompareSide, announceBinary: boolean) {
  try {
    const binary = await isProbablyBinary(file);
    if (binary) {
      setBinarySide(side, file);
      if (announceBinary) message.info(`已以二进制模式打开“${file.name}”。`);
      return;
    }
    if (file.size > maximumFileSize) {
      message.error(`文本文件超过 ${formatBytes(maximumFileSize)} 限制。`);
      return;
    }
    const content = await readTextFile(file);
    setSideContent(side, content, file.name, file);
  } catch (error) {
    message.error(error instanceof Error ? error.message : "读取文件失败。");
  }
}

async function openFolderEntry(payload: { left?: File; right?: File }) {
  compareMode.value = "file";
  clearSide("left");
  clearSide("right");
  await nextTick();
  if (payload.left) await loadSelectedFile(payload.left, "left", false);
  if (payload.right) await loadSelectedFile(payload.right, "right", false);
}

function setSideContent(side: CompareSide, content: string, fileName: string, source?: Blob) {
  const model = side === "left" ? leftModel : rightModel;
  if (!model) return;
  if (side === "left") suppressLeftModelChange = true;
  else suppressRightModelChange = true;
  model.setValue(content);
  if (side === "left") {
    suppressLeftModelChange = false;
    leftSource.value = source ?? new Blob([content], { type: "text/plain;charset=utf-8" });
    leftIsBinary.value = false;
    leftFileName.value = fileName;
  } else {
    suppressRightModelChange = false;
    rightSource.value = source ?? new Blob([content], { type: "text/plain;charset=utf-8" });
    rightIsBinary.value = false;
    rightFileName.value = fileName;
  }
}

function setBinarySide(side: CompareSide, file: File) {
  const model = side === "left" ? leftModel : rightModel;
  if (!model) return;
  if (side === "left") {
    suppressLeftModelChange = true;
    model.setValue("");
    suppressLeftModelChange = false;
    leftSource.value = file;
    leftIsBinary.value = true;
    leftFileName.value = file.name;
  } else {
    suppressRightModelChange = true;
    model.setValue("");
    suppressRightModelChange = false;
    rightSource.value = file;
    rightIsBinary.value = true;
    rightFileName.value = file.name;
  }
}

function clearSide(side: CompareSide) {
  setSideContent(side, "", side === "left" ? "左侧内容.txt" : "右侧内容.txt");
}

function loadExample() {
  setSideContent(
    "left",
    [
      "interface StationConfig {",
      "  name: string;",
      "  enabled: boolean;",
      "}",
      "",
      "const station: StationConfig = {",
      '  name: "Space Station",',
      "  enabled: true,",
      "};",
    ].join("\n"),
    "station-before.ts",
  );
  setSideContent(
    "right",
    [
      "interface StationConfig {",
      "  name: string;",
      "  enabled: boolean;",
      "  port: number;",
      "}",
      "",
      "const station: StationConfig = {",
      '  name: "Space Station",',
      "  enabled: false,",
      "  port: 443,",
      "};",
    ].join("\n"),
    "station-after.ts",
  );
  void diffEditor?.revealFirstDiff();
}

function downloadSide(side: CompareSide) {
  const fileName = side === "left" ? leftFileName.value : rightFileName.value;
  const source = side === "left" ? leftSource.value : rightSource.value;
  if (!source.size) return;

  const url = URL.createObjectURL(source);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = fileName || `${side}-compare.txt`;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

async function isProbablyBinary(file: File) {
  const sample = new Uint8Array(await file.slice(0, 8192).arrayBuffer());
  if (!sample.length || hasUnicodeBom(sample)) return false;

  let controlCharacters = 0;
  for (const byte of sample) {
    if (byte === 0) return true;
    if (byte < 7 || (byte > 13 && byte < 32) || byte === 127) controlCharacters += 1;
  }
  if (controlCharacters / sample.length > 0.08) return true;

  for (let trimmedBytes = 0; trimmedBytes <= 3 && trimmedBytes < sample.length; trimmedBytes += 1) {
    try {
      new TextDecoder("utf-8", { fatal: true }).decode(sample.subarray(0, sample.length - trimmedBytes));
      return false;
    } catch {
      // The sample can end in the middle of a multibyte UTF-8 character.
    }
  }
  return true;
}

function hasUnicodeBom(bytes: Uint8Array) {
  return (bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) ||
    (bytes[0] === 0xff && bytes[1] === 0xfe) ||
    (bytes[0] === 0xfe && bytes[1] === 0xff);
}

async function readTextFile(file: File) {
  const bytes = new Uint8Array(await file.arrayBuffer());
  let content: string;
  if (bytes[0] === 0xff && bytes[1] === 0xfe) {
    content = new TextDecoder("utf-16le").decode(bytes.subarray(2));
  } else if (bytes[0] === 0xfe && bytes[1] === 0xff) {
    content = new TextDecoder("utf-16be").decode(bytes.subarray(2));
  } else {
    content = new TextDecoder("utf-8").decode(bytes);
  }
  return content.charCodeAt(0) === 0xfeff ? content.slice(1) : content;
}

function formatSideMetric(side: CompareSide) {
  if (binaryMode.value || (side === "left" ? leftIsBinary.value : rightIsBinary.value)) {
    return formatBytes(side === "left" ? leftSource.value.size : rightSource.value.size);
  }
  return formatCharacters(side === "left" ? leftCharacters.value : rightCharacters.value);
}

function formatBinaryOffset(offset: number | null) {
  if (offset === null) return "—";
  return `0x${offset.toString(16).toUpperCase()}`;
}

function formatCharacters(count: number) {
  return `${count.toLocaleString("zh-CN")} 字符`;
}

function formatBytes(bytes: number) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GiB`;
}
</script>

<style scoped>
.text-compare-app {
  width: 100%;
  height: 100dvh;
  min-width: 0;
  display: grid;
  grid-template-rows: auto auto minmax(0, 1fr) auto;
  overflow: hidden;
  background: #f3f6f8;
  color: #1e2b35;
}

.compare-toolbar {
  min-height: 58px;
  padding: 9px 14px;
  display: grid;
  grid-template-columns: minmax(190px, 1fr) auto minmax(260px, 1fr);
  align-items: center;
  gap: 12px;
  border-bottom: 1px solid #ced7de;
  background: #ffffff;
}

.compare-brand,
.compare-actions,
.compare-options,
.compare-side-title,
.compare-file-actions,
.compare-status-bar {
  display: flex;
  align-items: center;
}

.compare-brand {
  min-width: 0;
  gap: 10px;
}

.compare-home {
  width: 34px;
  height: 34px;
  display: grid;
  place-items: center;
  flex: 0 0 auto;
  border-radius: 7px;
  background: #153243;
  color: #f7fbff;
  font-size: 12px;
  font-weight: 800;
  text-decoration: none;
}

.compare-brand > div {
  min-width: 0;
  display: grid;
  gap: 1px;
}

.compare-brand strong {
  font-size: 15px;
}

.compare-brand small,
.compare-side-title small {
  color: #6a7883;
  font-size: 11px;
}

.compare-actions {
  justify-content: center;
  gap: 6px;
}

.compare-kind-switch {
  padding: 2px;
  display: inline-flex;
  flex: 0 0 auto;
  gap: 2px;
  border: 1px solid #d3dce2;
  border-radius: 7px;
  background: #edf1f4;
}

.compare-kind-switch button {
  min-height: 26px;
  padding: 3px 10px;
  border: 0;
  border-radius: 5px;
  background: transparent;
  color: #5b6b76;
  font-size: 12px;
  cursor: pointer;
}

.compare-kind-switch button.active {
  background: #153243;
  color: #f7fbff;
}

.compare-options {
  justify-content: flex-end;
  gap: 14px;
}

.compare-options label {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  color: #445662;
  font-size: 12px;
  white-space: nowrap;
}

.compare-file-bars {
  display: grid;
  grid-template-columns: 1fr 1fr;
  border-bottom: 1px solid #ced7de;
  background: #e9eef2;
}

.compare-file-bar {
  min-width: 0;
  min-height: 42px;
  padding: 6px 10px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.compare-file-bar + .compare-file-bar {
  border-left: 1px solid #ced7de;
}

.compare-side-title {
  min-width: 0;
  gap: 8px;
}

.compare-side-badge {
  width: 24px;
  height: 24px;
  display: grid;
  place-items: center;
  flex: 0 0 auto;
  border-radius: 5px;
  font-size: 11px;
  font-weight: 800;
}

.compare-side-badge.original {
  background: #f8d7da;
  color: #842029;
}

.compare-side-badge.modified {
  background: #d1e7dd;
  color: #0f5132;
}

.compare-file-name {
  min-width: 0;
  max-width: 260px;
  overflow: hidden;
  color: #243641;
  font-size: 12px;
  font-weight: 700;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.compare-file-actions {
  flex: 0 0 auto;
  gap: 3px;
}

.compare-file-actions input {
  display: none;
}

.compare-editor-shell {
  min-width: 0;
  min-height: 0;
  position: relative;
  overflow: hidden;
  background: #ffffff;
}

.compare-editor {
  width: 100%;
  height: 100%;
}

.compare-editor-loading {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  background: #ffffff;
  color: #667681;
  font-size: 13px;
}

.compare-status-bar {
  min-height: 28px;
  padding: 4px 12px;
  justify-content: space-between;
  gap: 14px;
  border-top: 1px solid #ced7de;
  background: #153243;
  color: #e7f0f5;
  font-size: 11px;
}

@media (max-width: 920px) {
  .compare-toolbar {
    grid-template-columns: 1fr auto;
  }

  .compare-actions {
    grid-column: 1 / -1;
    grid-row: 2;
    justify-content: flex-start;
    overflow-x: auto;
  }

  .compare-options {
    justify-content: flex-end;
  }

  .compare-file-bar {
    align-items: flex-start;
    flex-direction: column;
  }

  .compare-file-actions {
    width: 100%;
  }
}

@media (max-width: 620px) {
  .compare-toolbar {
    grid-template-columns: 1fr;
  }

  .compare-actions,
  .compare-options {
    grid-column: 1;
    grid-row: auto;
    justify-content: flex-start;
  }

  .compare-options {
    overflow-x: auto;
  }

  .compare-file-bars {
    grid-template-columns: 1fr 1fr;
  }

  .compare-file-name {
    max-width: 100px;
  }

  .compare-file-actions :deep(.n-button__content) {
    font-size: 0;
  }

  .compare-file-actions :deep(.n-button__icon) {
    margin: 0;
  }

  .compare-status-bar span:first-child {
    display: none;
  }
}
</style>
