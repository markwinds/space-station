import type { Ref } from "vue";
import { ref } from "vue";

export type SnippetAction = "insert" | "run";

export interface CommandSnippet {
  id: string;
  name: string;
  command: string;
  pinned?: boolean;
  action: SnippetAction;
}

const sharedStorageKey = "space-station:command-snippets";
const legacyStorageKeys = ["ssh-command-snippets", "serial-command-snippets"];

function normalizeSnippets(raw: unknown): CommandSnippet[] {
  if (!Array.isArray(raw)) return [];
  return raw
    .filter((item): item is Record<string, unknown> => Boolean(item) && typeof item === "object")
    .filter((item) => typeof item.id === "string" && typeof item.name === "string" && typeof item.command === "string")
    .map((item) => ({
      id: item.id as string,
      name: item.name as string,
      command: item.command as string,
      pinned: item.pinned !== false,
      // Old entries without an action ran immediately in both terminal tools.
      action: item.action === "insert" ? "insert" : "run",
    }));
}

function parseStoredSnippets(key: string): CommandSnippet[] {
  try {
    return normalizeSnippets(JSON.parse(localStorage.getItem(key) || "[]"));
  } catch {
    return [];
  }
}

function loadSharedSnippets(): CommandSnippet[] {
  if (localStorage.getItem(sharedStorageKey) !== null) return parseStoredSnippets(sharedStorageKey);

  const merged: CommandSnippet[] = [];
  const usedIds = new Set<string>();
  for (const key of legacyStorageKeys) {
    for (const snippet of parseStoredSnippets(key)) {
      let id = snippet.id;
      if (usedIds.has(id)) id = crypto.randomUUID();
      usedIds.add(id);
      merged.push({ ...snippet, id });
    }
  }
  localStorage.setItem(sharedStorageKey, JSON.stringify(merged));
  return merged;
}

const sharedSnippets = ref<CommandSnippet[]>(loadSharedSnippets());

window.addEventListener("storage", (event) => {
  if (event.key !== sharedStorageKey) return;
  try {
    sharedSnippets.value = normalizeSnippets(JSON.parse(event.newValue || "[]"));
  } catch {
    sharedSnippets.value = [];
  }
});

export function useCommandSnippets(): Ref<CommandSnippet[]> {
  return sharedSnippets;
}

export function persistCommandSnippets(snippets: CommandSnippet[]) {
  localStorage.setItem(sharedStorageKey, JSON.stringify(snippets));
}

export function reorderCommandSnippet(snippets: CommandSnippet[], sourceId: string, targetId: string, placeAfter: boolean): boolean {
  const sourceIndex = snippets.findIndex((item) => item.id === sourceId);
  if (sourceIndex < 0 || sourceId === targetId) return false;

  const [source] = snippets.splice(sourceIndex, 1);
  const targetIndex = snippets.findIndex((item) => item.id === targetId);
  if (targetIndex < 0) {
    snippets.splice(sourceIndex, 0, source);
    return false;
  }
  snippets.splice(targetIndex + (placeAfter ? 1 : 0), 0, source);
  return true;
}
