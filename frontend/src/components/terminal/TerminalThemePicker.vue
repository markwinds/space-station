<template>
  <div class="terminal-theme-picker" role="radiogroup" aria-label="终端配色主题">
    <button
      v-for="theme in TERMINAL_THEMES"
      :key="theme.id"
      type="button"
      role="radio"
      :aria-checked="modelValue === theme.id"
      :class="{ active: modelValue === theme.id }"
      :style="themeStyle(theme)"
      @click="$emit('update:modelValue', theme.id)"
    >
      <span class="terminal-theme-picker__preview" aria-hidden="true">
        <span class="terminal-theme-picker__dots"><i /><i /><i /></span>
        <code><b>$</b> ssh production<br><em>✓ connected</em><span>_</span></code>
      </span>
      <span class="terminal-theme-picker__copy">
        <strong>{{ theme.label }}</strong>
        <small>{{ theme.description }}</small>
      </span>
      <span v-if="modelValue === theme.id" class="terminal-theme-picker__check" aria-hidden="true">✓</span>
    </button>
  </div>
</template>

<script setup lang="ts">
import type { CSSProperties } from "vue";
import { TERMINAL_THEMES, type TerminalThemeDefinition, type TerminalThemeId } from "./terminalPreferences";

defineProps<{ modelValue: TerminalThemeId }>();
defineEmits<{ "update:modelValue": [value: TerminalThemeId] }>();

function themeStyle(theme: TerminalThemeDefinition) {
  return {
    "--theme-background": theme.palette.background,
    "--theme-foreground": theme.palette.foreground,
    "--theme-cursor": theme.palette.cursor,
    "--theme-selection": theme.palette.selectionBackground,
    "--theme-green": theme.palette.green,
    "--theme-red": theme.palette.red,
    "--theme-yellow": theme.palette.yellow,
  } as CSSProperties;
}
</script>

<style scoped>
.terminal-theme-picker { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; }
.terminal-theme-picker > button { position: relative; min-width: 0; padding: 7px; display: grid; gap: 7px; border: 1px solid #d6dee2; border-radius: 8px; background: #ffffff; color: #293740; text-align: left; cursor: pointer; transition: border-color .14s ease, box-shadow .14s ease, transform .14s ease; }
.terminal-theme-picker > button:hover { border-color: #8aa8a5; transform: translateY(-1px); }
.terminal-theme-picker > button.active { border-color: #3f7772; box-shadow: 0 0 0 2px rgba(63, 119, 114, .16); }
.terminal-theme-picker__preview { min-width: 0; height: 58px; padding: 7px 8px; display: grid; align-content: start; gap: 5px; overflow: hidden; border-radius: 5px; background: var(--theme-background); color: var(--theme-foreground); }
.terminal-theme-picker__dots { display: flex; gap: 3px; }
.terminal-theme-picker__dots i { width: 5px; height: 5px; border-radius: 50%; background: var(--theme-red); }
.terminal-theme-picker__dots i:nth-child(2) { background: var(--theme-yellow); }
.terminal-theme-picker__dots i:nth-child(3) { background: var(--theme-green); }
.terminal-theme-picker code { overflow: hidden; color: var(--theme-foreground); font: 9px/1.55 "SFMono-Regular", Consolas, monospace; white-space: nowrap; }
.terminal-theme-picker code b { color: var(--theme-cursor); }
.terminal-theme-picker code em { color: var(--theme-green); font-style: normal; }
.terminal-theme-picker code span { margin-left: 2px; color: var(--theme-cursor); }
.terminal-theme-picker__copy { min-width: 0; display: grid; gap: 2px; }
.terminal-theme-picker__copy strong, .terminal-theme-picker__copy small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.terminal-theme-picker__copy strong { font-size: 12px; }
.terminal-theme-picker__copy small { color: #78858d; font-size: 10px; }
.terminal-theme-picker__check { position: absolute; top: 10px; right: 10px; width: 18px; height: 18px; display: grid; place-items: center; border-radius: 50%; background: #3f7772; color: #ffffff; font-size: 11px; font-weight: 900; }
@media (max-width: 760px) {
  .terminal-theme-picker { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .terminal-theme-picker > button { min-height: 96px; }
}
</style>
