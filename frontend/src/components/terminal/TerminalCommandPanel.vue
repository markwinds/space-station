<template>
  <div class="terminal-command-panel">
    <div class="terminal-command-toolbar">
      <n-button
        class="terminal-command-toggle"
        :class="{ active: showComposer }"
        secondary
        size="small"
        :aria-pressed="showComposer"
        @click="$emit('toggleComposer')"
      >
        <n-icon><CodeSlashOutline /></n-icon>
        {{ label }}
      </n-button>
      <n-button
        class="terminal-command-toggle"
        :class="{ active: showQuick }"
        secondary
        size="small"
        :aria-pressed="showQuick"
        @click="$emit('toggleQuick')"
      >
        <template #icon><n-icon><FlashOutline /></n-icon></template>
        片段
      </n-button>
      <div v-if="showQuick" ref="quickSnippetList" class="terminal-quick-snippets" @wheel="scrollQuickSnippets">
        <n-button
          v-for="snippet in snippets"
          :key="snippet.id"
          class="terminal-snippet-chip"
          :class="{ 'terminal-snippet-chip--run': snippet.action !== 'insert' }"
          secondary
          size="small"
          :title="snippet.action === 'insert' ? `插入：${snippet.command}` : `立即执行：${snippet.command}`"
          @mousedown.prevent
          @click="$emit('useSnippet', snippet)"
        >
          {{ snippet.name }}<span v-if="snippet.action !== 'insert'" aria-hidden="true">↵</span>
        </n-button>
        <span v-if="snippets.length === 0">暂无快捷片段</span>
      </div>
    </div>
    <div v-if="showComposer" class="terminal-command-composer"><slot name="composer" /></div>
  </div>
</template>

<script setup lang="ts">
import { CodeSlashOutline, FlashOutline } from "@vicons/ionicons5";
import { NButton, NIcon } from "naive-ui";
import { ref } from "vue";

export interface TerminalCommandSnippet {
  id: string;
  name: string;
  command: string;
  pinned?: boolean;
  action: "insert" | "run";
}

withDefaults(defineProps<{
  showQuick: boolean;
  showComposer: boolean;
  snippets: TerminalCommandSnippet[];
  label?: string;
}>(), { label: "命令框" });

defineEmits<{
  toggleQuick: [];
  toggleComposer: [];
  useSnippet: [snippet: TerminalCommandSnippet];
}>();

const quickSnippetList = ref<HTMLElement | null>(null);

function scrollQuickSnippets(event: WheelEvent) {
  const element = quickSnippetList.value;
  if (!element || element.scrollWidth <= element.clientWidth) return;
  const rawDelta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
  if (!rawDelta) return;
  const scale = event.deltaMode === WheelEvent.DOM_DELTA_LINE
    ? 24
    : event.deltaMode === WheelEvent.DOM_DELTA_PAGE ? element.clientWidth : 1;
  const maximum = element.scrollWidth - element.clientWidth;
  const next = Math.max(0, Math.min(maximum, element.scrollLeft + rawDelta * scale));
  if (next === element.scrollLeft) return;
  element.scrollLeft = next;
  event.preventDefault();
}
</script>

<style scoped>
.terminal-command-panel { display: grid; border-top: 1px solid #34414b; background: #171e23; }
.terminal-command-toolbar { min-width: 0; min-height: 46px; padding: 7px 9px; display: flex; align-items: center; gap: 7px; }
.terminal-command-toolbar :deep(.n-button) { color: #dce6eb; border-color: #50616c; background: #2c3941; }
.terminal-command-toolbar :deep(.n-button) { min-height: 32px; }
.terminal-command-toolbar :deep(.n-button:not(.n-button--disabled):hover) { color: #102027; border-color: #9bc7c4; background: #9bc7c4; }
.terminal-command-toolbar :deep(.n-button:not(.n-button--disabled):focus) { color: #ffffff; border-color: #78909d; background: #3a4b55; }
.terminal-command-toolbar :deep(.n-button:not(.n-button--disabled):active) { color: #102027; border-color: #aed8d5; background: #82b2af; }
.terminal-command-toolbar :deep(.n-button:focus-visible) { box-shadow: 0 0 0 2px #171e23, 0 0 0 4px #9bc7c4; }
.terminal-command-toolbar :deep(.n-button.n-button--disabled) { color: #8999a3; border-color: #3d4c55; background: #242f36; opacity: 1; }
.terminal-command-toolbar :deep(.terminal-command-toggle.active),
.terminal-command-toolbar :deep(.terminal-command-toggle.active:hover),
.terminal-command-toolbar :deep(.terminal-command-toggle.active:focus) { border-color: #74aaa6; background: #31504f; color: #efffff; }
.terminal-command-toolbar :deep(.terminal-command-toggle.active:active) { border-color: #9bc7c4; background: #3b625f; color: #ffffff; }
.terminal-quick-snippets { min-width: 0; flex: 1; display: flex; gap: 6px; overflow-x: auto; overscroll-behavior-x: contain; scrollbar-width: none; }
.terminal-quick-snippets::-webkit-scrollbar { display: none; }
.terminal-quick-snippets > span { align-self: center; color: #83939e; font-size: 12px; }
.terminal-quick-snippets :deep(.terminal-snippet-chip) { flex: 0 0 auto; max-width: 180px; }
.terminal-quick-snippets :deep(.terminal-snippet-chip .n-button__content) { min-width: 0; gap: 4px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.terminal-quick-snippets :deep(.terminal-snippet-chip--run:not(:hover):not(:focus):not(:active)) { border-color: #6c6045; color: #ecd08e; }
.terminal-command-composer { padding: 0 9px 9px; }
.terminal-command-composer :deep(.n-input),
.terminal-command-composer :deep(.n-button),
.terminal-command-composer :deep(.n-base-selection) { min-height: 32px; }
@media (max-width: 760px) {
  .terminal-command-toolbar { padding: 6px; flex-wrap: wrap; gap: 5px; }
  .terminal-command-toolbar :deep(.n-button) { min-height: 32px; }
  .terminal-quick-snippets { order: 3; flex-basis: 100%; padding-top: 1px; }
  .terminal-command-composer { padding-bottom: max(9px, env(safe-area-inset-bottom)); }
}
</style>
