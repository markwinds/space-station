export async function writeClipboard(text: string): Promise<boolean> {
  // Try the synchronous path first while a click/pointer event still carries
  // transient user activation. This makes the first copy reliable in browsers
  // that reject the initial async Clipboard API request.
  if (fallbackWriteClipboard(text)) return true;

  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
      return true;
    }
  } catch {
    // Some mobile browsers expose clipboard but reject it outside secure contexts.
  }

  return false;
}

function fallbackWriteClipboard(text: string): boolean {
  const textarea = document.createElement("textarea");
  textarea.value = text;
  textarea.setAttribute("readonly", "");
  textarea.style.position = "fixed";
  textarea.style.top = "0";
  textarea.style.left = "0";
  textarea.style.width = "1px";
  textarea.style.height = "1px";
  textarea.style.opacity = "0";
  textarea.style.pointerEvents = "none";

  document.body.appendChild(textarea);
  textarea.focus({ preventScroll: true });
  textarea.select();
  textarea.setSelectionRange(0, textarea.value.length);

  try {
    return document.execCommand("copy");
  } catch {
    return false;
  } finally {
    textarea.remove();
  }
}
