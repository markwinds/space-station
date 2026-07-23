<template>
  <div class="terminal-search-bar">
    <n-input
      ref="input"
      class="terminal-search-query"
      :value="query"
      :status="regexError ? 'error' : undefined"
      size="small"
      clearable
      placeholder="查找"
      :title="regexError ? '正则表达式无效' : undefined"
      @update:value="$emit('update:query', $event)"
      @keyup.enter="$emit('next')"
    />
    <n-button
      class="terminal-search-icon-button terminal-search-toggle"
      :class="{ active: caseSensitive }"
      size="small"
      quaternary
      :aria-pressed="caseSensitive"
      :title="caseSensitive ? '区分大小写：已开启' : '区分大小写：已关闭'"
      @click="$emit('update:caseSensitive', !caseSensitive)"
    >Aa</n-button>
    <n-button
      class="terminal-search-icon-button terminal-search-toggle"
      :class="{ active: wholeWord }"
      size="small"
      quaternary
      :aria-pressed="wholeWord"
      :title="wholeWord ? '全词匹配：已开启' : '全词匹配：已关闭'"
      @click="$emit('update:wholeWord', !wholeWord)"
    >ab</n-button>
    <n-button
      class="terminal-search-icon-button terminal-search-toggle"
      :class="{ active: regex }"
      size="small"
      quaternary
      :aria-pressed="regex"
      :title="regex ? '正则表达式：已开启' : '正则表达式：已关闭'"
      @click="$emit('update:regex', !regex)"
    >.*</n-button>
    <n-input
      :value="displayIndexText"
      class="terminal-search-position"
      size="small"
      inputmode="numeric"
      placeholder="0"
      title="输入匹配序号后按 Enter 跳转"
      @update:value="updateTargetIndex"
      @focus="focusIndex"
      @blur="$emit('indexFocus', false)"
      @keydown.enter.prevent="$emit('jump')"
    >
      <template #suffix><span class="terminal-search-total">/ {{ totalText }}</span></template>
    </n-input>
    <n-button class="terminal-search-icon-button" size="small" quaternary circle title="上一个匹配项" aria-label="上一个匹配项" @click="$emit('previous')">
      <template #icon><n-icon><ArrowUpOutline /></n-icon></template>
    </n-button>
    <n-button class="terminal-search-icon-button" size="small" quaternary circle title="下一个匹配项" aria-label="下一个匹配项" @click="$emit('next')">
      <template #icon><n-icon><ArrowDownOutline /></n-icon></template>
    </n-button>
    <n-button class="terminal-search-icon-button" size="small" quaternary circle title="关闭查找" aria-label="关闭查找" @click="$emit('close')">
      <template #icon><n-icon><CloseOutline /></n-icon></template>
    </n-button>
  </div>
</template>

<script setup lang="ts">
import { ArrowDownOutline, ArrowUpOutline, CloseOutline } from "@vicons/ionicons5";
import { NButton, NIcon, NInput, type InputInst } from "naive-ui";
import { computed, ref } from "vue";

const props = defineProps<{
  query: string;
  targetIndex: number | null;
  resultCount: number;
  countText: string;
  caseSensitive: boolean;
  wholeWord: boolean;
  regex: boolean;
}>();
const emit = defineEmits<{
  "update:query": [value: string];
  "update:targetIndex": [value: number | null];
  "update:caseSensitive": [value: boolean];
  "update:wholeWord": [value: boolean];
  "update:regex": [value: boolean];
  previous: [];
  next: [];
  jump: [];
  close: [];
  indexFocus: [focused: boolean];
}>();

const input = ref<InputInst>();
const regexError = computed(() => {
  if (!props.regex || !props.query) return false;
  try {
    new RegExp(props.query);
    return false;
  } catch {
    return true;
  }
});
const displayIndexText = computed(() => {
  if (props.targetIndex !== null) {
    return String(props.targetIndex);
  }
  const separatorIndex = props.countText.indexOf("/");
  return separatorIndex >= 0 ? props.countText.slice(0, separatorIndex) : "0";
});
const totalText = computed(() => {
  const separatorIndex = props.countText.indexOf("/");
  return separatorIndex >= 0 ? props.countText.slice(separatorIndex + 1) : String(props.resultCount);
});

function updateTargetIndex(value: string) {
  const normalized = value.replace(/\D/g, "");
  emit("update:targetIndex", normalized ? Number(normalized) : null);
}

function focusIndex(event: FocusEvent) {
  emit("indexFocus", true);
  (event.target as HTMLInputElement | null)?.select();
}

defineExpose({ focus: () => input.value?.focus() });
</script>

<style scoped>
.terminal-search-bar { position: absolute; z-index: 8; top: 8px; right: 12px; width: min(570px, calc(100% - 24px)); padding: 6px; display: flex; align-items: center; gap: 3px; border: 1px solid #4d606c; border-radius: 6px; background: #1a2228; box-shadow: 0 8px 24px #0008; }
.terminal-search-query { min-width: 120px; flex: 1; }
.terminal-search-position { width: 104px; flex: none; }
.terminal-search-position :deep(input) { text-align: right; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
.terminal-search-total { color: #9baab4; font: 12px/1 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; white-space: nowrap; }
.terminal-search-icon-button { flex: none; color: #dce5ea; }
.terminal-search-icon-button:hover { color: #ffffff; background: #34434e; }
.terminal-search-toggle { width: 30px; min-width: 30px; padding: 0; border-radius: 4px; font: 600 12px/1 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
.terminal-search-toggle:not(.active):focus { color: #dce5ea !important; background: transparent !important; }
.terminal-search-toggle:not(.active):focus-visible { box-shadow: inset 0 0 0 1px #6f818d; }
.terminal-search-toggle.active { color: #ffffff; background: #2563eb; }
.terminal-search-toggle.active:focus { color: #ffffff !important; background: #2563eb !important; }
.terminal-search-toggle.active::after { content: ""; position: absolute; right: 5px; bottom: 3px; left: 5px; height: 1px; background: currentColor; }
@media (max-width: 760px) {
  .terminal-search-bar { top: 6px; right: 6px; width: calc(100% - 12px); }
  .terminal-search-query { min-width: 88px; }
  .terminal-search-position { width: 82px; }
}
@media (max-width: 520px) {
  .terminal-search-bar { flex-wrap: wrap; }
  .terminal-search-query { flex-basis: 100%; }
  .terminal-search-position { margin-left: auto; }
}
</style>
