<template>
  <div class="binary-compare-view" :style="hexGridStyle">
    <div class="binary-view-controls">
      <span>按相同偏移比较</span>
      <label>
        每行
        <select v-model.number="bytesPerRow">
          <option :value="8">8 字节</option>
          <option :value="16">16 字节</option>
          <option :value="32">32 字节</option>
        </select>
      </label>
      <form @submit.prevent="jumpToInputOffset">
        <input v-model="offsetInput" aria-label="跳转偏移量" placeholder="偏移量，如 0x120" />
        <button type="submit">跳转</button>
      </form>
      <span v-if="stats.scanning">正在比较 {{ progressText }}</span>
      <span v-else-if="stats.error" class="binary-error">{{ stats.error }}</span>
      <span v-else-if="stats.rangesTruncated" class="binary-warning">差异区段过多，导航结果已截断</span>
    </div>

    <div ref="viewport" class="hex-viewport" @scroll="handleScroll">
      <div class="hex-scroll-content">
        <div class="hex-column-header">
          <span>偏移量</span>
          <span>左侧 Hex</span>
          <span>左侧 ASCII</span>
          <span>右侧 Hex</span>
          <span>右侧 ASCII</span>
        </div>

        <div class="hex-spacer" :style="{ height: `${spacerHeight}px` }">
          <div
            v-for="row in visibleRows"
            :key="row.offset"
            class="hex-row"
            :class="{ 'hex-row--different': row.different }"
            :style="{ transform: `translateY(${row.top}px)` }"
          >
            <button class="hex-offset" type="button" :title="`跳转到 0x${formatOffset(row.offset)}`" @click="selectRow(row.offset)">
              {{ formatOffset(row.offset) }}
            </button>

            <div class="hex-bytes" aria-label="左侧十六进制字节">
              <span
                v-for="index in bytesPerRow"
                :key="`left-${index}`"
                :class="byteClass(row, index - 1)"
              >{{ formatByte(row.left[index - 1]) }}</span>
            </div>
            <div class="ascii-bytes" aria-label="左侧 ASCII">
              <span
                v-for="index in bytesPerRow"
                :key="`left-ascii-${index}`"
                :class="byteClass(row, index - 1)"
              >{{ formatAscii(row.left[index - 1]) }}</span>
            </div>

            <div class="hex-bytes" aria-label="右侧十六进制字节">
              <span
                v-for="index in bytesPerRow"
                :key="`right-${index}`"
                :class="byteClass(row, index - 1)"
              >{{ formatByte(row.right[index - 1]) }}</span>
            </div>
            <div class="ascii-bytes" aria-label="右侧 ASCII">
              <span
                v-for="index in bytesPerRow"
                :key="`right-ascii-${index}`"
                :class="byteClass(row, index - 1)"
              >{{ formatAscii(row.right[index - 1]) }}</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import BinaryCompareWorker from "./binaryCompare.worker?worker";
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";

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

interface DifferenceRange {
  start: number;
  end: number;
}

interface HexRow {
  offset: number;
  top: number;
  left: Array<number | undefined>;
  right: Array<number | undefined>;
  different: boolean;
}

interface ProgressMessage {
  type: "progress";
  id: number;
  processedBytes: number;
  totalBytes: number;
  differenceBytes: number;
}

interface CompleteMessage {
  type: "complete";
  id: number;
  totalBytes: number;
  differenceBytes: number;
  ranges: DifferenceRange[];
  rangesTruncated: boolean;
}

interface ErrorMessage {
  type: "error";
  id: number;
  message: string;
}

const props = defineProps<{
  left: Blob;
  right: Blob;
}>();

const emit = defineEmits<{
  stats: [stats: BinaryCompareStats];
}>();

const rowHeight = 24;
const overscanRows = 8;
const maximumSpacerHeight = 8_000_000;
const viewport = ref<HTMLElement | null>(null);
const bytesPerRow = ref(16);
const offsetInput = ref("");
const scrollTop = ref(0);
const viewportHeight = ref(480);
const visibleRows = ref<HexRow[]>([]);
const ranges = ref<DifferenceRange[]>([]);
const stats = reactive<BinaryCompareStats>(emptyStats());
let worker: Worker | null = null;
let resizeObserver: ResizeObserver | null = null;
let comparisonId = 0;
let visibleReadId = 0;
let currentDifferenceIndex = -1;
let scrollFrame = 0;

const totalRows = computed(() => Math.ceil(Math.max(props.left.size, props.right.size) / bytesPerRow.value));
const naturalHeight = computed(() => totalRows.value * rowHeight);
const spacerHeight = computed(() => Math.max(1, Math.min(naturalHeight.value, maximumSpacerHeight)));
const progressText = computed(() => {
  if (!stats.totalBytes) return "0%";
  return `${Math.min(100, Math.round(stats.processedBytes / stats.totalBytes * 100))}%`;
});
const hexGridStyle = computed(() => {
  const groupGaps = Math.floor((bytesPerRow.value - 1) / 8) * 6;
  const hexWidth = 18 + bytesPerRow.value * 23 + groupGaps;
  const asciiWidth = 18 + bytesPerRow.value * 9 + groupGaps;
  const contentWidth = 92 + (hexWidth + asciiWidth) * 2;
  return {
    "--hex-grid-columns": `92px minmax(${hexWidth}px, 1.6fr) minmax(${asciiWidth}px, 0.7fr) minmax(${hexWidth}px, 1.6fr) minmax(${asciiWidth}px, 0.7fr)`,
    "--hex-content-min-width": `${contentWidth}px`,
  };
});

watch(() => [props.left, props.right] as const, startComparison);
watch(bytesPerRow, () => {
  if (viewport.value) viewport.value.scrollTop = 0;
  scrollTop.value = 0;
  void updateVisibleRows();
});

onMounted(() => {
  worker = new BinaryCompareWorker();
  worker.addEventListener("message", handleWorkerMessage);
  resizeObserver = new ResizeObserver(() => {
    viewportHeight.value = viewport.value?.clientHeight ?? 480;
    void updateVisibleRows();
  });
  if (viewport.value) resizeObserver.observe(viewport.value);
  startComparison();
});

onBeforeUnmount(() => {
  if (comparisonId) worker?.postMessage({ type: "cancel", id: comparisonId });
  worker?.terminate();
  resizeObserver?.disconnect();
  if (scrollFrame) cancelAnimationFrame(scrollFrame);
});

function startComparison() {
  if (!worker) return;
  if (comparisonId) worker.postMessage({ type: "cancel", id: comparisonId });
  comparisonId += 1;
  currentDifferenceIndex = -1;
  ranges.value = [];
  Object.assign(stats, emptyStats(), {
    scanning: true,
    totalBytes: Math.max(props.left.size, props.right.size),
  });
  emitStats();
  if (viewport.value) viewport.value.scrollTop = 0;
  scrollTop.value = 0;
  void updateVisibleRows();
  worker.postMessage({ type: "compare", id: comparisonId, left: props.left, right: props.right });
}

function handleWorkerMessage(event: MessageEvent<ProgressMessage | CompleteMessage | ErrorMessage>) {
  const result = event.data;
  if (result.id !== comparisonId) return;

  if (result.type === "progress") {
    stats.processedBytes = result.processedBytes;
    stats.totalBytes = result.totalBytes;
    stats.differenceBytes = result.differenceBytes;
    emitStats();
    return;
  }

  if (result.type === "error") {
    stats.scanning = false;
    stats.error = result.message;
    emitStats();
    return;
  }

  ranges.value = result.ranges;
  Object.assign(stats, {
    scanning: false,
    processedBytes: result.totalBytes,
    totalBytes: result.totalBytes,
    differenceBytes: result.differenceBytes,
    blocks: result.ranges.length,
    firstDifference: result.ranges[0]?.start ?? null,
    rangesTruncated: result.rangesTruncated,
  });
  emitStats();
}

function handleScroll() {
  scrollTop.value = viewport.value?.scrollTop ?? 0;
  if (scrollFrame) cancelAnimationFrame(scrollFrame);
  scrollFrame = requestAnimationFrame(() => {
    scrollFrame = 0;
    void updateVisibleRows();
  });
}

async function updateVisibleRows() {
  const readId = ++visibleReadId;
  const count = Math.ceil(viewportHeight.value / rowHeight) + overscanRows * 2;
  const startRow = firstVisibleRow(count);
  const endRow = Math.min(totalRows.value, startRow + count);
  const startOffset = startRow * bytesPerRow.value;
  const endOffset = Math.min(Math.max(props.left.size, props.right.size), endRow * bytesPerRow.value);
  const [leftBuffer, rightBuffer] = await Promise.all([
    props.left.slice(startOffset, Math.min(endOffset, props.left.size)).arrayBuffer(),
    props.right.slice(startOffset, Math.min(endOffset, props.right.size)).arrayBuffer(),
  ]);
  if (readId !== visibleReadId) return;

  const leftBytes = new Uint8Array(leftBuffer);
  const rightBytes = new Uint8Array(rightBuffer);
  const nextRows: HexRow[] = [];
  for (let rowIndex = startRow; rowIndex < endRow; rowIndex += 1) {
    const offset = rowIndex * bytesPerRow.value;
    const left: Array<number | undefined> = [];
    const right: Array<number | undefined> = [];
    let different = false;
    for (let column = 0; column < bytesPerRow.value; column += 1) {
      const absoluteOffset = offset + column;
      const bufferIndex = absoluteOffset - startOffset;
      const leftValue = absoluteOffset < props.left.size ? leftBytes[bufferIndex] : undefined;
      const rightValue = absoluteOffset < props.right.size ? rightBytes[bufferIndex] : undefined;
      left.push(leftValue);
      right.push(rightValue);
      if (leftValue !== rightValue) different = true;
    }
    nextRows.push({ offset, top: rowTop(rowIndex, startRow), left, right, different });
  }
  visibleRows.value = nextRows;
}

function firstVisibleRow(visibleCount: number) {
  if (naturalHeight.value <= maximumSpacerHeight) {
    return Math.max(0, Math.floor(scrollTop.value / rowHeight) - overscanRows);
  }
  const maximumStart = Math.max(0, totalRows.value - visibleCount);
  const scrollableHeight = Math.max(1, spacerHeight.value - viewportHeight.value);
  return Math.min(maximumStart, Math.max(0, Math.round(scrollTop.value / scrollableHeight * maximumStart)));
}

function rowTop(rowIndex: number, startRow: number) {
  if (naturalHeight.value <= maximumSpacerHeight) return rowIndex * rowHeight;
  return scrollTop.value + (rowIndex - startRow) * rowHeight;
}

function byteClass(row: HexRow, column: number) {
  const leftValue = row.left[column];
  const rightValue = row.right[column];
  return {
    "hex-byte--different": leftValue !== rightValue,
    "hex-byte--missing": leftValue === undefined || rightValue === undefined,
    "hex-byte--group": column > 0 && column % 8 === 0,
  };
}

function goToDifference(direction: "previous" | "next") {
  if (!ranges.value.length) return;
  if (direction === "next") {
    currentDifferenceIndex = (currentDifferenceIndex + 1) % ranges.value.length;
  } else {
    currentDifferenceIndex = (currentDifferenceIndex - 1 + ranges.value.length) % ranges.value.length;
  }
  jumpToOffset(ranges.value[currentDifferenceIndex].start);
}

function jumpToInputOffset() {
  const input = offsetInput.value.trim().toLowerCase();
  const offset = input.startsWith("0x") ? Number.parseInt(input.slice(2), 16) : Number.parseInt(input, 10);
  if (!Number.isFinite(offset) || offset < 0) return;
  jumpToOffset(Math.min(offset, Math.max(0, stats.totalBytes - 1)));
}

function jumpToOffset(offset: number) {
  const targetRow = Math.floor(offset / bytesPerRow.value);
  const target = viewport.value;
  if (!target) return;

  if (naturalHeight.value <= maximumSpacerHeight) {
    target.scrollTop = targetRow * rowHeight;
  } else {
    const visibleCount = Math.ceil(viewportHeight.value / rowHeight) + overscanRows * 2;
    const maximumStart = Math.max(1, totalRows.value - visibleCount);
    const scrollableHeight = Math.max(1, spacerHeight.value - viewportHeight.value);
    target.scrollTop = Math.min(scrollableHeight, targetRow / maximumStart * scrollableHeight);
  }
  scrollTop.value = target.scrollTop;
  void updateVisibleRows();
}

function selectRow(offset: number) {
  offsetInput.value = `0x${formatOffset(offset)}`;
}

function formatByte(value: number | undefined) {
  return value === undefined ? "--" : value.toString(16).padStart(2, "0").toUpperCase();
}

function formatAscii(value: number | undefined) {
  if (value === undefined) return " ";
  return value >= 32 && value <= 126 ? String.fromCharCode(value) : ".";
}

function formatOffset(offset: number) {
  const width = stats.totalBytes > 0xffffffff ? 12 : 8;
  return offset.toString(16).toUpperCase().padStart(width, "0");
}

function emitStats() {
  emit("stats", { ...stats });
}

function emptyStats(): BinaryCompareStats {
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

defineExpose({ goToDifference, jumpToOffset });
</script>

<style scoped>
.binary-compare-view {
  width: 100%;
  height: 100%;
  min-width: 0;
  display: grid;
  grid-template-rows: auto minmax(0, 1fr);
  overflow: hidden;
  background: #fff;
}

.binary-view-controls {
  min-height: 38px;
  padding: 5px 10px;
  display: flex;
  align-items: center;
  gap: 14px;
  border-bottom: 1px solid #d7dfe5;
  background: #f8fafb;
  color: #53636e;
  font-size: 12px;
}

.binary-view-controls label,
.binary-view-controls form {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.binary-view-controls select,
.binary-view-controls input,
.binary-view-controls button {
  min-height: 26px;
  border: 1px solid #c8d2da;
  border-radius: 5px;
  background: #fff;
  color: #263945;
  font: inherit;
}

.binary-view-controls input {
  width: 150px;
  padding: 3px 7px;
}

.binary-view-controls button {
  padding: 3px 9px;
  cursor: pointer;
}

.binary-warning {
  color: #a35a00;
}

.binary-error {
  color: #b42318;
}

.hex-column-header,
.hex-row {
  width: 100%;
  display: grid;
  grid-template-columns: var(--hex-grid-columns);
}

.hex-column-header {
  height: 30px;
  position: sticky;
  top: 0;
  z-index: 2;
  align-items: center;
  border-bottom: 1px solid #cbd5dc;
  background: #e9eef2;
  color: #485a66;
  font-size: 11px;
  font-weight: 700;
}

.hex-column-header span {
  height: 100%;
  padding: 7px 9px;
  border-right: 1px solid #d4dce2;
}

.hex-viewport {
  min-width: 0;
  min-height: 0;
  position: relative;
  overflow: auto;
  background: #fff;
}

.hex-scroll-content {
  width: 100%;
  min-width: var(--hex-content-min-width);
}

.hex-spacer {
  width: 100%;
  position: relative;
}

.hex-row {
  font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
  font-size: 12px;
}

.hex-row {
  height: 24px;
  position: absolute;
  top: 0;
  left: 0;
  align-items: center;
  border-bottom: 1px solid #f0f3f5;
  color: #26343d;
  line-height: 24px;
}

.hex-row:hover,
.hex-row--different:hover {
  background: #edf4f8;
}

.hex-row--different {
  background: #fffdf8;
}

.hex-offset {
  height: 100%;
  padding: 0 9px;
  border: 0;
  border-right: 1px solid #e1e7eb;
  background: #f7f9fa;
  color: #637581;
  font: inherit;
  text-align: right;
  cursor: pointer;
}

.hex-bytes,
.ascii-bytes {
  height: 100%;
  padding: 0 9px;
  display: flex;
  align-items: center;
  overflow: hidden;
  border-right: 1px solid #e1e7eb;
  white-space: nowrap;
}

.hex-bytes span {
  width: 23px;
  flex: 0 0 23px;
  text-align: center;
}

.ascii-bytes span {
  width: 9px;
  flex: 0 0 9px;
  text-align: center;
}

.hex-bytes .hex-byte--group,
.ascii-bytes .hex-byte--group {
  margin-left: 6px;
}

.hex-byte--different {
  border-radius: 2px;
  background: #ffd9d6;
  color: #9b2118;
  font-weight: 700;
}

.hex-byte--missing {
  background: #e9edf0;
  color: #89969f;
}

@media (max-width: 700px) {
  .binary-view-controls {
    gap: 8px;
    overflow-x: auto;
    white-space: nowrap;
  }
}
</style>
