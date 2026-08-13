<template>
  <n-tooltip trigger="hover" placement="top">
    <template #trigger>
      <button class="terminal-setting-help" type="button" :aria-label="text">
        <n-icon size="14"><HelpCircleOutline /></n-icon>
      </button>
    </template>
    {{ text }}
  </n-tooltip>
</template>

<script setup lang="ts">
import { HelpCircleOutline } from "@vicons/ionicons5";
import { NIcon, NTooltip } from "naive-ui";

defineProps<{ text: string }>();
</script>

<style scoped>
.terminal-setting-help {
  width: 18px;
  height: 18px;
  padding: 0;
  display: inline-grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: transparent;
  color: #77858e;
  vertical-align: -3px;
  cursor: help;
}
.terminal-setting-help:hover,
.terminal-setting-help:focus-visible { background: #e5ecee; color: #315f5c; outline: none; }
</style>
