import { tools, type ToolId } from "@/tools";

const FAVORITES_KEY = "space-station:favorites";
const RECENTS_KEY = "space-station:recent-tools";
const MAX_RECENT_TOOLS = 5;

export interface RecentToolEntry {
  id: ToolId;
  lastUsedAt: string;
}

const knownToolIds = new Set(tools.map((tool) => tool.id));

export function loadFavoriteToolIds(): ToolId[] {
  return readJson(FAVORITES_KEY, [])
    .filter((id): id is ToolId => typeof id === "string" && knownToolIds.has(id as ToolId));
}

export function saveFavoriteToolIds(ids: ToolId[]) {
  localStorage.setItem(FAVORITES_KEY, JSON.stringify([...new Set(ids)]));
}

export function loadRecentTools(): RecentToolEntry[] {
  return readJson(RECENTS_KEY, [])
    .filter((entry): entry is RecentToolEntry => Boolean(
      entry
      && typeof entry === "object"
      && typeof entry.id === "string"
      && knownToolIds.has(entry.id as ToolId)
      && typeof entry.lastUsedAt === "string",
    ))
    .sort((left, right) => right.lastUsedAt.localeCompare(left.lastUsedAt))
    .slice(0, MAX_RECENT_TOOLS);
}

export function recordRecentTool(id: ToolId) {
  const recent = loadRecentTools().filter((entry) => entry.id !== id);
  recent.unshift({ id, lastUsedAt: new Date().toISOString() });
  localStorage.setItem(RECENTS_KEY, JSON.stringify(recent.slice(0, MAX_RECENT_TOOLS)));
}

function readJson(key: string, fallback: unknown[]) {
  try {
    const value = JSON.parse(localStorage.getItem(key) ?? "null");
    return Array.isArray(value) ? value : fallback;
  } catch {
    return fallback;
  }
}
