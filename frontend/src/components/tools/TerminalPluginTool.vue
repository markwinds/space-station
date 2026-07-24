<template>
  <section class="terminal-plugin-tool">
    <header class="terminal-plugin-toolbar">
      <div>
        <strong>{{ plugins.length }} 个插件</strong>
        <span>{{ directory || "正在读取插件目录…" }}</span>
      </div>
      <div class="terminal-plugin-actions">
        <n-button :loading="loading" @click="refresh">刷新</n-button>
        <n-button :loading="reloading" @click="reload">重新加载</n-button>
        <n-button type="primary" @click="openCreate">新建插件</n-button>
      </div>
    </header>

    <n-alert type="info" :show-icon="false">
      插件运行在后端 QuickJS 沙箱中。服务器串口、SSH 和已开启共享通道的浏览器串口均可触发；纯浏览器本地串口不会把数据发送给后端。
    </n-alert>

    <div v-if="loading && plugins.length === 0" class="terminal-plugin-state"><n-spin size="large" /></div>
    <n-empty v-else-if="plugins.length === 0" class="terminal-plugin-state" description="还没有终端插件">
      <template #extra><n-button type="primary" @click="openCreate">创建第一个插件</n-button></template>
    </n-empty>
    <div v-else class="terminal-plugin-grid">
      <article v-for="plugin in plugins" :key="plugin.directory" class="terminal-plugin-card">
        <div class="terminal-plugin-card-heading">
          <div>
            <strong>{{ plugin.name || plugin.id || plugin.directory }}</strong>
            <code>{{ plugin.id || plugin.directory }}</code>
          </div>
          <n-tag v-if="plugin.error" type="error" size="small">加载失败</n-tag>
          <n-tag v-else-if="!plugin.enabled" size="small">已停用</n-tag>
          <n-tag v-else-if="plugin.loaded" type="success" size="small">运行中</n-tag>
          <n-tag v-else type="warning" size="small">未加载</n-tag>
        </div>
        <p v-if="plugin.error" class="terminal-plugin-error">{{ plugin.error }}</p>
        <div class="terminal-plugin-meta">
          <span v-for="transport in plugin.transports || []" :key="transport">{{ transportLabel(transport) }}</span>
          <span v-if="!(plugin.transports || []).length">清单未生效</span>
        </div>
        <small>目标：{{ (plugin.targets || ["*"]).join(", ") }}</small>
        <small>网络：{{ (plugin.allowedHosts || []).join(", ") || "未授权" }}</small>
        <div class="terminal-plugin-card-actions">
          <n-button size="small" @click="openEdit(plugin)">编辑</n-button>
          <n-button size="small" :loading="togglingId === plugin.directory" @click="toggle(plugin)">
            {{ plugin.enabled ? "停用" : "启用" }}
          </n-button>
          <n-popconfirm @positive-click="remove(plugin)">
            <template #trigger><n-button size="small" type="error" secondary>删除</n-button></template>
            确定删除“{{ plugin.name || plugin.id }}”及其源码吗？
          </n-popconfirm>
        </div>
      </article>
    </div>

    <n-modal v-model:show="showEditor" preset="card" :title="editingId ? `编辑插件 · ${editingId}` : '新建终端插件'" :style="dialogStyle">
      <div v-if="editorLoading" class="terminal-plugin-editor-loading"><n-spin /></div>
      <n-form v-else label-placement="top">
        <div class="terminal-plugin-form-grid">
          <n-form-item label="插件 ID">
            <n-input v-model:value="draft.id" :disabled="Boolean(editingId)" placeholder="例如 device-lookup" />
          </n-form-item>
          <n-form-item label="显示名称"><n-input v-model:value="draft.name" placeholder="设备查询插件" /></n-form-item>
          <n-form-item label="运行状态">
            <n-switch v-model:value="draft.enabled"><template #checked>启用</template><template #unchecked>停用</template></n-switch>
          </n-form-item>
        </div>

        <n-form-item label="终端类型">
          <n-select v-model:value="draft.transports" multiple :options="transportOptions" />
        </n-form-item>
        <div class="terminal-plugin-form-grid two">
          <n-form-item label="目标（每行一个，* 表示全部）">
            <n-input v-model:value="draft.targetsText" type="textarea" :rows="3" placeholder="*" />
          </n-form-item>
          <n-form-item label="允许访问的 HTTP 主机（每行一个）">
            <n-input v-model:value="draft.allowedHostsText" type="textarea" :rows="3" placeholder="api.example.com&#10;*.example.org" />
          </n-form-item>
        </div>
        <div class="terminal-plugin-form-grid limits">
          <n-form-item label="内存 MiB"><n-input-number v-model:value="draft.memoryLimitMiB" :min="1" :max="64" /></n-form-item>
          <n-form-item label="执行超时 ms"><n-input-number v-model:value="draft.executionTimeoutMs" :min="5" :max="1000" /></n-form-item>
          <n-form-item label="HTTP 并发"><n-input-number v-model:value="draft.maxConcurrentRequests" :min="1" :max="16" /></n-form-item>
          <n-form-item label="响应上限 KiB"><n-input-number v-model:value="draft.maxResponseKiB" :min="1" :max="4096" /></n-form-item>
        </div>
        <n-form-item label="index.js">
          <n-input v-model:value="draft.source" class="terminal-plugin-source" type="textarea" :rows="22" spellcheck="false" />
        </n-form-item>
      </n-form>
      <template #footer>
        <div class="terminal-plugin-editor-actions">
          <n-button @click="showEditor = false">取消</n-button>
          <n-button type="primary" :loading="saving" :disabled="editorLoading" @click="save">保存并重新加载</n-button>
        </div>
      </template>
    </n-modal>
  </section>
</template>

<script setup lang="ts">
import axios from "axios";
import {
  NAlert,
  NButton,
  NEmpty,
  NForm,
  NFormItem,
  NInput,
  NInputNumber,
  NModal,
  NPopconfirm,
  NSelect,
  NSpin,
  NSwitch,
  NTag,
  useMessage,
} from "naive-ui";
import { onMounted, reactive, ref } from "vue";
import {
  deleteTerminalPlugin,
  fetchTerminalPlugin,
  fetchTerminalPlugins,
  reloadTerminalPlugins,
  saveTerminalPlugin,
  type TerminalPluginDetail,
  type TerminalPluginManifest,
  type TerminalPluginStatus,
} from "@/api";

type Transport = "serial" | "ssh" | "browser-serial";

const defaultSource = `const sessions = new Map();
const prefix = "@@PING";
const suffix = "@@";

async function onTerminalData(event) {
  const state = sessions.get(event.sessionId) || { buffered: "", seenIds: [] };
  state.buffered += event.data;
  while (true) {
    const start = state.buffered.indexOf(prefix);
    if (start < 0) {
      state.buffered = state.buffered.slice(-prefix.length);
      break;
    }
    const end = state.buffered.indexOf(suffix, start + prefix.length);
    if (end < 0) {
      state.buffered = state.buffered.slice(start);
      break;
    }
    const frame = state.buffered.slice(start, end + suffix.length);
    state.buffered = state.buffered.slice(end + suffix.length);
    const requestId = frame === "@@PING@@" ? "legacy" : frame.startsWith("@@PING:") ? frame.slice(7, -2) : "";
    if (!requestId || !/^[A-Za-z0-9._-]{1,64}$/.test(requestId)) continue;

    // 推荐发送 @@PING:<请求ID>@@；同一 ID 被终端或 Shell 回显时不会重复执行。
    if (state.seenIds.includes(requestId)) continue;
    state.seenIds.push(requestId);
    if (state.seenIds.length > 128) state.seenIds.shift();
    sessions.set(event.sessionId, state);

    // const response = await space.http.request({ url: "https://api.example.com/query" });
    space.terminal.write("PONG\\r\\n");
  }
  sessions.set(event.sessionId, state);
}

function onTerminalClose(event) {
  sessions.delete(event.sessionId);
}`;

const transportOptions = [
  { label: "服务器串口", value: "serial" },
  { label: "SSH", value: "ssh" },
  { label: "浏览器共享串口", value: "browser-serial" },
];
const dialogStyle = { width: "min(1040px, calc(100vw - 32px))" };
const message = useMessage();
const plugins = ref<TerminalPluginStatus[]>([]);
const directory = ref("");
const loading = ref(false);
const reloading = ref(false);
const saving = ref(false);
const editorLoading = ref(false);
const togglingId = ref("");
const showEditor = ref(false);
const editingId = ref("");
const draft = reactive({
  id: "",
  name: "",
  enabled: false,
  transports: ["serial"] as Transport[],
  targetsText: "*",
  allowedHostsText: "",
  memoryLimitMiB: 8,
  executionTimeoutMs: 50,
  maxConcurrentRequests: 4,
  maxResponseKiB: 1024,
  source: defaultSource,
});

function errorMessage(error: unknown, fallback: string) {
  if (axios.isAxiosError(error)) return String(error.response?.data?.message || error.message || fallback);
  return error instanceof Error ? error.message : fallback;
}

function transportLabel(value: string) {
  return transportOptions.find((item) => item.value === value)?.label || value;
}

function lines(value: string, fallback: string[] = []) {
  const result = value.split(/\r?\n/).map((item) => item.trim()).filter(Boolean);
  return result.length ? Array.from(new Set(result)) : fallback;
}

function resetDraft() {
  Object.assign(draft, {
    id: "",
    name: "",
    enabled: false,
    transports: ["serial"] as Transport[],
    targetsText: "*",
    allowedHostsText: "",
    memoryLimitMiB: 8,
    executionTimeoutMs: 50,
    maxConcurrentRequests: 4,
    maxResponseKiB: 1024,
    source: defaultSource,
  });
}

function applyDetail(detail: TerminalPluginDetail) {
  const manifest = detail.manifest;
  Object.assign(draft, {
    id: manifest.id,
    name: manifest.name || manifest.id,
    enabled: Boolean(manifest.enabled),
    transports: (manifest.transports?.length ? manifest.transports : ["serial"]) as Transport[],
    targetsText: (manifest.targets?.length ? manifest.targets : ["*"]).join("\n"),
    allowedHostsText: (manifest.allowedHosts || []).join("\n"),
    memoryLimitMiB: manifest.memoryLimitMiB || 8,
    executionTimeoutMs: manifest.executionTimeoutMs || 50,
    maxConcurrentRequests: manifest.maxConcurrentRequests || 4,
    maxResponseKiB: Math.max(1, Math.round((manifest.maxResponseBytes || 1048576) / 1024)),
    source: detail.source,
  });
}

function detailFromDraft(enabled = draft.enabled): TerminalPluginDetail {
  const manifest: TerminalPluginManifest = {
    id: draft.id.trim(),
    name: draft.name.trim() || draft.id.trim(),
    enabled,
    entry: "index.js",
    transports: draft.transports.length ? draft.transports : ["serial"],
    targets: lines(draft.targetsText, ["*"]),
    allowedHosts: lines(draft.allowedHostsText),
    memoryLimitMiB: draft.memoryLimitMiB,
    executionTimeoutMs: draft.executionTimeoutMs,
    maxConcurrentRequests: draft.maxConcurrentRequests,
    maxResponseBytes: draft.maxResponseKiB * 1024,
  };
  return { manifest, source: draft.source };
}

async function refresh() {
  loading.value = true;
  try {
    const result = await fetchTerminalPlugins();
    plugins.value = result.plugins;
    directory.value = result.directory;
  } catch (error) {
    message.error(errorMessage(error, "插件列表读取失败"));
  } finally {
    loading.value = false;
  }
}

async function refreshAfterReload() {
  await refresh();
}

async function reload() {
  reloading.value = true;
  try {
    await reloadTerminalPlugins();
    await refreshAfterReload();
    message.success("插件已重新加载");
  } catch (error) {
    message.error(errorMessage(error, "插件重新加载失败"));
  } finally {
    reloading.value = false;
  }
}

function openCreate() {
  editingId.value = "";
  resetDraft();
  showEditor.value = true;
}

async function openEdit(plugin: TerminalPluginStatus) {
  editingId.value = plugin.directory;
  resetDraft();
  showEditor.value = true;
  editorLoading.value = true;
  try {
    applyDetail(await fetchTerminalPlugin(plugin.directory));
    draft.id = plugin.directory;
  } catch (error) {
    message.error(errorMessage(error, "插件读取失败"));
    showEditor.value = false;
  } finally {
    editorLoading.value = false;
  }
}

async function save() {
  const pluginId = draft.id.trim();
  if (!/^[A-Za-z0-9._-]{1,80}$/.test(pluginId)) {
    message.warning("插件 ID 只能包含字母、数字、点、下划线和短横线");
    return;
  }
  if (!draft.source.trim()) {
    message.warning("插件源码不能为空");
    return;
  }
  saving.value = true;
  try {
    await saveTerminalPlugin(pluginId, detailFromDraft());
    showEditor.value = false;
    await refreshAfterReload();
    message.success("插件已保存并重新加载");
  } catch (error) {
    message.error(errorMessage(error, "插件保存失败"));
  } finally {
    saving.value = false;
  }
}

async function toggle(plugin: TerminalPluginStatus) {
  togglingId.value = plugin.directory;
  try {
    const detail = await fetchTerminalPlugin(plugin.directory);
    detail.manifest.enabled = !plugin.enabled;
    await saveTerminalPlugin(plugin.directory, detail);
    await refreshAfterReload();
    message.success(plugin.enabled ? "插件已停用" : "插件已启用");
  } catch (error) {
    message.error(errorMessage(error, "插件状态更新失败"));
  } finally {
    togglingId.value = "";
  }
}

async function remove(plugin: TerminalPluginStatus) {
  try {
    await deleteTerminalPlugin(plugin.directory);
    await refreshAfterReload();
    message.success("插件已删除");
  } catch (error) {
    message.error(errorMessage(error, "插件删除失败"));
  }
}

onMounted(refresh);
</script>

<style scoped>
.terminal-plugin-tool { display: grid; gap: 16px; }
.terminal-plugin-toolbar { display: flex; align-items: center; justify-content: space-between; gap: 16px; }
.terminal-plugin-toolbar > div:first-child { min-width: 0; display: grid; gap: 3px; }
.terminal-plugin-toolbar strong { color: #1d303e; font-size: 16px; }
.terminal-plugin-toolbar span { overflow: hidden; color: #6b7c89; font: 12px/1.5 monospace; text-overflow: ellipsis; white-space: nowrap; }
.terminal-plugin-actions, .terminal-plugin-card-actions, .terminal-plugin-editor-actions { display: flex; justify-content: flex-end; gap: 8px; }
.terminal-plugin-state { min-height: 280px; display: grid; place-items: center; }
.terminal-plugin-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 14px; }
.terminal-plugin-card { min-width: 0; padding: 16px; display: grid; gap: 11px; border: 1px solid rgba(51, 72, 91, .14); border-radius: 8px; background: rgba(255, 255, 255, .84); box-shadow: 0 8px 22px rgba(52, 75, 94, .07); }
.terminal-plugin-card-heading { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; }
.terminal-plugin-card-heading > div { min-width: 0; display: grid; gap: 3px; }
.terminal-plugin-card-heading strong { overflow: hidden; color: #1c2f3d; text-overflow: ellipsis; white-space: nowrap; }
.terminal-plugin-card-heading code, .terminal-plugin-card small { overflow-wrap: anywhere; color: #71818d; font-size: 12px; }
.terminal-plugin-error { margin: 0; padding: 8px; border-radius: 5px; background: #fff0ed; color: #a33d32; font: 12px/1.5 monospace; white-space: pre-wrap; }
.terminal-plugin-meta { display: flex; flex-wrap: wrap; gap: 6px; }
.terminal-plugin-meta span { padding: 3px 7px; border-radius: 999px; background: #e3edf1; color: #385666; font-size: 11px; }
.terminal-plugin-card-actions { margin-top: 2px; justify-content: flex-start; }
.terminal-plugin-editor-loading { min-height: 360px; display: grid; place-items: center; }
.terminal-plugin-form-grid { display: grid; grid-template-columns: 1fr 1fr 130px; gap: 12px; }
.terminal-plugin-form-grid.two { grid-template-columns: repeat(2, minmax(0, 1fr)); }
.terminal-plugin-form-grid.limits { grid-template-columns: repeat(4, minmax(120px, 1fr)); }
.terminal-plugin-form-grid :deep(.n-input-number) { width: 100%; }
.terminal-plugin-source :deep(textarea) { font: 12px/1.55 "SFMono-Regular", Consolas, monospace; tab-size: 2; }
@media (max-width: 760px) {
  .terminal-plugin-toolbar { align-items: stretch; flex-direction: column; }
  .terminal-plugin-actions { justify-content: flex-start; flex-wrap: wrap; }
  .terminal-plugin-form-grid, .terminal-plugin-form-grid.two, .terminal-plugin-form-grid.limits { grid-template-columns: 1fr; }
}
</style>
