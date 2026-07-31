import type { CSSProperties, Ref } from "vue";
import { computed, ref, watch } from "vue";

export type TerminalSplitDirection = "columns" | "rows";
export type TerminalSplitAction = TerminalSplitDirection | "close";

interface PersistedTerminalSplit {
  enabled?: boolean;
  direction?: TerminalSplitDirection;
  ratio?: number;
}

function loadPersistedSplit(key: string): Required<PersistedTerminalSplit> {
  try {
    const value = JSON.parse(localStorage.getItem(key) || "{}") as PersistedTerminalSplit;
    return {
      enabled: value.enabled === true,
      direction: value.direction === "rows" ? "rows" : "columns",
      ratio: Math.min(82, Math.max(18, Number(value.ratio) || 50)),
    };
  } catch {
    return { enabled: false, direction: "columns", ratio: 50 };
  }
}

export function useTerminalSplit(activeId: Ref<string>, availableIds: () => string[], storageKey: string) {
  const persisted = loadPersistedSplit(storageKey);
  const splitPreferred = ref(persisted.enabled);
  const direction = ref<TerminalSplitDirection>(persisted.direction);
  const ratio = ref(persisted.ratio);
  const paneIds = ref<string[]>([]);
  const focusedPaneIndex = ref(0);
  const containerRef = ref<HTMLElement | null>(null);
  const resizing = ref(false);
  let resizePointerId: number | null = null;

  const isSplit = computed(() => paneIds.value.length === 2);
  const focusedPaneId = computed(() => isSplit.value ? paneIds.value[focusedPaneIndex.value] ?? "" : activeId.value);
  const directionLabel = computed(() => direction.value === "columns" ? "左右分屏" : "上下分屏");
  const gridStyle = computed<CSSProperties>(() => {
    if (!isSplit.value) return {};
    if (direction.value === "columns") {
      return {
        gridTemplateColumns: `minmax(0, ${ratio.value}fr) 6px minmax(0, ${100 - ratio.value}fr)`,
        gridTemplateRows: "minmax(0, 1fr)",
      };
    }
    return {
      gridTemplateColumns: "minmax(0, 1fr)",
      gridTemplateRows: `minmax(0, ${ratio.value}fr) 6px minmax(0, ${100 - ratio.value}fr)`,
    };
  });
  const dividerStyle = computed<CSSProperties>(() => direction.value === "columns"
    ? { gridArea: "1 / 2 / 2 / 3" }
    : { gridArea: "2 / 1 / 3 / 2" });

  function persist() {
    localStorage.setItem(storageKey, JSON.stringify({
      enabled: splitPreferred.value,
      direction: direction.value,
      ratio: ratio.value,
    }));
  }

  function createPanes() {
    const ids = availableIds();
    if (ids.length < 2) return false;
    const primary = ids.includes(activeId.value) ? activeId.value : ids[0];
    const secondary = ids.find((id) => id !== primary);
    if (!primary || !secondary) return false;
    paneIds.value = [primary, secondary];
    focusedPaneIndex.value = 0;
    activeId.value = primary;
    return true;
  }

  function split(nextDirection: TerminalSplitDirection) {
    direction.value = nextDirection;
    splitPreferred.value = true;
    if (!isSplit.value) createPanes();
    persist();
  }

  function closeSplit() {
    const retainedId = focusedPaneId.value || activeId.value;
    paneIds.value = [];
    focusedPaneIndex.value = 0;
    splitPreferred.value = false;
    if (retainedId) activeId.value = retainedId;
    persist();
  }

  function focusPane(id: string) {
    if (!isSplit.value) {
      activeId.value = id;
      return;
    }
    const index = paneIds.value.indexOf(id);
    if (index < 0) return;
    focusedPaneIndex.value = index;
    activeId.value = id;
  }

  function isPaneVisible(id: string) {
    return isSplit.value ? paneIds.value.includes(id) : activeId.value === id;
  }

  function isPaneFocused(id: string) {
    return activeId.value === id;
  }

  function paneStyle(id: string): CSSProperties {
    if (!isSplit.value) return {};
    const index = paneIds.value.indexOf(id);
    if (direction.value === "columns") {
      return index === 0 ? { gridArea: "1 / 1 / 2 / 2" } : { gridArea: "1 / 3 / 2 / 4" };
    }
    return index === 0 ? { gridArea: "1 / 1 / 2 / 2" } : { gridArea: "3 / 1 / 4 / 2" };
  }

  function updateRatio(event: PointerEvent) {
    const rect = containerRef.value?.getBoundingClientRect();
    if (!rect) return;
    const raw = direction.value === "columns"
      ? ((event.clientX - rect.left) / rect.width) * 100
      : ((event.clientY - rect.top) / rect.height) * 100;
    ratio.value = Math.round(Math.min(82, Math.max(18, raw)) * 10) / 10;
  }

  function startResize(event: PointerEvent) {
    if (!isSplit.value) return;
    event.preventDefault();
    resizePointerId = event.pointerId;
    resizing.value = true;
    (event.currentTarget as HTMLElement | null)?.setPointerCapture?.(event.pointerId);
    updateRatio(event);
  }

  function resize(event: PointerEvent) {
    if (!resizing.value || resizePointerId !== event.pointerId) return;
    updateRatio(event);
  }

  function stopResize(event: PointerEvent) {
    if (resizePointerId !== event.pointerId) return;
    if (event.type !== "pointercancel") updateRatio(event);
    try {
      (event.currentTarget as HTMLElement | null)?.releasePointerCapture?.(event.pointerId);
    } catch {
      // Pointer capture may already be released when the window loses focus.
    }
    resizePointerId = null;
    resizing.value = false;
    persist();
  }

  watch(
    availableIds,
    (ids) => {
      if (!splitPreferred.value) return;
      if (ids.length < 2) {
        paneIds.value = [];
        focusedPaneIndex.value = 0;
        return;
      }
      if (!isSplit.value) {
        createPanes();
        return;
      }
      const nextPanes = paneIds.value.filter((id) => ids.includes(id));
      for (const id of ids) {
        if (nextPanes.length >= 2) break;
        if (!nextPanes.includes(id)) nextPanes.push(id);
      }
      paneIds.value = nextPanes.slice(0, 2);
      const activeIndex = paneIds.value.indexOf(activeId.value);
      focusedPaneIndex.value = activeIndex >= 0 ? activeIndex : Math.min(focusedPaneIndex.value, 1);
      if (!paneIds.value.includes(activeId.value)) activeId.value = paneIds.value[focusedPaneIndex.value] ?? "";
    },
    { flush: "sync" },
  );

  watch(
    activeId,
    (id) => {
      if (!isSplit.value || !id) return;
      const existingIndex = paneIds.value.indexOf(id);
      if (existingIndex >= 0) {
        focusedPaneIndex.value = existingIndex;
        return;
      }
      if (!availableIds().includes(id)) return;
      const next = paneIds.value.slice();
      next[focusedPaneIndex.value] = id;
      paneIds.value = next;
    },
    { flush: "sync" },
  );

  return {
    containerRef,
    direction,
    directionLabel,
    dividerStyle,
    gridStyle,
    isSplit,
    paneIds,
    resizing,
    split,
    closeSplit,
    focusPane,
    isPaneVisible,
    isPaneFocused,
    paneStyle,
    startResize,
    resize,
    stopResize,
  };
}
