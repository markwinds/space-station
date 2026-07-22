<template>
  <div class="terminal-search-bar">
    <n-input
      ref="input"
      :value="query"
      size="small"
      clearable
      placeholder="输入文字，Enter 查找下一个"
      @update:value="$emit('update:query', $event)"
      @keyup.enter="$emit('next')"
    />
    <span class="terminal-search-count">{{ countText }}</span>
    <n-input-number
      :value="targetIndex"
      class="terminal-search-index"
      size="small"
      :min="1"
      :max="Math.max(1, resultCount)"
      :show-button="false"
      placeholder="序号"
      @update:value="$emit('update:targetIndex', $event)"
      @focus="$emit('indexFocus', true)"
      @blur="$emit('indexFocus', false)"
      @keyup.enter="$emit('jump')"
    />
    <n-button size="small" @click="$emit('jump')">跳转</n-button>
    <n-button size="small" @click="$emit('previous')">上一个</n-button>
    <n-button size="small" @click="$emit('next')">下一个</n-button>
    <n-button size="small" @click="$emit('close')">关闭</n-button>
  </div>
</template>

<script setup lang="ts">
import { NButton, NInput, NInputNumber, type InputInst } from "naive-ui";
import { ref } from "vue";

defineProps<{ query: string; targetIndex: number | null; resultCount: number; countText: string }>();
defineEmits<{
  "update:query": [value: string];
  "update:targetIndex": [value: number | null];
  previous: [];
  next: [];
  jump: [];
  close: [];
  indexFocus: [focused: boolean];
}>();

const input = ref<InputInst>();
defineExpose({ focus: () => input.value?.focus() });
</script>

<style scoped>
.terminal-search-bar { position: absolute; z-index: 8; top: 8px; right: 12px; width: min(650px, calc(100% - 24px)); padding: 8px; display: flex; align-items: center; gap: 7px; border: 1px solid #4d606c; border-radius: 7px; background: #1a2228; box-shadow: 0 8px 24px #0008; }
.terminal-search-bar :deep(.n-button) { color: #e5edf2; background: #34434e; border-color: #5d707c; }
.terminal-search-bar :deep(.n-button:hover) { color: #101418; background: #9bc7c4; }
.terminal-search-bar > :first-child { min-width: 160px; flex: 1; }
.terminal-search-index { width: 86px; flex: none; }
.terminal-search-count { min-width: 54px; color: #dce5ea; font: 12px/1.4 monospace; text-align: center; white-space: nowrap; }
@media (max-width: 760px) {
  .terminal-search-bar { top: 6px; flex-wrap: wrap; }
  .terminal-search-bar > :first-child { flex-basis: 100%; }
}
</style>
