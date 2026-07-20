<template>
  <div class="sftp-panel">
    <div class="sftp-toolbar">
      <nav class="sftp-breadcrumb" :title="currentPath" aria-label="当前远程路径">
        <template v-for="(crumb, index) in breadcrumbs" :key="crumb.path">
          <span v-if="index" class="sftp-breadcrumb-separator">/</span>
          <button
            type="button"
            :class="{ current: index === currentBreadcrumbIndex, future: index > currentBreadcrumbIndex }"
            :disabled="loading || index === currentBreadcrumbIndex"
            @click="navigate(crumb.path)"
          >{{ crumb.label }}</button>
        </template>
      </nav>
      <n-button quaternary size="small" :loading="loading" @click="refresh">刷新</n-button>
      <n-button size="small" :disabled="!canAuthenticate || busy" @click="openCreateFolder">新建文件夹</n-button>
      <n-button type="primary" size="small" :disabled="!canAuthenticate || busy || activeUploads.length > 0" @click="chooseFiles">上传</n-button>
      <input ref="fileInput" class="sftp-file-input" type="file" multiple @change="uploadSelectedFiles" />
    </div>

    <n-alert v-if="!canAuthenticate" class="sftp-alert" type="warning" :show-icon="true">
      SFTP 需要已保存的加密凭据。请重新连接此主机，并勾选“加密保存凭据”。
      <n-button text type="warning" @click="$emit('request-credentials')">现在设置</n-button>
    </n-alert>

    <div v-if="activeUploads.length" class="sftp-uploads">
      <div v-for="upload in activeUploads" :key="upload.path" class="sftp-upload-progress">
        <div class="sftp-download-summary">
          <strong>正在上传：{{ upload.name }}</strong>
          <span class="sftp-download-size">{{ formatSize(upload.loaded) }} / {{ formatSize(upload.total) }}</span>
          <span class="sftp-upload-percent">{{ transferPercentage(upload) }}%</span>
        </div>
        <n-progress
          type="line"
          :percentage="transferPercentage(upload)"
          :processing="transferPercentage(upload) < 100"
          :show-indicator="false"
          :height="10"
          :border-radius="5"
        />
      </div>
    </div>

    <div v-if="activeDownloads.length" class="sftp-downloads">
      <div v-for="download in activeDownloads" :key="download.path" class="sftp-download-progress">
        <div class="sftp-download-summary">
          <strong>正在下载：{{ download.name }}</strong>
          <span class="sftp-download-size">{{ formatSize(download.loaded) }} / {{ download.total > 0 ? formatSize(download.total) : '未知大小' }}</span>
          <span class="sftp-download-percent">{{ downloadPercentage(download) }}%</span>
        </div>
        <n-progress
          type="line"
          :percentage="downloadPercentage(download)"
          :processing="downloadPercentage(download) < 100"
          :show-indicator="false"
          :height="10"
          :border-radius="5"
        />
      </div>
    </div>

    <div class="sftp-list-header">
      <span>名称</span><span>大小</span><span>修改时间</span><span>操作</span>
    </div>
    <div class="sftp-list" @dblclick="handleListDoubleClick">
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
        @dblclick.stop="item.type === 'directory' && navigate(item.path)"
      >
        <button class="sftp-name" type="button" @click="item.type === 'directory' && navigate(item.path)">
          <span class="sftp-kind">{{ item.type === 'directory' ? '▸' : item.type === 'symlink' ? '↗' : '·' }}</span>
          <span>{{ item.name }}</span>
        </button>
        <span class="sftp-meta">{{ item.type === 'directory' ? '—' : formatSize(item.size) }}</span>
        <span class="sftp-meta">{{ formatTime(item.modifiedAt) }}</span>
        <span class="sftp-actions">
          <button
            v-if="item.type !== 'directory'"
            class="sftp-action"
            :class="{ 'sftp-action--downloading': downloadStateFor(item.path) }"
            type="button"
            :disabled="Boolean(downloadStateFor(item.path))"
            @click="downloadItem(item)"
          >{{ downloadStateFor(item.path) ? `${downloadPercentage(downloadStateFor(item.path)!)}%` : '下载' }}</button>
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
import { NAlert, NButton, NFormItem, NInput, NModal, NProgress, useMessage } from "naive-ui";
import { computed, onMounted, reactive, ref, watch } from "vue";
import {
  createSftpFolder,
  deleteSftpItem,
  downloadSftpFile,
  listSftp,
  renameSftpItem,
  uploadSftpFile,
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
type UploadState = { path: string; name: string; loaded: number; total: number };
type DownloadState = { path: string; name: string; loaded: number; total: number };
const maximumConcurrentUploads = 2;
const maximumConcurrentDownloads = 4;
const uploadStates = reactive(new Map<string, UploadState>());
const downloadStates = reactive(new Map<string, DownloadState>());
const activeUploads = computed(() => Array.from(uploadStates.values()));
const activeDownloads = computed(() => Array.from(downloadStates.values()));
const breadcrumbTrail = ref<Array<{ label: string; path: string }>>([{ label: "/", path: "/" }]);
function downloadPercentage(state: DownloadState) {
  if (state.total <= 0) return 0;
  return Math.min(100, Math.round((state.loaded / state.total) * 100));
}

function transferPercentage(state: UploadState) {
  if (state.total <= 0) return 100;
  return Math.min(100, Math.round((state.loaded / state.total) * 100));
}

function downloadStateFor(path: string) {
  return downloadStates.get(path);
}
const breadcrumbs = computed(() => breadcrumbTrail.value);
const currentBreadcrumbIndex = computed(() => {
  const index = breadcrumbTrail.value.findIndex((crumb) => crumb.path === currentPath.value);
  return index < 0 ? breadcrumbTrail.value.length - 1 : index;
});

function breadcrumbsForPath(pathValue: string) {
  const parts = pathValue.split("/").filter(Boolean);
  const result = [{ label: "/", path: "/" }];
  let path = "";
  for (const part of parts) {
    path += `/${part}`;
    result.push({ label: part, path });
  }
  return result;
}

function rememberPath(path: string) {
  if (!breadcrumbTrail.value.some((crumb) => crumb.path === path)) {
    breadcrumbTrail.value = breadcrumbsForPath(path);
  }
  currentPath.value = path;
}

onMounted(() => {
  if (canAuthenticate.value) void refresh();
});
watch(() => props.host.id, () => {
  currentPath.value = "/";
  breadcrumbTrail.value = [{ label: "/", path: "/" }];
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
    rememberPath(result.path);
    parentPath.value = result.parentPath;
    items.value = result.items;
  } catch (error) {
    errorMessage.value = errorText(error, "远程目录读取失败");
  } finally {
    loading.value = false;
  }
}

async function navigate(path: string) {
  rememberPath(path);
  await refresh();
}

function handleListDoubleClick(event: MouseEvent) {
  const target = event.target as HTMLElement;
  const clickedBackground = target === event.currentTarget ||
    (target.classList.contains("sftp-empty") && !target.classList.contains("sftp-empty--error"));
  if (!clickedBackground || currentPath.value === "/" || loading.value || busy.value) return;
  void navigate(parentPath.value);
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
  const selectedFiles = Array.from(input.files);
  input.value = "";
  const targetDirectory = currentPath.value;
  const accepted: Array<{ file: File; path: string }> = [];
  for (const file of selectedFiles) {
    if (file.size > 2 * 1024 * 1024 * 1024) {
      message.error(`“${file.name}”超过 2 GiB 上传限制`);
      continue;
    }
    const destination = joinPath(targetDirectory, file.name);
    if (uploadStates.has(destination)) {
      message.warning(`“${file.name}”正在上传中`);
      continue;
    }
    uploadStates.set(destination, { path: destination, name: file.name, loaded: 0, total: file.size });
    accepted.push({ file, path: destination });
  }
  let nextUpload = 0;
  const workers = Array.from(
    { length: Math.min(maximumConcurrentUploads, accepted.length) },
    async () => {
      while (nextUpload < accepted.length) {
        const { file, path } = accepted[nextUpload++];
        await uploadOneFile(file, path, targetDirectory);
      }
    },
  );
  await Promise.all(workers);
  if (currentPath.value === targetDirectory) await refresh();
}

async function uploadOneFile(file: File, destination: string, targetDirectory: string) {
  try {
    await uploadSftpFile(props.host.id, targetDirectory, file, (loaded) => {
      const state = uploadStates.get(destination);
      if (state) state.loaded = loaded;
    });
    message.success(`已上传 ${file.name}`);
  } catch (error) {
    message.error(`${file.name}：${errorText(error, "上传失败")}`);
  } finally {
    uploadStates.delete(destination);
  }
}

async function downloadItem(item: SftpItem) {
  if (downloadStates.has(item.path)) return;
  if (downloadStates.size >= maximumConcurrentDownloads) {
    message.warning(`最多同时下载 ${maximumConcurrentDownloads} 个文件`);
    return;
  }
  const state: DownloadState = { path: item.path, name: item.name, loaded: 0, total: item.type === "file" ? item.size : 0 };
  downloadStates.set(item.path, state);
  try {
    const blob = await downloadSftpFile(props.host.id, item.path, (loaded) => {
      const current = downloadStates.get(item.path);
      if (current) current.loaded = loaded;
    });
    if (item.type === "file" && item.size > 0 && blob.size !== item.size) {
      throw new Error(`下载不完整：应为 ${formatSize(item.size)}，实际收到 ${formatSize(blob.size)}`);
    }
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = item.name;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 30000);
    message.success(`已下载 ${item.name}`);
  } catch (error) {
    message.error(errorText(error, "下载失败"));
  } finally {
    downloadStates.delete(item.path);
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
  if (axios.isAxiosError(error)) {
    if (error.response?.status === 413) return "上传分片过大，请刷新页面后重试";
    return String(error.response?.data?.message || error.message || fallback);
  }
  return error instanceof Error ? error.message : fallback;
}
</script>

<style scoped>
.sftp-panel { min-width: 0; min-height: 0; display: flex; flex-direction: column; background: #101418; }
.sftp-toolbar { min-width: 0; padding: 8px 12px; display: flex; align-items: center; gap: 7px; border-bottom: 1px solid #34414b; background: #1b2329; }
.sftp-toolbar :deep(.n-button) { color: #d3dce2; }
.sftp-toolbar :deep(.n-button:not(.n-button--disabled):hover) { color: #9fd0cc; }
.sftp-breadcrumb { min-width: 80px; flex: 1; padding: 4px 8px; display: flex; align-items: center; gap: 5px; overflow-x: auto; border: 1px solid #303b45; border-radius: 5px; color: #b7c2ca; font: 12px/1.4 monospace; scrollbar-width: thin; scrollbar-color: #52636f transparent; }
.sftp-breadcrumb button { padding: 1px 3px; flex: 0 0 auto; border: 0; border-radius: 3px; background: transparent; color: #91bbb8; font: inherit; cursor: pointer; }
.sftp-breadcrumb button:hover:not(:disabled) { background: #2b373f; color: #c3e0de; }
.sftp-breadcrumb button.current { background: #3a4a54; box-shadow: inset 0 0 0 1px #668079; color: #f0f5f6; cursor: default; }
.sftp-breadcrumb button.future { color: #667781; }
.sftp-breadcrumb button.future:hover:not(:disabled) { color: #b7d5d2; }
.sftp-breadcrumb-separator { color: #5f6e79; }
.sftp-file-input { display: none; }
.sftp-alert { margin: 12px; }
.sftp-uploads { max-height: 210px; overflow-y: auto; border-bottom: 1px solid #41515b; background: #20272f; }
.sftp-upload-progress { padding: 11px 14px 12px; border-bottom: 1px solid #34434c; }
.sftp-upload-progress:last-child { border-bottom: 0; }
.sftp-upload-percent { min-width: 52px; padding: 3px 8px; border: 1px solid #8fb7d4; border-radius: 999px; background: #9bc5e2; color: #101719; font-weight: 800; text-align: center; font-variant-numeric: tabular-nums; }
.sftp-upload-progress :deep(.n-progress-graph-line-rail) { background: #0d1215; }
.sftp-upload-progress :deep(.n-progress-graph-line-fill) { background: #9bc5e2; box-shadow: 0 0 8px rgb(155 197 226 / 45%); }
.sftp-downloads { max-height: 210px; overflow-y: auto; border-bottom: 1px solid #41515b; background: #202a31; }
.sftp-download-progress { padding: 11px 14px 12px; border-bottom: 1px solid #34434c; }
.sftp-download-progress:last-child { border-bottom: 0; }
.sftp-download-summary { margin-bottom: 9px; display: grid; grid-template-columns: minmax(0, 1fr) auto auto; align-items: center; gap: 14px; font-size: 13px; }
.sftp-download-summary strong { min-width: 0; overflow: hidden; color: #f0f5f6; font-weight: 650; text-overflow: ellipsis; white-space: nowrap; }
.sftp-download-size { color: #d2dde2; font-variant-numeric: tabular-nums; white-space: nowrap; }
.sftp-download-percent { min-width: 52px; padding: 3px 8px; border: 1px solid #91c5c1; border-radius: 999px; background: #9fd0cc; color: #101719; font-weight: 800; text-align: center; font-variant-numeric: tabular-nums; }
.sftp-download-progress :deep(.n-progress-graph-line-rail) { background: #0d1215; }
.sftp-download-progress :deep(.n-progress-graph-line-fill) { background: #9fd0cc; box-shadow: 0 0 8px rgb(159 208 204 / 45%); }
.sftp-list-header, .sftp-row { display: grid; grid-template-columns: minmax(220px, 1fr) 110px 170px 190px; align-items: center; }
.sftp-list-header { min-width: 690px; padding: 8px 14px; border-bottom: 1px solid #27313a; color: #71808b; font-size: 11px; }
.sftp-list { min-width: 0; min-height: 0; flex: 1; overflow: auto; }
.sftp-row { min-width: 690px; min-height: 42px; padding: 0 14px; border-bottom: 1px solid #20282f; }
.sftp-row:hover { background: #171e24; }
.sftp-name { min-width: 0; padding: 0; display: flex; align-items: center; gap: 8px; overflow: hidden; border: 0; background: transparent; color: #d5dce1; text-align: left; }
.sftp-name span:last-child { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.sftp-row--directory .sftp-name { color: #8eb9b6; cursor: pointer; }
.sftp-kind { width: 14px; flex: 0 0 auto; color: #718f9c; font-family: monospace; }
.sftp-meta { color: #7f8c96; font-size: 12px; }
.sftp-actions { display: flex; gap: 12px; }
.sftp-action { padding: 0; border: 0; background: transparent; color: #86aaa8; font-size: 12px; text-decoration: none; cursor: pointer; }
.sftp-action:disabled { color: #52616b; cursor: not-allowed; }
.sftp-action--downloading { color: #b9e3df; font-weight: 750; font-variant-numeric: tabular-nums; }
.sftp-action--danger { color: #c97b81; }
.sftp-empty { min-height: 220px; display: grid; place-content: center; justify-items: center; gap: 12px; color: #71808b; }
.sftp-empty--error { color: #c77a80; }
.sftp-dialog-actions { display: flex; justify-content: flex-end; gap: 9px; }
:global(.sftp-dialog) { width: min(440px, calc(100vw - 32px)); }
@media (max-width: 720px) {
  .sftp-toolbar { padding: 7px; flex-wrap: wrap; }
  .sftp-breadcrumb { order: -1; flex-basis: 100%; }
  .sftp-download-summary { grid-template-columns: minmax(0, 1fr) auto; gap: 7px 10px; }
  .sftp-download-size { grid-column: 1 / -1; grid-row: 2; }
  .sftp-download-percent { grid-column: 2; grid-row: 1; }
  .sftp-alert { margin: 8px; }
  :global(.sftp-dialog) { width: 100vw; max-width: 100vw; margin: 0; border-radius: 0; }
}
</style>
