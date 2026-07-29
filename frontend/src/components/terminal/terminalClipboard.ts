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
  const focusBeforeNativeContextMenu = (event: PointerEvent) => {
    if (event.button !== 2 || !options.pasteOnRightClick()) return;
    if (options.canPaste && !options.canPaste()) return;
    terminal.focus();
  };
  const copySelection = (event: MouseEvent) => {
    if (event.button !== 0 || !options.copyOnSelect()) return;
    const selection = terminal.getSelection() || latestSelection;
    if (!selection) return;
    if (!navigator.clipboard?.writeText) { options.onCopyError?.(); return; }
    void navigator.clipboard.writeText(selection).catch(() => options.onCopyError?.());
  };
  const beginMouseSelection = (event: MouseEvent) => {
    if (event.button !== 0) return;
    document.removeEventListener("mouseup", copySelection);
    // xterm registers its document-level mouseup listener from its own earlier
    // mousedown handler. Registering here puts copying after selection commit.
    document.addEventListener("mouseup", copySelection, { once: true });
  };
  const pasteClipboard = (event: MouseEvent) => {
    if (!options.pasteOnRightClick()) return;
    if (options.canPaste && !options.canPaste()) { options.onPasteUnavailable?.(); return; }
    if (!navigator.clipboard?.readText || (options.canReadClipboard && !options.canReadClipboard())) {
      options.onPasteUnavailable?.();
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    void navigator.clipboard.readText().then((text) => {
      if (!text) return;
      terminal.paste(text);
      terminal.focus();
    }).catch((error) => options.onPasteError?.(error));
  };
  element.addEventListener("mousedown", beginMouseSelection);
  element.addEventListener("pointerdown", focusBeforeNativeContextMenu, true);
  element.addEventListener("contextmenu", pasteClipboard, true);
  return () => {
    selectionDisposable.dispose();
    element.removeEventListener("mousedown", beginMouseSelection);
    document.removeEventListener("mouseup", copySelection);
    element.removeEventListener("pointerdown", focusBeforeNativeContextMenu, true);
    element.removeEventListener("contextmenu", pasteClipboard, true);
  };
}
