<template>
  <span class="terminal-renderer" :class="renderer">{{ renderer === 'webgl' ? 'GPU' : 'Canvas' }}</span>
</template>

<script setup lang="ts">
import type { TerminalRenderer } from "./WebTerminal.types";
defineProps<{ renderer: TerminalRenderer }>();
</script>

<style scoped>
.terminal-renderer { padding: 2px 7px; border: 1px solid #465965; border-radius: 999px; color: #aebcc5; background: #263640; font: 10px/1.4 monospace; white-space: nowrap; }
.terminal-renderer.webgl { border-color: #3b7256; color: #76cf96; background: #193124; }
</style>
