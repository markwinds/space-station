<template>
  <div class="terminal-special-keys" aria-label="终端特殊按键">
    <div class="terminal-special-keys__fixed">
      <button type="button" :disabled="disabled" @pointerdown.prevent @click="$emit('key', 'escape')">Esc</button>
      <button
        type="button"
        :class="{ active: ctrl }"
        :disabled="disabled"
        :aria-pressed="ctrl"
        @pointerdown.prevent
        @click="$emit('modifier', 'ctrl')"
      >Ctrl</button>
      <button
        type="button"
        :class="{ active: alt }"
        :disabled="disabled"
        :aria-pressed="alt"
        @pointerdown.prevent
        @click="$emit('modifier', 'alt')"
      >Alt</button>
      <button type="button" :disabled="disabled" @pointerdown.prevent @click="$emit('key', 'tab')">Tab</button>
    </div>
    <div class="terminal-special-keys__scroll">
      <button type="button" :disabled="disabled" aria-label="左方向键" @pointerdown.prevent @click="$emit('key', 'arrowLeft')">←</button>
      <button type="button" :disabled="disabled" aria-label="下方向键" @pointerdown.prevent @click="$emit('key', 'arrowDown')">↓</button>
      <button type="button" :disabled="disabled" aria-label="上方向键" @pointerdown.prevent @click="$emit('key', 'arrowUp')">↑</button>
      <button type="button" :disabled="disabled" aria-label="右方向键" @pointerdown.prevent @click="$emit('key', 'arrowRight')">→</button>
      <button type="button" :disabled="disabled" @pointerdown.prevent @click="$emit('key', 'home')">Home</button>
      <button type="button" :disabled="disabled" @pointerdown.prevent @click="$emit('key', 'end')">End</button>
      <button type="button" :disabled="disabled" @pointerdown.prevent @click="$emit('key', 'pageUp')">PgUp</button>
      <button type="button" :disabled="disabled" @pointerdown.prevent @click="$emit('key', 'pageDown')">PgDn</button>
      <button type="button" :disabled="disabled" aria-label="竖线" @pointerdown.prevent @click="$emit('key', 'pipe')">|</button>
      <button type="button" :disabled="disabled" aria-label="斜杠" @pointerdown.prevent @click="$emit('key', 'slash')">/</button>
      <button type="button" :disabled="disabled" aria-label="短横线" @pointerdown.prevent @click="$emit('key', 'dash')">-</button>
      <button type="button" :disabled="disabled" aria-label="波浪号" @pointerdown.prevent @click="$emit('key', 'tilde')">~</button>
    </div>
    <button class="terminal-special-keys__keyboard" type="button" @pointerdown.prevent @click="$emit('focus')">键盘</button>
  </div>
</template>

<script setup lang="ts">
type TerminalSpecialKey =
  | "escape"
  | "tab"
  | "arrowLeft"
  | "arrowDown"
  | "arrowUp"
  | "arrowRight"
  | "home"
  | "end"
  | "pageUp"
  | "pageDown"
  | "pipe"
  | "slash"
  | "dash"
  | "tilde";

type TerminalModifier = "ctrl" | "alt";

withDefaults(defineProps<{
  ctrl?: boolean;
  alt?: boolean;
  disabled?: boolean;
}>(), {
  ctrl: false,
  alt: false,
  disabled: false,
});

defineEmits<{
  key: [key: TerminalSpecialKey];
  modifier: [modifier: TerminalModifier];
  focus: [];
}>();
</script>

<style scoped>
.terminal-special-keys { display: none; }

@media (max-width: 760px) {
  .terminal-special-keys {
    min-width: 0;
    padding: 5px max(6px, env(safe-area-inset-right)) 5px max(6px, env(safe-area-inset-left));
    display: grid;
    grid-template-columns: auto minmax(0, 1fr) auto;
    align-items: center;
    gap: 5px;
    border-top: 1px solid #34414b;
    background: #151d23;
  }
  .terminal-special-keys__fixed { display: flex; gap: 4px; }
  .terminal-special-keys__scroll {
    min-width: 0;
    display: flex;
    gap: 4px;
    overflow-x: auto;
    overscroll-behavior-x: contain;
    scrollbar-width: none;
  }
  .terminal-special-keys__scroll::-webkit-scrollbar { display: none; }
  button {
    min-width: 38px;
    height: 36px;
    flex: 0 0 auto;
    padding: 0 8px;
    border: 1px solid #465863;
    border-radius: 6px;
    background: #26343d;
    color: #dbe5ea;
    font: 600 12px/1 "SFMono-Regular", Consolas, monospace;
    touch-action: manipulation;
  }
  button:active, button.active {
    border-color: #8fc0bd;
    background: #3b625f;
    color: #f3ffff;
  }
  button:focus-visible { outline: 2px solid #a9d4d1; outline-offset: 1px; }
  button:disabled { border-color: #394852; background: #202b32; color: #8999a3; opacity: 1; }
  .terminal-special-keys__keyboard { min-width: 48px; color: #9fc9c6; }
}
</style>
