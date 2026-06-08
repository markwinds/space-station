export async function writeClipboard(text: string): Promise<boolean> {
  if (!navigator.clipboard) {
    return false;
  }
  await navigator.clipboard.writeText(text);
  return true;
}
