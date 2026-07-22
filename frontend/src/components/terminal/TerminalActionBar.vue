<template>
  <div class="terminal-action-bar">
    <n-button v-if="search" secondary size="tiny" @click="$emit('search')">搜索</n-button>
    <n-button v-if="snippets" secondary size="tiny" @click="$emit('snippets')">片段</n-button>
    <n-button
      v-if="recordingEnabled"
      secondary
      size="tiny"
      :type="recording ? 'error' : 'default'"
      @click="$emit('recording')"
    >{{ recording ? '停止录制' : '录制' }}</n-button>
    <n-button v-if="settings" secondary size="tiny" @click="$emit('settings')">终端设置</n-button>
    <span v-if="renderer" class="terminal-renderer" :class="renderer">{{ renderer === 'webgl' ? 'GPU' : 'Canvas' }}</span>
  </div>
</template>

<script setup lang="ts">
import { NButton } from "naive-ui";
import type { TerminalRenderer } from "./WebTerminal.types";

withDefaults(defineProps<{
  search?: boolean;
  snippets?: boolean;
  recordingEnabled?: boolean;
  recording?: boolean;
  settings?: boolean;
  renderer?: TerminalRenderer;
}>(), {
  search: true,
  snippets: true,
  recordingEnabled: true,
  recording: false,
  settings: false,
  renderer: undefined,
});

defineEmits<{ search: []; snippets: []; recording: []; settings: [] }>();
</script>

<style scoped>
.terminal-action-bar { display: flex; align-items: center; gap: 7px; }
.terminal-action-bar :deep(.n-button) { color: #d8e1e6; background: #293842; border-color: #465965; }
.terminal-action-bar :deep(.n-button:hover) { color: #102027; background: #8fbfbc; border-color: #8fbfbc; }
.terminal-renderer { padding: 2px 7px; border: 1px solid #465965; border-radius: 999px; color: #aebcc5; font: 10px/1.4 monospace; white-space: nowrap; }
.terminal-renderer.webgl { border-color: #3b7256; color: #76cf96; background: #193124; }
@media (max-width: 760px) {
  .terminal-action-bar { max-width: 100%; overflow-x: auto; scrollbar-width: none; }
  .terminal-action-bar::-webkit-scrollbar { display: none; }
}
</style>
