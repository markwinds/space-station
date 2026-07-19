<template>
  <div class="sftp-panel">
    <div class="sftp-toolbar">
      <n-button quaternary size="small" :disabled="currentPath === '/' || busy" @click="navigate(parentPath)">上一级</n-button>
      <div class="sftp-path" :title="currentPath">{{ currentPath }}</div>
      <n-button quaternary size="small" :loading="loading" @click="refresh">刷新</n-button>
      <n-button size="small" :disabled="!canAuthenticate || busy" @click="openCreateFolder">新建文件夹</n-button>
      <n-button type="primary" size="small" :disabled="!canAuthenticate || busy" @click="chooseFiles">上传</n-button>
      <input ref="fileInput" class="sftp-file-input" type="file" multiple @change="uploadSelectedFiles" />
    </div>

    <n-alert v-if="!canAuthenticate" class="sftp-alert" type="warning" :show-icon="true">
      SFTP 需要已保存的加密凭据。请重新连接此主机，并勾选“加密保存凭据”。
      <n-button text type="warning" @click="$emit('request-credentials')">现在设置</n-button>
    </n-alert>

    <div class="sftp-list-header">
      <span>名称</span><span>大小</span><span>修改时间</span><span>操作</span>
    </div>
    <div class="sftp-list">
      <div v-if="loading" class="sftp-empty">正在读取远程目录…</div>
      <div v-else-if="errorMessage" class="sftp-empty sftp-empty--error">
        <span>{{ errorMessage }}</span>
        <n-button size="small" @click="refresh">重试</n-button>
      </div>
      <div v-else-if="items.length === 0" class="sftp-empty">此目录为空</div>
      <div
        v-for="item in items"
        v-else
        :key="item.path"
        class="sftp-row"
        :class="{ 'sftp-row--directory': item.type === 'directory' }"
        @dblclick="item.type === 'directory' && navigate(item.path)"
      >
        <button class="sftp-name" type="button" @click="item.type === 'directory' && navigate(item.path)">
          <span class="sftp-kind">{{ item.type === 'directory' ? '▸' : item.type === 'symlink' ? '↗' : '·' }}</span>
          <span>{{ item.name }}</span>
        </button>
        <span class="sftp-meta">{{ item.type === 'directory' ? '—' : formatSize(item.size) }}</span>
        <span class="sftp-meta">{{ formatTime(item.modifiedAt) }}</span>
        <span class="sftp-actions">
          <a v-if="item.type !== 'directory'" class="sftp-action" :href="sftpDownloadUrl(host.id, item.path)">下载</a>
          <button class="sftp-action" type="button" @click="openRename(item)">重命名</button>
          <button class="sftp-action sftp-action--danger" type="button" @click="remove(item)">删除</button>
        </span>
      </div>
    </div>

    <n-modal v-model:show="showNameDialog" preset="card" :title="dialogMode === 'folder' ? '新建文件夹' : '重命名'" class="sftp-dialog">
      <n-form-item label="名称">
        <n-input v-model:value="nameDraft" autofocus @keyup.enter="submitNameDialog" />
      </n-form-item>
      <template #footer>
        <div class="sftp-dialog-actions">
          <n-button @click="showNameDialog = false">取消</n-button>
          <n-button type="primary" :loading="busy" @click="submitNameDialog">确定</n-button>
        </div>
      </template>
    </n-modal>
  </div>
</template>

<script setup lang="ts">
import axios from "axios";
import { NAlert, NButton, NFormItem, NInput, NModal, useMessage } from "naive-ui";
import { computed, onMounted, ref, watch } from "vue";
import {
  createSftpFolder,
  deleteSftpItem,
  listSftp,
  renameSftpItem,
  sftpDownloadUrl,
  uploadSftpFiles,
  type SftpItem,
  type SshHost,
} from "@/api";

const props = defineProps<{ host: SshHost }>();
defineEmits<{ (event: "request-credentials"): void }>();

const message = useMessage();
const canAuthenticate = computed(() => Boolean(props.host.hasCredential || props.host.useAgent));
const items = ref<SftpItem[]>([]);
const currentPath = ref("/");
const parentPath = ref("/");
const loading = ref(false);
const busy = ref(false);
const errorMessage = ref("");
const fileInput = ref<HTMLInputElement | null>(null);
const showNameDialog = ref(false);
const dialogMode = ref<"folder" | "rename">("folder");
const nameDraft = ref("");
const editingItem = ref<SftpItem | null>(null);

onMounted(() => {
  if (canAuthenticate.value) void refresh();
});
watch(() => props.host.id, () => {
  currentPath.value = "/";
  items.value = [];
  errorMessage.value = "";
  if (canAuthenticate.value) void refresh();
});
watch(() => props.host.hasCredential, (hasCredential) => {
  if (hasCredential) void refresh();
});

async function refresh() {
  if (!canAuthenticate.value) return;
  loading.value = true;
  errorMessage.value = "";
  try {
    const result = await listSftp(props.host.id, currentPath.value);
    currentPath.value = result.path;
    parentPath.value = result.parentPath;
    items.value = result.items;
  } catch (error) {
    errorMessage.value = errorText(error, "远程目录读取失败");
  } finally {
    loading.value = false;
  }
}

async function navigate(path: string) {
  currentPath.value = path;
  await refresh();
}

function openCreateFolder() {
  dialogMode.value = "folder";
  editingItem.value = null;
  nameDraft.value = "";
  showNameDialog.value = true;
}

function openRename(item: SftpItem) {
  dialogMode.value = "rename";
  editingItem.value = item;
  nameDraft.value = item.name;
  showNameDialog.value = true;
}

async function submitNameDialog() {
  const name = nameDraft.value.trim();
  if (!name || name.includes("/") || name === "." || name === "..") {
    message.warning("请输入不包含 / 的有效名称");
    return;
  }
  busy.value = true;
  try {
    const destination = joinPath(currentPath.value, name);
    if (dialogMode.value === "folder") await createSftpFolder(props.host.id, destination);
    else if (editingItem.value) await renameSftpItem(props.host.id, editingItem.value.path, destination);
    showNameDialog.value = false;
    await refresh();
  } catch (error) {
    message.error(errorText(error, "操作失败"));
  } finally {
    busy.value = false;
  }
}

async function remove(item: SftpItem) {
  if (!window.confirm(`确定删除“${item.name}”吗？${item.type === "directory" ? "目录必须为空。" : ""}`)) return;
  busy.value = true;
  try {
    await deleteSftpItem(props.host.id, item.path, item.type === "directory");
    await refresh();
  } catch (error) {
    message.error(errorText(error, "删除失败"));
  } finally {
    busy.value = false;
  }
}

function chooseFiles() {
  fileInput.value?.click();
}

async function uploadSelectedFiles(event: Event) {
  const input = event.target as HTMLInputElement;
  if (!input.files?.length) return;
  busy.value = true;
  try {
    await uploadSftpFiles(props.host.id, currentPath.value, input.files);
    message.success(`已上传 ${input.files.length} 个文件`);
    await refresh();
  } catch (error) {
    message.error(errorText(error, "上传失败"));
  } finally {
    input.value = "";
    busy.value = false;
  }
}

function joinPath(parent: string, name: string) {
  return parent === "/" ? `/${name}` : `${parent}/${name}`;
}

function formatSize(size: number) {
  if (size < 1024) return `${size} B`;
  if (size < 1024 ** 2) return `${(size / 1024).toFixed(1)} KiB`;
  if (size < 1024 ** 3) return `${(size / 1024 ** 2).toFixed(1)} MiB`;
  return `${(size / 1024 ** 3).toFixed(1)} GiB`;
}

function formatTime(seconds: number) {
  return seconds ? new Date(seconds * 1000).toLocaleString() : "—";
}

function errorText(error: unknown, fallback: string) {
  if (axios.isAxiosError(error)) return String(error.response?.data?.message || error.message || fallback);
  return error instanceof Error ? error.message : fallback;
}
</script>

<style scoped>
.sftp-panel { min-width: 0; min-height: 0; display: grid; grid-template-rows: auto auto auto minmax(0, 1fr); background: #101418; }
.sftp-toolbar { min-width: 0; padding: 8px 12px; display: flex; align-items: center; gap: 7px; border-bottom: 1px solid #34414b; background: #1b2329; }
.sftp-toolbar :deep(.n-button) { color: #d3dce2; }
.sftp-toolbar :deep(.n-button:not(.n-button--disabled):hover) { color: #9fd0cc; }
.sftp-path { min-width: 80px; flex: 1; padding: 5px 9px; overflow: hidden; border: 1px solid #303b45; border-radius: 5px; color: #b7c2ca; font: 12px/1.4 monospace; text-overflow: ellipsis; white-space: nowrap; }
.sftp-file-input { display: none; }
.sftp-alert { margin: 12px; }
.sftp-list-header, .sftp-row { display: grid; grid-template-columns: minmax(220px, 1fr) 110px 170px 190px; align-items: center; }
.sftp-list-header { min-width: 690px; padding: 8px 14px; border-bottom: 1px solid #27313a; color: #71808b; font-size: 11px; }
.sftp-list { min-width: 0; min-height: 0; overflow: auto; }
.sftp-row { min-width: 690px; min-height: 42px; padding: 0 14px; border-bottom: 1px solid #20282f; }
.sftp-row:hover { background: #171e24; }
.sftp-name { min-width: 0; padding: 0; display: flex; align-items: center; gap: 8px; overflow: hidden; border: 0; background: transparent; color: #d5dce1; text-align: left; }
.sftp-name span:last-child { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.sftp-row--directory .sftp-name { color: #8eb9b6; cursor: pointer; }
.sftp-kind { width: 14px; flex: 0 0 auto; color: #718f9c; font-family: monospace; }
.sftp-meta { color: #7f8c96; font-size: 12px; }
.sftp-actions { display: flex; gap: 12px; }
.sftp-action { padding: 0; border: 0; background: transparent; color: #86aaa8; font-size: 12px; text-decoration: none; cursor: pointer; }
.sftp-action--danger { color: #c97b81; }
.sftp-empty { min-height: 220px; display: grid; place-content: center; justify-items: center; gap: 12px; color: #71808b; }
.sftp-empty--error { color: #c77a80; }
.sftp-dialog-actions { display: flex; justify-content: flex-end; gap: 9px; }
:global(.sftp-dialog) { width: min(440px, calc(100vw - 32px)); }
@media (max-width: 720px) {
  .sftp-toolbar { padding: 7px; flex-wrap: wrap; }
  .sftp-path { order: -1; flex-basis: 100%; }
  .sftp-alert { margin: 8px; }
  :global(.sftp-dialog) { width: 100vw; max-width: 100vw; margin: 0; border-radius: 0; }
}
</style>
