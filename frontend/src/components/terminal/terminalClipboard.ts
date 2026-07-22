import type { Terminal } from "@xterm/xterm";

export interface TerminalClipboardOptions {
  copyOnSelect: () => boolean;
  pasteOnRightClick: () => boolean;
  canPaste?: () => boolean;
  canReadClipboard?: () => boolean;
  onCopyError?: () => void;
  onPasteUnavailable?: () => void;
  onPasteError?: (error: unknown) => void;
}

export function attachTerminalClipboard(terminal: Terminal, element: HTMLElement, options: TerminalClipboardOptions) {
  let latestSelection = "";
  const selectionDisposable = terminal.onSelectionChange(() => { latestSelection = terminal.getSelection(); });
  const copySelection = (event: PointerEvent) => {
    if (event.button !== 0 || !options.copyOnSelect()) return;
    const selection = terminal.getSelection() || latestSelection;
    if (!selection) return;
    if (!navigator.clipboard?.writeText) { options.onCopyError?.(); return; }
    void navigator.clipboard.writeText(selection).catch(() => options.onCopyError?.());
  };
  const pasteClipboard = (event: MouseEvent) => {
    if (!options.pasteOnRightClick()) return;
    if (options.canPaste && !options.canPaste()) { options.onPasteUnavailable?.(); return; }
    if (!navigator.clipboard?.readText || (options.canReadClipboard && !options.canReadClipboard())) { options.onPasteUnavailable?.(); return; }
    // Permission is known to be usable, so suppress the menu before the async read.
    event.preventDefault();
    event.stopPropagation();
    void navigator.clipboard.readText().then((text) => {
      if (!text) return;
      terminal.paste(text);
      terminal.focus();
    }).catch((error) => options.onPasteError?.(error));
  };
  element.addEventListener("pointerup", copySelection);
  element.addEventListener("contextmenu", pasteClipboard, true);
  return () => {
    selectionDisposable.dispose();
    element.removeEventListener("pointerup", copySelection);
    element.removeEventListener("contextmenu", pasteClipboard, true);
  };
}
