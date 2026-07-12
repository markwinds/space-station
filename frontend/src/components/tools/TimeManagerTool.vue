<template>
  <div class="time-manager">
    <section
      v-show="viewMode === 'map'"
      class="tm-canvas-shell"
      @pointerdown.capture="recordCanvasPointer"
      @pointerup.capture="clearSelectionOnBlankTap"
    >
      <div ref="mindContainerRef" class="tm-mind-elixir"></div>
    </section>

    <section v-if="viewMode === 'calendar'" class="tm-calendar-surface">
      <div class="tm-calendar-toolbar">
        <n-radio-group v-model:value="calendarMode" size="small">
          <n-radio-button value="month">月</n-radio-button>
          <n-radio-button value="week">周</n-radio-button>
          <n-radio-button value="day">日</n-radio-button>
        </n-radio-group>
        <n-button-group>
          <n-button tertiary @click="shiftCalendar(-1)">
            <template #icon><n-icon><ChevronBackOutline /></n-icon></template>
          </n-button>
          <n-button tertiary @click="calendarCursor = startOfDay(new Date())">今天</n-button>
          <n-button tertiary @click="shiftCalendar(1)">
            <template #icon><n-icon><ChevronForwardOutline /></n-icon></template>
          </n-button>
        </n-button-group>
        <strong>{{ calendarTitle }}</strong>
      </div>
      <div class="tm-calendar-grid" :class="`mode-${calendarMode}`">
        <article v-for="day in calendarDays" :key="day.key" class="tm-day-cell" :class="{ today: day.isToday }">
          <header>
            <span>{{ day.label }}</span>
            <small>{{ day.tasks.length }}</small>
          </header>
          <button
            v-for="task in day.tasks"
            :key="task.id"
            class="tm-calendar-task"
            :class="{ done: task.completed }"
            @click="openTask(task.id)"
          >
            <time>{{ formatTime(task.scheduledAt) }}</time>
            <span>{{ task.title }}</span>
          </button>
        </article>
      </div>
    </section>

    <header class="tm-floating-top">
      <div class="tm-toolbar">
        <n-input v-model:value="keyword" clearable placeholder="查找" class="tm-search">
          <template #prefix><n-icon><SearchOutline /></n-icon></template>
        </n-input>
        <n-button tertiary circle title="撤销" @click="undo">
          <template #icon><n-icon><ArrowUndoOutline /></n-icon></template>
        </n-button>
        <n-button tertiary circle title="重做" @click="redo">
          <template #icon><n-icon><ArrowRedoOutline /></n-icon></template>
        </n-button>
        <n-button tertiary circle title="居中" @click="centerMind">
          <template #icon><n-icon><LocateOutline /></n-icon></template>
        </n-button>
        <n-button tertiary circle title="过滤器" @click="togglePanel('filters')">
          <template #icon><n-icon><FunnelOutline /></n-icon></template>
        </n-button>
        <n-button
          tertiary
          circle
          :type="viewMode === 'calendar' ? 'primary' : 'default'"
          :title="viewMode === 'calendar' ? '返回脑图' : '日历'"
          @click="toggleCalendarView"
        >
          <template #icon><n-icon><CalendarOutline /></n-icon></template>
        </n-button>
      </div>
    </header>

    <div v-if="viewMode === 'map'" class="tm-floating-bottom" @pointerdown.stop @pointerup.stop @click.stop>
      <n-button v-if="!selectedTaskId" size="small" tertiary @click="addTask(null)">
        <template #icon><n-icon><AddOutline /></n-icon></template>
        新任务
      </n-button>
      <n-button v-if="selectedTaskId" size="small" tertiary @click="addTask(selectedTaskId)">
        <template #icon><n-icon><AddOutline /></n-icon></template>
        子任务
      </n-button>
      <n-button
        v-if="selectedTask"
        size="small"
        tertiary
        :title="selectedTask.completed ? '标记未完成' : '标记完成'"
        @click="toggleSelectedTaskCompleted"
      >
        <template #icon>
          <n-icon>
            <CheckmarkCircleOutline v-if="selectedTask.completed" />
            <EllipseOutline v-else />
          </n-icon>
        </template>
      </n-button>
      <n-button v-if="selectedTaskId" size="small" tertiary @click="activePanel = 'editor'">
        <template #icon><n-icon><CreateOutline /></n-icon></template>
        详情
      </n-button>
      <n-button v-if="selectedTaskId" size="small" tertiary type="error" @click="deleteTask(selectedTaskId)">
        <template #icon><n-icon><TrashOutline /></n-icon></template>
        删除
      </n-button>
    </div>

    <aside class="tm-drawer" :class="{ open: activePanel === 'filters' }">
      <div class="tm-drawer-header">
        <strong>过滤器</strong>
        <n-button quaternary circle @click="activePanel = null">
          <template #icon><n-icon><CloseOutline /></n-icon></template>
        </n-button>
      </div>
      <div class="tm-drawer-body">
        <n-select
          v-model:value="activeFilterId"
          clearable
          placeholder="应用已保存过滤器"
          :options="savedFilterOptions"
        />
        <n-input v-model:value="draftFilterName" placeholder="过滤器名称" />
        <n-radio-group v-model:value="draftFilter.logic" size="small">
          <n-radio-button value="all">且</n-radio-button>
          <n-radio-button value="any">或</n-radio-button>
        </n-radio-group>
        <n-select
          v-model:value="draftFilter.nodeId"
          clearable
          filterable
          placeholder="只看某个节点下面"
          :options="nodeOptions"
        />
        <div class="tm-condition-list">
          <div v-for="condition in draftFilter.conditions" :key="condition.id" class="tm-condition-row">
            <n-select v-model:value="condition.field" :options="conditionFieldOptions" @update:value="resetCondition(condition)" />
            <n-select v-model:value="condition.operator" :options="operatorOptions(condition.field)" />
            <n-select
              v-if="condition.field === 'tag'"
              :value="conditionSelectValue(condition)"
              clearable
              :options="tagOptions"
              placeholder="标签"
              @update:value="condition.value = $event || ''"
            />
            <n-select
              v-else-if="condition.field === 'completed'"
              :value="conditionSelectValue(condition)"
              :options="completedOptions"
              @update:value="condition.value = $event || 'false'"
            />
            <n-date-picker
              v-else-if="condition.field === 'scheduledAt'"
              :value="dateValue(condition.value)"
              type="datetime"
              clearable
              @update:value="condition.value = $event ? new Date($event).toISOString() : ''"
            />
            <n-input
              v-else
              :value="String(condition.value ?? '')"
              placeholder="关键字"
              @update:value="condition.value = $event"
            />
            <n-button tertiary type="error" title="删除条件" @click="removeCondition(condition.id)">
              <template #icon><n-icon><TrashOutline /></n-icon></template>
            </n-button>
          </div>
        </div>
        <div class="tm-row-actions">
          <n-button tertiary @click="clearFilters">清除过滤</n-button>
          <n-button tertiary @click="addCondition">添加条件</n-button>
          <n-button type="primary" :disabled="!draftFilterName.trim()" @click="saveFilter">保存</n-button>
        </div>
        <div class="tm-saved-list">
          <button
            v-for="filter in state.filters"
            :key="filter.id"
            class="tm-saved-filter"
            :class="{ active: activeFilterId === filter.id }"
            @click="applyFilter(filter.id)"
          >
            <span>{{ filter.name }}</span>
            <n-button quaternary size="tiny" type="error" @click.stop="deleteFilter(filter.id)">
              <template #icon><n-icon><CloseOutline /></n-icon></template>
            </n-button>
          </button>
        </div>
      </div>
    </aside>

    <aside class="tm-drawer tm-drawer--right" :class="{ open: activePanel === 'editor' }">
      <div class="tm-drawer-header">
        <strong>任务详情</strong>
        <n-button quaternary circle @click="activePanel = null">
          <template #icon><n-icon><CloseOutline /></n-icon></template>
        </n-button>
      </div>
      <div v-if="selectedTask" class="tm-drawer-body">
        <n-input :value="selectedTask.title" placeholder="任务标题" @update:value="updateTask(selectedTask.id, { title: $event })" />
        <n-checkbox :checked="selectedTask.completed" @update:checked="toggleTask(selectedTask.id, $event)">
          已完成
        </n-checkbox>
        <n-date-picker
          :value="dateValue(selectedTask.scheduledAt)"
          type="datetime"
          clearable
          :actions="['now', 'confirm']"
          @update:value="updateTask(selectedTask.id, { scheduledAt: $event ? new Date($event).toISOString() : '' })"
        />
        <n-select
          :value="selectedTask.tagIds"
          multiple
          clearable
          placeholder="标签"
          :options="tagOptions"
          @update:value="updateTask(selectedTask.id, { tagIds: $event })"
        />
        <n-input-group>
          <n-input v-model:value="newTagName" placeholder="新增标签" @keydown.enter.prevent="addTag" />
          <n-button type="primary" @click="addTag">添加</n-button>
        </n-input-group>
        <div class="tm-tag-list">
          <span v-for="tag in state.tags" :key="tag.id" class="tm-tag" :style="{ '--tag-color': tag.color }">
            {{ tag.name }}
            <button @click="deleteTag(tag.id)">×</button>
          </span>
        </div>
        <n-input
          :value="selectedTask.notes"
          type="textarea"
          placeholder="备注"
          @update:value="updateTask(selectedTask.id, { notes: $event })"
        />
      </div>
      <div v-else class="tm-empty-state">
        选择一个节点后编辑任务。
      </div>
    </aside>
  </div>
</template>

<script setup lang="ts">
import {
  AddOutline,
  ArrowRedoOutline,
  ArrowUndoOutline,
  CalendarOutline,
  CheckmarkCircleOutline,
  ChevronBackOutline,
  ChevronForwardOutline,
  CloseOutline,
  CreateOutline,
  EllipseOutline,
  FunnelOutline,
  LocateOutline,
  SearchOutline,
  TrashOutline,
} from "@vicons/ionicons5";
import MindElixir, { RIGHT, type MindElixirData, type MindElixirInstance, type NodeObj } from "mind-elixir";
import "mind-elixir/style.css";
import {
  NButton,
  NButtonGroup,
  NCheckbox,
  NDatePicker,
  NIcon,
  NInput,
  NInputGroup,
  NRadioButton,
  NRadioGroup,
  NSelect,
} from "naive-ui";
import { computed, markRaw, nextTick, onBeforeUnmount, onMounted, reactive, ref, shallowRef, watch } from "vue";
import {
  fetchTimeManagerState,
  saveTimeManagerState,
  type TimeManagerFilterCondition,
  type TimeManagerSavedFilter,
  type TimeManagerState,
  type TimeManagerTask,
} from "@/api";

type ViewMode = "map" | "calendar";
type CalendarMode = "month" | "week" | "day";
type PanelName = "filters" | "editor" | null;
type TaskNodeMeta = {
  taskId?: string;
  completed?: boolean;
  scheduledAt?: string;
  tagIds?: string[];
  notes?: string;
};

const rootId = "space-station";
const mindContainerRef = ref<HTMLElement | null>(null);
const mind = shallowRef<MindElixirInstance | null>(null);
const state = reactive<TimeManagerState>({
  tasks: [],
  tags: [],
  filters: [],
  settings: { calendarStartHour: 7, calendarEndHour: 22 },
});

const viewMode = ref<ViewMode>("map");
const calendarMode = ref<CalendarMode>("month");
const calendarCursor = ref(startOfDay(new Date()));
const activePanel = ref<PanelName>(null);
const selectedTaskId = ref<string | null>(null);
const activeFilterId = ref<string | null>(null);
const keyword = ref("");
const newTagName = ref("");
const draftFilterName = ref("");
const draftFilter = reactive<Pick<TimeManagerSavedFilter, "logic" | "nodeId" | "conditions">>({
  logic: "all",
  nodeId: null,
  conditions: [],
});
const suppressMindSync = ref(false);
const skipNextMindRefresh = ref(false);
const canvasPointerStart = ref<{ x: number; y: number } | null>(null);
let saveTimer = 0;
let searchFocusTimer = 0;
let originalViewportContent = "";

const selectedTask = computed(() => state.tasks.find((task) => task.id === selectedTaskId.value));
const tagOptions = computed(() => state.tags.map((tag) => ({ label: tag.name, value: tag.id })));
const nodeOptions = computed(() =>
  flattenTasks(state.tasks).map(({ task, depth }) => ({ label: `${"　".repeat(depth)}${task.title}`, value: task.id })),
);
const savedFilterOptions = computed(() => state.filters.map((filter) => ({ label: filter.name, value: filter.id })));
const completedOptions = [
  { label: "已完成", value: "true" },
  { label: "未完成", value: "false" },
];
const conditionFieldOptions = [
  { label: "标签", value: "tag" },
  { label: "完成状态", value: "completed" },
  { label: "时间点", value: "scheduledAt" },
  { label: "关键字", value: "keyword" },
];

const filteredTasks = computed(() => {
  const descendants = draftFilter.nodeId ? new Set([draftFilter.nodeId, ...descendantIds(draftFilter.nodeId)]) : null;
  return state.tasks.filter((task) => {
    if (descendants && !descendants.has(task.id)) {
      return false;
    }
    if (keyword.value.trim() && !matchesKeyword(task, keyword.value)) {
      return false;
    }
    if (draftFilter.conditions.length === 0) {
      return true;
    }
    const results = draftFilter.conditions.map((condition) => matchesCondition(task, condition));
    return draftFilter.logic === "all" ? results.every(Boolean) : results.some(Boolean);
  });
});
const isFiltered = computed(() => Boolean(keyword.value.trim() || draftFilter.nodeId || draftFilter.conditions.length));
const mindData = computed(() => buildMindData(filteredTasks.value));
const timedVisibleTasks = computed(() => filteredTasks.value.filter((task) => task.scheduledAt));

const calendarTitle = computed(() => {
  const cursor = calendarCursor.value;
  if (calendarMode.value === "day") {
    return formatDate(cursor);
  }
  if (calendarMode.value === "week") {
    const start = startOfWeek(cursor);
    const end = addDays(start, 6);
    return `${formatDate(start)} - ${formatDate(end)}`;
  }
  return `${cursor.getFullYear()} 年 ${cursor.getMonth() + 1} 月`;
});
const calendarDays = computed(() => {
  let start = startOfMonth(calendarCursor.value);
  let count = 42;
  if (calendarMode.value === "week") {
    start = startOfWeek(calendarCursor.value);
    count = 7;
  }
  if (calendarMode.value === "day") {
    start = startOfDay(calendarCursor.value);
    count = 1;
  }
  return Array.from({ length: count }, (_, index) => {
    const date = addDays(start, index);
    const tasks = timedVisibleTasks.value
      .filter((task) => isSameDay(new Date(task.scheduledAt), date))
      .sort((a, b) => a.scheduledAt.localeCompare(b.scheduledAt));
    return {
      key: date.toISOString(),
      label: calendarMode.value === "month" ? `${date.getDate()}` : formatDate(date),
      isToday: isSameDay(date, new Date()),
      tasks,
    };
  });
});

onMounted(async () => {
  lockPageViewport();
  const remote = await fetchTimeManagerState();
  Object.assign(state, normalizeState(remote));
  await nextTick();
  initMind();
});

onBeforeUnmount(() => {
  mind.value?.destroy();
  window.clearTimeout(saveTimer);
  window.clearTimeout(searchFocusTimer);
  unlockPageViewport();
});

watch(
  () => [mindData.value, viewMode.value],
  async () => {
    if (!mind.value || viewMode.value !== "map") {
      return;
    }
    if (skipNextMindRefresh.value) {
      skipNextMindRefresh.value = false;
      return;
    }
    await nextTick();
    refreshMind();
  },
  { deep: true },
);

watch(
  state,
  () => {
    scheduleSave();
  },
  { deep: true },
);

watch(activeFilterId, (filterId) => {
  if (filterId) {
    loadFilter(filterId);
  }
});

watch(keyword, () => {
  window.clearTimeout(searchFocusTimer);
  if (!keyword.value.trim()) {
    return;
  }
  searchFocusTimer = window.setTimeout(() => {
    focusFirstSearchMatch();
  }, 180);
});

function initMind() {
  if (!mindContainerRef.value) {
    return;
  }
  mind.value = markRaw(new MindElixir({
    el: mindContainerRef.value,
    direction: RIGHT,
    editable: true,
    contextMenu: true,
    toolBar: false,
    keypress: true,
    allowUndo: true,
    draggable: true,
    compact: false,
    overflowHidden: false,
    newTopicName: "新任务",
    theme: {
      name: "space-station-task",
      palette,
      cssVar: {
        "--bgcolor": "#eef3f7",
        "--color": "#172632",
        "--root-bgcolor": "#153243",
        "--root-color": "#ffffff",
        "--root-radius": "8px",
        "--main-bgcolor": "#ffffff",
        "--main-color": "#172632",
        "--main-radius": "8px",
        "--selected": "#2563eb",
        "--accent-color": "#2563eb",
        "--topic-padding": "8px 12px",
        "--node-gap-x": "42px",
        "--node-gap-y": "14px",
        "--main-gap-x": "72px",
        "--main-gap-y": "18px",
      },
    },
  }));
  mind.value.bus.addListener("operation", handleMindOperation);
  mind.value.bus.addListener("selectNodes", (nodes) => {
    const selected = nodes.at(-1);
    selectedTaskId.value = selected?.id === rootId ? null : selected?.id ?? null;
  });
  mind.value.init(mindData.value);
  mind.value.clearHistory?.();
  setTimeout(() => mind.value?.scaleFit(), 80);
}

function refreshMind() {
  if (!mind.value) {
    return;
  }
  const retainedSelectedId = selectedTaskId.value && state.tasks.some((task) => task.id === selectedTaskId.value) ? selectedTaskId.value : null;
  selectedTaskId.value = retainedSelectedId;
  suppressMindSync.value = true;
  mind.value.refresh(mindData.value);
  mind.value.clearHistory?.();
  if (retainedSelectedId) {
    const selectedTopic = findTopic(retainedSelectedId);
    if (selectedTopic) {
      mind.value.selectNode(selectedTopic);
    }
  } else {
    selectedTaskId.value = null;
  }
  requestAnimationFrame(() => {
    suppressMindSync.value = false;
  });
}

function handleMindOperation(operation: unknown) {
  if (suppressMindSync.value || !mind.value) {
    return;
  }
  const op = operation as { name?: string; objs?: Array<{ id: string; children?: unknown[] }>; obj?: { id?: string; children?: unknown[] } };
  if (op.name === "removeNodes") {
    const ids = (op.objs ?? []).flatMap((node) => [node.id, ...collectNodeIds(node as NodeObj<TaskNodeMeta>)]);
    removeTaskIds(ids.filter((id) => id !== rootId));
  }
  skipNextMindRefresh.value = true;
  syncTasksFromMind(mind.value.getData());
}

function syncTasksFromMind(data: MindElixirData) {
  const exported = extractTasksFromNode(data.nodeData as NodeObj<TaskNodeMeta>);
  const exportedIds = new Set(exported.map((task) => task.id));
  const visibleIds = new Set(filteredTasks.value.map((task) => task.id));
  const now = new Date().toISOString();

  exported.forEach((task, index) => {
    const existing = state.tasks.find((item) => item.id === task.id);
    if (existing) {
      Object.assign(existing, {
        title: task.title,
        parentId: task.parentId,
        sortOrder: index,
        updatedAt: now,
      });
    } else {
      state.tasks.push({ ...task, sortOrder: index, createdAt: now, updatedAt: now });
    }
  });

  if (!isFiltered.value) {
    state.tasks = state.tasks.filter((task) => exportedIds.has(task.id));
  } else {
    const removedVisibleIds = [...visibleIds].filter((id) => !exportedIds.has(id));
    if (removedVisibleIds.length) {
      removeTaskIds(removedVisibleIds);
    }
  }
}

function buildMindData(tasks: TimeManagerTask[]): MindElixirData {
  const children = buildNodeChildren(tasks, null);
  return {
    nodeData: {
      id: rootId,
      topic: "space-station",
      expanded: true,
      children,
    },
    direction: RIGHT,
    compact: false,
    meta: { tool: "time-manager" },
  };
}

function buildNodeChildren(tasks: TimeManagerTask[], parentId: string | null): NodeObj<TaskNodeMeta>[] {
  const availableIds = new Set(tasks.map((task) => task.id));
  return tasks
    .filter((task) => (task.parentId && availableIds.has(task.parentId) ? task.parentId : null) === parentId)
    .sort((a, b) => a.sortOrder - b.sortOrder || a.createdAt.localeCompare(b.createdAt))
    .map((task) => ({
      ...taskToNode(task),
      children: buildNodeChildren(tasks, task.id),
    }));
}

function taskToNode(task: TimeManagerTask): NodeObj<TaskNodeMeta> {
  return {
    id: task.id,
    topic: task.title,
    expanded: true,
    style: nodeStyle(task),
    tags: nodeTags(task),
    icons: task.completed ? ["✓"] : task.scheduledAt ? ["⏱"] : [],
    note: task.notes,
    metadata: {
      taskId: task.id,
      completed: task.completed,
      scheduledAt: task.scheduledAt,
      tagIds: task.tagIds,
      notes: task.notes,
    },
  };
}

function nodeStyle(task: TimeManagerTask) {
  const firstTag = state.tags.find((tag) => task.tagIds.includes(tag.id));
  return {
    background: task.completed ? "#eef3f7" : "#ffffff",
    color: task.completed ? "#788895" : "#172632",
    border: `1px solid ${firstTag?.color ?? "rgba(54, 77, 99, 0.16)"}`,
    textDecoration: task.completed ? "line-through" : "none",
  };
}

function nodeTags(task: TimeManagerTask) {
  const tags = task.tagIds
    .map((id) => state.tags.find((tag) => tag.id === id))
    .filter(Boolean)
    .map((tag) => ({
      text: tag!.name,
      style: {
        background: `${tag!.color}20`,
        color: "#1f3442",
        border: `1px solid ${tag!.color}55`,
      },
    }));
  if (task.scheduledAt) {
    tags.unshift({
      text: formatDateTime(task.scheduledAt),
      style: { background: "#e8f0f6", color: "#334858", border: "1px solid rgba(54, 77, 99, 0.12)" },
    });
  }
  return tags;
}

function extractTasksFromNode(root: NodeObj<TaskNodeMeta>) {
  const tasks: TimeManagerTask[] = [];
  const now = new Date().toISOString();
  const visit = (node: NodeObj<TaskNodeMeta>, parentId: string | null) => {
    if (node.id !== rootId) {
      const existing = state.tasks.find((task) => task.id === node.id);
      tasks.push({
        id: node.id,
        title: node.topic || "未命名任务",
        parentId,
        tagIds: existing?.tagIds ?? node.metadata?.tagIds ?? [],
        scheduledAt: existing?.scheduledAt ?? node.metadata?.scheduledAt ?? "",
        completed: existing?.completed ?? Boolean(node.metadata?.completed),
        notes: existing?.notes ?? node.note ?? node.metadata?.notes ?? "",
        sortOrder: tasks.length,
        createdAt: existing?.createdAt ?? now,
        updatedAt: now,
      });
    }
    node.children?.forEach((child) => visit(child as NodeObj<TaskNodeMeta>, node.id === rootId ? null : node.id));
  };
  root.children?.forEach((child) => visit(child as NodeObj<TaskNodeMeta>, null));
  return tasks;
}

async function addTask(parentId: string | null) {
  const resolvedParentId = parentId && state.tasks.some((task) => task.id === parentId) ? parentId : null;
  const now = new Date().toISOString();
  const task: TimeManagerTask = {
    id: createId(),
    title: "新任务",
    parentId: resolvedParentId,
    tagIds: [],
    scheduledAt: "",
    completed: false,
    notes: "",
    sortOrder: state.tasks.length + 1,
    createdAt: now,
    updatedAt: now,
  };
  const node = taskToNode(task);
  const parentEl = findTopic(resolvedParentId ?? rootId);
  if (mind.value && parentEl) {
    await mind.value.addChild(parentEl, node);
    selectedTaskId.value = task.id;
    viewMode.value = "map";
    activePanel.value = "editor";
    await nextTick();
    const el = findTopic(task.id);
    if (el) {
      mind.value.selectNode(el, true);
    }
    return;
  }
  state.tasks.push(task);
  selectedTaskId.value = task.id;
  viewMode.value = "map";
  activePanel.value = "editor";
  nextTick(() => {
    refreshMind();
    const el = findTopic(task.id);
    if (el) {
      mind.value?.selectNode(el, true);
    }
  });
}

function updateTask(id: string, patch: Partial<TimeManagerTask>) {
  const task = state.tasks.find((item) => item.id === id);
  if (!task) {
    return;
  }
  Object.assign(task, patch, { updatedAt: new Date().toISOString() });
}

function toggleTask(id: string, checked: boolean) {
  updateTask(id, { completed: checked });
}

function toggleSelectedTaskCompleted() {
  if (!selectedTask.value) {
    return;
  }
  updateTask(selectedTask.value.id, { completed: !selectedTask.value.completed });
}

function deleteTask(id: string) {
  if (!state.tasks.some((task) => task.id === id)) {
    return;
  }
  const topic = findTopic(id);
  if (mind.value && topic) {
    try {
      mind.value.removeNodes([topic]);
      selectedTaskId.value = null;
      activePanel.value = null;
      return;
    } catch (error) {
      console.warn("Mind map removeNode failed, falling back to task removal.", error);
    }
  }
  removeTaskIds([id, ...descendantIds(id)]);
}

function removeTaskIds(ids: string[]) {
  const removeSet = new Set(ids);
  state.tasks = state.tasks.filter((task) => !removeSet.has(task.id));
  if (selectedTaskId.value && removeSet.has(selectedTaskId.value)) {
    selectedTaskId.value = null;
  }
  if (!selectedTaskId.value && activePanel.value === "editor") {
    activePanel.value = null;
  }
}

function addTag() {
  const name = newTagName.value.trim();
  if (!name) {
    return;
  }
  state.tags.push({ id: createId(), name, color: palette[state.tags.length % palette.length] });
  newTagName.value = "";
}

function deleteTag(id: string) {
  state.tags = state.tags.filter((tag) => tag.id !== id);
  state.tasks.forEach((task) => {
    task.tagIds = task.tagIds.filter((tagId) => tagId !== id);
  });
}

function addCondition() {
  draftFilter.conditions.push({ id: createId(), field: "keyword", operator: "contains", value: "" });
}

function removeCondition(id: string) {
  draftFilter.conditions = draftFilter.conditions.filter((condition) => condition.id !== id);
}

function resetCondition(condition: TimeManagerFilterCondition) {
  if (condition.field === "completed") {
    condition.operator = "equals";
    condition.value = "false";
  } else if (condition.field === "scheduledAt") {
    condition.operator = "after";
    condition.value = "";
  } else {
    condition.operator = "contains";
    condition.value = "";
  }
}

function clearFilters() {
  activeFilterId.value = null;
  draftFilterName.value = "";
  draftFilter.logic = "all";
  draftFilter.nodeId = null;
  draftFilter.conditions = [];
  keyword.value = "";
}

function saveFilter() {
  const now = new Date().toISOString();
  const existing = activeFilterId.value ? state.filters.find((filter) => filter.id === activeFilterId.value) : undefined;
  if (existing) {
    Object.assign(existing, {
      name: draftFilterName.value.trim(),
      logic: draftFilter.logic,
      nodeId: draftFilter.nodeId,
      conditions: JSON.parse(JSON.stringify(draftFilter.conditions)),
      updatedAt: now,
    });
    return;
  }
  const filter: TimeManagerSavedFilter = {
    id: createId(),
    name: draftFilterName.value.trim(),
    logic: draftFilter.logic,
    nodeId: draftFilter.nodeId,
    conditions: JSON.parse(JSON.stringify(draftFilter.conditions)),
    createdAt: now,
    updatedAt: now,
  };
  state.filters.push(filter);
  activeFilterId.value = filter.id;
}

function applyFilter(id: string) {
  activeFilterId.value = id;
  loadFilter(id);
}

function loadFilter(id: string) {
  const filter = state.filters.find((item) => item.id === id);
  if (!filter) {
    return;
  }
  draftFilterName.value = filter.name;
  draftFilter.logic = filter.logic;
  draftFilter.nodeId = filter.nodeId;
  draftFilter.conditions = JSON.parse(JSON.stringify(filter.conditions));
}

function deleteFilter(id: string) {
  state.filters = state.filters.filter((filter) => filter.id !== id);
  if (activeFilterId.value === id) {
    clearFilters();
  }
}

function operatorOptions(field: TimeManagerFilterCondition["field"]) {
  if (field === "scheduledAt") {
    return [
      { label: "早于", value: "before" },
      { label: "晚于", value: "after" },
      { label: "为空", value: "empty" },
      { label: "不为空", value: "notEmpty" },
    ];
  }
  if (field === "completed") {
    return [{ label: "等于", value: "equals" }];
  }
  return [
    { label: "包含", value: "contains" },
    { label: "等于", value: "equals" },
  ];
}

function matchesCondition(task: TimeManagerTask, condition: TimeManagerFilterCondition) {
  if (condition.field === "tag") {
    const value = String(condition.value);
    const selectedTag = state.tags.find((tag) => tag.id === value);
    return condition.operator === "equals"
      ? task.tagIds.includes(value)
      : task.tagIds.includes(value) ||
          task.tagIds.some((tagId) => tagName(tagId).toLowerCase().includes((selectedTag?.name ?? value).toLowerCase()));
  }
  if (condition.field === "completed") {
    return task.completed === (condition.value === true || condition.value === "true");
  }
  if (condition.field === "scheduledAt") {
    if (condition.operator === "empty") return !task.scheduledAt;
    if (condition.operator === "notEmpty") return Boolean(task.scheduledAt);
    if (!task.scheduledAt || !condition.value) return false;
    return condition.operator === "before"
      ? Date.parse(task.scheduledAt) < Date.parse(String(condition.value))
      : Date.parse(task.scheduledAt) > Date.parse(String(condition.value));
  }
  return matchesKeyword(task, String(condition.value));
}

function matchesKeyword(task: TimeManagerTask, value: string) {
  const needle = value.trim().toLowerCase();
  if (!needle) {
    return true;
  }
  return [task.title, task.notes, task.tagIds.map(tagName).join(" ")]
    .join(" ")
    .toLowerCase()
    .includes(needle);
}

function flattenTasks(tasks: TimeManagerTask[]) {
  const availableIds = new Set(tasks.map((task) => task.id));
  const rows: Array<{ task: TimeManagerTask; depth: number }> = [];
  const visit = (parentId: string | null, depth: number) => {
    tasks
      .filter((task) => (task.parentId && availableIds.has(task.parentId) ? task.parentId : null) === parentId)
      .sort((a, b) => a.sortOrder - b.sortOrder || a.createdAt.localeCompare(b.createdAt))
      .forEach((task) => {
        rows.push({ task, depth });
        visit(task.id, depth + 1);
      });
  };
  visit(null, 0);
  return rows;
}

function descendantIds(parentId: string) {
  const ids: string[] = [];
  const visit = (id: string) => {
    state.tasks.filter((task) => task.parentId === id).forEach((task) => {
      ids.push(task.id);
      visit(task.id);
    });
  };
  visit(parentId);
  return ids;
}

function collectNodeIds(node: NodeObj<TaskNodeMeta>) {
  const ids: string[] = [];
  node.children?.forEach((child) => {
    ids.push(child.id, ...collectNodeIds(child as NodeObj<TaskNodeMeta>));
  });
  return ids;
}

function tagName(id: string) {
  return state.tags.find((tag) => tag.id === id)?.name ?? "";
}

function openTask(id: string) {
  selectedTaskId.value = id;
  viewMode.value = "map";
  activePanel.value = "editor";
  nextTick(() => {
    const el = findTopic(id);
    if (el) {
      mind.value?.selectNode(el);
      mind.value?.scrollIntoView(el, true);
    }
  });
}

function togglePanel(panel: Exclude<PanelName, null>) {
  activePanel.value = activePanel.value === panel ? null : panel;
}

function toggleCalendarView() {
  viewMode.value = viewMode.value === "calendar" ? "map" : "calendar";
}

function centerMind() {
  mind.value?.scaleFit();
}

async function focusFirstSearchMatch() {
  const firstMatch = filteredTasks.value[0];
  if (!firstMatch) {
    return;
  }
  viewMode.value = "map";
  selectedTaskId.value = firstMatch.id;
  await nextTick();
  requestAnimationFrame(() => {
    const el = findTopic(firstMatch.id);
    if (!el) {
      return;
    }
    mind.value?.selectNode(el);
    mind.value?.scrollIntoView(el, true);
  });
}

function recordCanvasPointer(event: PointerEvent) {
  canvasPointerStart.value = { x: event.clientX, y: event.clientY };
}

function clearSelectionOnBlankTap(event: PointerEvent) {
  const start = canvasPointerStart.value;
  canvasPointerStart.value = null;
  if (!start) {
    return;
  }
  const moved = Math.hypot(event.clientX - start.x, event.clientY - start.y);
  if (moved > 8) {
    return;
  }
  const target = event.target instanceof Element ? event.target : null;
  if (target?.closest("me-tpc, .tm-toolbar, .tm-floating-bottom, .tm-drawer, .context-menu")) {
    return;
  }
  mind.value?.clearSelection();
  selectedTaskId.value = null;
  if (activePanel.value === "editor") {
    activePanel.value = null;
  }
}

function undo() {
  mind.value?.undo?.();
  if (mind.value) {
    skipNextMindRefresh.value = true;
    syncTasksFromMind(mind.value.getData());
  }
}

function redo() {
  mind.value?.redo?.();
  if (mind.value) {
    skipNextMindRefresh.value = true;
    syncTasksFromMind(mind.value.getData());
  }
}

function findTopic(id: string) {
  try {
    return mind.value?.findEle(id);
  } catch {
    return null;
  }
}

function lockPageViewport() {
  const viewport = document.querySelector<HTMLMetaElement>('meta[name="viewport"]');
  if (viewport) {
    originalViewportContent = viewport.content;
    viewport.content = "width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover";
  }
  document.documentElement.classList.add("time-manager-page-lock");
  document.body.classList.add("time-manager-page-lock");
}

function unlockPageViewport() {
  const viewport = document.querySelector<HTMLMetaElement>('meta[name="viewport"]');
  if (viewport && originalViewportContent) {
    viewport.content = originalViewportContent;
  }
  document.documentElement.classList.remove("time-manager-page-lock");
  document.body.classList.remove("time-manager-page-lock");
}

function shiftCalendar(delta: number) {
  const current = calendarCursor.value;
  if (calendarMode.value === "month") {
    calendarCursor.value = new Date(current.getFullYear(), current.getMonth() + delta, 1);
  } else if (calendarMode.value === "week") {
    calendarCursor.value = addDays(current, delta * 7);
  } else {
    calendarCursor.value = addDays(current, delta);
  }
}

function conditionSelectValue(condition: TimeManagerFilterCondition) {
  return typeof condition.value === "boolean" ? String(condition.value) : condition.value;
}

function dateValue(value: string | boolean) {
  if (!value || typeof value === "boolean") {
    return null;
  }
  const parsed = Date.parse(value);
  return Number.isNaN(parsed) ? null : parsed;
}

function normalizeState(input: TimeManagerState): TimeManagerState {
  return {
    tasks: Array.isArray(input.tasks)
      ? input.tasks.map((task, index) => ({
          id: task.id || createId(),
          title: task.title || "未命名任务",
          parentId: task.parentId ?? null,
          tagIds: Array.isArray(task.tagIds) ? task.tagIds : [],
          scheduledAt: task.scheduledAt || "",
          completed: Boolean(task.completed),
          notes: task.notes || "",
          sortOrder: Number.isFinite(task.sortOrder) ? task.sortOrder : index,
          createdAt: task.createdAt || new Date().toISOString(),
          updatedAt: task.updatedAt || new Date().toISOString(),
        }))
      : [],
    tags: Array.isArray(input.tags) ? input.tags : [],
    filters: Array.isArray(input.filters) ? input.filters : [],
    settings: {
      calendarStartHour: input.settings?.calendarStartHour ?? 7,
      calendarEndHour: input.settings?.calendarEndHour ?? 22,
    },
  };
}

function scheduleSave() {
  window.clearTimeout(saveTimer);
  saveTimer = window.setTimeout(async () => {
    await saveTimeManagerState(JSON.parse(JSON.stringify(state)));
  }, 350);
}

function createId() {
  return window.crypto.randomUUID ? window.crypto.randomUUID() : `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function startOfDay(date: Date) {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate());
}

function startOfWeek(date: Date) {
  const day = date.getDay() || 7;
  return addDays(startOfDay(date), 1 - day);
}

function startOfMonth(date: Date) {
  return new Date(date.getFullYear(), date.getMonth(), 1);
}

function addDays(date: Date, days: number) {
  const next = new Date(date);
  next.setDate(next.getDate() + days);
  return next;
}

function isSameDay(left: Date, right: Date) {
  return left.getFullYear() === right.getFullYear() && left.getMonth() === right.getMonth() && left.getDate() === right.getDate();
}

function formatDate(date: Date) {
  return `${date.getMonth() + 1}/${date.getDate()}`;
}

function formatTime(value: string) {
  return new Date(value).toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit" });
}

function formatDateTime(value: string) {
  return new Date(value).toLocaleString("zh-CN", { month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit" });
}

const palette = ["#2563eb", "#16a34a", "#d97706", "#dc2626", "#7c3aed", "#0891b2"];
</script>

<style scoped>
:global(html.time-manager-page-lock),
:global(body.time-manager-page-lock) {
  width: 100%;
  height: 100%;
  overflow: hidden;
  overscroll-behavior: none;
  touch-action: manipulation;
}

.time-manager {
  position: fixed;
  inset: 0;
  width: 100%;
  height: 100dvh;
  overflow: hidden;
  background: #eef3f7;
  color: #172632;
  overscroll-behavior: none;
  user-select: none;
  -webkit-user-select: none;
  -webkit-touch-callout: none;
}

.time-manager :deep(input),
.time-manager :deep(textarea),
.time-manager :deep([contenteditable="true"]),
.time-manager :deep([contenteditable="plaintext-only"]) {
  user-select: text;
  -webkit-user-select: text;
  -webkit-touch-callout: default;
}

.tm-canvas-shell,
.tm-mind-elixir {
  width: 100%;
  height: 100%;
}

.tm-mind-elixir :deep(.map-container) {
  width: 100%;
  height: 100%;
  background: #eef3f7;
  touch-action: none;
}

.tm-mind-elixir :deep(me-tpc) {
  box-shadow: 0 10px 22px rgba(42, 61, 78, 0.1);
  max-width: min(280px, 62vw);
}

.tm-mind-elixir :deep(me-tpc.selected) {
  box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.2), 0 12px 26px rgba(42, 61, 78, 0.16);
}

.tm-mind-elixir :deep(me-parent me-tpc .insert-preview) {
  left: -22px;
  width: calc(100% + 72px);
}

.tm-mind-elixir :deep(me-parent me-tpc .insert-preview.show) {
  background: #0ea5e9;
  opacity: 0.88;
  box-shadow: 0 0 0 2px rgba(255, 255, 255, 0.92), 0 8px 18px rgba(14, 116, 144, 0.28);
}

.tm-mind-elixir :deep(me-parent me-tpc .insert-preview.before) {
  height: 18px;
  top: -18px;
}

.tm-mind-elixir :deep(me-parent me-tpc .insert-preview.after) {
  height: 18px;
  bottom: -18px;
}

.tm-mind-elixir :deep(me-parent me-tpc .insert-preview.in.show) {
  background: rgba(14, 165, 233, 0.2);
  outline: 3px solid #0ea5e9;
  outline-offset: 3px;
}

.tm-floating-top {
  position: absolute;
  top: 12px;
  left: 12px;
  z-index: 20;
  pointer-events: none;
}

.tm-toolbar,
.tm-floating-bottom,
.tm-calendar-toolbar,
.tm-drawer {
  border: 1px solid rgba(54, 77, 99, 0.14);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.9);
  box-shadow: 0 14px 34px rgba(42, 61, 78, 0.12);
  backdrop-filter: blur(14px);
  pointer-events: auto;
}

.tm-toolbar {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px;
}

.tm-search {
  width: 210px;
}

.tm-floating-bottom {
  position: absolute;
  left: 50%;
  bottom: 14px;
  z-index: 20;
  display: flex;
  gap: 8px;
  padding: 8px;
  transform: translateX(-50%);
}

.tm-drawer {
  position: absolute;
  top: 86px;
  bottom: 14px;
  left: 12px;
  z-index: 30;
  width: min(380px, calc(100vw - 24px));
  display: grid;
  grid-template-rows: auto minmax(0, 1fr);
  overflow: hidden;
  opacity: 0;
  pointer-events: none;
  transform: translateX(calc(-100% - 18px));
  transition: transform 0.18s ease, opacity 0.18s ease;
}

.tm-drawer--right {
  right: 12px;
  left: auto;
  transform: translateX(calc(100% + 18px));
}

.tm-drawer.open {
  opacity: 1;
  pointer-events: auto;
  transform: translateX(0);
}

.tm-drawer-header {
  min-height: 52px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 10px 12px;
  border-bottom: 1px solid rgba(54, 77, 99, 0.12);
}

.tm-drawer-body {
  min-height: 0;
  display: grid;
  align-content: start;
  gap: 12px;
  padding: 12px;
  overflow: auto;
}

.tm-condition-list,
.tm-saved-list,
.tm-tag-list {
  display: grid;
  gap: 8px;
}

.tm-condition-row {
  display: grid;
  grid-template-columns: minmax(72px, 0.9fr) minmax(72px, 0.8fr) minmax(100px, 1.1fr) 34px;
  gap: 6px;
  align-items: center;
}

.tm-row-actions,
.tm-calendar-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.tm-saved-filter {
  width: 100%;
  min-height: 36px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 0 4px 0 10px;
  border: 1px solid rgba(54, 77, 99, 0.12);
  border-radius: 8px;
  background: #f7fafc;
  color: #21313d;
  text-align: left;
  cursor: pointer;
}

.tm-saved-filter.active {
  border-color: #153243;
  background: #e6eef4;
}

.tm-tag {
  --tag-color: #2563eb;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  justify-self: start;
  padding: 3px 8px;
  border-radius: 999px;
  background: color-mix(in srgb, var(--tag-color) 14%, white);
  color: #1f3442;
  font-size: 12px;
}

.tm-tag button {
  border: 0;
  background: transparent;
  color: inherit;
  cursor: pointer;
}

.tm-empty-state {
  padding: 18px;
  color: #657482;
}

.tm-calendar-surface {
  width: 100%;
  height: 100%;
  display: grid;
  grid-template-rows: auto minmax(0, 1fr);
  gap: 10px;
  padding: 86px 12px 12px;
  background: #eef3f7;
}

.tm-calendar-toolbar {
  justify-content: flex-start;
  padding: 10px;
}

.tm-calendar-grid {
  min-height: 0;
  overflow: auto;
  padding: 10px;
  display: grid;
  grid-template-columns: repeat(7, minmax(130px, 1fr));
  grid-auto-rows: minmax(130px, 1fr);
  gap: 8px;
  border: 1px solid rgba(54, 77, 99, 0.14);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.9);
}

.tm-calendar-grid.mode-week {
  grid-auto-rows: minmax(68vh, 1fr);
}

.tm-calendar-grid.mode-day {
  grid-template-columns: 1fr;
  grid-auto-rows: minmax(68vh, 1fr);
}

.tm-day-cell {
  min-width: 0;
  display: grid;
  align-content: start;
  gap: 6px;
  padding: 8px;
  border: 1px solid rgba(54, 77, 99, 0.1);
  border-radius: 8px;
  background: #f8fbfd;
}

.tm-day-cell.today {
  border-color: #2563eb;
}

.tm-day-cell header {
  display: flex;
  justify-content: space-between;
  gap: 8px;
}

.tm-day-cell small {
  color: #657482;
}

.tm-calendar-task {
  min-width: 0;
  display: grid;
  grid-template-columns: 44px minmax(0, 1fr);
  gap: 6px;
  align-items: center;
  padding: 6px;
  border: 0;
  border-radius: 7px;
  background: #e8f0f6;
  color: #1f3442;
  text-align: left;
  cursor: pointer;
}

.tm-calendar-task time {
  color: #60707e;
  font-size: 12px;
}

.tm-calendar-task span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tm-calendar-task.done span {
  color: #788895;
  text-decoration: line-through;
}

@media (max-width: 760px) {
  .time-manager {
    height: 100dvh;
    min-height: 100dvh;
    max-height: 100dvh;
  }

  .tm-floating-top {
    top: 0;
    left: 0;
    right: 0;
  }

  .tm-toolbar {
    width: 100%;
    padding: 6px;
    border-top: 0;
    border-right: 0;
    border-left: 0;
    border-radius: 0;
    overflow-x: auto;
    scrollbar-width: none;
  }

  .tm-toolbar::-webkit-scrollbar {
    display: none;
  }

  .tm-search {
    width: 132px;
    flex: 0 0 132px;
  }

  .tm-mind-elixir :deep(me-parent me-tpc .insert-preview) {
    left: -34px;
    width: calc(100% + 128px);
  }

  .tm-mind-elixir :deep(me-parent me-tpc .insert-preview.before) {
    height: 24px;
    top: -24px;
  }

  .tm-mind-elixir :deep(me-parent me-tpc .insert-preview.after) {
    height: 24px;
    bottom: -24px;
  }

  .tm-floating-bottom {
    right: 0;
    bottom: 0;
    left: 0;
    justify-content: center;
    padding: 6px 6px calc(6px + env(safe-area-inset-bottom, 0));
    border-right: 0;
    border-bottom: 0;
    border-left: 0;
    border-radius: 0;
    transform: none;
  }

  .tm-drawer,
  .tm-drawer--right {
    top: auto;
    right: 0;
    bottom: 0;
    left: 0;
    width: auto;
    max-height: min(72dvh, 620px);
    padding-bottom: env(safe-area-inset-bottom, 0);
    border-right: 0;
    border-bottom: 0;
    border-left: 0;
    border-radius: 8px 8px 0 0;
    transform: translateY(calc(100% + 18px));
  }

  .tm-drawer.open {
    transform: translateY(0);
  }

  .tm-condition-row {
    grid-template-columns: 1fr;
  }

  .tm-calendar-surface {
    height: 100dvh;
    padding: 58px 0 0;
    gap: 0;
    overflow: hidden;
  }

  .tm-calendar-toolbar {
    border-right: 0;
    border-left: 0;
    border-radius: 0;
    flex-wrap: wrap;
  }

  .tm-calendar-grid {
    grid-template-columns: repeat(7, minmax(0, 1fr));
    grid-auto-rows: minmax(64px, 1fr);
    gap: 3px;
    padding: 3px;
    border-right: 0;
    border-bottom: 0;
    border-left: 0;
    border-radius: 0;
    overflow: hidden;
  }

  .tm-calendar-grid.mode-week {
    grid-template-columns: repeat(7, minmax(0, 1fr));
    grid-auto-rows: minmax(0, 1fr);
  }

  .tm-calendar-grid.mode-day {
    grid-template-columns: 1fr;
  }

  .tm-day-cell {
    gap: 3px;
    padding: 4px;
    border-radius: 5px;
  }

  .tm-day-cell header {
    align-items: center;
    min-height: 18px;
    font-size: 11px;
  }

  .tm-day-cell small {
    font-size: 10px;
  }

  .tm-calendar-task {
    grid-template-columns: 1fr;
    gap: 0;
    min-height: 18px;
    padding: 2px 3px;
    border-radius: 4px;
    font-size: 10px;
  }

  .tm-calendar-task time {
    display: none;
  }

  .tm-calendar-task:nth-of-type(n + 4) {
    display: none;
  }
}
</style>
