<template>
  <div class="habit-app">
    <header class="habit-intro">
      <div>
        <p>{{ todayLabel }}</p>
        <h2>把今天过得更有意识一点</h2>
        <span>完成想坚持的事，也诚实记录想减少的事。</span>
      </div>
      <div class="intro-actions">
        <small :class="{ error: saveState === '保存失败' }">{{ saveState }}</small>
        <n-button type="primary" @click="openCreate">
          <template #icon><n-icon><AddOutline /></n-icon></template>
          新建习惯
        </n-button>
      </div>
    </header>

    <nav class="habit-tabs" aria-label="习惯功能">
      <button
        v-for="view in viewOptions"
        :key="view.key"
        type="button"
        :class="{ active: activeView === view.key }"
        @click="activeView = view.key"
      >
        {{ view.label }}
      </button>
    </nav>

    <n-spin :show="loading">
      <n-alert v-if="loadError" type="error" title="习惯数据加载失败">
        {{ loadError }}
        <n-button size="small" @click="loadState">重试</n-button>
      </n-alert>

      <section v-else-if="!state.habits.length" class="empty-state">
        <div class="empty-mark">✓</div>
        <h3>先从一个很小的行动开始</h3>
        <p>例如“晚饭后散步 10 分钟”，明确、轻松，也更容易持续。</p>
        <n-button type="primary" @click="openCreate">创建第一个习惯</n-button>
      </section>

      <template v-else-if="activeView === 'today'">
        <section class="summary-row" aria-label="本周概览">
          <article>
            <span>今天</span>
            <strong>{{ todayCompleted }}/{{ todayBuildHabits.length }}</strong>
            <small>培养行动已完成</small>
          </article>
          <article>
            <span>本周</span>
            <strong>{{ weeklyCompleted }}/{{ weeklyPlanned }}</strong>
            <small>计划完成进度</small>
          </article>
          <article>
            <span>减少行为</span>
            <strong>{{ weeklyOccurrences }}</strong>
            <small>本周发生 · {{ weeklyReplacements }} 次替代</small>
          </article>
        </section>

        <div class="habit-dashboard">
          <section class="surface today-surface">
            <div class="surface-heading">
              <div>
                <p>Today</p>
                <h3>今天</h3>
              </div>
              <span>{{ todayHabits.length }} 项</span>
            </div>

            <div v-if="todayHabits.length" class="today-list">
              <article v-for="habit in todayHabits" :key="habit.id" class="today-item">
                <i class="color-bar" :style="{ background: habit.color }"></i>
                <div class="today-copy">
                  <div class="item-title">
                    <div>
                      <span class="kind-tag" :class="`kind-tag--${habit.kind}`">
                        {{ habit.kind === "build" ? "培养" : "减少" }}
                      </span>
                      <h4>{{ habit.title }}</h4>
                    </div>
                    <n-dropdown :options="habitMenuOptions" @select="(key) => handleHabitMenu(key, habit)">
                      <n-button quaternary circle aria-label="习惯操作">···</n-button>
                    </n-dropdown>
                  </div>

                  <template v-if="habit.kind === 'build'">
                    <p class="item-meta">
                      {{ scheduleLabel(habit) }}
                    </p>
                    <div class="build-actions">
                      <button
                        class="done-button"
                        :class="{ active: logFor(habit.id, today)?.completed }"
                        type="button"
                        @click="toggleCompleted(habit.id)"
                      >
                        <n-icon><CheckmarkOutline /></n-icon>
                        {{ logFor(habit.id, today)?.completed ? "今天已完成" : "标记完成" }}
                      </button>
                      <button
                        v-if="!logFor(habit.id, today)?.completed"
                        class="text-action"
                        type="button"
                        @click="toggleSkipped(habit.id)"
                      >
                        {{ logFor(habit.id, today)?.skipped ? "取消跳过" : "今天跳过" }}
                      </button>
                    </div>
                  </template>

                  <template v-else>
                    <p class="item-meta">
                      {{ habit.alternative ? `替代方案：${habit.alternative}` : "需要时记录，不做评价。" }}
                    </p>
                    <div class="counter-actions">
                      <div class="counter-stepper">
                        <span>发生</span>
                        <div>
                          <button type="button" aria-label="减少一次发生" @click="changeCounter(habit.id, 'occurrences', -1)">−</button>
                          <strong>{{ logFor(habit.id, today)?.occurrences || 0 }}</strong>
                          <button type="button" aria-label="增加一次发生" @click="changeCounter(habit.id, 'occurrences', 1)">+</button>
                        </div>
                      </div>
                      <div class="counter-stepper">
                        <span>做了替代</span>
                        <div>
                          <button type="button" aria-label="减少一次替代" @click="changeCounter(habit.id, 'replacements', -1)">−</button>
                          <strong>{{ logFor(habit.id, today)?.replacements || 0 }}</strong>
                          <button type="button" aria-label="增加一次替代" @click="changeCounter(habit.id, 'replacements', 1)">+</button>
                        </div>
                      </div>
                      <button
                        v-if="!hasReduceLog(habit.id)"
                        class="confirm-zero"
                        type="button"
                        @click="confirmReduceDay(habit.id)"
                      >
                        今日无发生
                      </button>
                      <span v-else-if="logFor(habit.id, today)?.confirmed && !hasReduceCounts(habit.id)" class="confirmed-zero">
                        已确认今日无发生
                      </span>
                      <button
                        v-if="hasReduceLog(habit.id)"
                        class="undo-action"
                        type="button"
                        @click="clearToday(habit.id)"
                      >
                        清除今日
                      </button>
                    </div>
                  </template>
                </div>
              </article>
            </div>
            <div v-else class="section-empty">今天没有安排，留一点空白也很好。</div>
          </section>

          <aside class="dashboard-side">
            <section class="surface rhythm-surface">
              <div class="surface-heading">
                <div>
                  <p>This week</p>
                  <h3>本周节奏</h3>
                </div>
              </div>
              <div v-if="buildHabits.length" class="rhythm-list">
                <button
                  v-for="habit in buildHabits"
                  :key="habit.id"
                  type="button"
                  class="rhythm-item"
                  @click="selectedHabitId = habit.id"
                >
                  <div>
                    <i :style="{ background: habit.color }"></i>
                    <span>{{ habit.title }}</span>
                    <em>{{ weeklyCount(habit.id) }}/{{ weeklyTarget(habit) }}</em>
                  </div>
                  <div class="progress-track">
                    <i :style="{ width: `${weeklyPercent(habit)}%`, background: habit.color }"></i>
                  </div>
                </button>
              </div>
              <p v-else class="section-empty compact">还没有培养类型的习惯。</p>
            </section>
          </aside>
        </div>
      </template>

      <template v-else-if="activeView === 'stats'">
        <section class="surface stats-toolbar">
          <div>
            <p>时间范围</p>
            <div class="range-presets">
              <button
                v-for="preset in rangePresets"
                :key="preset.key"
                type="button"
                :class="{ active: rangePreset === preset.key }"
                @click="setRangePreset(preset.key)"
              >
                {{ preset.label }}
              </button>
            </div>
          </div>
          <div class="custom-range">
            <label>开始日期<input v-model="rangeStart" type="date" :max="rangeEnd" @change="rangePreset = 'custom'" /></label>
            <span>至</span>
            <label>结束日期<input v-model="rangeEnd" type="date" :min="rangeStart" :max="today" @change="rangePreset = 'custom'" /></label>
          </div>
        </section>

        <section class="summary-row stats-summary" aria-label="统计概览">
          <article>
            <span>培养完成率</span>
            <strong>{{ aggregateStats.buildRate }}%</strong>
            <small>{{ aggregateStats.completed }}/{{ aggregateStats.planned }} 次完成</small>
          </article>
          <article>
            <span>跳过 / 未完成</span>
            <strong>{{ aggregateStats.skipped }}/{{ aggregateStats.missed }}</strong>
            <small>只统计已到期的培养计划</small>
          </article>
          <article>
            <span>减少行为</span>
            <strong>{{ aggregateStats.occurrences }}</strong>
            <small>{{ aggregateStats.replacements }} 次替代 · {{ aggregateStats.confirmedDays }} 天有记录</small>
          </article>
        </section>

        <div class="stats-grid">
          <section class="surface trend-surface">
            <div class="surface-heading">
              <div><p>Trend</p><h3>趋势</h3></div>
              <span>{{ rangeStart }} 至 {{ rangeEnd }}</span>
            </div>
            <div v-if="trendBuckets.length" class="trend-chart">
              <div v-for="bucket in trendBuckets" :key="bucket.key" class="trend-column">
                <div class="trend-bars">
                  <i class="build-bar" :style="{ height: `${bucket.buildHeight}%` }" :title="`培养完成率 ${bucket.buildRate}%`"></i>
                  <i class="reduce-bar" :style="{ height: `${bucket.reduceHeight}%` }" :title="`减少行为发生 ${bucket.occurrences} 次`"></i>
                </div>
                <span>{{ bucket.showLabel ? bucket.label : "" }}</span>
              </div>
            </div>
            <div v-else class="section-empty compact">这个时间段还没有数据。</div>
            <div class="chart-legend"><span><i class="build-dot"></i>培养完成率</span><span><i class="reduce-dot"></i>减少行为发生次数</span></div>
          </section>

          <section class="surface heatmap-surface">
            <div class="surface-heading history-heading">
              <div><p>Calendar</p><h3>每日轨迹</h3></div>
              <n-select v-model:value="selectedHabitId" :options="statsHabitOptions" size="small" />
            </div>
            <p class="surface-note">点击日期可补录或修正记录，最多展示最近一年。</p>
            <div v-if="selectedHabit" class="heatmap-grid">
              <button
                v-for="day in heatmapDays"
                :key="day.date"
                type="button"
                class="heatmap-day"
                :class="historyClass(selectedHabit, day.date)"
                :title="historyTitle(selectedHabit, day.date)"
                :disabled="!canEditHistory(selectedHabit, day.date)"
                @click="openHistoryEditor(selectedHabit, day.date)"
              >
                <span>{{ day.monthDay }}</span>
                <i></i>
              </button>
            </div>
          </section>

          <section class="surface comparison-surface">
            <div class="surface-heading">
              <div><p>Habits</p><h3>习惯对比</h3></div>
              <span>点击一行查看每日轨迹</span>
            </div>
            <div class="stats-table-wrap">
              <table class="stats-table">
                <thead><tr><th>习惯</th><th>类型</th><th>计划/记录</th><th>完成/发生</th><th>跳过/替代</th><th>完成率</th></tr></thead>
                <tbody>
                  <tr
                    v-for="item in habitStats"
                    :key="item.habit.id"
                    :class="{ active: selectedHabitId === item.habit.id }"
                    @click="selectedHabitId = item.habit.id"
                  >
                    <td><i :style="{ background: item.habit.color }"></i>{{ item.habit.title }}</td>
                    <td>{{ item.habit.kind === 'build' ? '培养' : '减少' }}</td>
                    <template v-if="item.habit.kind === 'build'">
                      <td>{{ item.planned }}</td><td>{{ item.completed }}</td><td>{{ item.skipped }}</td><td>{{ item.rate }}%</td>
                    </template>
                    <template v-else>
                      <td>{{ item.confirmedDays }} 天</td><td>{{ item.occurrences }}</td><td>{{ item.replacements }}</td><td>—</td>
                    </template>
                  </tr>
                </tbody>
              </table>
            </div>
          </section>
        </div>
      </template>

      <template v-else>
        <div class="management-grid">
          <section class="surface">
            <div class="surface-heading">
              <div><p>Active</p><h3>正在进行</h3></div><span>{{ activeHabits.length }}</span>
            </div>
            <div v-if="activeHabits.length" class="manage-list">
              <article v-for="habit in activeHabits" :key="habit.id">
                <i :style="{ background: habit.color }"></i>
                <div><strong>{{ habit.title }}</strong><small>{{ habit.kind === 'build' ? scheduleLabel(habit) : `减少行为${habit.alternative ? ` · ${habit.alternative}` : ''}` }}</small></div>
                <n-button size="small" secondary @click="openEdit(habit)">编辑</n-button>
                <n-button size="small" quaternary @click="archiveHabit(habit)">归档</n-button>
              </article>
            </div>
            <p v-else class="section-empty compact">没有正在进行的习惯。</p>
          </section>
          <section class="surface">
            <div class="surface-heading">
              <div><p>Archived</p><h3>已归档</h3></div><span>{{ archivedHabits.length }}</span>
            </div>
            <div v-if="archivedHabits.length" class="manage-list archived-list">
              <article v-for="habit in archivedHabits" :key="habit.id">
                <i :style="{ background: habit.color }"></i>
                <div><strong>{{ habit.title }}</strong><small>历史记录已保留</small></div>
                <n-button size="small" secondary @click="restoreHabit(habit)">恢复</n-button>
              </article>
            </div>
            <p v-else class="section-empty compact">还没有归档习惯。</p>
          </section>
        </div>
      </template>
    </n-spin>

    <n-modal
      v-model:show="showEditor"
      preset="card"
      :title="editingId ? '编辑习惯' : '新建习惯'"
      :mask-closable="false"
      class="habit-editor"
    >
      <n-form label-placement="top" @submit.prevent="saveHabit">
        <n-form-item label="类型">
          <div class="kind-picker">
            <button
              type="button"
              :class="{ active: draft.kind === 'build' }"
              :disabled="Boolean(editingId)"
              @click="draft.kind = 'build'"
            >
              <strong>培养</strong>
              <span>完成想坚持的行动</span>
            </button>
            <button
              type="button"
              :class="{ active: draft.kind === 'reduce' }"
              :disabled="Boolean(editingId)"
              @click="draft.kind = 'reduce'"
            >
              <strong>减少</strong>
              <span>记录触发和替代选择</span>
            </button>
          </div>
          <small v-if="editingId" class="form-hint">类型创建后不可修改，避免历史记录被重新解释。</small>
        </n-form-item>

        <n-form-item label="名称" :feedback="titleError" :validation-status="titleError ? 'error' : undefined">
          <n-input
            v-model:value="draft.title"
            maxlength="40"
            show-count
            placeholder="例如：晚饭后散步 10 分钟"
            @update:value="titleError = ''"
          />
        </n-form-item>

        <template v-if="draft.kind === 'build'">
          <n-form-item label="计划方式">
            <div class="schedule-picker">
              <button
                type="button"
                :class="{ active: draft.scheduleMode === 'weekdays' }"
                @click="draft.scheduleMode = 'weekdays'"
              >
                指定星期
              </button>
              <button
                type="button"
                :class="{ active: draft.scheduleMode === 'weeklyTarget' }"
                @click="draft.scheduleMode = 'weeklyTarget'"
              >
                每周次数
              </button>
            </div>
          </n-form-item>
          <n-form-item v-if="draft.scheduleMode === 'weekdays'" label="安排在">
            <div class="weekday-picker">
              <button
                v-for="day in weekdayOptions"
                :key="day.value"
                type="button"
                :class="{ active: draft.weekdays.includes(day.value) }"
                @click="toggleWeekday(day.value)"
              >
                {{ day.short }}
              </button>
            </div>
          </n-form-item>
          <n-form-item v-else label="每周完成">
            <n-input-number v-model:value="draft.targetPerWeek" :min="1" :max="7">
              <template #suffix>次</template>
            </n-input-number>
          </n-form-item>
        </template>

        <n-form-item v-else label="替代方案（可选）">
          <n-input
            v-model:value="draft.alternative"
            maxlength="80"
            show-count
            placeholder="例如：先喝一杯水，等 10 分钟"
          />
        </n-form-item>

        <n-form-item label="颜色">
          <div class="color-picker">
            <button
              v-for="color in colors"
              :key="color"
              type="button"
              :style="{ background: color }"
              :class="{ active: draft.color === color }"
              :aria-label="`选择颜色 ${color}`"
              @click="draft.color = color"
            ></button>
          </div>
        </n-form-item>

        <div class="editor-actions">
          <n-button @click="showEditor = false">取消</n-button>
          <n-button type="primary" attr-type="submit">{{ editingId ? "保存" : "创建习惯" }}</n-button>
        </div>
      </n-form>
    </n-modal>

    <n-modal
      v-model:show="showHistoryEditor"
      preset="card"
      :title="historyEditorTitle"
      :mask-closable="false"
      class="history-editor"
    >
      <template v-if="historyEditingHabit?.kind === 'build'">
        <p class="modal-note">为这一天选择一个状态。清除后会重新按“未完成/无安排”计算。</p>
        <div class="history-status-actions">
          <button type="button" :class="{ active: historyDraftStatus === 'completed' }" @click="historyDraftStatus = 'completed'">完成</button>
          <button type="button" :class="{ active: historyDraftStatus === 'skipped' }" @click="historyDraftStatus = 'skipped'">跳过</button>
          <button type="button" :class="{ active: historyDraftStatus === 'empty' }" @click="historyDraftStatus = 'empty'">清除记录</button>
        </div>
      </template>
      <template v-else-if="historyEditingHabit">
        <p class="modal-note">记录当天发生与成功替代的次数；两个数字可以独立修正。</p>
        <div class="history-counter-grid">
          <label>发生次数<n-input-number v-model:value="historyDraftOccurrences" :min="0" /></label>
          <label>替代次数<n-input-number v-model:value="historyDraftReplacements" :min="0" /></label>
        </div>
        <label class="confirm-check"><input v-model="historyDraftConfirmed" type="checkbox" /> 确认当天已记录（包括零次发生）</label>
      </template>
      <div class="editor-actions">
        <n-button @click="showHistoryEditor = false">取消</n-button>
        <n-button type="primary" @click="saveHistoryEdit">保存记录</n-button>
      </div>
    </n-modal>
  </div>
</template>

<script setup lang="ts">
import { AddOutline, CheckmarkOutline } from "@vicons/ionicons5";
import {
  NAlert,
  NButton,
  NDropdown,
  NForm,
  NFormItem,
  NIcon,
  NInput,
  NInputNumber,
  NModal,
  NSelect,
  NSpin,
  useMessage,
} from "naive-ui";
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import {
  fetchHabitState,
  saveHabitState,
  type Habit,
  type HabitKind,
  type HabitLog,
  type HabitScheduleMode,
  type HabitState,
} from "@/api";

type ViewKey = "today" | "stats" | "manage";
type RangePreset = "7" | "30" | "90" | "month" | "year" | "custom";

interface HabitStat {
  habit: Habit;
  planned: number;
  completed: number;
  skipped: number;
  missed: number;
  rate: number;
  occurrences: number;
  replacements: number;
  confirmedDays: number;
}

const message = useMessage();
const colors = ["#47745f", "#3b7190", "#6f5d91", "#a55f48", "#a27c31", "#536575"];
const viewOptions: { key: ViewKey; label: string }[] = [
  { key: "today", label: "今天" },
  { key: "stats", label: "统计" },
  { key: "manage", label: "管理" },
];
const rangePresets: { key: RangePreset; label: string }[] = [
  { key: "7", label: "近 7 天" },
  { key: "30", label: "近 30 天" },
  { key: "90", label: "近 90 天" },
  { key: "month", label: "本月" },
  { key: "year", label: "今年" },
  { key: "custom", label: "自定义" },
];
const weekdayOptions = [
  { value: 1, short: "一", label: "周一" },
  { value: 2, short: "二", label: "周二" },
  { value: 3, short: "三", label: "周三" },
  { value: 4, short: "四", label: "周四" },
  { value: 5, short: "五", label: "周五" },
  { value: 6, short: "六", label: "周六" },
  { value: 0, short: "日", label: "周日" },
];
const habitMenuOptions = [
  { label: "编辑", key: "edit" },
  { label: "归档", key: "archive" },
];

const state = reactive<HabitState>({
  version: 2,
  habits: [],
  logs: [],
  settings: { weekStartsOn: 1 },
});
const loading = ref(true);
const loadError = ref("");
const saveState = ref("");
const activeView = ref<ViewKey>("today");
const showEditor = ref(false);
const editingId = ref("");
const titleError = ref("");
const selectedHabitId = ref("");
const currentDate = ref(startOfDay(new Date()));
const today = computed(() => formatDate(currentDate.value));
const rangePreset = ref<RangePreset>("30");
const rangeStart = ref(formatDate(addDays(currentDate.value, -29)));
const rangeEnd = ref(today.value);
const showHistoryEditor = ref(false);
const historyEditingHabitId = ref("");
const historyEditingDate = ref("");
const historyDraftStatus = ref<"completed" | "skipped" | "empty">("empty");
const historyDraftOccurrences = ref(0);
const historyDraftReplacements = ref(0);
const historyDraftConfirmed = ref(false);
const draft = reactive({
  title: "",
  kind: "build" as HabitKind,
  color: colors[0],
  scheduleMode: "weekdays" as HabitScheduleMode,
  weekdays: [1, 2, 3, 4, 5] as number[],
  targetPerWeek: 3,
  alternative: "",
});

let loaded = false;
let saveTimer: number | undefined;
let saving = false;
let saveAgain = false;
let originalViewport: string | null = null;
let clockTimer: number | undefined;

const weekStart = computed(() => formatDate(startOfWeek(currentDate.value)));
const todayLabel = computed(() => new Intl.DateTimeFormat("zh-CN", {
  month: "long",
  day: "numeric",
  weekday: "long",
}).format(currentDate.value));
const activeHabits = computed(() => state.habits.filter((habit) => !habit.archived));
const archivedHabits = computed(() => state.habits.filter((habit) => habit.archived));
const buildHabits = computed(() => activeHabits.value.filter((habit) => habit.kind === "build"));
const reduceHabits = computed(() => activeHabits.value.filter((habit) => habit.kind === "reduce"));
const todayBuildHabits = computed(() => buildHabits.value.filter(isBuildDueToday));
const todayHabits = computed(() => [...todayBuildHabits.value, ...reduceHabits.value]);
const todayCompleted = computed(() =>
  todayBuildHabits.value.filter((habit) => logFor(habit.id, today.value)?.completed).length,
);
const weeklyCompleted = computed(() =>
  buildHabits.value.reduce((sum, habit) => sum + Math.min(weeklyCount(habit.id), weeklyTarget(habit)), 0),
);
const weeklyPlanned = computed(() =>
  buildHabits.value.reduce((sum, habit) => sum + weeklyTarget(habit), 0),
);
const weeklyOccurrences = computed(() =>
  state.logs
    .filter((log) => log.date >= weekStart.value && log.date <= today.value && reduceHabits.value.some((habit) => habit.id === log.habitId))
    .reduce((sum, log) => sum + log.occurrences, 0),
);
const weeklyReplacements = computed(() =>
  state.logs
    .filter((log) => log.date >= weekStart.value && log.date <= today.value && reduceHabits.value.some((habit) => habit.id === log.habitId))
    .reduce((sum, log) => sum + log.replacements, 0),
);
const selectedHabit = computed(() =>
  state.habits.find((habit) => habit.id === selectedHabitId.value) ?? statisticsHabits.value[0],
);
const safeRangeDates = computed(() => {
  const start = parseDate(rangeStart.value);
  const requestedEnd = parseDate(rangeEnd.value);
  if (!start || !requestedEnd || start > requestedEnd) return [];
  const end = requestedEnd > currentDate.value ? currentDate.value : requestedEnd;
  return eachDate(start, end);
});
const statisticsHabits = computed(() => state.habits.filter((habit) =>
  safeRangeDates.value.some((date) => isHabitActiveOn(habit, formatDate(date))),
));
const statsHabitOptions = computed(() => statisticsHabits.value.map((habit) => ({
  label: `${habit.title}${habit.archived ? "（已归档）" : ""}`,
  value: habit.id,
})));
const habitStats = computed(() => statisticsHabits.value.map((habit) => calculateHabitStats(habit, safeRangeDates.value)));
const aggregateStats = computed(() => {
  const result = habitStats.value.reduce((sum, item) => ({
    planned: sum.planned + item.planned,
    completed: sum.completed + item.completed,
    skipped: sum.skipped + item.skipped,
    missed: sum.missed + item.missed,
    occurrences: sum.occurrences + item.occurrences,
    replacements: sum.replacements + item.replacements,
    confirmedDays: sum.confirmedDays + item.confirmedDays,
  }), { planned: 0, completed: 0, skipped: 0, missed: 0, occurrences: 0, replacements: 0, confirmedDays: 0 });
  const confirmedDates = new Set(state.logs
    .filter((log) => safeRangeDates.value.some((date) => formatDate(date) === log.date))
    .filter((log) => statisticsHabits.value.some((habit) => habit.id === log.habitId && habit.kind === "reduce"))
    .filter((log) => log.confirmed || log.occurrences || log.replacements)
    .map((log) => log.date));
  return {
    ...result,
    confirmedDays: confirmedDates.size,
    buildRate: result.planned ? Math.min(100, Math.round((result.completed / result.planned) * 100)) : 0,
  };
});
const trendBuckets = computed(() => {
  const dates = safeRangeDates.value;
  if (!dates.length) return [];
  const mode = dates.length <= 31 ? "day" : dates.length <= 150 ? "week" : "month";
  const groups = new Map<string, Date[]>();
  for (const date of dates) {
    const key = mode === "day"
      ? formatDate(date)
      : mode === "week"
        ? formatDate(startOfWeek(date))
        : `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, "0")}`;
    groups.set(key, [...(groups.get(key) ?? []), date]);
  }
  const raw = [...groups.entries()].map(([key, bucketDates], index, entries) => {
    const items = statisticsHabits.value.map((habit) => calculateHabitStats(habit, bucketDates));
    const planned = items.reduce((sum, item) => sum + item.planned, 0);
    const completed = items.reduce((sum, item) => sum + item.completed, 0);
    const occurrences = items.reduce((sum, item) => sum + item.occurrences, 0);
    return {
      key,
      label: mode === "month" ? key : key.slice(5),
      showLabel: mode !== "day" || index === 0 || index === entries.length - 1 || index % 5 === 0,
      buildRate: planned ? Math.min(100, Math.round((completed / planned) * 100)) : 0,
      occurrences,
    };
  });
  const maxOccurrences = Math.max(1, ...raw.map((item) => item.occurrences));
  return raw.map((item) => ({
    ...item,
    buildHeight: item.buildRate,
    reduceHeight: Math.round((item.occurrences / maxOccurrences) * 100),
  }));
});
const heatmapDays = computed(() => safeRangeDates.value.slice(-366).map((date) => ({
  date: formatDate(date),
  monthDay: `${date.getMonth() + 1}/${date.getDate()}`,
})));
const historyEditingHabit = computed(() => state.habits.find((habit) => habit.id === historyEditingHabitId.value));
const historyEditorTitle = computed(() => historyEditingHabit.value
  ? `${historyEditingHabit.value.title} · ${historyEditingDate.value}`
  : "补录习惯");

onMounted(() => {
  lockViewport();
  refreshToday();
  clockTimer = window.setInterval(refreshToday, 60_000);
  document.addEventListener("visibilitychange", refreshToday);
  void loadState();
});

onBeforeUnmount(() => {
  window.clearTimeout(saveTimer);
  window.clearInterval(clockTimer);
  document.removeEventListener("visibilitychange", refreshToday);
  unlockViewport();
});

watch(
  state,
  () => {
    if (!loaded) return;
    saveState.value = "等待保存";
    window.clearTimeout(saveTimer);
    saveTimer = window.setTimeout(() => void persistState(), 400);
  },
  { deep: true },
);

watch(statisticsHabits, (habits) => {
  if (!habits.some((habit) => habit.id === selectedHabitId.value)) selectedHabitId.value = habits[0]?.id ?? "";
});

async function loadState() {
  loading.value = true;
  loadError.value = "";
  try {
    const remote = await fetchHabitState();
    state.version = 2;
    state.habits = Array.isArray(remote.habits) ? remote.habits : [];
    state.logs = Array.isArray(remote.logs) ? remote.logs : [];
    state.settings = remote.settings ?? { weekStartsOn: 1 };
    selectedHabitId.value = activeHabits.value[0]?.id ?? state.habits[0]?.id ?? "";
    loaded = true;
  } catch (error) {
    loadError.value = error instanceof Error ? error.message : "未知错误";
  } finally {
    loading.value = false;
  }
}

async function persistState() {
  if (saving) {
    saveAgain = true;
    return;
  }
  saving = true;
  saveState.value = "保存中…";
  try {
    await saveHabitState(JSON.parse(JSON.stringify(state)));
    saveState.value = "已保存";
  } catch {
    saveState.value = "保存失败";
    message.error("习惯数据保存失败");
  } finally {
    saving = false;
    if (saveAgain) {
      saveAgain = false;
      void persistState();
    } else if (saveState.value === "已保存") {
      window.setTimeout(() => {
        if (saveState.value === "已保存") saveState.value = "";
      }, 1400);
    }
  }
}

function openCreate() {
  editingId.value = "";
  titleError.value = "";
  Object.assign(draft, {
    title: "",
    kind: "build",
    color: colors[state.habits.length % colors.length],
    scheduleMode: "weekdays",
    weekdays: [1, 2, 3, 4, 5],
    targetPerWeek: 3,
    alternative: "",
  });
  showEditor.value = true;
}

function openEdit(habit: Habit) {
  editingId.value = habit.id;
  titleError.value = "";
  Object.assign(draft, {
    title: habit.title,
    kind: habit.kind,
    color: habit.color,
    scheduleMode: habit.schedule.mode,
    weekdays: [...habit.schedule.weekdays],
    targetPerWeek: habit.schedule.targetPerWeek,
    alternative: habit.alternative,
  });
  showEditor.value = true;
}

function saveHabit() {
  const title = draft.title.trim();
  if (!title) {
    titleError.value = "请输入习惯名称";
    return;
  }
  if (draft.kind === "build" && draft.scheduleMode === "weekdays" && !draft.weekdays.length) {
    message.warning("请至少选择一天");
    return;
  }
  const now = new Date().toISOString();
  const existing = state.habits.find((habit) => habit.id === editingId.value);
  const kind = existing?.kind ?? draft.kind;
  const habit: Habit = {
    id: existing?.id ?? crypto.randomUUID(),
    title,
    kind,
    color: draft.color,
    schedule: kind === "reduce"
      ? { mode: "weekdays", weekdays: [0, 1, 2, 3, 4, 5, 6], targetPerWeek: 1 }
      : {
          mode: draft.scheduleMode,
          weekdays: draft.scheduleMode === "weekdays" ? [...draft.weekdays] : [],
          targetPerWeek: draft.scheduleMode === "weeklyTarget" ? draft.targetPerWeek : 1,
        },
    alternative: kind === "reduce" ? draft.alternative.trim() : "",
    archived: false,
    archivedAt: existing?.archivedAt ?? "",
    createdAt: existing?.createdAt ?? now,
    updatedAt: now,
  };
  if (existing) Object.assign(existing, habit);
  else state.habits.push(habit);
  selectedHabitId.value = habit.id;
  showEditor.value = false;
  message.success(existing ? "习惯已更新" : "习惯已创建");
}

function handleHabitMenu(key: string, habit: Habit) {
  if (key === "edit") {
    openEdit(habit);
    return;
  }
  if (key === "archive") {
    archiveHabit(habit);
  }
}

function archiveHabit(habit: Habit) {
  habit.archived = true;
  habit.archivedAt = today.value;
  habit.updatedAt = new Date().toISOString();
  if (selectedHabitId.value === habit.id) selectedHabitId.value = activeHabits.value[0]?.id ?? "";
  message.info("习惯已归档，历史记录仍会保留");
}

function restoreHabit(habit: Habit) {
  habit.archived = false;
  habit.archivedAt = "";
  habit.updatedAt = new Date().toISOString();
  selectedHabitId.value = habit.id;
  message.success("习惯已恢复");
}

function toggleWeekday(day: number) {
  draft.weekdays = draft.weekdays.includes(day)
    ? draft.weekdays.filter((value) => value !== day)
    : [...draft.weekdays, day];
}

function logFor(habitId: string, date: string) {
  return state.logs.find((log) => log.habitId === habitId && log.date === date);
}

function ensureLog(habitId: string, date = today.value) {
  let log = logFor(habitId, date);
  if (!log) {
    log = {
      habitId,
      date,
      completed: false,
      skipped: false,
      occurrences: 0,
      replacements: 0,
      confirmed: false,
      note: "",
      updatedAt: new Date().toISOString(),
    };
    state.logs.push(log);
  }
  return log;
}

function cleanupLog(log: HabitLog) {
  if (!log.completed && !log.skipped && !log.occurrences && !log.replacements && !log.confirmed && !log.note) {
    state.logs = state.logs.filter((item) => item !== log);
  }
}

function toggleCompleted(habitId: string) {
  const log = ensureLog(habitId);
  log.completed = !log.completed;
  if (log.completed) log.skipped = false;
  log.updatedAt = new Date().toISOString();
  cleanupLog(log);
}

function toggleSkipped(habitId: string) {
  const log = ensureLog(habitId);
  log.skipped = !log.skipped;
  if (log.skipped) log.completed = false;
  log.updatedAt = new Date().toISOString();
  cleanupLog(log);
}

function changeCounter(habitId: string, field: "occurrences" | "replacements", delta: number) {
  const log = ensureLog(habitId);
  log[field] = Math.max(0, log[field] + delta);
  log.confirmed = true;
  log.updatedAt = new Date().toISOString();
  cleanupLog(log);
}

function clearToday(habitId: string) {
  state.logs = state.logs.filter((log) => !(log.habitId === habitId && log.date === today.value));
}

function hasReduceLog(habitId: string) {
  const log = logFor(habitId, today.value);
  return Boolean(log?.confirmed || log?.occurrences || log?.replacements);
}

function hasReduceCounts(habitId: string) {
  const log = logFor(habitId, today.value);
  return Boolean(log?.occurrences || log?.replacements);
}

function confirmReduceDay(habitId: string) {
  const log = ensureLog(habitId);
  log.confirmed = true;
  log.updatedAt = new Date().toISOString();
}

function isBuildDueToday(habit: Habit) {
  if (habit.createdAt.slice(0, 10) > today.value) return false;
  if (habit.schedule.mode === "weekdays") return habit.schedule.weekdays.includes(currentDate.value.getDay());
  return weeklyCount(habit.id) < habit.schedule.targetPerWeek || Boolean(logFor(habit.id, today.value)?.completed);
}

function weeklyCount(habitId: string) {
  return state.logs.filter((log) =>
    log.habitId === habitId && log.completed && log.date >= weekStart.value && log.date <= today.value
  ).length;
}

function weeklyTarget(habit: Habit) {
  if (habit.schedule.mode === "weeklyTarget") return habit.schedule.targetPerWeek;
  const created = habit.createdAt.slice(0, 10);
  return eachDate(startOfWeek(currentDate.value), currentDate.value)
    .filter((date) => formatDate(date) >= created && habit.schedule.weekdays.includes(date.getDay()))
    .length;
}

function weeklyPercent(habit: Habit) {
  const target = weeklyTarget(habit);
  return target ? Math.min(100, Math.round((weeklyCount(habit.id) / target) * 100)) : 0;
}

function scheduleLabel(habit: Habit) {
  if (habit.schedule.mode === "weeklyTarget") return `每周 ${habit.schedule.targetPerWeek} 次`;
  if (habit.schedule.weekdays.length === 7) return "每天";
  return weekdayOptions
    .filter((day) => habit.schedule.weekdays.includes(day.value))
    .map((day) => day.label)
    .join("、");
}

function historyClass(habit: Habit, date: string) {
  const log = logFor(habit.id, date);
  if (!log) {
    if (habit.kind === "build" && habit.schedule.mode === "weekdays" && date < today.value && isHabitDueOn(habit, date)) return "missed";
    return "empty";
  }
  if (habit.kind === "build") {
    if (log.completed) return "completed";
    if (log.skipped) return "skipped";
    return "empty";
  }
  if (log.replacements && !log.occurrences) return "replaced";
  if (log.occurrences) return "occurred";
  if (log.confirmed) return "confirmed";
  return "empty";
}

function historyValue(habit: Habit, date: string) {
  const log = logFor(habit.id, date);
  if (!log) return "—";
  if (habit.kind === "build") return log.completed ? "完成" : log.skipped ? "跳过" : "—";
  if (log.occurrences && log.replacements) return `${log.occurrences}/${log.replacements}`;
  if (log.occurrences) return `${log.occurrences} 次`;
  if (log.replacements) return `替 ${log.replacements}`;
  if (log.confirmed) return "无发生";
  return "—";
}

function historyTitle(habit: Habit, date: string) {
  const value = historyValue(habit, date);
  return `${date} · ${value}`;
}

function calculateHabitStats(habit: Habit, dates: Date[]): HabitStat {
  const activeDates = dates.filter((date) => isHabitActiveOn(habit, formatDate(date)) && formatDate(date) <= today.value);
  const logs = state.logs.filter((log) => log.habitId === habit.id && activeDates.some((date) => formatDate(date) === log.date));
  if (habit.kind === "reduce") {
    return {
      habit,
      planned: 0,
      completed: 0,
      skipped: 0,
      missed: 0,
      rate: 0,
      occurrences: logs.reduce((sum, log) => sum + log.occurrences, 0),
      replacements: logs.reduce((sum, log) => sum + log.replacements, 0),
      confirmedDays: logs.filter((log) => log.confirmed || log.occurrences || log.replacements).length,
    };
  }

  let planned = 0;
  let completed = 0;
  let skipped = 0;
  let missed = 0;
  if (habit.schedule.mode === "weekdays") {
    const dueDates = activeDates.filter((date) => habit.schedule.weekdays.includes(date.getDay()));
    planned = dueDates.length;
    for (const date of dueDates) {
      const dateString = formatDate(date);
      const log = logFor(habit.id, dateString);
      if (log?.completed) completed += 1;
      else if (log?.skipped) skipped += 1;
      else if (dateString < today.value) missed += 1;
    }
  } else {
    const weeks = new Map<string, Date[]>();
    for (const date of activeDates) {
      const key = formatDate(startOfWeek(date));
      weeks.set(key, [...(weeks.get(key) ?? []), date]);
    }
    for (const weekDates of weeks.values()) {
      const weekTarget = Math.min(habit.schedule.targetPerWeek, weekDates.length);
      const weekLogs = logs.filter((log) => weekDates.some((date) => formatDate(date) === log.date));
      const weekCompleted = Math.min(weekTarget, weekLogs.filter((log) => log.completed).length);
      const weekSkipped = weekLogs.filter((log) => log.skipped).length;
      planned += weekTarget;
      completed += weekCompleted;
      skipped += weekSkipped;
      const weekEnded = formatDate(weekDates[weekDates.length - 1]) < today.value
        && addDays(weekDates[weekDates.length - 1], 1).getDay() === 1;
      if (weekEnded) missed += Math.max(0, weekTarget - weekCompleted);
    }
  }
  return {
    habit,
    planned,
    completed,
    skipped,
    missed,
    rate: planned ? Math.min(100, Math.round((completed / planned) * 100)) : 0,
    occurrences: 0,
    replacements: 0,
    confirmedDays: 0,
  };
}

function setRangePreset(preset: RangePreset) {
  rangePreset.value = preset;
  if (preset === "custom") return;
  const end = currentDate.value;
  const start = preset === "month"
    ? new Date(end.getFullYear(), end.getMonth(), 1)
    : preset === "year"
      ? new Date(end.getFullYear(), 0, 1)
      : addDays(end, -(Number(preset) - 1));
  rangeStart.value = formatDate(start);
  rangeEnd.value = formatDate(end);
}

function isHabitActiveOn(habit: Habit, date: string) {
  const created = habit.createdAt.slice(0, 10);
  const archived = habit.archivedAt || (habit.archived ? habit.updatedAt.slice(0, 10) : "");
  return (!created || created <= date) && (!archived || date <= archived);
}

function isHabitDueOn(habit: Habit, date: string) {
  if (!isHabitActiveOn(habit, date)) return false;
  if (habit.kind === "reduce" || habit.schedule.mode === "weeklyTarget") return true;
  const parsed = parseDate(date);
  return Boolean(parsed && habit.schedule.weekdays.includes(parsed.getDay()));
}

function canEditHistory(habit: Habit, date: string) {
  return date <= today.value && isHabitActiveOn(habit, date)
    && (habit.kind === "reduce" || habit.schedule.mode === "weeklyTarget" || isHabitDueOn(habit, date) || Boolean(logFor(habit.id, date)));
}

function openHistoryEditor(habit: Habit, date: string) {
  if (!canEditHistory(habit, date)) return;
  const log = logFor(habit.id, date);
  historyEditingHabitId.value = habit.id;
  historyEditingDate.value = date;
  historyDraftStatus.value = log?.completed ? "completed" : log?.skipped ? "skipped" : "empty";
  historyDraftOccurrences.value = log?.occurrences ?? 0;
  historyDraftReplacements.value = log?.replacements ?? 0;
  historyDraftConfirmed.value = Boolean(log?.confirmed || log?.occurrences || log?.replacements);
  showHistoryEditor.value = true;
}

function saveHistoryEdit() {
  const habit = historyEditingHabit.value;
  if (!habit) return;
  if (habit.kind === "build") {
    if (historyDraftStatus.value === "empty") {
      state.logs = state.logs.filter((log) => !(log.habitId === habit.id && log.date === historyEditingDate.value));
    } else {
      const log = ensureLog(habit.id, historyEditingDate.value);
      log.completed = historyDraftStatus.value === "completed";
      log.skipped = historyDraftStatus.value === "skipped";
      log.occurrences = 0;
      log.replacements = 0;
      log.confirmed = false;
      log.updatedAt = new Date().toISOString();
    }
  } else {
    const occurrences = Math.max(0, Number(historyDraftOccurrences.value) || 0);
    const replacements = Math.max(0, Number(historyDraftReplacements.value) || 0);
    if (!historyDraftConfirmed.value && !occurrences && !replacements) {
      state.logs = state.logs.filter((log) => !(log.habitId === habit.id && log.date === historyEditingDate.value));
    } else {
      const log = ensureLog(habit.id, historyEditingDate.value);
      log.completed = false;
      log.skipped = false;
      log.occurrences = occurrences;
      log.replacements = replacements;
      log.confirmed = true;
      log.updatedAt = new Date().toISOString();
    }
  }
  showHistoryEditor.value = false;
  message.success("历史记录已更新");
}

function refreshToday() {
  const next = startOfDay(new Date());
  if (formatDate(next) === today.value) return;
  currentDate.value = next;
  if (rangePreset.value !== "custom") setRangePreset(rangePreset.value);
}

function lockViewport() {
  const viewport = document.querySelector<HTMLMetaElement>('meta[name="viewport"]');
  if (!viewport) return;
  originalViewport = viewport.content;
  viewport.content = "width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover";
}

function unlockViewport() {
  const viewport = document.querySelector<HTMLMetaElement>('meta[name="viewport"]');
  if (viewport && originalViewport !== null) viewport.content = originalViewport;
  originalViewport = null;
}

function formatDate(date: Date) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  return `${year}-${month}-${day}`;
}

function parseDate(value: string) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  if (!match) return null;
  const date = new Date(Number(match[1]), Number(match[2]) - 1, Number(match[3]));
  return Number.isNaN(date.getTime()) ? null : date;
}

function startOfDay(date: Date) {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate());
}

function startOfWeek(date: Date) {
  const result = new Date(date.getFullYear(), date.getMonth(), date.getDate());
  result.setDate(result.getDate() - ((result.getDay() + 6) % 7));
  return result;
}

function addDays(date: Date, count: number) {
  const result = new Date(date.getFullYear(), date.getMonth(), date.getDate());
  result.setDate(result.getDate() + count);
  return result;
}

function eachDate(start: Date, end: Date) {
  const dates: Date[] = [];
  for (let cursor = new Date(start); cursor <= end; cursor = addDays(cursor, 1)) dates.push(cursor);
  return dates;
}
</script>

<style scoped>
.habit-app {
  --ink: #20312b;
  --muted: #6c7a75;
  --line: rgba(45, 72, 62, 0.13);
  width: 100%;
  min-width: 0;
  max-width: 100%;
  display: grid;
  gap: 16px;
  color: var(--ink);
  overflow-x: clip;
}

.habit-app :deep(.n-spin-container),
.habit-app :deep(.n-spin-content) { min-width: 0; max-width: 100%; }

.habit-intro {
  min-width: 0;
  min-height: 142px;
  padding: 26px 28px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 24px;
  border-radius: 18px;
  background: linear-gradient(125deg, #203f34, #3f6756 62%, #73836d);
  color: white;
  box-shadow: 0 18px 42px rgba(32, 65, 52, 0.18);
}

.habit-intro p,
.surface-heading p {
  margin: 0 0 5px;
  font-size: 11px;
  font-weight: 800;
  letter-spacing: 0.1em;
  text-transform: uppercase;
}

.habit-intro p { color: rgba(255, 255, 255, 0.64); }
.habit-intro h2 { margin: 0 0 7px; font-size: clamp(23px, 3vw, 31px); line-height: 1.2; }
.habit-intro span { color: rgba(255, 255, 255, 0.72); font-size: 14px; }
.intro-actions { display: flex; align-items: center; gap: 12px; flex: 0 0 auto; }
.intro-actions small { color: rgba(255, 255, 255, 0.66); }
.intro-actions small.error { color: #ffd1c8; }
.habit-intro :deep(.n-button--primary-type) {
  --n-color: #f5f8f4 !important;
  --n-color-hover: #fff !important;
  --n-text-color: #294b3d !important;
  --n-text-color-hover: #294b3d !important;
}

.habit-tabs {
  width: fit-content;
  padding: 4px;
  display: flex;
  gap: 3px;
  border: 1px solid var(--line);
  border-radius: 11px;
  background: rgba(255, 255, 255, 0.72);
}

.habit-tabs button,
.range-presets button {
  min-height: 34px;
  padding: 0 16px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: var(--muted);
  font-weight: 700;
  cursor: pointer;
}

.habit-tabs button.active,
.range-presets button.active { background: #315849; color: white; }

.summary-row {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 12px;
}

.summary-row article {
  min-height: 106px;
  padding: 17px 19px;
  display: grid;
  align-content: center;
  gap: 3px;
  border: 1px solid var(--line);
  border-radius: 13px;
  background: rgba(255, 255, 255, 0.82);
  box-shadow: 0 8px 24px rgba(39, 65, 56, 0.05);
}

.summary-row span,
.summary-row small { color: var(--muted); }
.summary-row span { font-size: 12px; font-weight: 700; }
.summary-row strong { font-size: 27px; line-height: 1.15; }

.habit-dashboard {
  width: 100%;
  min-width: 0;
  display: grid;
  grid-template-columns: minmax(0, 1fr) 310px;
  gap: 14px;
  align-items: start;
}

.surface {
  width: 100%;
  min-width: 0;
  padding: 20px;
  border: 1px solid var(--line);
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.86);
  box-shadow: 0 10px 28px rgba(39, 65, 56, 0.05);
}

.surface-heading {
  margin-bottom: 16px;
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 12px;
}

.surface-heading p { color: #789087; }
.surface-heading h3 { margin: 0; font-size: 19px; }
.surface-heading > span { color: var(--muted); font-size: 12px; }
.dashboard-side { min-width: 0; display: grid; gap: 14px; }
.today-list { display: grid; gap: 10px; }

.today-item {
  position: relative;
  min-width: 0;
  display: grid;
  grid-template-columns: 5px minmax(0, 1fr);
  overflow: hidden;
  border: 1px solid var(--line);
  border-radius: 11px;
  background: #fff;
}

.color-bar { width: 5px; height: 100%; }
.today-copy { min-width: 0; padding: 15px 16px; }
.item-title { display: flex; align-items: flex-start; justify-content: space-between; gap: 10px; }
.item-title > div { min-width: 0; }
.item-title h4 { overflow-wrap: anywhere; display: inline; margin: 0 0 0 7px; font-size: 16px; }
.kind-tag { padding: 3px 7px; border-radius: 999px; font-size: 10px; font-weight: 800; white-space: nowrap; }
.kind-tag--build { background: #dcebe3; color: #315d4c; }
.kind-tag--reduce { background: #f0e4dc; color: #7a523f; }
.item-meta { margin: 8px 0 12px; color: var(--muted); font-size: 12px; line-height: 1.6; }
.build-actions, .counter-actions { display: flex; align-items: center; flex-wrap: wrap; gap: 8px; }

.done-button {
  min-height: 36px;
  padding: 0 14px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  border: 1px solid #315849;
  border-radius: 8px;
  background: #315849;
  color: white;
  font-weight: 700;
  cursor: pointer;
}

.done-button.active { background: #e6f1ea; color: #315849; }
.text-action, .undo-action { border: 0; background: transparent; color: var(--muted); cursor: pointer; }
.text-action:hover, .undo-action:hover { color: var(--ink); }

.counter-stepper {
  min-width: 156px;
  min-height: 48px;
  padding: 7px 8px 7px 11px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  border: 1px solid var(--line);
  border-radius: 9px;
  background: #f7f9f8;
  color: var(--ink);
}

.counter-stepper > span { color: var(--muted); font-size: 12px; }
.counter-stepper > div { display: grid; grid-template-columns: 28px 26px 28px; align-items: center; text-align: center; }
.counter-stepper button { width: 28px; height: 28px; padding: 0; border: 1px solid var(--line); border-radius: 7px; background: white; color: #315849; font-size: 18px; cursor: pointer; }
.counter-stepper strong { font-size: 16px; }
.confirm-zero { min-height: 36px; padding: 0 11px; border: 1px solid var(--line); border-radius: 8px; background: white; color: var(--muted); cursor: pointer; }
.confirmed-zero { color: #47745f; font-size: 12px; font-weight: 700; }
.section-empty { padding: 32px 18px; border-radius: 9px; background: #f5f7f6; color: var(--muted); text-align: center; }
.section-empty.compact { padding: 18px; margin: 0; }
.rhythm-list, .habit-list { display: grid; }

.rhythm-item {
  padding: 11px 0;
  display: grid;
  gap: 8px;
  border: 0;
  border-bottom: 1px solid var(--line);
  background: transparent;
  color: inherit;
  cursor: pointer;
}

.rhythm-item:last-child { border-bottom: 0; }
.rhythm-item > div:first-child { display: grid; grid-template-columns: 8px minmax(0, 1fr) auto; align-items: center; gap: 8px; }
.rhythm-item > div > i { width: 8px; height: 8px; border-radius: 50%; }
.rhythm-item span { overflow: hidden; font-size: 13px; text-align: left; text-overflow: ellipsis; white-space: nowrap; }
.rhythm-item em { color: var(--muted); font-size: 11px; font-style: normal; }
.progress-track { height: 5px; overflow: hidden; border-radius: 999px; background: #e6ebe8; }
.progress-track i { height: 100%; display: block; border-radius: inherit; }

.habit-list > button {
  width: 100%;
  padding: 10px 5px;
  display: grid;
  grid-template-columns: 6px minmax(0, 1fr) auto;
  align-items: center;
  gap: 9px;
  border: 0;
  border-bottom: 1px solid var(--line);
  background: transparent;
  color: inherit;
  text-align: left;
  cursor: pointer;
}

.habit-list > button.active { background: #f2f6f3; }
.habit-list > button > i { width: 6px; height: 30px; border-radius: 999px; }
.habit-list button > span { min-width: 0; display: grid; gap: 2px; }
.habit-list strong { overflow: hidden; font-size: 13px; text-overflow: ellipsis; white-space: nowrap; }
.habit-list small, .habit-list em { color: var(--muted); font-size: 10px; font-style: normal; }
.history-surface { grid-column: 1 / -1; }
.history-heading { align-items: center; }
.history-heading :deep(.n-select) { width: 220px; }

.history-grid {
  width: 100%;
  min-width: 0;
  display: grid;
  grid-template-columns: repeat(14, minmax(44px, 1fr));
  gap: 6px;
}

.history-day {
  min-width: 0;
  padding: 8px 4px;
  display: grid;
  justify-items: center;
  gap: 4px;
  border: 1px solid var(--line);
  border-radius: 8px;
  background: #fafbfa;
}

.history-day span, .history-day small { color: var(--muted); font-size: 9px; }
.history-day strong { font-size: 12px; }
.history-day i { width: 10px; height: 10px; border-radius: 50%; background: #d9dfdc; }
.history-day.completed i { background: #4d8767; }
.history-day.skipped i { background: #abb5b0; }
.history-day.occurred i { background: #b46b50; }
.history-day.replaced i { background: #647cab; }
.history-day.completed { background: #f0f7f3; }
.history-day.occurred { background: #fbf3ef; }
.history-day.replaced { background: #f2f4fa; }

.stats-toolbar {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 18px;
}

.stats-toolbar p { margin: 0 0 9px; color: var(--muted); font-size: 12px; font-weight: 800; }
.range-presets { display: flex; flex-wrap: wrap; gap: 4px; }
.range-presets button { min-height: 32px; padding: 0 11px; border: 1px solid transparent; }
.custom-range { display: flex; align-items: flex-end; gap: 8px; }
.custom-range > span { padding-bottom: 9px; color: var(--muted); }
.custom-range label { display: grid; gap: 5px; color: var(--muted); font-size: 11px; font-weight: 700; }
.custom-range input {
  height: 36px;
  padding: 0 9px;
  border: 1px solid #ccd6d1;
  border-radius: 8px;
  background: white;
  color: var(--ink);
  font: inherit;
}

.stats-grid { min-width: 0; display: grid; grid-template-columns: minmax(0, 1.35fr) minmax(300px, 0.65fr); gap: 14px; }
.comparison-surface { grid-column: 1 / -1; }
.surface-note { margin: -8px 0 13px; color: var(--muted); font-size: 11px; }
.trend-chart { height: 230px; display: flex; align-items: stretch; gap: 5px; overflow-x: auto; }
.trend-column { min-width: 22px; flex: 1 0 22px; display: grid; grid-template-rows: 1fr auto; gap: 7px; }
.trend-bars { min-height: 0; display: flex; align-items: flex-end; justify-content: center; gap: 2px; border-bottom: 1px solid var(--line); }
.trend-bars i { width: min(9px, 42%); min-height: 2px; border-radius: 4px 4px 0 0; }
.build-bar, .build-dot { background: #4d8767; }
.reduce-bar, .reduce-dot { background: #b46b50; }
.trend-column > span { overflow: hidden; color: var(--muted); font-size: 9px; text-align: center; text-overflow: clip; white-space: nowrap; }
.chart-legend { margin-top: 13px; display: flex; flex-wrap: wrap; gap: 16px; color: var(--muted); font-size: 11px; }
.chart-legend span { display: flex; align-items: center; gap: 6px; }
.chart-legend i { width: 8px; height: 8px; border-radius: 2px; }

.heatmap-grid { max-height: 246px; display: grid; grid-template-columns: repeat(7, minmax(30px, 1fr)); gap: 5px; overflow-y: auto; }
.heatmap-day { min-width: 0; min-height: 38px; padding: 4px 2px; display: grid; place-items: center; gap: 3px; border: 1px solid var(--line); border-radius: 6px; background: #fafbfa; cursor: pointer; }
.heatmap-day span { color: var(--muted); font-size: 8px; }
.heatmap-day i { width: 9px; height: 9px; border-radius: 50%; background: #d9dfdc; }
.heatmap-day.completed { background: #f0f7f3; }
.heatmap-day.completed i { background: #4d8767; }
.heatmap-day.skipped i { background: #abb5b0; }
.heatmap-day.missed { background: #fbf3ef; }
.heatmap-day.missed i { background: #d8a48e; }
.heatmap-day.occurred { background: #fbf3ef; }
.heatmap-day.occurred i { background: #b46b50; }
.heatmap-day.replaced { background: #f2f4fa; }
.heatmap-day.replaced i { background: #647cab; }
.heatmap-day.confirmed { background: #f0f7f3; }
.heatmap-day.confirmed i { background: #89ab99; }
.heatmap-day:disabled { opacity: 0.42; cursor: default; }

.stats-table-wrap { overflow-x: auto; }
.stats-table { width: 100%; border-collapse: collapse; font-size: 12px; }
.stats-table th, .stats-table td { padding: 11px 12px; border-bottom: 1px solid var(--line); text-align: right; white-space: nowrap; }
.stats-table th { color: var(--muted); font-size: 10px; }
.stats-table th:first-child, .stats-table td:first-child { text-align: left; }
.stats-table tbody tr { cursor: pointer; }
.stats-table tbody tr:hover, .stats-table tbody tr.active { background: #f2f6f3; }
.stats-table td:first-child { display: flex; align-items: center; gap: 8px; font-weight: 700; }
.stats-table td:first-child i { width: 6px; height: 22px; border-radius: 999px; }

.management-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 14px; align-items: start; }
.manage-list { display: grid; }
.manage-list article { min-width: 0; padding: 12px 4px; display: grid; grid-template-columns: 6px minmax(0, 1fr) auto auto; align-items: center; gap: 10px; border-bottom: 1px solid var(--line); }
.manage-list article:last-child { border-bottom: 0; }
.manage-list article > i { width: 6px; height: 34px; border-radius: 999px; }
.manage-list article > div { min-width: 0; display: grid; gap: 3px; }
.manage-list strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.manage-list small { overflow: hidden; color: var(--muted); font-size: 11px; text-overflow: ellipsis; white-space: nowrap; }
.archived-list { opacity: 0.82; }

.empty-state {
  min-height: 430px;
  padding: 40px 20px;
  display: grid;
  place-items: center;
  align-content: center;
  gap: 12px;
  border: 1px solid var(--line);
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.84);
  text-align: center;
}

.empty-state h3, .empty-state p { margin: 0; }
.empty-state p { max-width: 430px; color: var(--muted); line-height: 1.7; }
.empty-mark { width: 58px; height: 58px; display: grid; place-items: center; border-radius: 50%; background: #e4efe8; color: #315849; font-size: 28px; }

.habit-editor {
  width: min(560px, calc(100dvw - 24px));
  max-height: calc(100dvh - 24px);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.habit-editor :deep(.n-card-header) { flex: 0 0 auto; border-bottom: 1px solid var(--line); }
.habit-editor :deep(.n-card__content) { min-height: 0; overflow-y: auto; overscroll-behavior: contain; }
.habit-editor :deep(.n-input), .habit-editor :deep(.n-base-selection), .habit-editor :deep(.n-input-number) { font-size: 16px; }
.habit-editor :deep(.n-input-number), .habit-editor :deep(.n-time-picker) { width: 100%; }
.kind-picker, .schedule-picker { display: grid; grid-template-columns: 1fr 1fr; gap: 9px; }

.kind-picker button, .schedule-picker button {
  padding: 11px 12px;
  border: 1px solid #ccd6d1;
  border-radius: 9px;
  background: white;
  color: var(--ink);
  cursor: pointer;
}

.kind-picker button { min-height: 68px; display: grid; gap: 3px; text-align: left; }
.kind-picker span { color: var(--muted); font-size: 11px; }
.kind-picker button.active, .schedule-picker button.active { border-color: #315849; background: #edf5f0; box-shadow: inset 0 0 0 1px #315849; }
.kind-picker button:disabled { cursor: default; opacity: 0.68; }
.form-hint { display: block; margin-top: 7px; color: var(--muted); font-size: 11px; }
.weekday-picker { display: grid; grid-template-columns: repeat(7, 1fr); gap: 6px; }
.weekday-picker button { aspect-ratio: 1; border: 1px solid #ccd6d1; border-radius: 50%; background: white; color: var(--muted); cursor: pointer; }
.weekday-picker button.active { border-color: #315849; background: #315849; color: white; }
.color-picker { display: flex; flex-wrap: wrap; gap: 12px; }
.color-picker button { width: 28px; height: 28px; padding: 0; border: 3px solid white; border-radius: 50%; box-shadow: 0 0 0 1px #c7d0cc; cursor: pointer; }
.color-picker button.active { box-shadow: 0 0 0 2px #203f34; }
.editor-actions { position: sticky; bottom: -1px; padding-top: 14px; display: flex; justify-content: flex-end; gap: 9px; background: linear-gradient(to bottom, transparent, white 12px); }

.history-editor { width: min(500px, calc(100dvw - 24px)); }
.modal-note { margin: 0 0 16px; color: var(--muted); line-height: 1.6; }
.history-status-actions { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; }
.history-status-actions button { min-height: 44px; border: 1px solid #ccd6d1; border-radius: 9px; background: white; color: var(--ink); cursor: pointer; }
.history-status-actions button.active { border-color: #315849; background: #edf5f0; box-shadow: inset 0 0 0 1px #315849; }
.history-counter-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.history-counter-grid label { display: grid; gap: 7px; color: var(--muted); font-size: 12px; }
.confirm-check { margin: 16px 0 4px; display: flex; align-items: center; gap: 7px; color: var(--muted); font-size: 12px; }

@media (max-width: 900px) {
  .habit-dashboard { grid-template-columns: 1fr; }
  .dashboard-side { grid-template-columns: 1fr 1fr; }
  .history-surface { grid-column: auto; }
  .history-grid { grid-template-columns: repeat(7, minmax(0, 1fr)); }
  .stats-toolbar { align-items: stretch; flex-direction: column; }
  .custom-range { justify-content: flex-start; }
  .stats-grid { grid-template-columns: 1fr; }
  .comparison-surface { grid-column: auto; }
  .management-grid { grid-template-columns: 1fr; }
}

@media (max-width: 620px) {
  .habit-app { gap: 12px; }
  .habit-intro { min-height: 0; padding: 18px; display: grid; gap: 18px; }
  .habit-intro h2 { font-size: 23px; }
  .habit-intro span { font-size: 13px; line-height: 1.6; }
  .intro-actions { width: 100%; justify-content: space-between; }
  .intro-actions :deep(.n-button) { flex: 0 0 auto; }
  .habit-tabs { width: 100%; }
  .habit-tabs button { flex: 1; }
  .summary-row { grid-template-columns: 1fr; gap: 8px; }
  .summary-row article { min-height: 76px; padding: 12px 14px; grid-template-columns: minmax(0, 1fr) auto; align-items: center; }
  .summary-row article span, .summary-row article small { grid-column: 1; }
  .summary-row article strong { grid-column: 2; grid-row: 1 / span 2; }
  .summary-row strong { font-size: 23px; }
  .surface { padding: 15px; }
  .dashboard-side { grid-template-columns: 1fr; }
  .today-copy { padding: 14px 12px; }
  .item-title { gap: 6px; }
  .item-title h4 { display: block; margin: 7px 0 0; line-height: 1.35; }
  .build-actions { display: grid; grid-template-columns: minmax(0, 1fr) auto; }
  .done-button { width: 100%; justify-content: center; }
  .counter-actions { display: grid; grid-template-columns: 1fr 1fr; }
  .counter-stepper { min-width: 0; width: 100%; }
  .confirm-zero, .confirmed-zero { grid-column: 1 / -1; }
  .undo-action { grid-column: 1 / -1; justify-self: end; padding: 6px 0; }
  .history-heading { align-items: flex-start; display: grid; }
  .history-heading :deep(.n-select) { width: 100%; }
  .history-grid { grid-template-columns: repeat(7, minmax(0, 1fr)); gap: 4px; }
  .history-day { padding: 7px 2px; gap: 3px; }
  .history-day small { overflow: hidden; width: 100%; text-align: center; text-overflow: ellipsis; white-space: nowrap; }
  .range-presets { display: grid; grid-template-columns: repeat(3, 1fr); }
  .custom-range { display: grid; grid-template-columns: 1fr auto 1fr; align-items: end; }
  .custom-range input { width: 100%; min-width: 0; }
  .trend-chart { height: 190px; }
  .history-counter-grid { grid-template-columns: 1fr; }
  .manage-list article { grid-template-columns: 6px minmax(0, 1fr) auto; }
  .manage-list article :deep(.n-button:last-child) { grid-column: 2 / -1; justify-self: end; }
  .habit-editor { width: calc(100dvw - 12px); max-height: calc(100dvh - 12px); }
  .kind-picker { grid-template-columns: 1fr; }
  .schedule-picker { grid-template-columns: 1fr 1fr; }
  .weekday-picker { grid-template-columns: repeat(4, minmax(0, 1fr)); }
  .weekday-picker button { width: 38px; max-width: 100%; justify-self: center; }
  .editor-actions :deep(.n-button) { flex: 1 1 0; }
}

@media (max-width: 360px) {
  .habit-intro { padding: 16px; }
  .intro-actions small { max-width: 90px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .counter-actions { grid-template-columns: 1fr; }
  .undo-action, .confirm-zero, .confirmed-zero { grid-column: 1; }
  .history-day span, .history-day small { font-size: 8px; }
  .history-day strong { font-size: 11px; }
  .schedule-picker { grid-template-columns: 1fr; }
}
</style>
