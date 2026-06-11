<template>
  <div class="scheduler-layout">
    <div class="scheduler-sidebar">
      <n-card title="新增任务" embedded>
        <n-space vertical :size="14">
          <n-input v-model:value="draftTitle" placeholder="任务名称" />
          <n-date-picker
            v-model:value="draftDueAt"
            type="datetime"
            clearable
            :actions="['now', 'confirm']"
          />
          <n-grid cols="2" :x-gap="12">
            <n-grid-item>
              <n-input-number v-model:value="draftMinutes" :min="10" :step="10" placeholder="预计分钟" />
            </n-grid-item>
            <n-grid-item>
              <n-select v-model:value="draftPriority" :options="priorityOptions" />
            </n-grid-item>
          </n-grid>
          <n-select
            v-model:value="draftTagIds"
            multiple
            clearable
            placeholder="选择 tag"
            :options="tagOptions"
          />
          <n-select
            v-model:value="draftDependencyIds"
            multiple
            clearable
            placeholder="依赖任务"
            :options="dependencyOptions"
          />
          <n-input v-model:value="draftNotes" type="textarea" placeholder="备注" />
          <n-button type="primary" block :disabled="!draftTitle.trim()" @click="addTask">
            加入时间井
          </n-button>
        </n-space>
      </n-card>

      <n-card title="Tags" embedded>
        <n-space vertical :size="12">
          <n-input-group>
            <n-input v-model:value="newTagName" placeholder="新增 tag" @keydown.enter.prevent="addTag" />
            <n-button type="primary" @click="addTag">添加</n-button>
          </n-input-group>
          <n-checkbox-group v-model:value="activeTagIds">
            <n-space wrap>
              <n-checkbox v-for="tag in state.tags" :key="tag.id" :value="tag.id">
                <span class="tag-label" :style="{ '--tag-color': tag.color }">{{ tag.name }}</span>
              </n-checkbox>
            </n-space>
          </n-checkbox-group>
          <n-button quaternary size="small" @click="activeTagIds = []">清除过滤</n-button>
        </n-space>
      </n-card>

      <n-card title="场景" embedded>
        <n-space vertical :size="12">
          <n-input-group>
            <n-input v-model:value="sceneName" placeholder="场景名称" @keydown.enter.prevent="saveScene" />
            <n-button type="primary" @click="saveScene">保存</n-button>
          </n-input-group>
          <n-select v-model:value="selectedSceneId" clearable placeholder="选择场景" :options="sceneOptions" />
          <n-space>
            <n-button size="small" tertiary :disabled="!selectedSceneId" @click="applySelectedScene">应用</n-button>
            <n-button size="small" tertiary type="error" :disabled="!selectedSceneId" @click="deleteSelectedScene">删除</n-button>
          </n-space>
        </n-space>
      </n-card>
    </div>

    <div class="scheduler-board-card">
      <div class="board-toolbar">
        <div>
          <p class="board-eyebrow">Deadline Well</p>
          <h3>任务斩杀线</h3>
        </div>
        <n-space>
          <n-tag :bordered="false">未来 {{ state.settings.horizonDays }} 天</n-tag>
          <n-tag :bordered="false">{{ Math.round(zoom * 100) }}%</n-tag>
          <n-tag type="error" :bordered="false">逾期 {{ overdueTasks.length }}</n-tag>
          <n-tag type="success" :bordered="false">完成 {{ doneTasks.length }}</n-tag>
        </n-space>
      </div>
      <div class="board-controls">
        <n-space align="center" :size="10">
          <n-button size="small" tertiary @click="zoomOut">缩小</n-button>
          <n-button size="small" tertiary @click="zoomIn">放大</n-button>
          <n-button size="small" tertiary @click="panLanes(-1)">左移</n-button>
          <n-button size="small" tertiary @click="panLanes(1)">右移</n-button>
          <n-button size="small" quaternary @click="resetViewport">回到现在</n-button>
          <span class="board-hint">滚轮滚动时间，Shift + 滚轮横移，Ctrl/⌘ + 滚轮缩放</span>
        </n-space>
      </div>

      <div ref="boardRef" class="scheduler-board" @wheel.prevent="handleBoardWheel">
        <v-stage :config="{ width: stageWidth, height: boardSize.height }">
          <v-layer>
            <v-rect
              :config="boardBackgroundConfig"
              @click="clearSelectedTask"
              @tap="clearSelectedTask"
            />
            <v-rect
              v-for="track in laneBackgrounds"
              :key="track.key"
              :config="track.config"
            />
            <v-group
              v-for="label in laneLabels"
              :key="label.key"
              :config="label.config"
              @click.cancelBubble
              @tap.cancelBubble
              @dragend="handleLaneDragEnd(label.id, $event)"
            >
              <v-rect :config="laneLabelRectConfig(label)" />
              <v-text :config="laneLabelTextConfig(label)" />
            </v-group>
            <v-line
              v-for="line in timeGridLines"
              :key="line.key"
              :config="line.config"
            />
            <v-text
              v-for="label in timeLabels"
              :key="label.key"
              :config="label.config"
            />
            <v-line :config="killLineConfig" />
            <v-text :config="killLineLabelConfig" />
            <v-arrow
              v-for="edge in dependencyEdges"
              :key="edge.key"
              :config="edge.config"
            />
            <v-group
              v-for="task in orderedVisibleTasks"
              :key="task.id"
              :config="taskGroupConfig(task)"
              @click="selectTaskFromCanvas(task.id, $event)"
              @tap="selectTaskFromCanvas(task.id, $event)"
              @dragmove="handleTaskDragMove(task.id, $event)"
              @dragend="handleTaskDragEnd(task.id, $event)"
            >
              <v-rect :config="taskRectConfig(task)" />
              <v-text :config="taskTitleConfig(task)" />
              <v-text :config="taskMetaConfig(task)" />
            </v-group>
          </v-layer>
        </v-stage>
      </div>
    </div>

    <div class="scheduler-inspector">
      <n-card title="推荐执行顺序" embedded>
        <n-space vertical :size="10">
          <div
            v-for="(task, index) in recommendedTasks"
            :key="task.id"
            class="recommend-row"
            :class="{ blocked: isBlocked(task) }"
            @click="selectTask(task.id)"
          >
            <strong>{{ index + 1 }}</strong>
            <span>{{ task.title }}</span>
            <n-tag size="small" :type="isBlocked(task) ? 'warning' : 'success'" :bordered="false">
              {{ isBlocked(task) ? "等待依赖" : "可执行" }}
            </n-tag>
          </div>
          <n-empty v-if="recommendedTasks.length === 0" description="暂无待办任务" />
        </n-space>
      </n-card>

      <n-card title="任务详情" embedded>
        <n-space v-if="selectedTask" vertical :size="12">
          <n-input :value="selectedTask.title" @update:value="updateSelected({ title: $event })" />
          <n-date-picker
            :value="Date.parse(selectedTask.dueAt)"
            type="datetime"
            :actions="['now', 'confirm']"
            @update:value="updateSelectedDueAt"
          />
          <n-grid cols="2" :x-gap="12">
            <n-grid-item>
              <n-input-number
                :value="selectedTask.estimatedMinutes"
                :min="10"
                :step="10"
                @update:value="updateSelected({ estimatedMinutes: $event || 10 })"
              />
            </n-grid-item>
            <n-grid-item>
              <n-select
                :value="selectedTask.priority"
                :options="priorityOptions"
                @update:value="updateSelected({ priority: $event })"
              />
            </n-grid-item>
          </n-grid>
          <n-select
            :value="selectedTask.tagIds"
            multiple
            clearable
            :options="tagOptions"
            @update:value="updateSelected({ tagIds: $event })"
          />
          <n-select
            :value="selectedTask.dependencyIds"
            multiple
            clearable
            :options="selectedDependencyOptions"
            @update:value="updateSelected({ dependencyIds: $event })"
          />
          <n-input
            :value="selectedTask.notes"
            type="textarea"
            @update:value="updateSelected({ notes: $event })"
          />
          <n-space>
            <n-button
              :type="selectedTask.status === 'done' ? 'default' : 'success'"
              @click="toggleDone(selectedTask.id)"
            >
              {{ selectedTask.status === "done" ? "恢复待办" : "完成任务" }}
            </n-button>
            <n-button tertiary type="error" @click="deleteTask(selectedTask.id)">删除</n-button>
          </n-space>
        </n-space>
        <n-empty v-else description="点击任务块查看详情" />
      </n-card>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import {
  NButton,
  NCard,
  NCheckbox,
  NCheckboxGroup,
  NDatePicker,
  NEmpty,
  NGrid,
  NGridItem,
  NInput,
  NInputGroup,
  NInputNumber,
  NSelect,
  NSpace,
  NTag,
} from "naive-ui";
import {
  fetchSchedulerState,
  saveSchedulerState,
  type SchedulerScene,
  type SchedulerState,
  type SchedulerTag,
  type SchedulerTask,
  type SchedulerTaskPriority,
} from "@/api";

const tagPalette = ["#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c", "#0891b2", "#be123c"];
const priorityWeight: Record<SchedulerTaskPriority, number> = {
  low: 1,
  normal: 2,
  high: 3,
  urgent: 4,
};
const timeGutterWidth = 104;
const laneHeaderHeight = 34;
const laneGap = 16;
const minLaneWidth = 176;
const taskInset = 12;
const maxTaskWidth = 184;

const state = reactive<SchedulerState>({
  tasks: [],
  tags: [],
  scenes: [],
  settings: {
    horizonDays: 14,
    overdueDays: 3,
    dayStartHour: 8,
    dayEndHour: 22,
  },
});

const boardRef = ref<HTMLElement | null>(null);
const boardSize = reactive({ width: 820, height: 620 });
const scrollHours = ref(0);
const zoom = ref(1);
const selectedTaskId = ref("");
const activeTagIds = ref<string[]>([]);
const newTagName = ref("");
const sceneName = ref("");
const selectedSceneId = ref<string | null>(null);
const draftTitle = ref("");
const draftDueAt = ref(Date.now() + 24 * 60 * 60 * 1000);
const draftMinutes = ref(60);
const draftPriority = ref<SchedulerTaskPriority>("normal");
const draftTagIds = ref<string[]>([]);
const draftDependencyIds = ref<string[]>([]);
const draftNotes = ref("");
const dragPositions = reactive<Record<string, { x: number; y: number }>>({});
let resizeObserver: ResizeObserver | null = null;
let saveTimer: number | null = null;

const priorityOptions = [
  { label: "低", value: "low" },
  { label: "普通", value: "normal" },
  { label: "高", value: "high" },
  { label: "紧急", value: "urgent" },
];

const tagOptions = computed(() => state.tags.map((tag) => ({ label: tag.name, value: tag.id })));
const sceneOptions = computed(() => state.scenes.map((scene) => ({ label: scene.name, value: scene.id })));
const dependencyOptions = computed(() =>
  state.tasks
    .filter((task) => task.status !== "done")
    .map((task) => ({ label: task.title, value: task.id })),
);
const selectedDependencyOptions = computed(() =>
  state.tasks
    .filter((task) => task.id !== selectedTaskId.value && task.status !== "done")
    .map((task) => ({ label: task.title, value: task.id })),
);
const selectedTask = computed(() => state.tasks.find((task) => task.id === selectedTaskId.value) ?? null);
const visibleTasks = computed(() => {
  const filters = activeTagIds.value;
  return state.tasks.filter((task) => filters.length === 0 || task.tagIds.some((tagId) => filters.includes(tagId)));
});
const orderedVisibleTasks = computed(() => [
  ...visibleTasks.value.filter((task) => task.id !== selectedTaskId.value),
  ...visibleTasks.value.filter((task) => task.id === selectedTaskId.value),
]);
const dependencyHighlight = computed(() => buildDependencyHighlight(selectedTaskId.value, state.tasks));
const doneTasks = computed(() => state.tasks.filter((task) => task.status === "done"));
const overdueTasks = computed(() => state.tasks.filter((task) => task.status !== "done" && Date.parse(task.dueAt) < Date.now()));
const recommendedTasks = computed(() => buildRecommendedOrder(visibleTasks.value));
const lanes = computed(() => [
  { id: "", name: "未分类", color: "#64748b" },
  ...state.tags.map((tag) => ({ id: tag.id, name: tag.name, color: tag.color })),
]);
const laneWidth = computed(() => Math.max(minLaneWidth, (boardSize.width - timeGutterWidth - laneGap) / Math.max(1, lanes.value.length)));
const taskWidth = computed(() => Math.max(112, Math.min(maxTaskWidth, laneWidth.value - taskInset * 2)));
const stageWidth = computed(() => Math.max(boardSize.width, timeGutterWidth + laneGap + lanes.value.length * laneWidth.value));
const laneBackgrounds = computed(() =>
  lanes.value.map((lane, index) => ({
    key: `lane-bg-${lane.id || "uncategorized"}`,
    config: {
      x: laneX(index) - taskInset,
      y: 0,
      width: laneWidth.value,
      height: boardSize.height,
      fill: index % 2 === 0 ? "rgba(255,255,255,0.3)" : "rgba(241,245,249,0.32)",
      stroke: "rgba(148,163,184,0.18)",
      strokeWidth: 1,
    },
  })),
);
const laneLabels = computed(() =>
  lanes.value.map((lane, index) => ({
    key: `lane-label-${lane.id || "uncategorized"}`,
    id: lane.id,
    name: lane.name,
    color: lane.color,
    draggable: Boolean(lane.id),
    config: {
      x: laneX(index),
      y: 6,
      draggable: Boolean(lane.id),
      cursor: lane.id ? "grab" : "default",
    },
  })),
);

const pixelsPerHour = computed(() => 24 * zoom.value);
const viewportStart = computed(() => Date.now() + scrollHours.value * hourMs());
const visibleTimeRange = computed(() => {
  const top = viewportStart.value;
  const bottom = viewportStart.value - boardSize.height / pixelsPerHour.value * hourMs();
  return {
    top,
    bottom,
    firstTick: floorToHour(bottom),
    lastTick: floorToHour(top) + hourMs(),
  };
});
const killY = computed(() => timestampToScreenY(Date.now()));
const boardBackgroundConfig = computed(() => ({
  x: 0,
  y: 0,
  width: stageWidth.value,
  height: boardSize.height,
  fillLinearGradientStartPoint: { x: 0, y: 0 },
  fillLinearGradientEndPoint: { x: stageWidth.value, y: boardSize.height },
  fillLinearGradientColorStops: [0, "#f8fbff", 0.7, "#eef4ff", 1, "#fff1f2"],
  cornerRadius: 8,
}));
const killLineConfig = computed(() => ({
  points: [24, killY.value, stageWidth.value - 24, killY.value],
  stroke: "#dc2626",
  strokeWidth: 3,
  dash: [10, 8],
}));
const killLineLabelConfig = computed(() => ({
  x: 32,
  y: Math.min(boardSize.height - 26, Math.max(10, killY.value + 10)),
  text: `斩杀线 ${formatDateTime(new Date(), true)}`,
  fontSize: 14,
  fontStyle: "bold",
  fill: "#b91c1c",
}));
const timeGridLines = computed(() => {
  const lines = [];
  const { firstTick, lastTick } = visibleTimeRange.value;
  for (let cursor = firstTick; cursor <= lastTick; cursor += hourMs()) {
    const date = new Date(cursor);
    const y = timestampToScreenY(cursor);
    if (y < -24 || y > boardSize.height + 24) {
      continue;
    }
    const isDayStart = date.getHours() === 0;
    const isSixHour = date.getHours() % 6 === 0;
    lines.push({
      key: `hour-${cursor}`,
      config: {
        points: [isDayStart ? 24 : timeGutterWidth, y, stageWidth.value - 24, y],
        stroke: isDayStart ? "#a7b3c6" : isSixHour ? "#d6deea" : "#e9eef6",
        strokeWidth: isDayStart ? 1.5 : 1,
      },
    });
  }
  return lines;
});
const timeLabels = computed(() => {
  const labels = [];
  const { firstTick, lastTick } = visibleTimeRange.value;
  for (let cursor = firstTick; cursor <= lastTick; cursor += hourMs()) {
    const date = new Date(cursor);
    const isDayStart = date.getHours() === 0;
    const isSixHour = date.getHours() % 6 === 0;
    if (!isDayStart && !isSixHour) {
      continue;
    }
    const y = timestampToScreenY(cursor);
    if (y < -24 || y > boardSize.height + 24) {
      continue;
    }
    labels.push({
      key: `label-${cursor}`,
      config: {
        x: 18,
        y: y + (isDayStart ? 6 : -7),
        width: timeGutterWidth - 26,
        text: isDayStart ? formatDateTime(date, false) : formatTickDateTime(date),
        fontSize: isDayStart ? 12 : 11,
        fontStyle: isDayStart ? "bold" : "normal",
        fill: isDayStart ? "#344054" : "#667085",
      },
    });
  }
  return labels;
});
const dependencyEdges = computed(() => {
  const centers = new Map<string, { x: number; y: number }>();
  visibleTasks.value.forEach((task, index) => {
    const pos = currentTaskPosition(task, index);
    centers.set(task.id, { x: pos.x + taskWidth.value / 2, y: pos.y + taskHeight(task) / 2 });
  });
  return visibleTasks.value.flatMap((task) =>
    task.dependencyIds.flatMap((dependencyId) => {
      const from = centers.get(dependencyId);
      const to = centers.get(task.id);
      if (!from || !to) {
        return [];
      }
      return [{
        key: `${dependencyId}-${task.id}`,
        config: dependencyEdgeConfig(dependencyId, task.id, from, to),
      }];
    }),
  );
});

onMounted(async () => {
  try {
    const loaded = await fetchSchedulerState();
    state.tasks = normalizeTasks(loaded.tasks);
    state.tags = loaded.tags ?? [];
    state.scenes = normalizeScenes(loaded.scenes);
    state.settings = { ...state.settings, ...(loaded.settings ?? {}) };
  } catch {
    // Keep the empty local state visible when the backend is not reachable in dev.
  }
  resizeObserver = new ResizeObserver(([entry]) => {
    if (!entry) return;
    boardSize.width = Math.max(520, Math.floor(entry.contentRect.width));
    boardSize.height = Math.max(520, Math.floor(entry.contentRect.height));
    clampScroll();
  });
  if (boardRef.value) {
    resizeObserver.observe(boardRef.value);
  }
  resetViewport();
});

onBeforeUnmount(() => {
  resizeObserver?.disconnect();
  if (saveTimer) {
    window.clearTimeout(saveTimer);
  }
});

function addTask() {
  const now = new Date().toISOString();
  const task: SchedulerTask = {
    id: crypto.randomUUID(),
    title: draftTitle.value.trim(),
    dueAt: new Date(draftDueAt.value || Date.now()).toISOString(),
    estimatedMinutes: draftMinutes.value || 60,
    priority: draftPriority.value,
    tagIds: [...draftTagIds.value],
    dependencyIds: [...draftDependencyIds.value],
    status: "todo",
    notes: draftNotes.value,
    createdAt: now,
  };
  state.tasks.push(task);
  selectedTaskId.value = task.id;
  draftTitle.value = "";
  draftNotes.value = "";
  draftDependencyIds.value = [];
  scheduleSave();
}

function addTag() {
  const name = newTagName.value.trim();
  if (!name || state.tags.some((tag) => tag.name === name)) {
    return;
  }
  const tag: SchedulerTag = {
    id: crypto.randomUUID(),
    name,
    color: tagPalette[state.tags.length % tagPalette.length],
  };
  state.tags.push(tag);
  newTagName.value = "";
  scheduleSave();
}

function saveScene() {
  const name = sceneName.value.trim();
  if (!name) {
    return;
  }
  const now = new Date().toISOString();
  const existing = state.scenes.find((scene) => scene.name === name);
  const scene: SchedulerScene = {
    id: existing?.id ?? crypto.randomUUID(),
    name,
    activeTagIds: [...activeTagIds.value],
    tagOrder: state.tags.map((tag) => tag.id),
    zoom: zoom.value,
    scrollHours: scrollHours.value,
    createdAt: existing?.createdAt ?? now,
    updatedAt: now,
  };
  if (existing) {
    Object.assign(existing, scene);
  } else {
    state.scenes.push(scene);
  }
  selectedSceneId.value = scene.id;
  scheduleSave();
}

function applySelectedScene() {
  const scene = state.scenes.find((item) => item.id === selectedSceneId.value);
  if (!scene) {
    return;
  }
  activeTagIds.value = scene.activeTagIds.filter((tagId) => state.tags.some((tag) => tag.id === tagId));
  applyTagOrder(scene.tagOrder);
  zoom.value = clamp(scene.zoom || 1, 0.45, 3);
  scrollHours.value = scene.scrollHours || scrollHours.value;
  clampScroll();
  sceneName.value = scene.name;
}

function deleteSelectedScene() {
  if (!selectedSceneId.value) {
    return;
  }
  state.scenes = state.scenes.filter((scene) => scene.id !== selectedSceneId.value);
  selectedSceneId.value = null;
  scheduleSave();
}

function applyTagOrder(tagOrder: string[]) {
  const order = new Map(tagOrder.map((id, index) => [id, index]));
  state.tags.sort((left, right) => {
    const leftIndex = order.get(left.id) ?? Number.MAX_SAFE_INTEGER;
    const rightIndex = order.get(right.id) ?? Number.MAX_SAFE_INTEGER;
    if (leftIndex !== rightIndex) {
      return leftIndex - rightIndex;
    }
    return left.name.localeCompare(right.name, "zh-CN");
  });
}

function selectTask(id: string) {
  selectedTaskId.value = id;
}

function selectTaskFromCanvas(id: string, event: { cancelBubble?: boolean }) {
  event.cancelBubble = true;
  selectTask(id);
}

function clearSelectedTask() {
  selectedTaskId.value = "";
}

function updateSelected(patch: Partial<SchedulerTask>) {
  const task = selectedTask.value;
  if (!task) {
    return;
  }
  Object.assign(task, sanitizeTaskPatch(task, patch));
  scheduleSave();
}

function updateSelectedDueAt(value: number | null) {
  if (!value) {
    return;
  }
  updateSelected({ dueAt: new Date(value).toISOString() });
}

function toggleDone(id: string) {
  const task = state.tasks.find((item) => item.id === id);
  if (!task) {
    return;
  }
  task.status = task.status === "done" ? "todo" : "done";
  task.completedAt = task.status === "done" ? new Date().toISOString() : undefined;
  scheduleSave();
}

function deleteTask(id: string) {
  state.tasks = state.tasks.filter((task) => task.id !== id);
  state.tasks.forEach((task) => {
    task.dependencyIds = task.dependencyIds.filter((dependencyId) => dependencyId !== id);
  });
  if (selectedTaskId.value === id) {
    selectedTaskId.value = "";
  }
  scheduleSave();
}

function handleTaskDragMove(id: string, event: { target: { x: () => number; y: () => number } }) {
  const task = state.tasks.find((item) => item.id === id);
  if (!task || task.status === "done") {
    return;
  }
  dragPositions[id] = {
    x: clampTaskX(event.target.x()),
    y: clampTaskY(event.target.y(), task),
  };
}

function handleTaskDragEnd(id: string, event: { target: { x: (value?: number) => number; y: (value?: number) => number } }) {
  const task = state.tasks.find((item) => item.id === id);
  if (!task || task.status === "done") {
    return;
  }
  const x = dragPositions[id]?.x ?? clampTaskX(event.target.x());
  const y = dragPositions[id]?.y ?? clampTaskY(event.target.y(), task);
  task.dueAt = timestampForY(y + taskHeight(task) / 2).toISOString();
  task.tagIds = tagIdsForLaneX(x, task);
  delete dragPositions[id];
  const index = visibleTasks.value.findIndex((item) => item.id === id);
  const next = taskPosition(task, index);
  event.target.x(next.x);
  event.target.y(taskPosition(task, index).y);
  scheduleSave();
}

function handleLaneDragEnd(id: string, event: { target: { x: (value?: number) => number; y: (value?: number) => number } }) {
  if (!id) {
    return;
  }
  const fromIndex = state.tags.findIndex((tag) => tag.id === id);
  if (fromIndex < 0) {
    return;
  }
  const targetLaneIndex = laneIndexForX(event.target.x());
  const toIndex = Math.max(0, Math.min(state.tags.length - 1, targetLaneIndex - 1));
  const [moved] = state.tags.splice(fromIndex, 1);
  state.tags.splice(toIndex, 0, moved);
  event.target.x(laneX(toIndex + 1));
  event.target.y(6);
  scheduleSave();
}

function taskGroupConfig(task: SchedulerTask) {
  const index = visibleTasks.value.findIndex((item) => item.id === task.id);
  const pos = currentTaskPosition(task, index);
  return {
    x: pos.x,
    y: pos.y,
    draggable: task.status !== "done",
    opacity: taskOpacity(task),
    cursor: "pointer",
  };
}

function currentTaskPosition(task: SchedulerTask, index: number) {
  return dragPositions[task.id] ?? taskPosition(task, index);
}

function laneLabelRectConfig(label: { id: string; color: string }) {
  return {
    x: 0,
    y: 0,
    width: taskWidth.value,
    height: 24,
    fill: label.id ? "rgba(255,255,255,0.68)" : "rgba(241,245,249,0.72)",
    stroke: label.id ? label.color : "rgba(100,116,139,0.35)",
    strokeWidth: 1,
    cornerRadius: 6,
  };
}

function laneLabelTextConfig(label: { name: string; color: string }) {
  return {
    x: 10,
    y: 5,
    width: Math.max(40, taskWidth.value - 20),
    text: label.name,
    fontSize: 12,
    fontStyle: "bold",
    fill: label.color,
    ellipsis: true,
  };
}

function taskRelation(taskId: string): "self" | "upstream" | "downstream" | null {
  const highlight = dependencyHighlight.value;
  if (!highlight.selectedId) return null;
  if (taskId === highlight.selectedId) return "self";
  if (highlight.upstream.has(taskId)) return "upstream";
  if (highlight.downstream.has(taskId)) return "downstream";
  return null;
}

function taskOpacity(task: SchedulerTask) {
  const baseOpacity = task.status === "done" ? 0.44 : 1;
  if (!dependencyHighlight.value.selectedId) {
    return baseOpacity;
  }
  return taskRelation(task.id) ? baseOpacity : 0.28;
}

function dependencyEdgeConfig(
  fromId: string,
  toId: string,
  from: { x: number; y: number },
  to: { x: number; y: number },
) {
  const edgeId = `${fromId}->${toId}`;
  const upstream = dependencyHighlight.value.upstreamEdges.has(edgeId);
  const downstream = dependencyHighlight.value.downstreamEdges.has(edgeId);
  const highlighted = upstream || downstream;
  const edgeColor = upstream ? "#0891b2" : downstream ? "#7c3aed" : "#94a3b8";
  return {
    points: edgePoints(from, to),
    stroke: edgeColor,
    fill: edgeColor,
    strokeWidth: highlighted ? 3 : 1.8,
    dash: highlighted ? [] : [5, 5],
    opacity: dependencyHighlight.value.selectedId && !highlighted ? 0.22 : 1,
    pointerLength: highlighted ? 14 : 12,
    pointerWidth: highlighted ? 12 : 10,
  };
}

function taskRectConfig(task: SchedulerTask) {
  const relation = taskRelation(task.id);
  return {
    x: 0,
    y: 0,
    width: taskWidth.value,
    height: taskHeight(task),
    fill: taskColor(task),
    stroke: relation === "self" ? "#111827" : relation === "upstream" ? "#0891b2" : relation === "downstream" ? "#7c3aed" : isBlocked(task) ? "#f59e0b" : "#ffffff",
    strokeWidth: relation ? 3 : 2,
    cornerRadius: 8,
    shadowColor: "#0f172a",
    shadowOpacity: task.status === "done" ? 0.08 : 0.16,
    shadowBlur: 12,
    shadowOffsetY: 4,
  };
}

function taskTitleConfig(task: SchedulerTask) {
  return {
    x: 12,
    y: 10,
    width: Math.max(40, taskWidth.value - 24),
    text: task.title,
    fontSize: 14,
    fontStyle: "bold",
    fill: "#ffffff",
    ellipsis: true,
  };
}

function taskMetaConfig(task: SchedulerTask) {
  const dependencyText = isBlocked(task) ? "依赖未完成" : `${task.estimatedMinutes} min`;
  return {
    x: 12,
    y: Math.max(34, taskHeight(task) - 24),
    width: Math.max(40, taskWidth.value - 24),
    text: `${priorityLabel(task.priority)} · ${dependencyText}`,
    fontSize: 12,
    fill: "rgba(255,255,255,0.82)",
    ellipsis: true,
  };
}

function taskPosition(task: SchedulerTask, index: number) {
  const laneIndex = laneIndexForTask(task);
  return {
    x: laneTaskX(laneIndex),
    y: clampTaskY(yForTimestamp(Date.parse(task.dueAt)) - taskHeight(task) / 2, task),
  };
}

function edgePoints(from: { x: number; y: number }, to: { x: number; y: number }) {
  const sameLane = Math.abs(from.x - to.x) < taskWidth.value * 0.6;
  if (sameLane) {
    const verticalDirection = to.y >= from.y ? 1 : -1;
    const startY = from.y + verticalDirection * 42;
    const endY = to.y - verticalDirection * 46;
    if (Math.abs(endY - startY) >= 18) {
      return [
        from.x,
        startY,
        to.x,
        endY,
      ];
    }
    const offsetX = Math.min(26, taskWidth.value * 0.18);
    const sideX = from.x + offsetX;
    return [
      from.x,
      startY,
      sideX,
      startY,
      sideX,
      endY,
      to.x,
      endY,
    ];
  }

  const horizontalDirection = to.x >= from.x ? 1 : -1;
  const startX = from.x + horizontalDirection * (taskWidth.value / 2 + 8);
  const endX = to.x - horizontalDirection * (taskWidth.value / 2 + 12);
  const midX = startX + (endX - startX) / 2;
  return [
    startX,
    from.y,
    midX,
    from.y,
    midX,
    to.y,
    endX,
    to.y,
  ];
}

function laneX(index: number) {
  return timeGutterWidth + laneGap + index * laneWidth.value;
}

function laneTaskX(index: number) {
  return laneX(index) + Math.max(0, (laneWidth.value - taskWidth.value) / 2) - taskInset;
}

function laneIndexForTask(task: SchedulerTask) {
  const primaryTagId = task.tagIds[0] ?? "";
  const index = lanes.value.findIndex((lane) => lane.id === primaryTagId);
  return Math.max(0, index);
}

function laneIndexForX(x: number) {
  const index = Math.round((x - timeGutterWidth - laneGap + taskInset) / laneWidth.value);
  return Math.max(0, Math.min(lanes.value.length - 1, index));
}

function tagIdsForLaneX(x: number, task: SchedulerTask) {
  const lane = lanes.value[laneIndexForX(x)];
  const validTagIds = task.tagIds.filter((tagId) => state.tags.some((tag) => tag.id === tagId));
  const rest = validTagIds.filter((tagId) => tagId !== lane?.id);
  if (!lane?.id) {
    return [];
  }
  return [lane.id, ...rest];
}

function clampTaskX(x: number) {
  const min = laneTaskX(0);
  const max = laneTaskX(Math.max(0, lanes.value.length - 1));
  return Math.max(min, Math.min(max, x));
}

function clampTaskY(y: number, task: SchedulerTask) {
  return Math.max(laneHeaderHeight + 12, Math.min(boardSize.height - taskHeight(task) - 18, y));
}

function taskHeight(task: SchedulerTask) {
  return Math.max(58, Math.min(132, 44 + task.estimatedMinutes / 2));
}

function taskColor(task: SchedulerTask) {
  if (task.status === "done") {
    return "#64748b";
  }
  if (Date.parse(task.dueAt) < Date.now()) {
    return "#dc2626";
  }
  const tag = state.tags.find((item) => item.id === task.tagIds[0]);
  if (tag) {
    return tag.color;
  }
  return task.priority === "urgent" ? "#be123c" : task.priority === "high" ? "#ea580c" : "#2563eb";
}

function yForTimestamp(timestamp: number) {
  return timestampToScreenY(timestamp);
}

function timestampForY(y: number) {
  return new Date(viewportStart.value - y / pixelsPerHour.value * hourMs());
}

function timestampToScreenY(timestamp: number) {
  return (viewportStart.value - timestamp) / hourMs() * pixelsPerHour.value;
}

function handleBoardWheel(event: WheelEvent) {
  if (event.shiftKey) {
    const board = event.currentTarget instanceof HTMLElement ? event.currentTarget : null;
    if (board) {
      board.scrollLeft += event.deltaY;
    }
    return;
  }
  if (event.ctrlKey || event.metaKey) {
    const nextZoom = clamp(zoom.value * (event.deltaY > 0 ? 0.9 : 1.1), 0.45, 3);
    const pointerHour = event.offsetY / pixelsPerHour.value;
    const pointerTime = viewportStart.value - pointerHour * hourMs();
    zoom.value = nextZoom;
    scrollHours.value = (pointerTime - Date.now()) / hourMs() + event.offsetY / pixelsPerHour.value;
    clampScroll();
    return;
  }
  scrollHours.value -= event.deltaY / pixelsPerHour.value;
  clampScroll();
}

function zoomIn() {
  zoomAtCenter(1.2);
}

function zoomOut() {
  zoomAtCenter(1 / 1.2);
}

function panLanes(direction: -1 | 1) {
  if (!boardRef.value) {
    return;
  }
  boardRef.value.scrollBy({
    left: direction * laneWidth.value,
    behavior: "smooth",
  });
}

function zoomAtCenter(factor: number) {
  const centerY = boardSize.height / 2;
  const centerTime = viewportStart.value - centerY / pixelsPerHour.value * hourMs();
  zoom.value = clamp(zoom.value * factor, 0.45, 3);
  scrollHours.value = (centerTime - Date.now()) / hourMs() + centerY / pixelsPerHour.value;
  clampScroll();
}

function resetViewport() {
  scrollHours.value = boardSize.height * 0.68 / pixelsPerHour.value;
}

function clampScroll() {
  const minHours = -state.settings.overdueDays * 24 + boardSize.height / pixelsPerHour.value;
  const maxHours = state.settings.horizonDays * 24;
  scrollHours.value = clamp(scrollHours.value, Math.min(minHours, maxHours), maxHours);
}

function isBlocked(task: SchedulerTask) {
  return task.dependencyIds.some((id) => state.tasks.find((item) => item.id === id)?.status !== "done");
}

function buildRecommendedOrder(tasks: SchedulerTask[]) {
  const pending = tasks.filter((task) => task.status !== "done");
  const result: SchedulerTask[] = [];
  const remaining = new Map(pending.map((task) => [task.id, task]));
  while (remaining.size > 0) {
    const batch = Array.from(remaining.values()).sort(compareTasksForRecommendation);
    const ready = batch.find((task) => task.dependencyIds.every((id) => !remaining.has(id)));
    const picked = ready ?? batch[0];
    result.push(picked);
    remaining.delete(picked.id);
  }
  return result;
}

function buildDependencyHighlight(selectedId: string, tasks: SchedulerTask[]) {
  const upstream = new Set<string>();
  const downstream = new Set<string>();
  const upstreamEdges = new Set<string>();
  const downstreamEdges = new Set<string>();
  if (!selectedId) {
    return { selectedId: "", upstream, downstream, upstreamEdges, downstreamEdges };
  }

  const byId = new Map(tasks.map((task) => [task.id, task]));
  const collectUpstream = (id: string) => {
    const task = byId.get(id);
    if (!task) return;
    task.dependencyIds.forEach((dependencyId) => {
      upstreamEdges.add(`${dependencyId}->${id}`);
      if (upstream.has(dependencyId)) return;
      upstream.add(dependencyId);
      collectUpstream(dependencyId);
    });
  };
  const collectDownstream = (id: string) => {
    tasks.forEach((task) => {
      if (!task.dependencyIds.includes(id)) return;
      downstreamEdges.add(`${id}->${task.id}`);
      if (downstream.has(task.id)) return;
      downstream.add(task.id);
      collectDownstream(task.id);
    });
  };

  collectUpstream(selectedId);
  collectDownstream(selectedId);
  return { selectedId, upstream, downstream, upstreamEdges, downstreamEdges };
}

function compareTasksForRecommendation(left: SchedulerTask, right: SchedulerTask) {
  const leftBlocked = isBlocked(left) ? 1 : 0;
  const rightBlocked = isBlocked(right) ? 1 : 0;
  if (leftBlocked !== rightBlocked) return leftBlocked - rightBlocked;
  const priorityDiff = priorityWeight[right.priority] - priorityWeight[left.priority];
  if (priorityDiff !== 0) return priorityDiff;
  return Date.parse(left.dueAt) - Date.parse(right.dueAt);
}

function sanitizeTaskPatch(task: SchedulerTask, patch: Partial<SchedulerTask>) {
  const next = { ...patch };
  if (next.dependencyIds) {
    next.dependencyIds = next.dependencyIds.filter((id) => id !== task.id && !wouldCreateCycle(task.id, id));
  }
  return next;
}

function wouldCreateCycle(taskId: string, dependencyId: string) {
  const visit = (id: string, seen = new Set<string>()): boolean => {
    if (id === taskId) return true;
    if (seen.has(id)) return false;
    seen.add(id);
    const task = state.tasks.find((item) => item.id === id);
    return Boolean(task?.dependencyIds.some((nextId) => visit(nextId, seen)));
  };
  return visit(dependencyId);
}

function normalizeTasks(tasks: SchedulerTask[]) {
  return (tasks ?? []).map((task) => ({
    ...task,
    tagIds: task.tagIds ?? [],
    dependencyIds: task.dependencyIds ?? [],
    estimatedMinutes: task.estimatedMinutes || 60,
    priority: task.priority || "normal",
    status: task.status || "todo",
    notes: task.notes ?? "",
  }));
}

function normalizeScenes(scenes: SchedulerScene[] | undefined) {
  return (scenes ?? []).map((scene) => ({
    ...scene,
    activeTagIds: scene.activeTagIds ?? [],
    tagOrder: scene.tagOrder ?? [],
    zoom: scene.zoom || 1,
    scrollHours: scene.scrollHours || 0,
    createdAt: scene.createdAt ?? new Date().toISOString(),
    updatedAt: scene.updatedAt ?? scene.createdAt ?? new Date().toISOString(),
  }));
}

function scheduleSave() {
  if (saveTimer) {
    window.clearTimeout(saveTimer);
  }
  saveTimer = window.setTimeout(() => {
    void saveSchedulerState(JSON.parse(JSON.stringify(state))).catch(() => {
      // A later edit will retry saving the full state.
    });
  }, 300);
}

function priorityLabel(priority: SchedulerTaskPriority) {
  return priorityOptions.find((item) => item.value === priority)?.label ?? "普通";
}

function formatDateTime(date: Date, includeTime = true) {
  return new Intl.DateTimeFormat("zh-CN", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    ...(includeTime ? { hour: "2-digit", minute: "2-digit" } : {}),
  }).format(date);
}

function formatTickDateTime(date: Date) {
  return new Intl.DateTimeFormat("zh-CN", {
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  }).format(date);
}

function floorToHour(timestamp: number) {
  const date = new Date(timestamp);
  date.setMinutes(0, 0, 0);
  return date.getTime();
}

function hourMs() {
  return 60 * 60 * 1000;
}

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}
</script>

<style scoped>
.scheduler-layout {
  display: grid;
  grid-template-columns: 280px minmax(520px, 1fr) 320px;
  gap: 16px;
  height: calc(100vh - 150px);
  min-height: 720px;
}

.scheduler-sidebar,
.scheduler-inspector {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.scheduler-board-card {
  display: flex;
  flex-direction: column;
  min-width: 0;
  border: 1px solid rgba(148, 163, 184, 0.24);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.78);
  overflow: hidden;
}

.board-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 16px 18px;
  border-bottom: 1px solid rgba(148, 163, 184, 0.2);
}

.board-controls {
  padding: 10px 18px;
  border-bottom: 1px solid rgba(148, 163, 184, 0.16);
  background: rgba(248, 250, 252, 0.74);
}

.board-hint {
  color: #667085;
  font-size: 12px;
}

.board-toolbar h3,
.board-eyebrow {
  margin: 0;
}

.board-eyebrow {
  color: #667085;
  font-size: 12px;
  text-transform: uppercase;
}

.scheduler-board {
  flex: 1;
  min-height: 0;
  overflow: auto;
  touch-action: none;
}

.tag-label {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.tag-label::before {
  content: "";
  width: 9px;
  height: 9px;
  border-radius: 50%;
  background: var(--tag-color);
}

.recommend-row {
  display: grid;
  grid-template-columns: 24px 1fr auto;
  align-items: center;
  gap: 8px;
  padding: 9px 10px;
  border: 1px solid rgba(148, 163, 184, 0.22);
  border-radius: 8px;
  cursor: pointer;
  background: #ffffff;
}

.recommend-row.blocked {
  background: #fffbeb;
}

@media (max-width: 1180px) {
  .scheduler-layout {
    grid-template-columns: 1fr;
    height: auto;
    min-height: 0;
  }

  .scheduler-board {
    height: 620px;
    flex: none;
  }
}

@media (max-width: 720px) {
  .scheduler-layout {
    gap: 12px;
  }

  .scheduler-sidebar,
  .scheduler-inspector {
    gap: 12px;
    min-width: 0;
  }

  .scheduler-board-card {
    margin-inline: -4px;
  }

  .board-toolbar {
    align-items: flex-start;
    flex-direction: column;
    padding: 14px;
  }

  .board-toolbar h3 {
    font-size: 18px;
  }

  .board-controls {
    padding: 10px 14px;
    overflow-x: auto;
  }

  .board-controls :deep(.n-space) {
    flex-wrap: nowrap !important;
    min-width: max-content;
  }

  .board-hint {
    display: none;
  }

  .scheduler-board {
    height: 560px;
    -webkit-overflow-scrolling: touch;
  }

  .recommend-row {
    grid-template-columns: 22px minmax(0, 1fr) auto;
  }

  .recommend-row span {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
}

@media (max-width: 480px) {
  .scheduler-board {
    height: 520px;
  }
}
</style>
