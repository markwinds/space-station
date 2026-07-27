import type { Ref } from "vue";
import { ref } from "vue";
import {
  deleteSharedCommandSnippet,
  fetchSharedCommandSnippets,
  saveSharedCommandSnippet,
  type SharedCommandSnippet,
} from "@/api";

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
const librarySnippets = ref<SharedCommandSnippet[]>([]);
const libraryLoading = ref(false);
const libraryLoaded = ref(false);

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

export function useSharedCommandSnippetLibrary() {
  async function refresh(force = false) {
    if (libraryLoading.value || (libraryLoaded.value && !force)) return;
    libraryLoading.value = true;
    try {
      librarySnippets.value = await fetchSharedCommandSnippets();
      libraryLoaded.value = true;
    } finally {
      libraryLoading.value = false;
    }
  }

  async function store(snippet: CommandSnippet) {
    const saved = await saveSharedCommandSnippet({
      id: snippet.id,
      name: snippet.name,
      command: snippet.command,
      action: snippet.action,
    });
    const index = librarySnippets.value.findIndex((item) => item.id === saved.id);
    if (index < 0) librarySnippets.value.push(saved);
    else librarySnippets.value[index] = saved;
    libraryLoaded.value = true;
    return saved;
  }

  async function remove(id: string) {
    await deleteSharedCommandSnippet(id);
    librarySnippets.value = librarySnippets.value.filter((item) => item.id !== id);
  }

  return {
    librarySnippets,
    libraryLoading,
    refreshSharedSnippets: refresh,
    storeSharedSnippet: store,
    removeSharedSnippet: remove,
  };
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

export function useCommandSnippetReorder(snippets: Ref<CommandSnippet[]>, rowSelector: string) {
  const draggedSnippetId = ref("");
  const snippetDropTargetId = ref("");
  const snippetDropPosition = ref<"before" | "after">("before");

  function finish() {
    draggedSnippetId.value = "";
    snippetDropTargetId.value = "";
  }

  function start(event: PointerEvent, id: string) {
    if (event.button !== 0) return;
    draggedSnippetId.value = id;
    snippetDropTargetId.value = "";
    (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
    event.preventDefault();
  }

  function update(event: PointerEvent) {
    if (!draggedSnippetId.value) return;
    const rows = Array.from(document.querySelectorAll<HTMLElement>(rowSelector))
      .filter((row) => row.dataset.snippetId && row.dataset.snippetId !== draggedSnippetId.value);
    if (!rows.length) return;

    const row = rows.find((candidate) => {
      const bounds = candidate.getBoundingClientRect();
      return event.clientY < bounds.top + bounds.height / 2;
    }) ?? rows.at(-1)!;
    const bounds = row.getBoundingClientRect();
    snippetDropTargetId.value = row.dataset.snippetId!;
    snippetDropPosition.value = event.clientY < bounds.top + bounds.height / 2 ? "before" : "after";
    event.preventDefault();
  }

  function drop(event: PointerEvent) {
    if (!draggedSnippetId.value) return;
    const handle = event.currentTarget as HTMLElement;
    if (handle.hasPointerCapture(event.pointerId)) handle.releasePointerCapture(event.pointerId);
    if (snippetDropTargetId.value && reorderCommandSnippet(
      snippets.value,
      draggedSnippetId.value,
      snippetDropTargetId.value,
      snippetDropPosition.value === "after",
    )) persistCommandSnippets(snippets.value);
    finish();
    event.preventDefault();
  }

  function cancel(event: PointerEvent) {
    const handle = event.currentTarget as HTMLElement;
    if (handle.hasPointerCapture(event.pointerId)) handle.releasePointerCapture(event.pointerId);
    finish();
  }

  return {
    draggedSnippetId,
    snippetDropTargetId,
    snippetDropPosition,
    startSnippetPointerDrag: start,
    updateSnippetPointerDrag: update,
    dropSnippetPointerDrag: drop,
    cancelSnippetPointerDrag: cancel,
  };
}
