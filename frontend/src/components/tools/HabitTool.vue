<template>
  <div class="habit-tool">
    <section class="habit-hero">
      <div>
        <p class="habit-kicker">{{ todayLabel }}</p>
        <h3>{{ greeting }}</h3>
        <p>不追求完美的一天，只让想做的事更容易发生。</p>
      </div>
      <div class="hero-actions">
        <span v-if="saveLabel" class="save-state">{{ saveLabel }}</span>
        <n-button type="primary" @click="openCreate">新建习惯</n-button>
      </div>
    </section>

    <n-spin :show="loading">
      <n-alert v-if="loadError" type="error" title="暂时无法读取习惯数据" class="load-alert">
        {{ loadError }}
        <n-button size="small" class="retry-button" @click="loadState">重试</n-button>
      </n-alert>

      <template v-else>
        <section v-if="!activeHabits.length" class="habit-empty">
          <div class="empty-symbol">○</div>
          <h3>从一个足够小的行动开始</h3>
          <p>例如“睡前读 5 分钟”，比“每天读一本书”更容易留下来。</p>
          <n-button type="primary" @click="openCreate">创建第一个习惯</n-button>
        </section>

        <template v-else>
          <section class="overview-grid">
            <article class="overview-card overview-card--accent">
              <span>今日进度</span>
              <strong>{{ todayCompleted }}/{{ buildHabitsDueToday.length }}</strong>
              <small>{{ todayProgressText }}</small>
            </article>
            <article class="overview-card">
              <span>近 7 天</span>
              <strong>{{ sevenDayCompletion }}%</strong>
              <small>培养习惯完成率</small>
            </article>
            <article class="overview-card">
              <span>本周记录</span>
              <strong>{{ weeklyOccurrenceCount }}</strong>
              <small>减少习惯发生次数</small>
            </article>
            <article class="overview-card">
              <span>正在进行</span>
              <strong>{{ activeHabits.length }}</strong>
              <small>{{ buildHabits.length }} 个培养 · {{ reduceHabits.length }} 个减少</small>
            </article>
          </section>

          <div class="habit-layout">
            <main class="habit-main">
              <section class="habit-section">
                <div class="section-heading">
                  <div>
                    <p>Today</p>
                    <h3>今天的小行动</h3>
                  </div>
                  <span>{{ dueToday.length }} 项安排</span>
                </div>

                <div v-if="dueToday.length" class="today-list">
                  <article
                    v-for="habit in dueToday"
                    :key="habit.id"
                    class="today-card"
                    :class="{ 'today-card--done': isBuildDone(habit.id) }"
                  >
                    <div class="habit-color" :style="{ background: habit.color }"></div>
                    <div class="today-card-body">
                      <div class="today-card-title">
                        <div>
                          <span class="kind-label" :class="`kind-label--${habit.kind}`">
                            {{ habit.kind === 'build' ? '培养' : '减少' }}
                          </span>
                          <h4>{{ habit.title }}</h4>
                        </div>
                        <n-dropdown :options="habitMenuOptions" @select="(key) => handleHabitMenu(key, habit)">
                          <n-button quaternary circle aria-label="更多操作">···</n-button>
                        </n-dropdown>
                      </div>

                      <p v-if="habit.kind === 'build'" class="habit-meta">
                        <span>{{ frequencyLabel(habit) }}</span>
                        <span v-if="habit.reminderTime">页面提醒 {{ habit.reminderTime }}</span>
                      </p>
                      <p v-else-if="habit.replacementAction" class="replacement-copy">
                        想做时，可以先试试：{{ habit.replacementAction }}
                      </p>
                      <p v-else class="habit-meta">需要时再记录，这里不会主动提醒你。</p>

                      <div v-if="habit.kind === 'build'" class="card-actions">
                        <n-button
                          :type="isBuildDone(habit.id) ? 'success' : 'primary'"
                          @click="toggleBuildDone(habit)"
                        >
                          {{ isBuildDone(habit.id) ? '已完成 ✓' : '完成打卡' }}
                        </n-button>
                        <n-button v-if="!isBuildDone(habit.id)" tertiary @click="markSkipped(habit)">今天跳过</n-button>
                        <span v-if="todayRecord(habit.id)?.status === 'skipped'" class="quiet-state">已主动跳过</span>
                      </div>
                      <div v-else class="card-actions">
                        <n-button tertiary @click="recordOccurrence(habit)">
                          发生了一次<span v-if="occurrenceCount(habit.id)"> · {{ occurrenceCount(habit.id) }}</span>
                        </n-button>
                        <n-button v-if="habit.replacementAction" @click="recordReplacement(habit)">我做了替代动作</n-button>
                        <n-button v-if="todayRecord(habit.id)" text type="tertiary" @click="clearTodayRecord(habit.id)">撤销今日记录</n-button>
                      </div>
                    </div>
                  </article>
                </div>
                <div v-else class="quiet-empty">今天没有固定安排。休息也是计划的一部分。</div>
              </section>

              <section class="habit-section calendar-section">
                <div class="section-heading calendar-heading">
                  <div>
                    <p>Calendar</p>
                    <h3>{{ calendarTitle }}</h3>
                  </div>
                  <div class="calendar-controls">
                    <n-select v-model:value="selectedHabitId" :options="habitSelectOptions" size="small" />
                    <n-button-group>
                      <n-button size="small" @click="moveMonth(-1)">‹</n-button>
                      <n-button size="small" @click="resetMonth">今天</n-button>
                      <n-button size="small" @click="moveMonth(1)">›</n-button>
                    </n-button-group>
                  </div>
                </div>

                <div class="calendar-grid calendar-weekdays">
                  <span v-for="day in weekdayLabels" :key="day">{{ day }}</span>
                </div>
                <div class="calendar-grid calendar-days">
                  <div
                    v-for="cell in calendarCells"
                    :key="cell.date"
                    class="calendar-day"
                    :class="{
                      'calendar-day--outside': !cell.inMonth,
                      'calendar-day--today': cell.date === today,
                    }"
                  >
                    <span>{{ cell.day }}</span>
                    <i
                      v-if="recordFor(selectedHabitId, cell.date)"
                      :class="`record-dot record-dot--${recordFor(selectedHabitId, cell.date)?.status}`"
                      :title="recordStatusLabel(recordFor(selectedHabitId, cell.date))"
                    ></i>
                  </div>
                </div>
                <div class="calendar-legend">
                  <span><i class="record-dot record-dot--completed"></i>完成</span>
                  <span><i class="record-dot record-dot--skipped"></i>跳过</span>
                  <span><i class="record-dot record-dot--occurred"></i>发生</span>
                  <span><i class="record-dot record-dot--replaced"></i>替代动作</span>
                  <span><i class="record-dot record-dot--empty"></i>无记录</span>
                </div>
              </section>
            </main>

            <aside class="habit-sidebar">
              <section class="habit-section insight-card">
                <div class="section-heading">
                  <div><p>Weekly review</p><h3>本周回顾</h3></div>
                </div>
                <p class="insight-copy">{{ weeklyInsight }}</p>
                <div class="insight-bar"><i :style="{ width: `${sevenDayCompletion}%` }"></i></div>
                <small>回顾用于调整目标，不用于评价自己。</small>
              </section>

              <section class="habit-section all-habits">
                <div class="section-heading">
                  <div><p>Habits</p><h3>全部习惯</h3></div>
                </div>
                <button
                  v-for="habit in activeHabits"
                  :key="habit.id"
                  class="habit-list-item"
                  type="button"
                  title="编辑习惯"
                  @click="openEdit(habit)"
                >
                  <i :style="{ background: habit.color }"></i>
                  <span><strong>{{ habit.title }}</strong><small>{{ frequencyLabel(habit) }}</small></span>
                  <em>{{ habit.kind === 'build' ? '培养' : '减少' }}</em>
                </button>
              </section>
            </aside>
          </div>
        </template>
      </template>
    </n-spin>

    <n-modal v-model:show="showEditor" preset="card" :title="editingId ? '编辑习惯' : '创建习惯'" class="habit-modal">
      <n-form label-placement="top" @submit.prevent="saveHabit">
        <n-form-item label="我想要">
          <n-radio-group v-model:value="draft.kind" class="kind-choice">
            <n-radio-button value="build">培养一个行动</n-radio-button>
            <n-radio-button value="reduce">减少一个行动</n-radio-button>
          </n-radio-group>
        </n-form-item>
        <n-form-item label="习惯名称" :feedback="titleError" :validation-status="titleError ? 'error' : undefined">
          <n-input v-model:value="draft.title" maxlength="40" placeholder="例如：睡前阅读 5 分钟" @keydown.enter.prevent="saveHabit" />
        </n-form-item>
        <div class="editor-grid">
          <n-form-item label="频率">
            <n-select v-model:value="draft.schedule" :options="scheduleOptions" />
          </n-form-item>
          <n-form-item v-if="draft.schedule === 'weekly'" label="每周目标">
            <n-input-number v-model:value="draft.targetCount" :min="1" :max="7" />
          </n-form-item>
          <n-form-item label="标记颜色">
            <div class="color-options">
              <button
                v-for="color in colors"
                :key="color"
                type="button"
                :style="{ background: color }"
                :class="{ active: draft.color === color }"
                @click="draft.color = color"
              ></button>
            </div>
          </n-form-item>
        </div>
        <n-form-item v-if="draft.schedule === 'daily'" label="安排在">
          <n-checkbox-group v-model:value="draft.weekdays" class="weekday-choice">
            <n-checkbox v-for="day in weekdayOptions" :key="day.value" :value="day.value" :label="day.label" />
          </n-checkbox-group>
        </n-form-item>
        <n-form-item v-if="draft.kind === 'build'" label="页面提醒时间（可选）">
          <n-time-picker v-model:formatted-value="draft.reminderTime" format="HH:mm" value-format="HH:mm" clearable />
          <template #feedback>进入习惯页面后会显示时间提示；目前不会发送系统通知。</template>
        </n-form-item>
        <n-form-item v-else label="替代动作（可选）">
          <n-input v-model:value="draft.replacementAction" maxlength="80" placeholder="例如：先喝水并等待 10 分钟" />
          <template #feedback>不会提醒原来的行动，只在你打开页面时提供这个选择。</template>
        </n-form-item>
        <div class="modal-actions">
          <n-button @click="showEditor = false">取消</n-button>
          <n-button type="primary" attr-type="submit">{{ editingId ? '保存修改' : '创建习惯' }}</n-button>
        </div>
      </n-form>
    </n-modal>
  </div>
</template>

<script setup lang="ts">
import {
  NAlert,
  NButton,
  NButtonGroup,
  NCheckbox,
  NCheckboxGroup,
  NDropdown,
  NForm,
  NFormItem,
  NInput,
  NInputNumber,
  NModal,
  NRadioButton,
  NRadioGroup,
  NSelect,
  NSpin,
  NTimePicker,
  useMessage,
} from "naive-ui";
import { computed, onMounted, reactive, ref, watch } from "vue";
import {
  fetchHabitState,
  saveHabitState,
  type Habit,
  type HabitKind,
  type HabitRecord,
  type HabitRecordStatus,
  type HabitState,
} from "@/api";

const message = useMessage();
const loading = ref(true);
const loadError = ref("");
const saveLabel = ref("");
const showEditor = ref(false);
const editingId = ref("");
const titleError = ref("");
const selectedHabitId = ref("");
const calendarCursor = ref(startOfMonth(new Date()));
const state = reactive<HabitState>({ habits: [], records: [], settings: { weekStartsOn: 1 } });
let saveTimer: number | undefined;
let hasLoaded = false;

const colors = ["#4f7c66", "#3d6f91", "#785f91", "#b06c4f", "#b08a3e", "#596875"];
const weekdayOptions = [
  { label: "一", value: 1 }, { label: "二", value: 2 }, { label: "三", value: 3 },
  { label: "四", value: 4 }, { label: "五", value: 5 }, { label: "六", value: 6 }, { label: "日", value: 0 },
];
const weekdayLabels = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"];
const scheduleOptions = [
  { label: "指定星期", value: "daily" },
  { label: "每周完成若干次", value: "weekly" },
];
const habitMenuOptions = [
  { label: "编辑", key: "edit" },
  { label: "归档", key: "archive" },
];

const draft = reactive({
  title: "",
  kind: "build" as HabitKind,
  color: colors[0],
  schedule: "daily" as Habit["schedule"],
  targetCount: 3,
  weekdays: [1, 2, 3, 4, 5] as number[],
  reminderTime: null as string | null,
  replacementAction: "",
});

const today = formatDate(new Date());
const todayLabel = new Intl.DateTimeFormat("zh-CN", { month: "long", day: "numeric", weekday: "long" }).format(new Date());
const greeting = computed(() => {
  const hour = new Date().getHours();
  return hour < 11 ? "早上好，今天从一件小事开始" : hour < 18 ? "下午好，给今天留一点进展" : "晚上好，看看今天留下了什么";
});
const activeHabits = computed(() => state.habits.filter((habit) => !habit.archived));
const buildHabits = computed(() => activeHabits.value.filter((habit) => habit.kind === "build"));
const reduceHabits = computed(() => activeHabits.value.filter((habit) => habit.kind === "reduce"));
const dueToday = computed(() => activeHabits.value.filter((habit) => isDueOn(habit, new Date())));
const buildHabitsDueToday = computed(() => dueToday.value.filter((habit) => habit.kind === "build"));
const todayCompleted = computed(() => buildHabitsDueToday.value.filter((habit) => isBuildDone(habit.id)).length);
const todayProgressText = computed(() => {
  if (!buildHabitsDueToday.value.length) return "今天没有培养任务";
  if (todayCompleted.value === buildHabitsDueToday.value.length) return "今天的计划都完成了";
  return `还剩 ${buildHabitsDueToday.value.length - todayCompleted.value} 个小行动`;
});
const habitSelectOptions = computed(() => activeHabits.value.map((habit) => ({ label: habit.title, value: habit.id })));
const calendarTitle = computed(() => new Intl.DateTimeFormat("zh-CN", { year: "numeric", month: "long" }).format(calendarCursor.value));
const calendarCells = computed(() => buildCalendarCells(calendarCursor.value));
const lastSevenDates = computed(() => Array.from({ length: 7 }, (_, offset) => addDays(new Date(), offset - 6)));
const sevenDayCompletion = computed(() => {
  let planned = 0;
  let completed = 0;
  for (const date of lastSevenDates.value) {
    for (const habit of buildHabits.value) {
      if (!isDueOn(habit, date)) continue;
      planned += 1;
      if (recordFor(habit.id, formatDate(date))?.status === "completed") completed += 1;
    }
  }
  return planned ? Math.round((completed / planned) * 100) : 0;
});
const weeklyOccurrenceCount = computed(() => {
  const start = formatDate(startOfWeek(new Date()));
  return state.records
    .filter((record) => record.date >= start && reduceHabits.value.some((habit) => habit.id === record.habitId))
    .reduce((sum, record) => sum + (record.status === "occurred" ? record.count : 0), 0);
});
const weeklyInsight = computed(() => {
  if (!buildHabits.value.length) return "还没有培养习惯。可以从一个两分钟内能完成的行动开始。";
  if (sevenDayCompletion.value >= 80) return "最近一周节奏很稳定。先保持当前难度，不必急着增加目标。";
  if (sevenDayCompletion.value >= 40) return "已经形成一些稳定落点。可以观察最容易完成的是哪几天。";
  return "最近的目标可能有点重。试着缩短时长或减少每周次数，会更容易继续。";
});

onMounted(loadState);

watch(
  state,
  () => {
    if (!hasLoaded) return;
    window.clearTimeout(saveTimer);
    saveLabel.value = "等待保存…";
    saveTimer = window.setTimeout(persistState, 450);
  },
  { deep: true },
);

async function loadState() {
  loading.value = true;
  loadError.value = "";
  try {
    const remote = await fetchHabitState();
    state.habits = Array.isArray(remote.habits) ? remote.habits : [];
    state.records = Array.isArray(remote.records) ? remote.records : [];
    state.settings = remote.settings ?? { weekStartsOn: 1 };
    selectedHabitId.value = activeHabits.value[0]?.id ?? "";
    hasLoaded = true;
  } catch (error) {
    loadError.value = error instanceof Error ? error.message : "未知错误";
  } finally {
    loading.value = false;
  }
}

async function persistState() {
  saveLabel.value = "正在保存…";
  try {
    await saveHabitState(JSON.parse(JSON.stringify(state)));
    saveLabel.value = "已保存";
    window.setTimeout(() => { if (saveLabel.value === "已保存") saveLabel.value = ""; }, 1600);
  } catch {
    saveLabel.value = "保存失败";
    message.error("习惯数据保存失败，请稍后重试");
  }
}

function openCreate() {
  editingId.value = "";
  titleError.value = "";
  Object.assign(draft, {
    title: "", kind: "build", color: colors[state.habits.length % colors.length], schedule: "daily",
    targetCount: 3, weekdays: [1, 2, 3, 4, 5], reminderTime: null, replacementAction: "",
  });
  showEditor.value = true;
}

function openEdit(habit: Habit) {
  editingId.value = habit.id;
  titleError.value = "";
  Object.assign(draft, {
    title: habit.title, kind: habit.kind, color: habit.color, schedule: habit.schedule,
    targetCount: habit.targetCount, weekdays: [...habit.weekdays], reminderTime: habit.reminderTime || null,
    replacementAction: habit.replacementAction,
  });
  showEditor.value = true;
}

function saveHabit() {
  const title = draft.title.trim();
  if (!title) {
    titleError.value = "给这个习惯起一个容易识别的名字";
    return;
  }
  if (draft.schedule === "daily" && !draft.weekdays.length) {
    message.warning("请至少选择一天");
    return;
  }
  const now = new Date().toISOString();
  const existing = state.habits.find((habit) => habit.id === editingId.value);
  const value: Habit = {
    id: existing?.id ?? crypto.randomUUID(),
    title,
    kind: draft.kind,
    color: draft.color,
    schedule: draft.schedule,
    targetCount: draft.schedule === "weekly" ? Number(draft.targetCount || 1) : 1,
    weekdays: draft.schedule === "daily" ? [...draft.weekdays] : [],
    reminderTime: draft.kind === "build" ? draft.reminderTime ?? "" : "",
    replacementAction: draft.kind === "reduce" ? draft.replacementAction.trim() : "",
    archived: false,
    createdAt: existing?.createdAt ?? now,
    updatedAt: now,
  };
  if (existing) Object.assign(existing, value);
  else state.habits.push(value);
  selectedHabitId.value = value.id;
  showEditor.value = false;
  message.success(existing ? "习惯已更新" : "已加入今天的旅程");
}

function handleHabitMenu(key: string, habit: Habit) {
  if (key === "edit") openEdit(habit);
  if (key === "archive") {
    habit.archived = true;
    habit.updatedAt = new Date().toISOString();
    if (selectedHabitId.value === habit.id) selectedHabitId.value = activeHabits.value[0]?.id ?? "";
    message.info("习惯已归档，历史记录会被保留");
  }
}

function toggleBuildDone(habit: Habit) {
  if (isBuildDone(habit.id)) {
    clearTodayRecord(habit.id);
    return;
  }
  upsertRecord(habit.id, "completed", 1);
  const completed = todayCompleted.value;
  message.success(completed === buildHabitsDueToday.value.length ? "今天的计划都完成了，做得很好。" : "记下来了，这次行动正在积累。", { duration: 2200 });
}

function markSkipped(habit: Habit) {
  upsertRecord(habit.id, "skipped", 0);
  message.info("已跳过。主动调整计划，也是一种掌控。", { duration: 2200 });
}

function recordOccurrence(habit: Habit) {
  const current = todayRecord(habit.id);
  upsertRecord(habit.id, "occurred", current?.status === "occurred" ? current.count + 1 : 1);
  message.info("记录好了，不评价，只帮助你看清规律。", { duration: 2200 });
}

function recordReplacement(habit: Habit) {
  const current = todayRecord(habit.id);
  upsertRecord(habit.id, current?.status === "occurred" ? "occurred" : "replaced", current?.count ?? 0);
  message.success("你为自己创造了另一个选择。", { duration: 2200 });
}

function upsertRecord(habitId: string, status: HabitRecordStatus, count: number) {
  const now = new Date().toISOString();
  const existing = todayRecord(habitId);
  if (existing) Object.assign(existing, { status, count, updatedAt: now });
  else state.records.push({ id: crypto.randomUUID(), habitId, date: today, status, count, note: "", createdAt: now, updatedAt: now });
}

function clearTodayRecord(habitId: string) {
  state.records = state.records.filter((record) => !(record.habitId === habitId && record.date === today));
}

function todayRecord(habitId: string) { return recordFor(habitId, today); }
function isBuildDone(habitId: string) { return todayRecord(habitId)?.status === "completed"; }
function occurrenceCount(habitId: string) { const record = todayRecord(habitId); return record?.status === "occurred" ? record.count : 0; }
function recordFor(habitId: string, date: string) { return state.records.find((record) => record.habitId === habitId && record.date === date); }

function frequencyLabel(habit: Habit) {
  if (habit.schedule === "weekly") return `每周 ${habit.targetCount} 次`;
  if (habit.weekdays.length === 7) return "每天";
  return habit.weekdays.map((value) => weekdayOptions.find((day) => day.value === value)?.label).filter(Boolean).join("、");
}

function isDueOn(habit: Habit, date: Date) {
  if (formatDate(date) < habit.createdAt.slice(0, 10)) return false;
  return habit.schedule === "weekly" || habit.weekdays.includes(date.getDay());
}

function recordStatusLabel(record?: HabitRecord) {
  if (!record) return "无记录";
  const labels: Record<HabitRecordStatus, string> = {
    completed: "已完成", partial: "部分完成", skipped: "主动跳过", occurred: `发生 ${record.count} 次`, replaced: "使用了替代动作",
  };
  return labels[record.status];
}

function moveMonth(offset: number) { calendarCursor.value = new Date(calendarCursor.value.getFullYear(), calendarCursor.value.getMonth() + offset, 1); }
function resetMonth() { calendarCursor.value = startOfMonth(new Date()); }

function formatDate(date: Date) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  return `${year}-${month}-${day}`;
}
function addDays(date: Date, days: number) { const result = new Date(date); result.setDate(result.getDate() + days); return result; }
function startOfMonth(date: Date) { return new Date(date.getFullYear(), date.getMonth(), 1); }
function startOfWeek(date: Date) { const result = new Date(date); const offset = (result.getDay() + 6) % 7; result.setDate(result.getDate() - offset); result.setHours(0, 0, 0, 0); return result; }
function buildCalendarCells(cursor: Date) {
  const first = startOfMonth(cursor);
  const start = addDays(first, -((first.getDay() + 6) % 7));
  return Array.from({ length: 42 }, (_, index) => {
    const date = addDays(start, index);
    return { date: formatDate(date), day: date.getDate(), inMonth: date.getMonth() === cursor.getMonth() };
  });
}
</script>

<style scoped>
.habit-tool { --ink: #20322d; --muted: #697973; --line: rgba(41, 69, 60, .13); display: grid; gap: 18px; color: var(--ink); }
.habit-hero { min-height: 150px; padding: 28px 30px; display: flex; align-items: center; justify-content: space-between; gap: 24px; border-radius: 14px; color: #f5fbf7; background: linear-gradient(120deg, #244a3d, #426b59 58%, #6d8267); box-shadow: 0 18px 35px rgba(38, 72, 59, .18); }
.habit-hero h3 { max-width: 620px; margin: 5px 0 7px; font-size: clamp(23px, 3vw, 32px); line-height: 1.25; }
.habit-hero p { margin: 0; color: rgba(245, 251, 247, .76); }
.habit-kicker { font-size: 12px; font-weight: 800; letter-spacing: .08em; text-transform: uppercase; }
.hero-actions { display: flex; align-items: center; gap: 12px; flex: 0 0 auto; }
.save-state { color: rgba(255,255,255,.7); font-size: 12px; }
.habit-hero :deep(.n-button--primary-type) { --n-color: #f3f7ef !important; --n-color-hover: #fff !important; --n-text-color: #294b3f !important; --n-text-color-hover: #294b3f !important; }
.load-alert { margin-top: 6px; }
.habit-empty { min-height: 390px; padding: 60px 24px; display: grid; place-items: center; align-content: center; gap: 12px; border: 1px solid var(--line); border-radius: 12px; background: rgba(255,255,255,.78); text-align: center; }
.habit-empty h3, .habit-empty p { margin: 0; }.habit-empty p { color: var(--muted); }.empty-symbol { font-size: 56px; color: #658375; }
.overview-grid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
.overview-card { min-height: 112px; padding: 18px; display: grid; align-content: center; gap: 4px; border: 1px solid var(--line); border-radius: 10px; background: rgba(255,255,255,.82); box-shadow: 0 8px 20px rgba(43, 67, 60, .05); }
.overview-card span, .overview-card small { color: var(--muted); }.overview-card span { font-size: 12px; font-weight: 700; }.overview-card strong { font-size: 28px; line-height: 1.15; }.overview-card--accent { color: #f6fbf8; background: #315849; }.overview-card--accent span, .overview-card--accent small { color: rgba(255,255,255,.7); }
.habit-layout { display: grid; grid-template-columns: minmax(0, 1fr) 300px; gap: 16px; align-items: start; }.habit-main, .habit-sidebar { display: grid; gap: 16px; }
.habit-section { padding: 22px; border: 1px solid var(--line); border-radius: 12px; background: rgba(255,255,255,.84); box-shadow: 0 10px 28px rgba(43, 67, 60, .05); }
.section-heading { margin-bottom: 16px; display: flex; align-items: flex-end; justify-content: space-between; gap: 12px; }.section-heading p { margin: 0 0 3px; color: #789086; font-size: 10px; font-weight: 800; letter-spacing: .1em; text-transform: uppercase; }.section-heading h3 { margin: 0; font-size: 19px; }.section-heading > span { color: var(--muted); font-size: 12px; }
.today-list { display: grid; gap: 10px; }.today-card { position: relative; display: grid; grid-template-columns: 5px 1fr; overflow: hidden; border: 1px solid var(--line); border-radius: 10px; background: #fff; transition: opacity .2s, background .2s; }.today-card--done { background: #f2f7f3; }.habit-color { width: 5px; height: 100%; }.today-card-body { min-width: 0; padding: 16px 18px; }.today-card-title { display: flex; align-items: flex-start; justify-content: space-between; gap: 10px; }.today-card-title h4 { display: inline; margin: 0 0 0 8px; font-size: 16px; }.kind-label { padding: 3px 7px; border-radius: 999px; font-size: 10px; font-weight: 800; }.kind-label--build { color: #315d4c; background: #dcebe3; }.kind-label--reduce { color: #785542; background: #f0e5dc; }
.habit-meta, .replacement-copy { margin: 9px 0 12px; color: var(--muted); font-size: 12px; }.habit-meta { display: flex; flex-wrap: wrap; gap: 12px; }.replacement-copy { padding: 9px 11px; border-radius: 7px; background: #f4f1ea; color: #665f50; }.card-actions { display: flex; align-items: center; flex-wrap: wrap; gap: 8px; }.quiet-state { color: var(--muted); font-size: 12px; }.quiet-empty { padding: 30px; border-radius: 8px; background: #f4f7f5; color: var(--muted); text-align: center; }
.calendar-heading { align-items: center; }.calendar-controls { display: flex; align-items: center; gap: 8px; }.calendar-controls > :deep(.n-select) { width: 170px; }.calendar-grid { display: grid; grid-template-columns: repeat(7, 1fr); }.calendar-weekdays { padding-bottom: 8px; color: var(--muted); font-size: 11px; text-align: center; }.calendar-days { overflow: hidden; border-top: 1px solid var(--line); border-left: 1px solid var(--line); border-radius: 8px; }.calendar-day { position: relative; min-height: 68px; padding: 8px; border-right: 1px solid var(--line); border-bottom: 1px solid var(--line); background: rgba(255,255,255,.65); font-size: 12px; }.calendar-day--outside { color: #afb9b5; background: #f4f6f5; }.calendar-day--today > span { width: 24px; height: 24px; display: grid; place-items: center; margin: -4px; border-radius: 50%; background: #315849; color: white; font-weight: 800; }.record-dot { width: 9px; height: 9px; display: inline-block; border-radius: 50%; background: #d4dbd7; }.calendar-day .record-dot { position: absolute; right: 9px; bottom: 9px; width: 11px; height: 11px; }.record-dot--completed { background: #4e8a69; }.record-dot--partial { background: #d09b48; }.record-dot--skipped { background: #aab4b0; }.record-dot--occurred { background: #b66f54; }.record-dot--replaced { background: #657cac; }.record-dot--empty { border: 1px solid #bcc6c1; background: transparent; }.calendar-legend { padding-top: 12px; display: flex; flex-wrap: wrap; gap: 12px; color: var(--muted); font-size: 11px; }.calendar-legend span { display: flex; align-items: center; gap: 5px; }
.insight-card { background: linear-gradient(150deg, #f7f4e9, #fff); }.insight-copy { min-height: 86px; margin: 0; color: #4e5f58; font-size: 14px; line-height: 1.8; }.insight-bar { height: 6px; margin: 12px 0 10px; overflow: hidden; border-radius: 999px; background: #e1e5df; }.insight-bar i { height: 100%; display: block; border-radius: inherit; background: #66866e; }.insight-card > small { color: var(--muted); line-height: 1.6; }
.all-habits { padding-bottom: 12px; }.habit-list-item { width: 100%; padding: 11px 4px; display: grid; grid-template-columns: 8px minmax(0, 1fr) auto; align-items: center; gap: 10px; border: 0; border-bottom: 1px solid var(--line); background: transparent; color: inherit; text-align: left; cursor: pointer; }.habit-list-item:hover { background: #f4f7f5; }.habit-list-item > i { width: 8px; height: 28px; border-radius: 999px; }.habit-list-item > span { min-width: 0; display: grid; gap: 2px; }.habit-list-item strong { overflow: hidden; font-size: 13px; text-overflow: ellipsis; white-space: nowrap; }.habit-list-item small { color: var(--muted); }.habit-list-item em { color: var(--muted); font-size: 10px; font-style: normal; }
.habit-modal { width: min(620px, calc(100vw - 28px)); }.kind-choice { width: 100%; display: grid; grid-template-columns: 1fr 1fr; }.kind-choice :deep(.n-radio-button) { justify-content: center; }.editor-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }.editor-grid :deep(.n-input-number) { width: 100%; }.color-options { min-height: 34px; display: flex; align-items: center; gap: 8px; }.color-options button { width: 24px; height: 24px; padding: 0; border: 3px solid white; border-radius: 50%; box-shadow: 0 0 0 1px #ccd4d0; cursor: pointer; }.color-options button.active { box-shadow: 0 0 0 2px #263f36; }.weekday-choice { display: flex; flex-wrap: wrap; gap: 11px; }.modal-actions { display: flex; justify-content: flex-end; gap: 10px; }
@media (max-width: 900px) { .overview-grid { grid-template-columns: repeat(2, 1fr); }.habit-layout { grid-template-columns: 1fr; }.habit-sidebar { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
@media (max-width: 620px) { .habit-hero { padding: 22px; display: grid; }.hero-actions { justify-content: space-between; }.overview-grid, .habit-sidebar, .editor-grid { grid-template-columns: 1fr; }.overview-card { min-height: 92px; }.habit-section { padding: 16px; }.calendar-heading { align-items: flex-start; display: grid; }.calendar-controls { width: 100%; justify-content: space-between; }.calendar-controls > :deep(.n-select) { width: min(170px, 52%); }.calendar-day { min-height: 48px; padding: 6px; }.calendar-day .record-dot { right: 6px; bottom: 6px; }.kind-choice { grid-template-columns: 1fr; } }
</style>
