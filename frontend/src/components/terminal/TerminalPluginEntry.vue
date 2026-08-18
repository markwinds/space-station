<template>
  <n-popover trigger="click" placement="bottom-end" :show-arrow="false" @update:show="handleShow">
    <template #trigger>
      <n-button class="terminal-plugin-trigger" secondary size="tiny" :loading="loading" aria-label="查看当前终端插件">
        <template #icon><n-icon><ExtensionPuzzleOutline /></n-icon></template>
        插件 <span class="terminal-plugin-count">{{ plugins.length }}</span>
      </n-button>
    </template>
    <section class="terminal-plugin-popover">
      <header>
        <strong>当前生效插件</strong>
        <small v-if="available">{{ transport }} · {{ target }}</small>
      </header>
      <p v-if="!available" class="terminal-plugin-muted">
        {{ unavailableHint || "当前终端没有接入后端插件通道。" }}
      </p>
      <p v-else-if="error" class="terminal-plugin-error">{{ error }}</p>
      <p v-else-if="!loading && plugins.length === 0" class="terminal-plugin-muted">没有匹配且运行中的插件。</p>
      <ul v-else-if="plugins.length">
        <li v-for="plugin in plugins" :key="plugin.id || plugin.directory">
          <span aria-hidden="true" />
          <div>
            <strong>{{ plugin.name || plugin.id || plugin.directory }}</strong>
            <small>{{ plugin.id || plugin.directory }}</small>
          </div>
        </li>
      </ul>
      <router-link class="terminal-plugin-manage" to="/tools/terminal-plugins" target="_blank">
        打开插件管理
      </router-link>
    </section>
  </n-popover>
</template>

<script setup lang="ts">
import { ExtensionPuzzleOutline } from "@vicons/ionicons5";
import { NButton, NIcon, NPopover } from "naive-ui";
import { computed, ref, watch } from "vue";
import { fetchEffectiveTerminalPlugins, type TerminalPluginStatus } from "@/api";

const props = defineProps<{
  transport?: string;
  target?: string;
  unavailableHint?: string;
}>();

const plugins = ref<TerminalPluginStatus[]>([]);
const loading = ref(false);
const error = ref("");
const available = computed(() => Boolean(props.transport && props.target));
let requestSequence = 0;

async function refresh() {
  const sequence = ++requestSequence;
  if (!props.transport || !props.target) {
    plugins.value = [];
    error.value = "";
    loading.value = false;
    return;
  }
  loading.value = true;
  error.value = "";
  try {
    const response = await fetchEffectiveTerminalPlugins(props.transport, props.target);
    if (sequence === requestSequence) plugins.value = response.plugins;
  } catch (caught) {
    if (sequence === requestSequence) {
      plugins.value = [];
      error.value = caught instanceof Error ? caught.message : "插件状态读取失败";
    }
  } finally {
    if (sequence === requestSequence) loading.value = false;
  }
}

function handleShow(show: boolean) {
  if (show) void refresh();
}

watch(() => [props.transport, props.target], () => void refresh(), { immediate: true });
</script>

<style scoped>
.terminal-plugin-trigger { color: #d8e1e6; background: #293842; border-color: #465965; }
.terminal-plugin-trigger:not(.n-button--disabled):hover { color: #102027; background: #8fbfbc; border-color: #8fbfbc; }
.terminal-plugin-trigger:not(.n-button--disabled):focus { color: #ffffff; background: #34434e; border-color: #78909d; }
.terminal-plugin-trigger:not(.n-button--disabled):active { color: #102027; background: #78aaa7; border-color: #9bc7c4; }
.terminal-plugin-trigger:focus-visible { box-shadow: 0 0 0 2px #151a1f, 0 0 0 4px #9bc7c4; }
.terminal-plugin-trigger.n-button--disabled { color: #8999a3; background: #202b32; border-color: #394852; opacity: 1; }
.terminal-plugin-popover { width: min(320px, calc(100vw - 40px)); display: grid; gap: 10px; }
.terminal-plugin-count { min-width: 16px; height: 16px; padding: 0 4px; display: inline-grid; place-items: center; border-radius: 999px; background: rgba(255, 255, 255, .12); font-size: 10px; line-height: 1; }
.terminal-plugin-popover header { min-width: 0; display: grid; gap: 2px; }
.terminal-plugin-popover header > strong { color: #1d303e; font-size: 14px; }
.terminal-plugin-popover header small { overflow: hidden; color: #778792; font: 11px/1.4 monospace; text-overflow: ellipsis; white-space: nowrap; }
.terminal-plugin-popover p { margin: 0; font-size: 12px; line-height: 1.55; }
.terminal-plugin-muted { color: #6c7d88; }
.terminal-plugin-error { color: #b4453c; }
.terminal-plugin-popover ul { max-height: 220px; margin: 0; padding: 0; display: grid; gap: 8px; overflow: auto; list-style: none; }
.terminal-plugin-popover li { display: flex; align-items: center; gap: 9px; }
.terminal-plugin-popover li > span { width: 8px; height: 8px; flex: 0 0 auto; border-radius: 50%; background: #43aa72; box-shadow: 0 0 0 3px rgba(67, 170, 114, .13); }
.terminal-plugin-popover li > div { min-width: 0; display: grid; }
.terminal-plugin-popover li strong, .terminal-plugin-popover li small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.terminal-plugin-popover li strong { color: #243845; font-size: 13px; }
.terminal-plugin-popover li small { color: #82909a; font: 11px/1.4 monospace; }
.terminal-plugin-manage { padding-top: 9px; border-top: 1px solid #e2e8eb; color: #287c78; font-size: 12px; font-weight: 700; text-decoration: none; }
.terminal-plugin-manage:hover { color: #175b58; }
</style>
