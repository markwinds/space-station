/// <reference lib="webworker" />

interface ComparePair {
  path: string;
  left: File;
  right: File;
}

interface CompareRequest {
  type: "compare";
  id: number;
  pairs: ComparePair[];
}

interface CancelRequest {
  type: "cancel";
  id: number;
}

type WorkerRequest = CompareRequest | CancelRequest;

const workerScope: DedicatedWorkerGlobalScope = self as unknown as DedicatedWorkerGlobalScope;
const chunkSize = 1024 * 1024;
let cancelledId = -1;

workerScope.addEventListener("message", (event: MessageEvent<WorkerRequest>) => {
  if (event.data.type === "cancel") {
    cancelledId = event.data.id;
    return;
  }
  void comparePairs(event.data);
});

async function comparePairs(request: CompareRequest) {
  const totalBytes = request.pairs.reduce((sum, pair) => sum + Math.max(pair.left.size, pair.right.size), 0);
  let processedBytes = 0;

  for (let index = 0; index < request.pairs.length; index += 1) {
    if (cancelledId === request.id) return;
    const pair = request.pairs[index];
    try {
      const result = await compareFilePair(request.id, pair, processedBytes, totalBytes, index, request.pairs.length);
      if (!result) return;
      processedBytes += Math.max(pair.left.size, pair.right.size);
      workerScope.postMessage({
        type: "result",
        id: request.id,
        path: pair.path,
        equal: result.equal,
        firstDifference: result.firstDifference,
        processedFiles: index + 1,
        totalFiles: request.pairs.length,
        processedBytes,
        totalBytes,
      });
    } catch (error) {
      processedBytes += Math.max(pair.left.size, pair.right.size);
      workerScope.postMessage({
        type: "result",
        id: request.id,
        path: pair.path,
        equal: false,
        firstDifference: null,
        error: error instanceof Error ? error.message : "读取文件失败。",
        processedFiles: index + 1,
        totalFiles: request.pairs.length,
        processedBytes,
        totalBytes,
      });
    }
  }

  workerScope.postMessage({ type: "complete", id: request.id, totalFiles: request.pairs.length, totalBytes });
}

async function compareFilePair(
  id: number,
  pair: ComparePair,
  completedBytes: number,
  totalBytes: number,
  fileIndex: number,
  totalFiles: number,
) {
  if (pair.left.size !== pair.right.size) return { equal: false, firstDifference: Math.min(pair.left.size, pair.right.size) };

  for (let offset = 0; offset < pair.left.size; offset += chunkSize) {
    if (cancelledId === id) return null;
    const end = Math.min(pair.left.size, offset + chunkSize);
    const [leftBuffer, rightBuffer] = await Promise.all([
      pair.left.slice(offset, end).arrayBuffer(),
      pair.right.slice(offset, end).arrayBuffer(),
    ]);
    if (cancelledId === id) return null;

    const leftBytes = new Uint8Array(leftBuffer);
    const rightBytes = new Uint8Array(rightBuffer);
    for (let index = 0; index < leftBytes.length; index += 1) {
      if (leftBytes[index] !== rightBytes[index]) {
        return { equal: false, firstDifference: offset + index };
      }
    }

    workerScope.postMessage({
      type: "progress",
      id,
      path: pair.path,
      processedFiles: fileIndex,
      totalFiles,
      processedBytes: completedBytes + end,
      totalBytes,
    });
  }
  return { equal: true, firstDifference: null };
}

export {};
