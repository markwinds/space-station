<template>
  <div class="terminal-command-panel">
    <div class="terminal-command-toolbar">
      <n-button secondary size="tiny" @click="$emit('toggleQuick')">
        {{ showQuick ? '隐藏快捷片段' : '显示快捷片段' }}
      </n-button>
      <div v-if="showQuick" class="terminal-quick-snippets">
        <n-button v-for="snippet in snippets" :key="snippet.id" secondary size="tiny" @click="$emit('sendSnippet', snippet)">{{ snippet.name }}</n-button>
        <span v-if="snippets.length === 0">暂无快捷片段</span>
      </div>
      <n-button text size="tiny" @click="$emit('manageSnippets')">管理片段</n-button>
      <n-button text size="tiny" @click="$emit('toggleComposer')">{{ showComposer ? hideLabel : showLabel }}</n-button>
    </div>
    <div v-if="showComposer" class="terminal-command-composer"><slot name="composer" /></div>
  </div>
</template>

<script setup lang="ts">
import { NButton } from "naive-ui";

export interface TerminalCommandSnippet { id: string; name: string; command: string; pinned?: boolean }

withDefaults(defineProps<{
  showQuick: boolean;
  showComposer: boolean;
  snippets: TerminalCommandSnippet[];
  hideLabel?: string;
  showLabel?: string;
}>(), { hideLabel: "隐藏命令框", showLabel: "显示命令框" });

defineEmits<{
  toggleQuick: [];
  manageSnippets: [];
  toggleComposer: [];
  sendSnippet: [snippet: TerminalCommandSnippet];
}>();
</script>

<style scoped>
.terminal-command-panel { display: grid; border-top: 1px solid #34414b; background: #171e23; }
.terminal-command-toolbar { min-width: 0; padding: 7px 9px; display: flex; align-items: center; gap: 7px; }
.terminal-command-toolbar :deep(.n-button) { color: #dce6eb; border-color: #50616c; background: #2c3941; }
.terminal-quick-snippets { min-width: 0; flex: 1; display: flex; gap: 6px; overflow-x: auto; scrollbar-width: thin; }
.terminal-quick-snippets > span { align-self: center; color: #83939e; font-size: 12px; }
.terminal-command-composer { padding: 0 9px 9px; }
@media (max-width: 760px) {
  .terminal-command-toolbar { flex-wrap: wrap; }
  .terminal-quick-snippets { order: 3; flex-basis: 100%; }
  .terminal-command-composer { padding-bottom: max(9px, env(safe-area-inset-bottom)); }
}
</style>
