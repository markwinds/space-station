/// <reference lib="webworker" />

type SearchWorkerRequest =
  | { type: "start"; id: number; query: string }
  | { type: "chunk"; id: number; segments: Array<[row: number, text: string, wrapsToNext: boolean]> }
  | { type: "finish"; id: number }
  | { type: "locate"; id: number; index: number }
  | { type: "window"; id: number; start: number; end: number };

interface SearchWorkerResult {
  type: "result";
  id: number;
  count: number;
}

type SearchWorkerResponse = SearchWorkerResult
  | { type: "location"; id: number; index: number; row: number; offset: number }
  | { type: "window"; id: number; locations: Array<[index: number, row: number, offset: number]> };

const workerScope = self as unknown as DedicatedWorkerGlobalScope;
let activeId = 0;
let needle = "";
let count = 0;
let logicalLineTail = "";
let logicalLineTailOrigins: Array<[row: number, offset: number]> = [];
// Packed numeric arrays avoid allocating one object/tuple per match for large buffers.
let matchRows: number[] = [];
let matchOffsets: number[] = [];

workerScope.onmessage = (event: MessageEvent<SearchWorkerRequest>) => {
  const message = event.data;
  if (message.type === "start") {
    activeId = message.id;
    needle = message.query.toLowerCase();
    count = 0;
    logicalLineTail = "";
    logicalLineTailOrigins = [];
    matchRows = [];
    matchOffsets = [];
    return;
  }
  if (message.id !== activeId) return;
  if (message.type === "chunk") {
    if (!needle) return;
    for (const [row, text, wrapsToNext] of message.segments) {
      const tailLength = logicalLineTail.length;
      const searchable = logicalLineTail + text.toLowerCase();
      let offset = 0;
      while ((offset = searchable.indexOf(needle, offset)) >= 0) {
        // Matches fully inside the retained tail were counted with the previous segment.
        if (offset + needle.length > tailLength) {
          const origin = offset < tailLength
            ? logicalLineTailOrigins[offset]
            : [row, offset - tailLength] as [number, number];
          if (origin) {
            matchRows.push(origin[0]);
            matchOffsets.push(origin[1]);
          }
          count += 1;
        }
        // SearchAddon advances one column, so overlapping matches count too.
        offset += 1;
      }
      if (wrapsToNext && needle.length > 1) {
        const keep = Math.min(needle.length - 1, searchable.length);
        const keepFrom = searchable.length - keep;
        const nextOrigins: Array<[number, number]> = [];
        for (let index = keepFrom; index < searchable.length; index += 1) {
          nextOrigins.push(index < tailLength
            ? logicalLineTailOrigins[index]
            : [row, index - tailLength]);
        }
        logicalLineTail = searchable.slice(keepFrom);
        logicalLineTailOrigins = nextOrigins;
      } else {
        logicalLineTail = "";
        logicalLineTailOrigins = [];
      }
    }
    return;
  }
  if (message.type === "locate") {
    const row = matchRows[message.index];
    const offset = matchOffsets[message.index];
    if (row !== undefined && offset !== undefined) {
      const response: SearchWorkerResponse = {
        type: "location",
        id: activeId,
        index: message.index,
        row,
        offset,
      };
      workerScope.postMessage(response);
    }
    return;
  }
  if (message.type === "window") {
    const start = Math.max(0, Math.trunc(message.start));
    const end = Math.min(matchRows.length, Math.max(start, Math.trunc(message.end)));
    const locations: Array<[number, number, number]> = [];
    for (let index = start; index < end; index += 1) {
      locations.push([index, matchRows[index], matchOffsets[index]]);
    }
    const response: SearchWorkerResponse = { type: "window", id: activeId, locations };
    workerScope.postMessage(response);
    return;
  }
  const result: SearchWorkerResult = { type: "result", id: activeId, count };
  workerScope.postMessage(result);
};

export {};
