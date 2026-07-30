/// <reference lib="webworker" />

interface CompareRequest {
  type: "compare";
  id: number;
  left: Blob;
  right: Blob;
}

interface CancelRequest {
  type: "cancel";
  id: number;
}

type WorkerRequest = CompareRequest | CancelRequest;

interface DifferenceRange {
  start: number;
  end: number;
}

const workerScope: DedicatedWorkerGlobalScope = self as unknown as DedicatedWorkerGlobalScope;
const chunkSize = 1024 * 1024;
const maximumStoredRanges = 100_000;
let cancelledId = -1;

workerScope.addEventListener("message", (event: MessageEvent<WorkerRequest>) => {
  if (event.data.type === "cancel") {
    cancelledId = event.data.id;
    return;
  }
  void compareFiles(event.data).catch((error: unknown) => {
    workerScope.postMessage({
      type: "error",
      id: event.data.id,
      message: error instanceof Error ? error.message : "读取二进制文件失败。",
    });
  });
});

async function compareFiles(request: CompareRequest) {
  const { id, left, right } = request;
  const totalBytes = Math.max(left.size, right.size);
  const ranges: DifferenceRange[] = [];
  let rangeStart = -1;
  let differenceBytes = 0;
  let truncated = false;

  for (let offset = 0; offset < totalBytes; offset += chunkSize) {
    if (cancelledId === id) return;

    const end = Math.min(totalBytes, offset + chunkSize);
    const [leftBuffer, rightBuffer] = await Promise.all([
      left.slice(offset, Math.min(end, left.size)).arrayBuffer(),
      right.slice(offset, Math.min(end, right.size)).arrayBuffer(),
    ]);
    if (cancelledId === id) return;

    const leftBytes = new Uint8Array(leftBuffer);
    const rightBytes = new Uint8Array(rightBuffer);
    const length = end - offset;
    for (let index = 0; index < length; index += 1) {
      const leftExists = index < leftBytes.length;
      const rightExists = index < rightBytes.length;
      const different = !leftExists || !rightExists || leftBytes[index] !== rightBytes[index];
      const absoluteOffset = offset + index;

      if (different) {
        differenceBytes += 1;
        if (rangeStart < 0) rangeStart = absoluteOffset;
      } else if (rangeStart >= 0) {
        truncated = storeRange(ranges, rangeStart, absoluteOffset) || truncated;
        rangeStart = -1;
      }
    }

    workerScope.postMessage({
      type: "progress",
      id,
      processedBytes: end,
      totalBytes,
      differenceBytes,
    });
  }

  if (rangeStart >= 0) {
    truncated = storeRange(ranges, rangeStart, totalBytes) || truncated;
  }

  workerScope.postMessage({
    type: "complete",
    id,
    totalBytes,
    differenceBytes,
    ranges,
    rangesTruncated: truncated,
  });
}

function storeRange(ranges: DifferenceRange[], start: number, end: number) {
  if (ranges.length >= maximumStoredRanges) return true;
  ranges.push({ start, end });
  return false;
}

export {};
