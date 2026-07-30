<template>
  <div class="folder-compare-view">
    <section class="folder-pickers">
      <div class="folder-picker folder-picker--left">
        <span class="folder-side-badge">左</span>
        <div class="folder-picker-copy">
          <strong>{{ leftRoot.name || "选择左侧文件夹" }}</strong>
          <small v-if="leftRoot.files">{{ leftRoot.files.toLocaleString("zh-CN") }} 个文件 · {{ formatBytes(leftRoot.bytes) }}</small>
          <small v-else>文件只在当前浏览器中读取</small>
        </div>
        <n-button size="small" secondary @click="leftDirectoryInput?.click()">选择文件夹</n-button>
        <input ref="leftDirectoryInput" type="file" multiple webkitdirectory @change="selectDirectory($event, 'left')" />
      </div>

      <div class="folder-picker folder-picker--right">
        <span class="folder-side-badge">右</span>
        <div class="folder-picker-copy">
          <strong>{{ rightRoot.name || "选择右侧文件夹" }}</strong>
          <small v-if="rightRoot.files">{{ rightRoot.files.toLocaleString("zh-CN") }} 个文件 · {{ formatBytes(rightRoot.bytes) }}</small>
          <small v-else>文件只在当前浏览器中读取</small>
        </div>
        <n-button size="small" secondary @click="rightDirectoryInput?.click()">选择文件夹</n-button>
        <input ref="rightDirectoryInput" type="file" multiple webkitdirectory @change="selectDirectory($event, 'right')" />
      </div>
    </section>

    <section class="folder-filter-bar">
      <n-input v-model:value="keyword" class="folder-keyword" clearable size="small" placeholder="筛选相对路径" />
      <n-select v-model:value="statusFilter" class="folder-status-filter" size="small" :options="statusFilterOptions" />
      <n-button size="small" secondary :disabled="!leftFiles.size && !rightFiles.size" @click="buildComparison">重新比较</n-button>
      <div class="folder-summary">
        <span class="summary-different">不同 {{ summary.different }}</span>
        <span>仅左 {{ summary.leftOnly }}</span>
        <span>仅右 {{ summary.rightOnly }}</span>
        <span class="summary-same">相同 {{ summary.same }}</span>
      </div>
    </section>

    <div v-if="comparison.scanning" class="folder-progress">
      <n-progress type="line" :percentage="comparisonPercentage" :show-indicator="false" processing />
      <span>
        正在比较 {{ comparison.currentPath || "文件内容" }} ·
        {{ comparison.processedFiles }}/{{ comparison.totalFiles }}
      </span>
    </div>
    <div v-else-if="selectionMessage" class="folder-message">{{ selectionMessage }}</div>
    <div v-else class="folder-message folder-message--empty" aria-hidden="true" />

    <section class="folder-results">
      <div class="folder-result-header">
        <span>相对路径</span>
        <span>左侧</span>
        <span>状态</span>
        <span>右侧</span>
        <span>操作</span>
      </div>

      <div v-if="filteredEntries.length" ref="resultViewport" class="folder-result-viewport" @scroll="handleResultScroll">
        <div class="folder-result-spacer" :style="{ height: `${resultSpacerHeight}px` }">
          <div
            v-for="row in visibleEntries"
            :key="row.entry.path"
            class="folder-result-row"
            :class="`folder-result-row--${row.entry.status}`"
            :style="{ transform: `translateY(${row.top}px)` }"
            @dblclick="openEntry(row.entry)"
          >
            <div class="folder-path-cell" :title="row.entry.path">
              <strong>{{ row.entry.name }}</strong>
              <small>{{ row.entry.directory || "根目录" }}</small>
            </div>
            <div class="folder-file-cell">
              <template v-if="row.entry.left">
                <span>{{ formatBytes(row.entry.left.size) }}</span>
                <small>{{ formatModifiedTime(row.entry.left.lastModified) }}</small>
              </template>
              <span v-else>—</span>
            </div>
            <div class="folder-status-cell">
              <n-tag size="small" :type="statusTagType(row.entry.status)" :bordered="false">
                {{ statusText(row.entry.status) }}
              </n-tag>
              <small v-if="row.entry.firstDifference !== null">首差 0x{{ row.entry.firstDifference.toString(16).toUpperCase() }}</small>
              <small v-if="row.entry.error" :title="row.entry.error">{{ row.entry.error }}</small>
            </div>
            <div class="folder-file-cell">
              <template v-if="row.entry.right">
                <span>{{ formatBytes(row.entry.right.size) }}</span>
                <small>{{ formatModifiedTime(row.entry.right.lastModified) }}</small>
              </template>
              <span v-else>—</span>
            </div>
            <div class="folder-action-cell">
              <n-button size="tiny" tertiary :disabled="row.entry.status === 'pending'" @click="openEntry(row.entry)">
                打开对比
              </n-button>
            </div>
          </div>
        </div>
      </div>

      <n-empty
        v-else
        class="folder-empty"
        :description="leftRoot.files || rightRoot.files ? '没有符合筛选条件的文件' : '选择左右文件夹开始比较'"
      />
    </section>

    <footer class="folder-footer">
      <span>严格模式：同名同大小文件会逐字节比较</span>
      <span>{{ entries.length.toLocaleString("zh-CN") }} 个文件 · 空文件夹不会由浏览器目录选择器返回</span>
    </footer>
  </div>
</template>

<script setup lang="ts">
import FolderCompareWorker from "./folderCompare.worker?worker";
import { NButton, NEmpty, NInput, NProgress, NSelect, NTag } from "naive-ui";
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, shallowRef, watch } from "vue";

type CompareSide = "left" | "right";
type EntryStatus = "pending" | "same" | "different" | "leftOnly" | "rightOnly" | "error";

interface FolderEntry {
  path: string;
  name: string;
  directory: string;
  left?: File;
  right?: File;
  status: EntryStatus;
  firstDifference: number | null;
  error: string;
}

interface DirectorySummary {
  name: string;
  files: number;
  bytes: number;
}

interface WorkerProgressMessage {
  type: "progress";
  id: number;
  path: string;
  processedFiles: number;
  totalFiles: number;
  processedBytes: number;
  totalBytes: number;
}

interface WorkerResultMessage {
  type: "result";
  id: number;
  path: string;
  equal: boolean;
  firstDifference: number | null;
  error?: string;
  processedFiles: number;
  totalFiles: number;
  processedBytes: number;
  totalBytes: number;
}

interface WorkerCompleteMessage {
  type: "complete";
  id: number;
  totalFiles: number;
  totalBytes: number;
}

type WorkerMessage = WorkerProgressMessage | WorkerResultMessage | WorkerCompleteMessage;

const emit = defineEmits<{
  openFile: [payload: { left?: File; right?: File }];
}>();

const rowHeight = 52;
const overscanRows = 6;
const maximumSpacerHeight = 8_000_000;
const leftDirectoryInput = ref<HTMLInputElement | null>(null);
const rightDirectoryInput = ref<HTMLInputElement | null>(null);
const resultViewport = ref<HTMLElement | null>(null);
const leftFiles = shallowRef(new Map<string, File>());
const rightFiles = shallowRef(new Map<string, File>());
const leftRoot = reactive<DirectorySummary>({ name: "", files: 0, bytes: 0 });
const rightRoot = reactive<DirectorySummary>({ name: "", files: 0, bytes: 0 });
const entries = ref<FolderEntry[]>([]);
const keyword = ref("");
const statusFilter = ref("differences");
const selectionMessage = ref("");
const resultScrollTop = ref(0);
const resultViewportHeight = ref(480);
const comparison = reactive({
  scanning: false,
  currentPath: "",
  processedFiles: 0,
  totalFiles: 0,
  processedBytes: 0,
  totalBytes: 0,
});
const statusFilterOptions = [
  { label: "仅显示差异", value: "differences" },
  { label: "全部文件", value: "all" },
  { label: "内容相同", value: "same" },
  { label: "内容不同", value: "different" },
  { label: "仅左侧", value: "leftOnly" },
  { label: "仅右侧", value: "rightOnly" },
  { label: "读取失败", value: "error" },
];
let worker: Worker | null = null;
let comparisonId = 0;
let resizeObserver: ResizeObserver | null = null;
let scrollFrame = 0;
let entryIndexes = new Map<string, number>();

const summary = computed(() => {
  const result = { same: 0, different: 0, leftOnly: 0, rightOnly: 0, error: 0, pending: 0 };
  for (const entry of entries.value) result[entry.status] += 1;
  return result;
});
const filteredEntries = computed(() => {
  const normalizedKeyword = keyword.value.trim().toLocaleLowerCase();
  return entries.value.filter((entry) => {
    if (normalizedKeyword && !entry.path.toLocaleLowerCase().includes(normalizedKeyword)) return false;
    if (statusFilter.value === "all") return true;
    if (statusFilter.value === "differences") return entry.status !== "same" && entry.status !== "pending";
    return entry.status === statusFilter.value;
  });
});
const resultNaturalHeight = computed(() => filteredEntries.value.length * rowHeight);
const resultSpacerHeight = computed(() => Math.max(1, Math.min(resultNaturalHeight.value, maximumSpacerHeight)));
const visibleEntries = computed(() => {
  const count = Math.ceil(resultViewportHeight.value / rowHeight) + overscanRows * 2;
  const start = firstVisibleResultIndex(count);
  const end = Math.min(filteredEntries.value.length, start + count);
  return filteredEntries.value.slice(start, end).map((entry, index) => ({
    entry,
    top: resultRowTop(start + index, start),
  }));
});
const comparisonPercentage = computed(() => {
  if (!comparison.totalBytes) return comparison.scanning ? 0 : 100;
  return Math.min(100, Math.round(comparison.processedBytes / comparison.totalBytes * 100));
});

watch([keyword, statusFilter], resetResultScroll);

onMounted(() => {
  worker = new FolderCompareWorker();
  worker.addEventListener("message", handleWorkerMessage);
  resizeObserver = new ResizeObserver(() => {
    resultViewportHeight.value = resultViewport.value?.clientHeight ?? 480;
  });
});

onBeforeUnmount(() => {
  if (comparisonId) worker?.postMessage({ type: "cancel", id: comparisonId });
  worker?.terminate();
  resizeObserver?.disconnect();
  if (scrollFrame) cancelAnimationFrame(scrollFrame);
});

function selectDirectory(event: Event, side: CompareSide) {
  const input = event.target as HTMLInputElement;
  const selected = Array.from(input.files ?? []);
  input.value = "";
  const root = side === "left" ? leftRoot : rightRoot;
  const target = new Map<string, File>();
  let rootName = "";
  let bytes = 0;

  for (const file of selected) {
    const pathParts = (file.webkitRelativePath || file.name).split("/").filter(Boolean);
    if (!rootName && pathParts.length > 1) rootName = pathParts[0];
    const relativePath = pathParts.length > 1 ? pathParts.slice(1).join("/") : pathParts[0];
    if (!relativePath) continue;
    target.set(relativePath, file);
    bytes += file.size;
  }

  if (!selected.length) {
    selectionMessage.value = "没有读取到文件；浏览器无法从空文件夹生成文件列表。";
    return;
  }
  selectionMessage.value = "";
  Object.assign(root, { name: rootName || "已选文件夹", files: target.size, bytes });
  if (side === "left") leftFiles.value = target;
  else rightFiles.value = target;
  buildComparison();
}

function buildComparison() {
  if (!worker) return;
  if (comparisonId) worker.postMessage({ type: "cancel", id: comparisonId });
  comparisonId += 1;
  const allPaths = new Set([...leftFiles.value.keys(), ...rightFiles.value.keys()]);
  const nextEntries: FolderEntry[] = [];
  const pairs: Array<{ path: string; left: File; right: File }> = [];

  for (const path of allPaths) {
    const left = leftFiles.value.get(path);
    const right = rightFiles.value.get(path);
    let status: EntryStatus;
    if (!left) status = "rightOnly";
    else if (!right) status = "leftOnly";
    else if (left.size !== right.size) status = "different";
    else if (left.size === 0) status = "same";
    else {
      status = "pending";
      pairs.push({ path, left, right });
    }
    const slashIndex = path.lastIndexOf("/");
    nextEntries.push({
      path,
      name: slashIndex >= 0 ? path.slice(slashIndex + 1) : path,
      directory: slashIndex >= 0 ? path.slice(0, slashIndex) : "",
      left,
      right,
      status,
      firstDifference: left && right && left.size !== right.size ? Math.min(left.size, right.size) : null,
      error: "",
    });
  }

  nextEntries.sort((left, right) => left.path.localeCompare(right.path, "zh-CN", { numeric: true, sensitivity: "base" }));
  entries.value = nextEntries;
  entryIndexes = new Map(nextEntries.map((entry, index) => [entry.path, index]));
  Object.assign(comparison, {
    scanning: pairs.length > 0,
    currentPath: pairs[0]?.path ?? "",
    processedFiles: 0,
    totalFiles: pairs.length,
    processedBytes: 0,
    totalBytes: pairs.reduce((sum, pair) => sum + pair.left.size, 0),
  });
  resetResultScroll();
  if (pairs.length) worker.postMessage({ type: "compare", id: comparisonId, pairs });
}

function handleWorkerMessage(event: MessageEvent<WorkerMessage>) {
  const result = event.data;
  if (result.id !== comparisonId) return;
  if (result.type === "complete") {
    comparison.scanning = false;
    comparison.processedFiles = result.totalFiles;
    comparison.processedBytes = result.totalBytes;
    comparison.currentPath = "";
    return;
  }

  comparison.currentPath = result.path;
  comparison.processedFiles = result.processedFiles;
  comparison.totalFiles = result.totalFiles;
  comparison.processedBytes = result.processedBytes;
  comparison.totalBytes = result.totalBytes;
  if (result.type === "progress") return;

  const index = entryIndexes.get(result.path);
  if (index === undefined) return;
  const entry = entries.value[index];
  entry.status = result.error ? "error" : result.equal ? "same" : "different";
  entry.firstDifference = result.firstDifference;
  entry.error = result.error ?? "";
}

function openEntry(entry: FolderEntry) {
  if (entry.status === "pending") return;
  emit("openFile", { left: entry.left, right: entry.right });
}

function handleResultScroll() {
  if (scrollFrame) cancelAnimationFrame(scrollFrame);
  scrollFrame = requestAnimationFrame(() => {
    scrollFrame = 0;
    resultScrollTop.value = resultViewport.value?.scrollTop ?? 0;
  });
}

async function resetResultScroll() {
  resultScrollTop.value = 0;
  if (resultViewport.value) resultViewport.value.scrollTop = 0;
  await nextTick();
  observeResultViewport();
}

function observeResultViewport() {
  if (!resultViewport.value || !resizeObserver) return;
  resizeObserver.disconnect();
  resizeObserver.observe(resultViewport.value);
  resultViewportHeight.value = resultViewport.value.clientHeight || 480;
}

function firstVisibleResultIndex(visibleCount: number) {
  if (resultNaturalHeight.value <= maximumSpacerHeight) {
    return Math.max(0, Math.floor(resultScrollTop.value / rowHeight) - overscanRows);
  }
  const maximumStart = Math.max(0, filteredEntries.value.length - visibleCount);
  const scrollableHeight = Math.max(1, resultSpacerHeight.value - resultViewportHeight.value);
  return Math.min(maximumStart, Math.max(0, Math.round(resultScrollTop.value / scrollableHeight * maximumStart)));
}

function resultRowTop(rowIndex: number, startIndex: number) {
  if (resultNaturalHeight.value <= maximumSpacerHeight) return rowIndex * rowHeight;
  return resultScrollTop.value + (rowIndex - startIndex) * rowHeight;
}

function statusText(status: EntryStatus) {
  return {
    pending: "比较中",
    same: "相同",
    different: "不同",
    leftOnly: "仅左侧",
    rightOnly: "仅右侧",
    error: "失败",
  }[status];
}

function statusTagType(status: EntryStatus): "default" | "success" | "warning" | "error" | "info" {
  if (status === "same") return "success";
  if (status === "different" || status === "error") return "error";
  if (status === "leftOnly" || status === "rightOnly") return "warning";
  return "info";
}

function formatModifiedTime(timestamp: number) {
  return new Intl.DateTimeFormat("zh-CN", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  }).format(timestamp);
}

function formatBytes(bytes: number) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GiB`;
}
</script>

<style scoped>
.folder-compare-view {
  grid-row: 2 / -1;
  min-width: 0;
  min-height: 0;
  display: grid;
  grid-template-rows: auto auto auto minmax(0, 1fr) auto;
  overflow: hidden;
  background: #f3f6f8;
}

.folder-pickers {
  display: grid;
  grid-template-columns: 1fr 1fr;
  border-bottom: 1px solid #cbd5dc;
  background: #e9eef2;
}

.folder-picker {
  min-width: 0;
  min-height: 64px;
  padding: 9px 12px;
  display: flex;
  align-items: center;
  gap: 10px;
}

.folder-picker + .folder-picker {
  border-left: 1px solid #cbd5dc;
}

.folder-picker input {
  display: none;
}

.folder-side-badge {
  width: 28px;
  height: 28px;
  display: grid;
  place-items: center;
  flex: 0 0 auto;
  border-radius: 6px;
  font-size: 11px;
  font-weight: 800;
}

.folder-picker--left .folder-side-badge {
  background: #f8d7da;
  color: #842029;
}

.folder-picker--right .folder-side-badge {
  background: #d1e7dd;
  color: #0f5132;
}

.folder-picker-copy {
  min-width: 0;
  display: grid;
  flex: 1;
  gap: 2px;
}

.folder-picker-copy strong,
.folder-picker-copy small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.folder-picker-copy strong {
  color: #263945;
  font-size: 13px;
}

.folder-picker-copy small {
  color: #6a7883;
  font-size: 11px;
}

.folder-filter-bar {
  min-height: 46px;
  padding: 7px 10px;
  display: flex;
  align-items: center;
  gap: 8px;
  border-bottom: 1px solid #d4dde3;
  background: #fff;
}

.folder-keyword {
  width: min(280px, 28vw);
}

.folder-status-filter {
  width: 138px;
}

.folder-summary {
  margin-left: auto;
  display: flex;
  gap: 12px;
  color: #586975;
  font-size: 11px;
  white-space: nowrap;
}

.summary-different {
  color: #b42318;
}

.summary-same {
  color: #18794e;
}

.folder-progress,
.folder-message {
  min-height: 28px;
  padding: 4px 10px;
  border-bottom: 1px solid #dbe2e7;
  background: #f9fbfc;
  color: #637581;
  font-size: 11px;
}

.folder-progress {
  display: grid;
  grid-template-columns: minmax(120px, 240px) minmax(0, 1fr);
  align-items: center;
  gap: 10px;
}

.folder-progress span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.folder-message--empty {
  visibility: hidden;
}

.folder-results {
  min-width: 0;
  min-height: 0;
  display: grid;
  grid-template-rows: 34px minmax(0, 1fr);
  overflow: hidden;
  background: #fff;
}

.folder-result-header,
.folder-result-row {
  min-width: 920px;
  display: grid;
  grid-template-columns: minmax(300px, 1fr) 170px 130px 170px 92px;
}

.folder-result-header {
  align-items: center;
  border-bottom: 1px solid #cbd5dc;
  background: #e9eef2;
  color: #53636e;
  font-size: 11px;
  font-weight: 700;
}

.folder-result-header span {
  height: 100%;
  padding: 9px 10px;
  border-right: 1px solid #d4dce2;
}

.folder-result-viewport {
  min-width: 0;
  min-height: 0;
  overflow: auto;
}

.folder-result-spacer {
  min-width: 920px;
  position: relative;
}

.folder-result-row {
  height: 52px;
  position: absolute;
  top: 0;
  left: 0;
  align-items: stretch;
  border-bottom: 1px solid #edf1f3;
  background: #fff;
  color: #2b3c47;
  font-size: 12px;
}

.folder-result-row:hover {
  background: #f3f8fb;
}

.folder-result-row--different {
  background: #fffafa;
}

.folder-result-row--leftOnly,
.folder-result-row--rightOnly {
  background: #fffcf4;
}

.folder-result-row > div {
  min-width: 0;
  padding: 7px 10px;
  border-right: 1px solid #edf1f3;
}

.folder-path-cell,
.folder-file-cell,
.folder-status-cell {
  display: grid;
  align-content: center;
  gap: 2px;
}

.folder-path-cell strong,
.folder-path-cell small,
.folder-status-cell small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.folder-path-cell small,
.folder-file-cell small,
.folder-status-cell small {
  color: #74838d;
  font-size: 10px;
}

.folder-status-cell {
  justify-items: start;
}

.folder-action-cell {
  display: flex;
  align-items: center;
  justify-content: center;
}

.folder-empty {
  min-height: 240px;
}

.folder-footer {
  min-height: 28px;
  padding: 4px 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  background: #153243;
  color: #e7f0f5;
  font-size: 11px;
}

@media (max-width: 760px) {
  .folder-picker {
    align-items: flex-start;
    flex-wrap: wrap;
  }

  .folder-filter-bar {
    align-items: stretch;
    flex-wrap: wrap;
  }

  .folder-keyword {
    width: calc(100% - 146px);
  }

  .folder-summary {
    width: 100%;
    margin-left: 0;
    overflow-x: auto;
  }

  .folder-footer span:first-child {
    display: none;
  }
}
</style>
