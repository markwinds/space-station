<template>
  <div class="file-share-layout">
    <n-card class="file-share-panel" title="共享目录" embedded>
      <n-space vertical :size="14">
        <div class="share-list">
          <button
            v-for="share in shares"
            :key="share.id"
            :class="['share-row', { active: share.id === selectedShareId, missing: share.exists === false }]"
            type="button"
            @click="selectShare(share.id)"
          >
            <span>
              <strong>{{ share.name }}</strong>
              <small>{{ share.path }}</small>
            </span>
            <n-tag size="small" :type="share.exists === false ? 'warning' : 'success'" :bordered="false">
              {{ share.exists === false ? "未创建" : "可用" }}
            </n-tag>
          </button>
        </div>

        <n-form label-placement="top">
          <n-form-item label="名称">
            <n-input v-model:value="draftShareName" placeholder="例如：项目文件" />
          </n-form-item>
          <n-form-item label="本机目录路径">
            <n-input v-model:value="draftSharePath" placeholder="/Users/mark/Downloads" />
          </n-form-item>
          <div class="file-share-actions">
            <n-button type="primary" :disabled="!canAddShare" @click="addShare">添加映射</n-button>
            <n-button tertiary :disabled="!selectedShareId" @click="removeSelectedShare">移除</n-button>
            <n-button tertiary :loading="savingShares" @click="saveShareConfig">保存配置</n-button>
          </div>
        </n-form>
      </n-space>
    </n-card>

    <n-card class="file-browser-panel" embedded>
      <template #header>
        <div class="file-browser-header">
          <div>
            <p class="file-browser-eyebrow">{{ selectedShare?.name || "未选择" }}</p>
            <h3>{{ currentPath || "根目录" }}</h3>
          </div>
          <div class="file-browser-actions">
            <n-button tertiary :disabled="!selectedShareId || !currentPath" @click="goParent">上级</n-button>
            <n-button tertiary :disabled="!selectedShareId" @click="refreshList">刷新</n-button>
            <n-button type="primary" :disabled="!selectedShareId || busy" @click="triggerUpload">上传</n-button>
          </div>
        </div>
      </template>

      <input ref="fileInputRef" class="hidden-file-input" type="file" multiple @change="handleFileChange" />

      <n-space vertical :size="14">
        <div class="path-bar">
          <button type="button" @click="goToPath('')">根目录</button>
          <template v-for="crumb in breadcrumbs" :key="crumb.path">
            <span>/</span>
            <button type="button" @click="goToPath(crumb.path)">{{ crumb.name }}</button>
          </template>
        </div>

        <n-input-group>
          <n-input v-model:value="newFolderName" placeholder="新文件夹名称" @keydown.enter.prevent="createFolder" />
          <n-button type="primary" :disabled="!selectedShareId || !newFolderName.trim()" @click="createFolder">
            新建
          </n-button>
        </n-input-group>

        <n-alert v-if="errorMessage" type="error" :show-icon="false">{{ errorMessage }}</n-alert>

        <div class="file-list" :aria-busy="loading">
          <div v-if="loading" class="empty-file-state">加载中...</div>
          <template v-else-if="items.length">
            <div v-for="item in items" :key="item.path" class="file-row">
              <button class="file-main" type="button" @click="item.type === 'directory' ? openDirectory(item.path) : undefined">
                <span class="file-icon">{{ item.type === "directory" ? "DIR" : "FILE" }}</span>
                <span>
                  <strong>{{ item.name }}</strong>
                  <small>{{ item.type === "directory" ? "文件夹" : formatFileSize(item.size) }}</small>
                </span>
              </button>
              <div class="file-row-actions">
                <a
                  v-if="item.type === 'file'"
                  class="download-link"
                  :href="fileShareDownloadUrl(selectedShareId, item.path)"
                >
                  下载
                </a>
                <n-popconfirm @positive-click="deleteItem(item)">
                  <template #trigger>
                    <n-button size="small" tertiary type="error">删除</n-button>
                  </template>
                  删除 {{ item.name }}？
                </n-popconfirm>
              </div>
            </div>
          </template>
          <div v-else class="empty-file-state">目录为空</div>
        </div>
      </n-space>
    </n-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import {
  NAlert,
  NButton,
  NCard,
  NForm,
  NFormItem,
  NInput,
  NInputGroup,
  NPopconfirm,
  NSpace,
  NTag,
  useMessage,
} from "naive-ui";
import {
  createFileShareFolder,
  deleteFileShareItem,
  fetchFileShareState,
  fileShareDownloadUrl,
  listFileShare,
  saveFileShares,
  uploadFileShareFiles,
  type FileShare,
  type FileShareItem,
} from "@/api";

const message = useMessage();
const shares = ref<FileShare[]>([]);
const selectedShareId = ref("");
const currentPath = ref("");
const items = ref<FileShareItem[]>([]);
const draftShareName = ref("");
const draftSharePath = ref("");
const newFolderName = ref("");
const loading = ref(false);
const busy = ref(false);
const savingShares = ref(false);
const errorMessage = ref("");
const fileInputRef = ref<HTMLInputElement | null>(null);

const selectedShare = computed(() => shares.value.find((share) => share.id === selectedShareId.value) ?? null);
const canAddShare = computed(() => Boolean(draftShareName.value.trim() && draftSharePath.value.trim()));
const breadcrumbs = computed(() => {
  const parts = currentPath.value.split("/").filter(Boolean);
  return parts.map((name, index) => ({
    name,
    path: parts.slice(0, index + 1).join("/"),
  }));
});

onMounted(async () => {
  try {
    await refreshShares();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "读取共享目录失败。";
  }
  if (shares.value.length) {
    selectedShareId.value = shares.value[0].id;
    await refreshList();
  }
});

async function refreshShares() {
  const state = await fetchFileShareState();
  shares.value = state.shares;
}

async function saveShareConfig() {
  savingShares.value = true;
  errorMessage.value = "";
  try {
    await saveFileShares(shares.value);
    await refreshShares();
    message.success("共享目录已保存。");
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "保存共享目录失败。";
  } finally {
    savingShares.value = false;
  }
}

function addShare() {
  if (!canAddShare.value) {
    return;
  }
  shares.value.push({
    id: crypto.randomUUID(),
    name: draftShareName.value.trim(),
    path: draftSharePath.value.trim(),
  });
  selectedShareId.value = shares.value[shares.value.length - 1].id;
  draftShareName.value = "";
  draftSharePath.value = "";
}

function removeSelectedShare() {
  if (!selectedShareId.value) {
    return;
  }
  shares.value = shares.value.filter((share) => share.id !== selectedShareId.value);
  selectedShareId.value = shares.value[0]?.id ?? "";
  currentPath.value = "";
  items.value = [];
  if (selectedShareId.value) {
    void refreshList();
  }
}

async function selectShare(id: string) {
  selectedShareId.value = id;
  currentPath.value = "";
  await refreshList();
}

async function refreshList() {
  if (!selectedShareId.value) {
    return;
  }
  loading.value = true;
  errorMessage.value = "";
  try {
    const result = await listFileShare(selectedShareId.value, currentPath.value);
    currentPath.value = result.path;
    items.value = result.items;
  } catch (error) {
    items.value = [];
    errorMessage.value = error instanceof Error ? error.message : "读取目录失败。";
  } finally {
    loading.value = false;
  }
}

async function openDirectory(path: string) {
  currentPath.value = path;
  await refreshList();
}

async function goToPath(path: string) {
  currentPath.value = path;
  await refreshList();
}

async function goParent() {
  const parts = currentPath.value.split("/").filter(Boolean);
  parts.pop();
  currentPath.value = parts.join("/");
  await refreshList();
}

function triggerUpload() {
  fileInputRef.value?.click();
}

async function handleFileChange(event: Event) {
  const target = event.target as HTMLInputElement;
  if (!selectedShareId.value || !target.files?.length) {
    return;
  }
  busy.value = true;
  errorMessage.value = "";
  try {
    await uploadFileShareFiles(selectedShareId.value, currentPath.value, target.files);
    target.value = "";
    message.success("文件已上传。");
    await refreshList();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "上传文件失败。";
  } finally {
    busy.value = false;
  }
}

async function createFolder() {
  const name = newFolderName.value.trim();
  if (!selectedShareId.value || !name) {
    return;
  }
  busy.value = true;
  errorMessage.value = "";
  try {
    await createFileShareFolder({ shareId: selectedShareId.value, path: currentPath.value, name });
    newFolderName.value = "";
    await refreshList();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "新建文件夹失败。";
  } finally {
    busy.value = false;
  }
}

async function deleteItem(item: FileShareItem) {
  if (!selectedShareId.value) {
    return;
  }
  busy.value = true;
  errorMessage.value = "";
  try {
    await deleteFileShareItem({ shareId: selectedShareId.value, path: item.path });
    await refreshList();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "删除失败。";
  } finally {
    busy.value = false;
  }
}

function formatFileSize(size: number) {
  if (size < 1024) {
    return `${size} B`;
  }
  if (size < 1024 * 1024) {
    return `${(size / 1024).toFixed(1)} KB`;
  }
  if (size < 1024 * 1024 * 1024) {
    return `${(size / 1024 / 1024).toFixed(1)} MB`;
  }
  return `${(size / 1024 / 1024 / 1024).toFixed(1)} GB`;
}
</script>

<style scoped>
.file-share-layout {
  display: grid;
  grid-template-columns: minmax(280px, 360px) minmax(0, 1fr);
  gap: 16px;
  align-items: start;
}

.file-share-panel,
.file-browser-panel {
  min-width: 0;
  border-radius: 8px;
}

.share-list {
  display: grid;
  gap: 10px;
}

.share-row,
.file-main,
.path-bar button {
  border: 0;
  font: inherit;
  text-align: left;
  cursor: pointer;
}

.share-row {
  width: 100%;
  min-width: 0;
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: center;
  gap: 10px;
  padding: 12px;
  border: 1px solid rgba(65, 88, 108, 0.14);
  border-radius: 8px;
  background: #ffffff;
  color: #21313d;
}

.share-row.active {
  border-color: #176b96;
  background: #eef8fd;
}

.share-row.missing {
  background: #fff8e8;
}

.share-row span,
.file-main span {
  min-width: 0;
  display: grid;
  gap: 4px;
}

.share-row strong,
.file-main strong {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.share-row small,
.file-main small {
  overflow: hidden;
  color: #687886;
  font-size: 12px;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.file-share-actions,
.file-browser-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.file-browser-header {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 14px;
}

.file-browser-header h3,
.file-browser-eyebrow {
  margin: 0;
}

.file-browser-header h3 {
  max-width: 48vw;
  overflow: hidden;
  font-size: 20px;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.file-browser-eyebrow {
  color: #657482;
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0;
  text-transform: uppercase;
}

.path-bar {
  min-width: 0;
  display: flex;
  align-items: center;
  gap: 6px;
  overflow-x: auto;
  color: #60707e;
  white-space: nowrap;
}

.path-bar button {
  padding: 4px 0;
  background: transparent;
  color: #176b96;
  font-weight: 700;
}

.file-list {
  min-height: 360px;
  display: grid;
  align-content: start;
  gap: 8px;
}

.file-row {
  min-width: 0;
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: center;
  gap: 12px;
  padding: 12px;
  border: 1px solid rgba(65, 88, 108, 0.12);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.86);
}

.file-main {
  min-width: 0;
  display: grid;
  grid-template-columns: 44px minmax(0, 1fr);
  align-items: center;
  gap: 10px;
  padding: 0;
  background: transparent;
  color: #21313d;
}

.file-icon {
  width: 44px;
  height: 34px;
  display: grid;
  place-items: center;
  border-radius: 8px;
  background: #e1edf5;
  color: #17445f;
  font-size: 11px;
  font-weight: 800;
}

.file-row-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.download-link {
  color: #176b96;
  font-weight: 700;
  text-decoration: none;
}

.empty-file-state {
  padding: 36px 12px;
  border: 1px dashed rgba(65, 88, 108, 0.2);
  border-radius: 8px;
  color: #687886;
  text-align: center;
}

@media (max-width: 900px) {
  .file-share-layout {
    grid-template-columns: 1fr;
  }

  .file-browser-header {
    align-items: stretch;
    flex-direction: column;
  }

  .file-browser-header h3 {
    max-width: 100%;
  }
}

@media (max-width: 560px) {
  .file-row {
    grid-template-columns: 1fr;
  }

  .file-row-actions {
    justify-content: flex-end;
  }

  .file-browser-actions .n-button,
  .file-share-actions .n-button {
    flex: 1 1 auto;
  }
}
</style>
