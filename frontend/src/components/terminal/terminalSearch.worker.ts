/// <reference lib="webworker" />

type SearchWorkerRequest =
  | { type: "start"; id: number; query: string; caseSensitive: boolean; wholeWord: boolean; regex: boolean }
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
  | { type: "location"; id: number; index: number; row: number; offset: number; length: number }
  | { type: "window"; id: number; locations: Array<[index: number, row: number, offset: number, length: number]> };

const workerScope = self as unknown as DedicatedWorkerGlobalScope;
const wholeWordSeparators = " ~!@#$%^&*()+`-=[]{}|\\;:\"',./<>?";
let activeId = 0;
let query = "";
let needle = "";
let caseSensitive = false;
let wholeWord = false;
let regex = false;
let regularExpression: RegExp | undefined;
let count = 0;
let logicalLine = "";
let logicalLineOrigins: Array<[row: number, offset: number]> = [];
// Packed numeric arrays avoid allocating one object per match for large buffers.
let matchRows: number[] = [];
let matchOffsets: number[] = [];
let matchLengths: number[] = [];

function isWholeWord(text: string, index: number, length: number) {
  return (index === 0 || wholeWordSeparators.includes(text[index - 1]))
    && (index + length === text.length || wholeWordSeparators.includes(text[index + length]));
}

function recordMatch(index: number, length: number) {
  const origin = logicalLineOrigins[index];
  if (!origin || length <= 0) return;
  matchRows.push(origin[0]);
  matchOffsets.push(origin[1]);
  matchLengths.push(length);
  count += 1;
}

function processLogicalLine() {
  if (!logicalLine || !query) return;
  if (regex) {
    if (!regularExpression) return;
    let offset = 0;
    while (offset <= logicalLine.length) {
      regularExpression.lastIndex = offset;
      const match = regularExpression.exec(logicalLine);
      if (!match) break;
      const length = match[0].length;
      if (length > 0 && (!wholeWord || isWholeWord(logicalLine, match.index, length))) {
        recordMatch(match.index, length);
      }
      // SearchAddon resumes one column after the previous match start, which
      // allows overlapping literal and regular-expression matches.
      offset = match.index + 1;
    }
    return;
  }

  const searchable = caseSensitive ? logicalLine : logicalLine.toLowerCase();
  let offset = 0;
  while ((offset = searchable.indexOf(needle, offset)) >= 0) {
    if (!wholeWord || isWholeWord(searchable, offset, needle.length)) {
      recordMatch(offset, needle.length);
    }
    offset += 1;
  }
}

function resetLogicalLine() {
  logicalLine = "";
  logicalLineOrigins = [];
}

workerScope.onmessage = (event: MessageEvent<SearchWorkerRequest>) => {
  const message = event.data;
  if (message.type === "start") {
    activeId = message.id;
    query = message.query;
    caseSensitive = message.caseSensitive;
    wholeWord = message.wholeWord;
    regex = message.regex;
    needle = caseSensitive ? query : query.toLowerCase();
    regularExpression = undefined;
    if (regex) {
      try {
        regularExpression = new RegExp(query, caseSensitive ? "g" : "gi");
      } catch {
        // Invalid expressions intentionally produce zero results instead of
        // terminating the worker or blocking the terminal UI.
      }
    }
    count = 0;
    resetLogicalLine();
    matchRows = [];
    matchOffsets = [];
    matchLengths = [];
    return;
  }
  if (message.id !== activeId) return;
  if (message.type === "chunk") {
    for (const [row, text, wrapsToNext] of message.segments) {
      logicalLine += text;
      for (let offset = 0; offset < text.length; offset += 1) {
        logicalLineOrigins.push([row, offset]);
      }
      if (!wrapsToNext) {
        processLogicalLine();
        resetLogicalLine();
      }
    }
    return;
  }
  if (message.type === "locate") {
    const row = matchRows[message.index];
    const offset = matchOffsets[message.index];
    const length = matchLengths[message.index];
    if (row !== undefined && offset !== undefined && length !== undefined) {
      const response: SearchWorkerResponse = {
        type: "location",
        id: activeId,
        index: message.index,
        row,
        offset,
        length,
      };
      workerScope.postMessage(response);
    }
    return;
  }
  if (message.type === "window") {
    const start = Math.max(0, Math.trunc(message.start));
    const end = Math.min(matchRows.length, Math.max(start, Math.trunc(message.end)));
    const locations: Array<[number, number, number, number]> = [];
    for (let index = start; index < end; index += 1) {
      locations.push([index, matchRows[index], matchOffsets[index], matchLengths[index]]);
    }
    const response: SearchWorkerResponse = { type: "window", id: activeId, locations };
    workerScope.postMessage(response);
    return;
  }
  if (logicalLine) {
    processLogicalLine();
    resetLogicalLine();
  }
  const result: SearchWorkerResult = { type: "result", id: activeId, count };
  workerScope.postMessage(result);
};

export {};
