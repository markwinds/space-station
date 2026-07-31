import type { CSSProperties, Ref } from "vue";
import { computed, ref, watch } from "vue";

export type TerminalSplitDirection = "columns" | "rows";
export type TerminalSplitAction = TerminalSplitDirection | "close-pane" | "close-all";

interface PersistedTerminalSplit {
  direction?: TerminalSplitDirection;
  ratio?: number;
}

interface TerminalSplitLeaf {
  type: "leaf";
  paneId: string;
}

interface TerminalSplitBranch {
  type: "split";
  id: string;
  direction: TerminalSplitDirection;
  ratio: number;
  first: TerminalSplitNode;
  second: TerminalSplitNode;
}

type TerminalSplitNode = TerminalSplitLeaf | TerminalSplitBranch;

interface LayoutRect {
  left: number;
  top: number;
  width: number;
  height: number;
}

export interface TerminalSplitDivider {
  id: string;
  direction: TerminalSplitDirection;
  style: CSSProperties;
}

const PANE_COLORS = ["#56b8c8", "#d5a44d", "#b58bdd", "#72bd7f", "#d77b79", "#6d9fdf", "#d58e57", "#66b8a7"];
const MIN_RATIO = 18;
const MAX_RATIO = 82;
const DIVIDER_SIZE = 6;
const EDGE_EPSILON = 0.0001;
let nextBranchId = 0;

function loadPersistedSplit(key: string): Required<PersistedTerminalSplit> {
  try {
    const value = JSON.parse(localStorage.getItem(key) || "{}") as PersistedTerminalSplit;
    return {
      direction: value.direction === "rows" ? "rows" : "columns",
      ratio: Math.min(MAX_RATIO, Math.max(MIN_RATIO, Number(value.ratio) || 50)),
    };
  } catch {
    return { direction: "columns", ratio: 50 };
  }
}

function leaf(paneId: string): TerminalSplitLeaf {
  return { type: "leaf", paneId };
}

function branch(direction: TerminalSplitDirection, first: TerminalSplitNode, second: TerminalSplitNode, ratio = 50): TerminalSplitBranch {
  nextBranchId += 1;
  return { type: "split", id: `terminal-split-${nextBranchId}`, direction, ratio, first, second };
}

function collectPaneIds(node: TerminalSplitNode | null, result: string[] = []): string[] {
  if (!node) return result;
  if (node.type === "leaf") result.push(node.paneId);
  else {
    collectPaneIds(node.first, result);
    collectPaneIds(node.second, result);
  }
  return result;
}

function firstPaneId(node: TerminalSplitNode): string {
  return node.type === "leaf" ? node.paneId : firstPaneId(node.first);
}

function replaceLeaf(node: TerminalSplitNode, paneId: string, replacement: TerminalSplitNode): TerminalSplitNode {
  if (node.type === "leaf") return node.paneId === paneId ? replacement : node;
  return {
    ...node,
    first: replaceLeaf(node.first, paneId, replacement),
    second: replaceLeaf(node.second, paneId, replacement),
  };
}

function swapLeafPaneIds(node: TerminalSplitNode, firstId: string, secondId: string): TerminalSplitNode {
  if (node.type === "leaf") {
    if (node.paneId === firstId) return leaf(secondId);
    if (node.paneId === secondId) return leaf(firstId);
    return node;
  }
  return {
    ...node,
    first: swapLeafPaneIds(node.first, firstId, secondId),
    second: swapLeafPaneIds(node.second, firstId, secondId),
  };
}

function updateBranchRatio(node: TerminalSplitNode, branchId: string, ratio: number): TerminalSplitNode {
  if (node.type === "leaf") return node;
  if (node.id === branchId) return { ...node, ratio };
  return {
    ...node,
    first: updateBranchRatio(node.first, branchId, ratio),
    second: updateBranchRatio(node.second, branchId, ratio),
  };
}

function pruneUnavailable(node: TerminalSplitNode | null, available: Set<string>): TerminalSplitNode | null {
  if (!node) return null;
  if (node.type === "leaf") return available.has(node.paneId) ? node : null;
  const first = pruneUnavailable(node.first, available);
  const second = pruneUnavailable(node.second, available);
  if (!first) return second;
  if (!second) return first;
  return { ...node, first, second };
}

function removePane(node: TerminalSplitNode, paneId: string): { node: TerminalSplitNode | null; focusId: string } {
  if (node.type === "leaf") return node.paneId === paneId ? { node: null, focusId: "" } : { node, focusId: node.paneId };
  if (node.first.type === "leaf" && node.first.paneId === paneId) return { node: node.second, focusId: firstPaneId(node.second) };
  if (node.second.type === "leaf" && node.second.paneId === paneId) return { node: node.first, focusId: firstPaneId(node.first) };

  const firstResult = removePane(node.first, paneId);
  if (!firstResult.node) return { node: node.second, focusId: firstResult.focusId || firstPaneId(node.second) };
  if (firstResult.node !== node.first) return { node: { ...node, first: firstResult.node }, focusId: firstResult.focusId };

  const secondResult = removePane(node.second, paneId);
  if (!secondResult.node) return { node: node.first, focusId: secondResult.focusId || firstPaneId(node.first) };
  if (secondResult.node !== node.second) return { node: { ...node, second: secondResult.node }, focusId: secondResult.focusId };
  return { node, focusId: paneId };
}

function percent(value: number) {
  return Math.round(value * 10000) / 10000;
}

function insetPosition(value: number, inset: number) {
  return inset ? `calc(${percent(value)}% + ${inset}px)` : `${percent(value)}%`;
}

function insetSize(value: number, inset: number) {
  return inset ? `calc(${percent(value)}% - ${inset}px)` : `${percent(value)}%`;
}

function paneColor(index: number) {
  if (index < PANE_COLORS.length) return PANE_COLORS[index];
  return `hsl(${percent((index * 137.508 + 188) % 360)} 58% 64%)`;
}

export function useTerminalSplit(activeId: Ref<string>, availableIds: () => string[], storageKey: string) {
  const persisted = loadPersistedSplit(storageKey);
  const preferredDirection = ref<TerminalSplitDirection>(persisted.direction);
  const preferredRatio = ref(persisted.ratio);
  const layout = ref<TerminalSplitNode | null>(null);
  const containerRef = ref<HTMLElement | null>(null);
  const resizing = ref(false);
  const resizingBranchId = ref("");
  let resizePointerId: number | null = null;
  let resizeRect: LayoutRect | null = null;
  let resizeDirection: TerminalSplitDirection = "columns";

  const paneIds = computed(() => collectPaneIds(layout.value));
  const isSplit = computed(() => paneIds.value.length > 1);
  const canSplit = computed(() => availableIds().some((id) => !paneIds.value.includes(id)));
  const splitLabel = computed(() => isSplit.value ? `${paneIds.value.length} 分屏` : "分屏");

  const layoutData = computed(() => {
    const panes = new Map<string, CSSProperties>();
    const dividers: TerminalSplitDivider[] = [];
    const walk = (node: TerminalSplitNode, rect: LayoutRect) => {
      if (node.type === "leaf") {
        const leftInset = rect.left > EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0;
        const topInset = rect.top > EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0;
        const rightInset = rect.left + rect.width < 100 - EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0;
        const bottomInset = rect.top + rect.height < 100 - EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0;
        const colorIndex = Math.max(0, paneIds.value.indexOf(node.paneId));
        panes.set(node.paneId, {
          position: "absolute",
          left: insetPosition(rect.left, leftInset),
          top: insetPosition(rect.top, topInset),
          width: insetSize(rect.width, leftInset + rightInset),
          height: insetSize(rect.height, topInset + bottomInset),
          "--terminal-pane-color": paneColor(colorIndex),
        } as CSSProperties);
        return;
      }

      const fraction = node.ratio / 100;
      if (node.direction === "columns") {
        const firstWidth = rect.width * fraction;
        const splitLeft = rect.left + firstWidth;
        dividers.push({
          id: node.id,
          direction: node.direction,
          style: {
            left: `calc(${percent(splitLeft)}% - ${DIVIDER_SIZE / 2}px)`,
            top: insetPosition(rect.top, rect.top > EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0),
            width: `${DIVIDER_SIZE}px`,
            height: insetSize(rect.height, (rect.top > EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0) + (rect.top + rect.height < 100 - EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0)),
          },
        });
        walk(node.first, { ...rect, width: firstWidth });
        walk(node.second, { left: splitLeft, top: rect.top, width: rect.width - firstWidth, height: rect.height });
      } else {
        const firstHeight = rect.height * fraction;
        const splitTop = rect.top + firstHeight;
        dividers.push({
          id: node.id,
          direction: node.direction,
          style: {
            left: insetPosition(rect.left, rect.left > EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0),
            top: `calc(${percent(splitTop)}% - ${DIVIDER_SIZE / 2}px)`,
            width: insetSize(rect.width, (rect.left > EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0) + (rect.left + rect.width < 100 - EDGE_EPSILON ? DIVIDER_SIZE / 2 : 0)),
            height: `${DIVIDER_SIZE}px`,
          },
        });
        walk(node.first, { ...rect, height: firstHeight });
        walk(node.second, { left: rect.left, top: splitTop, width: rect.width, height: rect.height - firstHeight });
      }
    };
    if (layout.value) walk(layout.value, { left: 0, top: 0, width: 100, height: 100 });
    return { panes, dividers };
  });

  const dividers = computed(() => layoutData.value.dividers);

  function persist() {
    localStorage.setItem(storageKey, JSON.stringify({
      direction: preferredDirection.value,
      ratio: preferredRatio.value,
    }));
  }

  function createInitialLayout() {
    const ids = availableIds();
    if (!ids.length) {
      layout.value = null;
      return false;
    }
    const primary = ids.includes(activeId.value) ? activeId.value : ids[0];
    if (!primary) return false;
    layout.value = leaf(primary);
    activeId.value = primary;
    return true;
  }

  function split(direction: TerminalSplitDirection) {
    preferredDirection.value = direction;
    const ids = availableIds();
    if (!layout.value) createInitialLayout();
    if (!layout.value) return false;
    const visibleIds = collectPaneIds(layout.value);
    const targetId = visibleIds.includes(activeId.value) ? activeId.value : visibleIds[0];
    const nextId = ids.find((id) => !visibleIds.includes(id));
    if (!targetId || !nextId) return false;
    layout.value = replaceLeaf(layout.value, targetId, branch(direction, leaf(targetId), leaf(nextId), preferredRatio.value));
    activeId.value = targetId;
    persist();
    return true;
  }

  function closeFocusedPane() {
    if (!layout.value || !isSplit.value) return;
    const result = removePane(layout.value, activeId.value);
    if (!result.node) return;
    layout.value = result.node;
    activeId.value = result.focusId || firstPaneId(result.node);
    persist();
  }

  function closeSplit() {
    const retainedId = paneIds.value.includes(activeId.value) ? activeId.value : paneIds.value[0];
    layout.value = retainedId ? leaf(retainedId) : null;
    if (retainedId) activeId.value = retainedId;
    persist();
  }

  function focusPane(id: string) {
    if (!paneIds.value.includes(id)) return;
    activeId.value = id;
  }

  function movePaneTo(sourceId: string, targetId: string) {
    if (!layout.value || sourceId === targetId || !availableIds().includes(sourceId)) return false;
    const visibleIds = collectPaneIds(layout.value);
    if (!visibleIds.includes(targetId)) return false;
    layout.value = visibleIds.includes(sourceId)
      ? swapLeafPaneIds(layout.value, sourceId, targetId)
      : replaceLeaf(layout.value, targetId, leaf(sourceId));
    activeId.value = sourceId;
    return true;
  }

  function isPaneVisible(id: string) {
    return paneIds.value.includes(id);
  }

  function isPaneFocused(id: string) {
    return activeId.value === id;
  }

  function paneStyle(id: string): CSSProperties {
    return layoutData.value.panes.get(id) || {};
  }

  function tabStyle(id: string): CSSProperties {
    const index = paneIds.value.indexOf(id);
    if (!isSplit.value || index < 0) return {};
    return { "--terminal-pane-color": paneColor(index) } as CSSProperties;
  }

  function findBranchRect(branchId: string): { node: TerminalSplitBranch; rect: LayoutRect } | null {
    let result: { node: TerminalSplitBranch; rect: LayoutRect } | null = null;
    const walk = (node: TerminalSplitNode, rect: LayoutRect) => {
      if (node.type === "leaf" || result) return;
      if (node.id === branchId) {
        result = { node, rect };
        return;
      }
      const fraction = node.ratio / 100;
      if (node.direction === "columns") {
        const firstWidth = rect.width * fraction;
        walk(node.first, { ...rect, width: firstWidth });
        walk(node.second, { left: rect.left + firstWidth, top: rect.top, width: rect.width - firstWidth, height: rect.height });
      } else {
        const firstHeight = rect.height * fraction;
        walk(node.first, { ...rect, height: firstHeight });
        walk(node.second, { left: rect.left, top: rect.top + firstHeight, width: rect.width, height: rect.height - firstHeight });
      }
    };
    if (layout.value) walk(layout.value, { left: 0, top: 0, width: 100, height: 100 });
    return result;
  }

  function updateRatio(event: PointerEvent) {
    const bounds = containerRef.value?.getBoundingClientRect();
    if (!bounds || !resizeRect || !resizingBranchId.value || !layout.value) return;
    const globalPosition = resizeDirection === "columns"
      ? ((event.clientX - bounds.left) / bounds.width) * 100
      : ((event.clientY - bounds.top) / bounds.height) * 100;
    const origin = resizeDirection === "columns" ? resizeRect.left : resizeRect.top;
    const size = resizeDirection === "columns" ? resizeRect.width : resizeRect.height;
    const ratio = Math.round(Math.min(MAX_RATIO, Math.max(MIN_RATIO, ((globalPosition - origin) / size) * 100)) * 10) / 10;
    preferredRatio.value = ratio;
    layout.value = updateBranchRatio(layout.value, resizingBranchId.value, ratio);
  }

  function startResize(event: PointerEvent, branchId: string) {
    const branchData = findBranchRect(branchId);
    if (!branchData) return;
    event.preventDefault();
    resizePointerId = event.pointerId;
    resizingBranchId.value = branchId;
    resizeRect = branchData.rect;
    resizeDirection = branchData.node.direction;
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
    resizeRect = null;
    resizingBranchId.value = "";
    resizing.value = false;
    persist();
  }

  watch(
    availableIds,
    (ids) => {
      if (!ids.length) {
        layout.value = null;
        activeId.value = "";
        return;
      }
      layout.value = pruneUnavailable(layout.value, new Set(ids));
      if (!layout.value) {
        createInitialLayout();
        return;
      }
      const nextVisibleIds = collectPaneIds(layout.value);
      if (!nextVisibleIds.includes(activeId.value)) activeId.value = nextVisibleIds[0] || ids[0];
    },
    { flush: "sync" },
  );

  watch(
    activeId,
    (id, previousId) => {
      if (!id || !availableIds().includes(id)) return;
      if (!layout.value) {
        layout.value = leaf(id);
        return;
      }
      const visibleIds = collectPaneIds(layout.value);
      if (visibleIds.includes(id)) return;
      const replacedId = visibleIds.includes(previousId) ? previousId : visibleIds[0];
      if (replacedId) layout.value = replaceLeaf(layout.value, replacedId, leaf(id));
    },
    { flush: "sync" },
  );

  return {
    canSplit,
    containerRef,
    dividers,
    isSplit,
    paneIds,
    resizing,
    resizingBranchId,
    splitLabel,
    split,
    closeFocusedPane,
    closeSplit,
    focusPane,
    movePaneTo,
    isPaneVisible,
    isPaneFocused,
    paneStyle,
    tabStyle,
    startResize,
    resize,
    stopResize,
  };
}
