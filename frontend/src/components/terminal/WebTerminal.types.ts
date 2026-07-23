import type { Terminal } from "@xterm/xterm";

export type TerminalRenderer = "webgl" | "canvas";

export interface WebTerminalReadyEvent {
  terminal: Terminal;
  element: HTMLElement;
  handle: WebTerminalHandle;
}

export interface WebTerminalResizeEvent {
  columns: number;
  rows: number;
}

export interface WebTerminalSearchResult {
  resultIndex: number;
  resultCount: number;
  limited: boolean;
}

export interface WebTerminalHandle {
  write(data: string | Uint8Array, callback?: () => void): void;
  writeln(data: string): void;
  fit(): void;
  focus(): void;
  search(
    term: string,
    previous?: boolean,
    incremental?: boolean,
    caseSensitive?: boolean,
    wholeWord?: boolean,
    regex?: boolean,
  ): boolean;
  jumpToSearchIndex(index: number): boolean;
  clearSearch(): void;
  setAppearance(options: { fontSize?: number; lineHeight?: number; letterSpacing?: number }): void;
  getTerminal(): Terminal | undefined;
  getElement(): HTMLElement | undefined;
}
