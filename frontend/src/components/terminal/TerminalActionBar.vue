<template>
  <div class="terminal-action-bar">
    <n-tooltip v-if="search" trigger="hover" placement="bottom">
      <template #trigger>
        <n-button class="terminal-action-bar__icon" secondary circle size="tiny" aria-label="搜索终端" @click="$emit('search')">
          <template #icon><n-icon><SearchOutline /></n-icon></template>
        </n-button>
      </template>
      搜索终端 · Ctrl/⌘ + F
    </n-tooltip>
    <n-button v-if="snippets" secondary size="tiny" aria-label="命令片段" title="管理命令片段" @click="$emit('snippets')">
      <template #icon><n-icon><CodeSlashOutline /></n-icon></template>
      片段
    </n-button>
    <n-button v-if="download" secondary size="tiny" aria-label="下载当前 Shell 数据" title="下载当前 Shell 数据（纯文本）" @click="$emit('download')">
      <template #icon><n-icon><DownloadOutline /></n-icon></template>
      下载
    </n-button>
    <n-button
      v-if="recordingEnabled"
      class="terminal-action-bar__recording"
      secondary
      size="tiny"
      :type="recording ? 'error' : 'default'"
      :aria-label="recording ? '停止终端录制' : '开始终端录制'"
      :title="recording ? '停止终端录制' : '开始终端录制'"
      @click="$emit('recording')"
    >
      <template #icon><n-icon><component :is="recording ? StopOutline : RadioButtonOnOutline" /></n-icon></template>
      {{ recording ? '停止' : '录制' }}
    </n-button>
    <n-tooltip v-if="settings" trigger="hover" placement="bottom">
      <template #trigger>
        <n-button class="terminal-action-bar__icon" secondary circle size="tiny" aria-label="终端设置" @click="$emit('settings')">
          <template #icon><n-icon><SettingsOutline /></n-icon></template>
        </n-button>
      </template>
      终端设置
    </n-tooltip>
    <terminal-renderer-badge v-if="renderer" :renderer="renderer" />
  </div>
</template>

<script setup lang="ts">
import { CodeSlashOutline, DownloadOutline, RadioButtonOnOutline, SearchOutline, SettingsOutline, StopOutline } from "@vicons/ionicons5";
import { NButton, NIcon, NTooltip } from "naive-ui";
import type { TerminalRenderer } from "./WebTerminal.types";
import TerminalRendererBadge from "./TerminalRendererBadge.vue";

withDefaults(defineProps<{
  search?: boolean;
  snippets?: boolean;
  download?: boolean;
  recordingEnabled?: boolean;
  recording?: boolean;
  settings?: boolean;
  renderer?: TerminalRenderer;
}>(), {
  search: true,
  snippets: true,
  download: false,
  recordingEnabled: true,
  recording: false,
  settings: false,
  renderer: undefined,
});

defineEmits<{ search: []; snippets: []; download: []; recording: []; settings: [] }>();
</script>

<style scoped>
.terminal-action-bar { display: flex; align-items: center; gap: 7px; }
.terminal-action-bar :deep(.n-button) { color: #d8e1e6; background: #293842; border-color: #465965; }
.terminal-action-bar :deep(.n-button:not(.n-button--disabled):hover) { color: #102027; background: #8fbfbc; border-color: #8fbfbc; }
.terminal-action-bar :deep(.n-button:not(.n-button--disabled):focus) { color: #ffffff; background: #34434e; border-color: #78909d; }
.terminal-action-bar :deep(.n-button:not(.n-button--disabled):active) { color: #102027; background: #78aaa7; border-color: #9bc7c4; }
.terminal-action-bar :deep(.n-button:focus-visible) { box-shadow: 0 0 0 2px #101418, 0 0 0 4px #9bc7c4; }
.terminal-action-bar :deep(.n-button.n-button--disabled) { color: #8999a3; background: #202b32; border-color: #394852; opacity: 1; }
.terminal-action-bar :deep(.terminal-action-bar__icon) { width: 30px; min-width: 30px; height: 30px; padding: 0; }
.terminal-action-bar :deep(.terminal-action-bar__recording.n-button--error-type) { color: #ffd8d5; background: #71352f; border-color: #a65349; }
.terminal-action-bar :deep(.terminal-action-bar__recording.n-button--error-type:hover),
.terminal-action-bar :deep(.terminal-action-bar__recording.n-button--error-type:focus) { color: #fff; background: #a8443a; border-color: #c25a4f; }
.terminal-action-bar :deep(.terminal-action-bar__recording.n-button--error-type:active) { color: #fff; background: #8f352d; border-color: #d06a5f; }
@media (max-width: 760px) {
  .terminal-action-bar { max-width: 100%; overflow-x: auto; scrollbar-width: none; }
  .terminal-action-bar::-webkit-scrollbar { display: none; }
}
</style>
