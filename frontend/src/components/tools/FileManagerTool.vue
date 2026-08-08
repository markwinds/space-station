<template>
  <section class="commander" tabindex="0" @keydown="handleKeydown">
    <header class="commander-toolbar">
      <div class="toolbar-brand">
        <router-link class="toolbar-icon-button" to="/" aria-label="返回工具首页">
          <n-icon size="19"><ArrowBackOutline /></n-icon>
        </router-link>
        <div><strong>文件管理器</strong><small>LOCAL COMMANDER</small></div>
      </div>

      <div class="toolbar-commands">
        <button type="button" :disabled="busy" @click="openCreateFolder">
          <n-icon size="18"><FolderOutline /></n-icon><span>新建文件夹</span>
        </button>
        <button type="button" :disabled="busy" @click="chooseFiles">
          <n-icon size="18"><CloudUploadOutline /></n-icon><span>上传</span>
        </button>
        <span class="toolbar-separator" />
        <button type="button" :disabled="!canTransfer" @click="requestTransfer('copy')">
          <n-icon size="18"><CopyOutline /></n-icon><span>复制 {{ transferArrow }}</span>
        </button>
        <button type="button" :disabled="!canTransfer" @click="requestTransfer('move')">
          <n-icon size="18"><SwapHorizontalOutline /></n-icon><span>移动 {{ transferArrow }}</span>
        </button>
        <span class="toolbar-separator" />
        <button type="button" :disabled="activeSelection.length !== 1 || busy" @click="openRename(activeSelection[0])">
          <n-icon size="18"><CreateOutline /></n-icon><span>重命名</span>
        </button>
        <button class="danger-command" type="button" :disabled="!activeSelection.length || busy" @click="removeSelected()">
          <n-icon size="18"><TrashOutline /></n-icon><span>{{ activePane.trashSupported ? '废纸篓' : '删除' }}</span>
        </button>
      </div>

      <div class="toolbar-tail">
        <button class="icon-only" type="button" :class="{ active: activePane.showHidden }" title="显示隐藏文件" @click="activePane.showHidden = !activePane.showHidden">
          <n-icon size="19"><EyeOutline /></n-icon>
        </button>
        <button class="icon-only" type="button" title="收藏当前路径" @click="addFavorite">
          <n-icon size="19"><StarOutline /></n-icon>
        </button>
      </div>
      <input ref="fileInput" class="native-file-input" type="file" multiple @change="handleFileInput" />
    </header>

    <div class="mobile-pane-switcher">
      <button v-for="pane in panes" :key="pane.id" type="button" :class="{ active: pane.id === activePaneId }" @click="activatePane(pane)">
        {{ pane.id === 'left' ? '左侧' : '右侧' }} · {{ shortPath(pane.path) }}
      </button>
    </div>

    <main class="commander-main">
      <aside class="commander-sidebar">
        <div class="sidebar-section">
          <h2>位置</h2>
          <button v-for="location in locations" :key="location.path" type="button" :class="{ active: activePane.path === location.path }" @click="navigate(activePane, location.path)">
            <n-icon size="17"><component :is="location.icon" /></n-icon>
            <span>{{ location.label }}</span>
          </button>
        </div>
        <div class="sidebar-section sidebar-favorites">
          <div class="sidebar-heading"><h2>收藏</h2><button type="button" title="收藏当前路径" @click="addFavorite">＋</button></div>
          <p v-if="!favorites.length">点击上方星标收藏常用路径</p>
          <div v-for="favorite in favorites" :key="favorite" class="favorite-row">
            <button type="button" :class="{ active: activePane.path === favorite }" :title="favorite" @click="navigate(activePane, favorite)">
              <n-icon size="16"><StarOutline /></n-icon><span>{{ shortPath(favorite) }}</span>
            </button>
            <button class="favorite-remove" type="button" title="移除收藏" @click="removeFavorite(favorite)">×</button>
          </div>
        </div>
        <div class="sidebar-shortcuts">
          <span>⌘↑ 上一级</span><span>⌘↓ 打开</span><span>⌘C / ⌘V 复制</span>
        </div>
      </aside>

      <div class="pane-deck">
        <section
          v-for="pane in panes"
          :key="pane.id"
          class="file-pane"
          :class="{ active: pane.id === activePaneId }"
          @mousedown="activatePane(pane)"
          @dragenter.prevent="activatePane(pane); pane.dragging = true"
          @dragover.prevent="pane.dragging = true"
          @dragleave.prevent="handleDragLeave($event, pane)"
          @drop.prevent="handleDrop($event, pane)"
        >
          <div class="pane-tabbar">
            <div class="pane-tab active"><n-icon size="14"><FolderOutline /></n-icon><span>{{ shortPath(pane.path) }}</span><small>{{ visibleItems(pane).length }}</small></div>
            <button type="button" title="新标签页暂未启用">＋</button>
          </div>

          <div class="pane-pathbar">
            <div class="history-controls">
              <button type="button" :disabled="pane.historyIndex <= 0" title="后退" @click.stop="goHistory(pane, -1)"><n-icon><ChevronBackOutline /></n-icon></button>
              <button type="button" :disabled="pane.historyIndex >= pane.history.length - 1" title="前进" @click.stop="goHistory(pane, 1)"><n-icon><ChevronForwardOutline /></n-icon></button>
              <button type="button" :disabled="pane.path === pane.rootPath" title="上一级" @click.stop="navigate(pane, pane.parentPath)"><n-icon><ArrowUpOutline /></n-icon></button>
            </div>
            <nav v-if="editingPathPane !== pane.id" class="pane-breadcrumbs" :title="pane.path">
              <template v-for="(crumb, index) in pane.breadcrumbs" :key="crumb.path">
                <span v-if="index">›</span>
                <button type="button" :disabled="crumb.path === pane.path" @click.stop="navigate(pane, crumb.path)">{{ crumb.label }}</button>
              </template>
            </nav>
            <input
              v-else
              :ref="(element) => setPathInput(pane.id, element)"
              v-model="pathDraft"
              class="pane-path-input"
              @keydown.enter.stop="submitPath(pane)"
              @keydown.escape.stop="editingPathPane = ''"
              @blur="editingPathPane = ''"
            />
            <button type="button" title="输入路径" @click.stop="beginEditPath(pane)"><n-icon><CreateOutline /></n-icon></button>
            <button type="button" :class="{ spinning: pane.loading }" title="刷新" @click.stop="refresh(pane)"><n-icon><RefreshOutline /></n-icon></button>
          </div>

          <div class="pane-filterbar">
            <n-icon size="15"><SearchOutline /></n-icon>
            <input v-model="pane.query" :placeholder="`筛选 ${shortPath(pane.path)}`" @mousedown.stop />
            <button v-if="pane.query" type="button" @click.stop="pane.query = ''">×</button>
          </div>

          <div class="pane-columns">
            <button type="button" @click.stop="toggleSort(pane, 'name')">名称 <span>{{ sortMark(pane, 'name') }}</span></button>
            <button type="button" @click.stop="toggleSort(pane, 'size')">大小 <span>{{ sortMark(pane, 'size') }}</span></button>
            <button type="button" @click.stop="toggleSort(pane, 'modifiedAt')">修改日期 <span>{{ sortMark(pane, 'modifiedAt') }}</span></button>
          </div>

          <div class="pane-content" @click.self="clearSelection(pane)">
            <div v-if="pane.loading" class="pane-placeholder"><n-spin size="small" /><span>正在读取…</span></div>
            <div v-else-if="pane.error" class="pane-placeholder error"><n-icon size="28"><AlertCircleOutline /></n-icon><strong>无法打开文件夹</strong><span>{{ pane.error }}</span><button type="button" @click.stop="refresh(pane)">重试</button></div>
            <div v-else-if="!visibleItems(pane).length" class="pane-placeholder"><n-icon size="34"><FolderOpenOutline /></n-icon><strong>{{ pane.query ? '没有匹配项目' : '空文件夹' }}</strong><span>可将文件拖放到这里</span></div>
            <div v-else class="pane-rows">
              <article
                v-for="(item, index) in visibleItems(pane)"
                :key="item.path"
                class="file-row"
                :class="{ selected: pane.selectedPaths.includes(item.path) }"
                @click.stop="selectItem($event, pane, item, index)"
                @dblclick.stop="openItem(pane, item)"
                @contextmenu.prevent.stop="openContextMenu($event, pane, item, index)"
              >
                <div class="file-name-cell">
                  <n-icon size="19"><component :is="iconFor(item)" /></n-icon>
                  <span>{{ item.name }}</span>
                </div>
                <span>{{ item.type === 'file' ? formatSize(item.size) : '—' }}</span>
                <span>{{ formatTime(item.modifiedAt) }}</span>
              </article>
            </div>
            <div v-if="pane.dragging" class="pane-drop-target"><n-icon size="34"><CloudUploadOutline /></n-icon><strong>上传到 {{ shortPath(pane.path) }}</strong></div>
          </div>

          <footer class="pane-statusbar">
            <span>{{ selectionSummary(pane) }}</span>
            <span>{{ formatSize(pane.availableBytes) }} 可用</span>
          </footer>
        </section>
      </div>
    </main>

    <aside v-if="uploads.length" class="activity-popover">
      <header><strong>活动</strong><button type="button" @click="clearFinishedUploads">清除</button></header>
      <div v-for="upload in uploads" :key="upload.id" class="activity-row">
        <div><span>{{ upload.name }}</span><small :class="{ error: upload.status === 'error' }">{{ uploadStatusText(upload) }}</small></div>
        <n-progress type="line" :percentage="uploadPercent(upload)" :status="upload.status === 'error' ? 'error' : upload.status === 'done' ? 'success' : 'default'" :show-indicator="false" :height="5" />
      </div>
    </aside>

    <div v-if="contextMenu.show" class="context-menu" :style="{ left: `${contextMenu.x}px`, top: `${contextMenu.y}px` }" @mousedown.stop>
      <button v-if="contextMenu.item?.type === 'directory'" type="button" @click="openContextItem">打开</button>
      <button v-else type="button" @click="downloadContextItem">下载</button>
      <span />
      <button type="button" @click="contextTransfer('copy')">复制到另一侧</button>
      <button type="button" @click="contextTransfer('move')">移动到另一侧</button>
      <span />
      <button type="button" @click="renameContextItem">重命名</button>
      <button type="button" @click="deleteContextItem(false)">{{ contextPane().trashSupported ? '移到废纸篓' : '删除' }}</button>
      <button v-if="contextPane().trashSupported" class="danger" type="button" @click="deleteContextItem(true)">永久删除…</button>
    </div>

    <n-modal v-model:show="showNameDialog" preset="card" :title="dialogMode === 'create' ? '新建文件夹' : '重命名'" class="commander-dialog">
      <n-form-item label="名称"><n-input v-model:value="nameDraft" autofocus maxlength="255" @keyup.enter="submitNameDialog" /></n-form-item>
      <template #footer><div class="dialog-actions"><n-button @click="showNameDialog = false">取消</n-button><n-button type="primary" :loading="busy" @click="submitNameDialog">确定</n-button></div></template>
    </n-modal>

    <n-modal v-model:show="showConflictDialog" preset="card" title="目标位置存在同名项目" class="commander-dialog conflict-dialog">
      <p>请选择这次{{ pendingTransfer?.operation === 'move' ? '移动' : '复制' }}如何处理同名文件或文件夹。</p>
      <div class="conflict-actions">
        <button type="button" @click="performPendingTransfer('keepBoth')"><strong>两者都保留</strong><span>为传入项目自动添加“copy”后缀</span></button>
        <button type="button" @click="performPendingTransfer('skip')"><strong>跳过</strong><span>只处理不存在冲突的项目</span></button>
        <button class="danger" type="button" @click="performPendingTransfer('replace')"><strong>替换</strong><span>删除目标中的同名项目后继续</span></button>
      </div>
      <template #footer><div class="dialog-actions"><n-button @click="showConflictDialog = false">取消</n-button></div></template>
    </n-modal>
  </section>
</template>

<script setup lang="ts">
import axios from "axios";
import {
  AlertCircleOutline, ArrowBackOutline, ArrowUpOutline, ChevronBackOutline, ChevronForwardOutline,
  CloudUploadOutline, CopyOutline, CreateOutline, DesktopOutline, DocumentOutline, DocumentsOutline,
  DownloadOutline, EyeOutline, FolderOpenOutline, FolderOutline, HomeOutline, LinkOutline, RefreshOutline,
  SearchOutline, StarOutline, SwapHorizontalOutline, TrashOutline,
} from "@vicons/ionicons5";
import { NButton, NFormItem, NIcon, NInput, NModal, NProgress, NSpin, useMessage } from "naive-ui";
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { RouterLink } from "vue-router";
import {
  createLocalFolder, deleteLocalFile, listLocalFiles, localFileDownloadUrl, moveLocalFilesToTrash, renameLocalFile,
  transferLocalFiles, uploadLocalFile, type LocalFileBreadcrumb, type LocalFileConflictPolicy, type LocalFileItem,
} from "@/api";

type PaneId = "left" | "right";
type SortKey = "name" | "size" | "modifiedAt";
type PaneState = {
  id: PaneId; path: string; parentPath: string; homePath: string; rootPath: string; availableBytes: number; trashSupported: boolean;
  breadcrumbs: LocalFileBreadcrumb[]; items: LocalFileItem[]; selectedPaths: string[]; selectionAnchor: number;
  history: string[]; historyIndex: number; query: string; showHidden: boolean; sortKey: SortKey;
  sortDirection: "asc" | "desc"; loading: boolean; error: string; dragging: boolean;
};
type UploadState = { id: string; name: string; loaded: number; total: number; status: "queued" | "uploading" | "done" | "error"; error: string };
type PendingTransfer = { sources: string[]; sourcePaneId: PaneId; destinationPaneId: PaneId; operation: "copy" | "move" };

const message = useMessage();
const panes = reactive<PaneState[]>([makePane("left"), makePane("right")]);
const activePaneId = ref<PaneId>("left");
const busy = ref(false);
const fileInput = ref<HTMLInputElement | null>(null);
const pathInputs = new Map<PaneId, HTMLInputElement>();
const editingPathPane = ref<PaneId | "">("");
const pathDraft = ref("");
const favorites = ref<string[]>(loadFavorites());
const uploads = reactive<UploadState[]>([]);
const showNameDialog = ref(false);
const dialogMode = ref<"create" | "rename">("create");
const nameDraft = ref("");
const editingItem = ref<LocalFileItem | null>(null);
const showConflictDialog = ref(false);
const pendingTransfer = ref<PendingTransfer | null>(null);
const clipboard = ref<{ paths: string[]; sourcePaneId: PaneId } | null>(null);
const contextMenu = reactive<{ show: boolean; x: number; y: number; paneId: PaneId; item: LocalFileItem | null }>({ show: false, x: 0, y: 0, paneId: "left", item: null });

const activePane = computed(() => panes.find((pane) => pane.id === activePaneId.value) ?? panes[0]);
const inactivePane = computed(() => panes.find((pane) => pane.id !== activePaneId.value) ?? panes[1]);
const activeSelection = computed(() => selectedItems(activePane.value));
const canTransfer = computed(() => Boolean(activeSelection.value.length && activePane.value.path !== inactivePane.value.path && !busy.value));
const transferArrow = computed(() => activePaneId.value === "left" ? "→" : "←");
const locations = computed(() => {
  const home = panes[0].homePath;
  const entries = [
    { label: "主目录", path: home, icon: HomeOutline },
    { label: "桌面", path: joinPath(home, "Desktop"), icon: DesktopOutline },
    { label: "文稿", path: joinPath(home, "Documents"), icon: DocumentsOutline },
    { label: "下载", path: joinPath(home, "Downloads"), icon: DownloadOutline },
    { label: "根目录", path: panes[0].rootPath, icon: FolderOpenOutline },
  ];
  return entries.filter((entry, index) => entry.path && entries.findIndex((candidate) => candidate.path === entry.path) === index);
});

onMounted(async () => {
  window.addEventListener("mousedown", closeContextMenu);
  const loaded = await navigate(panes[0], "", false);
  await navigate(panes[1], loaded ? joinPath(panes[0].homePath, "Downloads") : "", false);
});
onBeforeUnmount(() => window.removeEventListener("mousedown", closeContextMenu));

function makePane(id: PaneId): PaneState {
  return { id, path: "", parentPath: "", homePath: "", rootPath: "", availableBytes: 0, trashSupported: false, breadcrumbs: [], items: [], selectedPaths: [], selectionAnchor: -1, history: [], historyIndex: -1, query: "", showHidden: false, sortKey: "name", sortDirection: "asc", loading: false, error: "", dragging: false };
}

async function navigate(pane: PaneState, path: string, pushHistory = true) {
  pane.loading = true; pane.error = "";
  try {
    const result = await listLocalFiles(path);
    pane.path = result.path; pane.parentPath = result.parentPath; pane.homePath = result.homePath;
    pane.rootPath = result.rootPath; pane.availableBytes = result.availableBytes; pane.trashSupported = result.trashSupported; pane.breadcrumbs = result.breadcrumbs;
    pane.items = result.items; pane.selectedPaths = []; pane.selectionAnchor = -1; pane.query = "";
    if (pushHistory || pane.historyIndex < 0) {
      const nextHistory = pane.history.slice(0, pane.historyIndex + 1);
      if (nextHistory[nextHistory.length - 1] !== result.path) nextHistory.push(result.path);
      pane.history = nextHistory; pane.historyIndex = nextHistory.length - 1;
    }
    return true;
  } catch (error) { pane.error = errorText(error, "读取文件夹失败"); return false; }
  finally { pane.loading = false; }
}

async function goHistory(pane: PaneState, offset: number) {
  const index = pane.historyIndex + offset;
  if (index < 0 || index >= pane.history.length) return;
  if (await navigate(pane, pane.history[index], false)) pane.historyIndex = index;
}

function refresh(pane: PaneState) { return navigate(pane, pane.path, false); }
function activatePane(pane: PaneState) { activePaneId.value = pane.id; }
function clearSelection(pane: PaneState) { pane.selectedPaths = []; pane.selectionAnchor = -1; activatePane(pane); }

function visibleItems(pane: PaneState) {
  const needle = pane.query.trim().toLocaleLowerCase();
  return pane.items.filter((item) => (pane.showHidden || !item.hidden) && (!needle || item.name.toLocaleLowerCase().includes(needle))).slice().sort((left, right) => {
    const leftFolder = left.type === "directory", rightFolder = right.type === "directory";
    if (leftFolder !== rightFolder) return leftFolder ? -1 : 1;
    const comparison = pane.sortKey === "name" ? left.name.localeCompare(right.name, undefined, { numeric: true, sensitivity: "base" }) : left[pane.sortKey] - right[pane.sortKey];
    return pane.sortDirection === "asc" ? comparison : -comparison;
  });
}

function selectItem(event: MouseEvent, pane: PaneState, item: LocalFileItem, index: number) {
  activatePane(pane);
  if (event.shiftKey && pane.selectionAnchor >= 0) {
    const entries = visibleItems(pane); const start = Math.min(pane.selectionAnchor, index); const end = Math.max(pane.selectionAnchor, index);
    pane.selectedPaths = entries.slice(start, end + 1).map((entry) => entry.path);
  } else if (event.metaKey || event.ctrlKey) {
    pane.selectedPaths = pane.selectedPaths.includes(item.path) ? pane.selectedPaths.filter((path) => path !== item.path) : [...pane.selectedPaths, item.path];
    pane.selectionAnchor = index;
  } else { pane.selectedPaths = [item.path]; pane.selectionAnchor = index; }
}

function selectedItems(pane: PaneState) { return pane.items.filter((item) => pane.selectedPaths.includes(item.path)); }
function openItem(pane: PaneState, item: LocalFileItem) { if (item.type === "directory") void navigate(pane, item.path); else if (item.type === "file") download(item); }

function toggleSort(pane: PaneState, key: SortKey) { if (pane.sortKey === key) pane.sortDirection = pane.sortDirection === "asc" ? "desc" : "asc"; else { pane.sortKey = key; pane.sortDirection = "asc"; } }
function sortMark(pane: PaneState, key: SortKey) { return pane.sortKey === key ? (pane.sortDirection === "asc" ? "↑" : "↓") : ""; }

function beginEditPath(pane: PaneState) { activatePane(pane); pathDraft.value = pane.path; editingPathPane.value = pane.id; void nextTick(() => { pathInputs.get(pane.id)?.focus(); pathInputs.get(pane.id)?.select(); }); }
function setPathInput(id: PaneId, element: unknown) { if (element instanceof HTMLInputElement) pathInputs.set(id, element); }
function submitPath(pane: PaneState) { const path = pathDraft.value.trim(); editingPathPane.value = ""; if (path) void navigate(pane, path); }

function openCreateFolder() { dialogMode.value = "create"; editingItem.value = null; nameDraft.value = ""; showNameDialog.value = true; }
function openRename(item?: LocalFileItem) { if (!item) return; dialogMode.value = "rename"; editingItem.value = item; nameDraft.value = item.name; showNameDialog.value = true; }
async function submitNameDialog() {
  const name = nameDraft.value.trim(); if (!validName(name)) { message.warning("请输入不包含 / 或 \\ 的有效名称"); return; }
  busy.value = true;
  try { if (dialogMode.value === "create") await createLocalFolder(activePane.value.path, name); else if (editingItem.value) await renameLocalFile(editingItem.value.path, name); showNameDialog.value = false; await refresh(activePane.value); }
  catch (error) { message.error(errorText(error, "操作失败")); } finally { busy.value = false; }
}

function requestTransfer(operation: "copy" | "move", paths = activePane.value.selectedPaths, sourcePaneId = activePane.value.id) {
  const source = panes.find((pane) => pane.id === sourcePaneId) ?? activePane.value;
  const destination = panes.find((pane) => pane.id !== sourcePaneId) ?? inactivePane.value;
  if (!paths.length || source.path === destination.path) return;
  const names = source.items.filter((item) => paths.includes(item.path)).map((item) => item.name);
  const conflict = destination.items.some((item) => names.includes(item.name));
  pendingTransfer.value = { sources: [...paths], sourcePaneId, destinationPaneId: destination.id, operation };
  if (conflict) showConflictDialog.value = true; else void performPendingTransfer("error");
}

async function performPendingTransfer(policy: LocalFileConflictPolicy) {
  const pending = pendingTransfer.value; if (!pending) return;
  showConflictDialog.value = false; busy.value = true;
  const source = panes.find((pane) => pane.id === pending.sourcePaneId)!;
  const destination = panes.find((pane) => pane.id === pending.destinationPaneId)!;
  try {
    const result = await transferLocalFiles(pending.sources, destination.path, pending.operation, policy);
    message.success(`${pending.operation === "move" ? "移动" : "复制"}完成：${result.completed} 项${result.skipped ? `，跳过 ${result.skipped} 项` : ""}`);
    source.selectedPaths = []; await Promise.all([refresh(source), refresh(destination)]);
  } catch (error) { message.error(errorText(error, "文件操作失败")); }
  finally { busy.value = false; pendingTransfer.value = null; }
}

async function removeSelected(permanent = false) {
  const pane = activePane.value; const selected = selectedItems(pane); if (!selected.length) return;
  if (permanent || !pane.trashSupported) {
    const names = selected.slice(0, 3).map((item) => item.name).join("、");
    if (!window.confirm(`永久删除 ${selected.length} 个项目？\n${names}${selected.length > 3 ? "…" : ""}\n文件夹内的内容也会被删除，此操作无法撤销。`)) return;
  }
  busy.value = true;
  try {
    if (!permanent && pane.trashSupported) await moveLocalFilesToTrash(selected.map((item) => item.path));
    else for (const item of selected) await deleteLocalFile(item.path);
    message.success(!permanent && pane.trashSupported ? `已将 ${selected.length} 项移到废纸篓` : `已删除 ${selected.length} 项`);
    await refresh(pane);
  }
  catch (error) { message.error(errorText(error, "删除失败")); await refresh(pane); } finally { busy.value = false; }
}

function chooseFiles() { fileInput.value?.click(); }
function handleFileInput(event: Event) { const input = event.target as HTMLInputElement; if (input.files?.length) void uploadFiles(activePane.value, Array.from(input.files)); input.value = ""; }
function handleDragLeave(event: DragEvent, pane: PaneState) { const related = event.relatedTarget as Node | null; if (!related || !(event.currentTarget as HTMLElement).contains(related)) pane.dragging = false; }
function handleDrop(event: DragEvent, pane: PaneState) { pane.dragging = false; const files = Array.from(event.dataTransfer?.files ?? []); if (files.length) void uploadFiles(pane, files); }
async function uploadFiles(pane: PaneState, files: File[]) {
  const collisions = files.filter((file) => pane.items.some((item) => item.name === file.name));
  if (collisions.length && !window.confirm(`${collisions.length} 个同名项目已存在。继续将覆盖同名文件。`)) return;
  const target = pane.path; const jobs = files.map((file) => { const state: UploadState = { id: crypto.randomUUID(), name: file.name, loaded: 0, total: file.size, status: "queued", error: "" }; uploads.push(state); return { file, state }; });
  let next = 0; const workers = Array.from({ length: Math.min(2, jobs.length) }, async () => { while (next < jobs.length) { const job = jobs[next++]; job.state.status = "uploading"; try { await uploadLocalFile(target, job.file, (loaded) => job.state.loaded = loaded); job.state.loaded = job.file.size; job.state.status = "done"; } catch (error) { job.state.status = "error"; job.state.error = errorText(error, "上传失败"); } } });
  await Promise.all(workers); if (pane.path === target) await refresh(pane);
  const failures = jobs.filter((job) => job.state.status === "error").length; failures ? message.error(`${failures} 个文件上传失败`) : message.success(`已上传 ${jobs.length} 个文件`);
}

function openContextMenu(event: MouseEvent, pane: PaneState, item: LocalFileItem, index: number) { if (!pane.selectedPaths.includes(item.path)) selectItem(event, pane, item, index); contextMenu.show = true; contextMenu.x = Math.min(event.clientX, window.innerWidth - 190); contextMenu.y = Math.min(event.clientY, window.innerHeight - 245); contextMenu.paneId = pane.id; contextMenu.item = item; }
function closeContextMenu() { contextMenu.show = false; }
function contextPane() { return panes.find((pane) => pane.id === contextMenu.paneId) ?? activePane.value; }
function openContextItem() { const item = contextMenu.item; if (item) openItem(contextPane(), item); closeContextMenu(); }
function downloadContextItem() { if (contextMenu.item) download(contextMenu.item); closeContextMenu(); }
function contextTransfer(operation: "copy" | "move") { const pane = contextPane(); requestTransfer(operation, pane.selectedPaths, pane.id); closeContextMenu(); }
function renameContextItem() { const item = contextMenu.item; closeContextMenu(); openRename(item ?? undefined); }
function deleteContextItem(permanent: boolean) { activatePane(contextPane()); closeContextMenu(); void removeSelected(permanent); }

function handleKeydown(event: KeyboardEvent) {
  if (event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement) return;
  const pane = activePane.value; const entries = visibleItems(pane); const current = pane.selectedPaths.length === 1 ? entries.findIndex((item) => item.path === pane.selectedPaths[0]) : -1;
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "a") { event.preventDefault(); pane.selectedPaths = entries.map((item) => item.path); return; }
  if ((event.metaKey || event.ctrlKey) && event.key === "ArrowUp") { event.preventDefault(); void navigate(pane, pane.parentPath); return; }
  if ((event.metaKey || event.ctrlKey) && (event.key === "ArrowDown" || event.key === "Enter")) { event.preventDefault(); if (activeSelection.value.length === 1) openItem(pane, activeSelection.value[0]); return; }
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "c") { if (pane.selectedPaths.length) clipboard.value = { paths: [...pane.selectedPaths], sourcePaneId: pane.id }; return; }
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "v") { event.preventDefault(); if (clipboard.value) { const operation = event.altKey ? "move" : "copy"; const source = panes.find((item) => item.id === clipboard.value!.sourcePaneId)!; const paths = [...clipboard.value.paths]; pendingTransfer.value = { sources: paths, sourcePaneId: source.id, destinationPaneId: pane.id, operation }; const names = source.items.filter((item) => paths.includes(item.path)).map((item) => item.name); if (pane.items.some((item) => names.includes(item.name))) showConflictDialog.value = true; else void performPendingTransfer("error"); } return; }
  if ((event.metaKey || event.ctrlKey) && event.key === "Backspace") { event.preventDefault(); void removeSelected(event.altKey); return; }
  if (event.key === "Enter" && activeSelection.value.length === 1) { event.preventDefault(); openItem(pane, activeSelection.value[0]); return; }
  if ((event.key === "ArrowDown" || event.key === "ArrowUp") && entries.length) { event.preventDefault(); const next = current < 0 ? 0 : Math.max(0, Math.min(entries.length - 1, current + (event.key === "ArrowDown" ? 1 : -1))); pane.selectedPaths = [entries[next].path]; pane.selectionAnchor = next; }
}

function download(item: LocalFileItem) { const anchor = document.createElement("a"); anchor.href = localFileDownloadUrl(item.path); anchor.download = item.name; document.body.appendChild(anchor); anchor.click(); anchor.remove(); }
function addFavorite() { const path = activePane.value.path; if (!path || favorites.value.includes(path)) { if (path) message.info("该路径已在收藏中"); return; } favorites.value.push(path); saveFavorites(); }
function removeFavorite(path: string) { favorites.value = favorites.value.filter((favorite) => favorite !== path); saveFavorites(); }
function loadFavorites(): string[] { try { const value = JSON.parse(localStorage.getItem("space-station:file-favorites") ?? "[]"); return Array.isArray(value) ? value.filter((item): item is string => typeof item === "string") : []; } catch { return []; } }
function saveFavorites() { localStorage.setItem("space-station:file-favorites", JSON.stringify(favorites.value)); }
function selectionSummary(pane: PaneState) { const selected = selectedItems(pane); if (!selected.length) return `${visibleItems(pane).length} 项`; const size = selected.reduce((sum, item) => sum + (item.type === "file" ? item.size : 0), 0); return `已选择 ${selected.length} 项${size ? ` · ${formatSize(size)}` : ""}`; }
function iconFor(item: LocalFileItem) { return item.type === "directory" ? FolderOutline : item.type === "symlink" ? LinkOutline : DocumentOutline; }
function shortPath(path: string) { if (!path) return "…"; const clean = path.replace(/[\\/]$/, ""); return clean.split(/[\\/]/).filter(Boolean).pop() ?? path; }
function joinPath(parent: string, name: string) { if (!parent) return ""; const separator = parent.includes("\\") ? "\\" : "/"; return parent.endsWith(separator) ? `${parent}${name}` : `${parent}${separator}${name}`; }
function formatSize(size: number) { if (!size) return "0 B"; const units = ["B", "KB", "MB", "GB", "TB"]; const unit = Math.min(Math.floor(Math.log(size) / Math.log(1024)), units.length - 1); return `${(size / 1024 ** unit).toLocaleString(undefined, { maximumFractionDigits: unit ? 1 : 0 })} ${units[unit]}`; }
function formatTime(timestamp: number) { return timestamp ? new Intl.DateTimeFormat("zh-CN", { year: "2-digit", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit" }).format(timestamp) : "—"; }
function validName(name: string) { return Boolean(name && name !== "." && name !== ".." && !/[\\/]/.test(name)); }
function uploadPercent(upload: UploadState) { return upload.status === "done" ? 100 : !upload.total ? (upload.status === "uploading" ? 50 : 0) : Math.min(100, Math.round(upload.loaded / upload.total * 100)); }
function uploadStatusText(upload: UploadState) { if (upload.status === "queued") return "等待"; if (upload.status === "done") return "完成"; if (upload.status === "error") return upload.error; return `${formatSize(upload.loaded)} / ${formatSize(upload.total)}`; }
function clearFinishedUploads() { for (let index = uploads.length - 1; index >= 0; index--) if (!["queued", "uploading"].includes(uploads[index].status)) uploads.splice(index, 1); }
function errorText(error: unknown, fallback: string) { if (axios.isAxiosError(error)) return error.response?.data?.message || error.message || fallback; return error instanceof Error ? error.message : fallback; }
</script>

<style scoped>
.commander { --blue:#2d6ea3; --ink:#202b33; --muted:#77838c; --line:rgba(39,55,66,.14); height:100dvh; min-width:0; display:grid; grid-template-rows:58px minmax(0,1fr); overflow:hidden; outline:none; background:#e9edf0; color:var(--ink); }
.commander-toolbar { display:flex; align-items:center; gap:18px; padding:7px 14px; border-bottom:1px solid var(--line); background:rgba(250,251,252,.96); box-shadow:0 1px 5px rgba(30,45,55,.06); }
.toolbar-brand { min-width:186px; display:flex; align-items:center; gap:10px; }
.toolbar-brand>div { display:grid; }
.toolbar-brand strong { font-size:14px; }
.toolbar-brand small { color:#8a969e; font-size:8px; font-weight:800; letter-spacing:.12em; }
.toolbar-icon-button { width:34px; height:34px; display:grid; place-items:center; border:1px solid var(--line); border-radius:7px; background:#fff; color:#334b5a; }
.toolbar-commands { display:flex; align-items:stretch; gap:3px; height:43px; }
.toolbar-commands button,.toolbar-tail button { min-width:58px; padding:3px 8px; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:1px; border:0; border-radius:6px; background:transparent; color:#354651; font-size:10px; cursor:pointer; }
.toolbar-commands button:hover:not(:disabled),.toolbar-tail button:hover { background:#e8edf0; }
.toolbar-commands button:disabled { opacity:.35; cursor:default; }
.toolbar-commands .danger-command:not(:disabled) { color:#a54747; }
.toolbar-separator { width:1px; margin:4px 5px; background:var(--line); }
.toolbar-tail { margin-left:auto; display:flex; gap:3px; }
.toolbar-tail .icon-only { min-width:34px; height:34px; }
.toolbar-tail .active { color:var(--blue); background:#dfeaf2; }
.native-file-input { display:none; }
.mobile-pane-switcher { display:none; }

.commander-main { min-height:0; display:grid; grid-template-columns:190px minmax(0,1fr); }
.commander-sidebar { min-height:0; padding:12px 9px 10px; display:flex; flex-direction:column; gap:18px; overflow:auto; border-right:1px solid var(--line); background:#f3f5f6; }
.sidebar-section { display:grid; gap:2px; }
.sidebar-section h2 { margin:0 8px 5px; color:#8a949b; font-size:10px; font-weight:800; letter-spacing:.08em; text-transform:uppercase; }
.sidebar-section>button,.favorite-row>button:first-child { width:100%; min-width:0; height:30px; padding:0 8px; display:flex; align-items:center; gap:8px; border:0; border-radius:6px; background:transparent; color:#40525e; text-align:left; cursor:pointer; }
.sidebar-section button:hover,.sidebar-section button.active { background:#dfe8ee; color:#174f78; }
.sidebar-section button span { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.sidebar-heading { display:flex; align-items:center; justify-content:space-between; }
.sidebar-heading button { border:0; background:transparent; color:#71818c; cursor:pointer; }
.sidebar-favorites p { margin:4px 8px; color:#9ba4aa; font-size:10px; line-height:1.5; }
.favorite-row { display:grid; grid-template-columns:minmax(0,1fr) 22px; }
.favorite-row .favorite-remove { padding:0; border:0; background:transparent; color:#9aa4aa; opacity:0; cursor:pointer; }
.favorite-row:hover .favorite-remove { opacity:1; }
.sidebar-shortcuts { margin-top:auto; padding:9px; display:grid; gap:4px; border-top:1px solid var(--line); color:#9aa3a9; font-size:9px; }

.pane-deck { min-width:0; min-height:0; padding:8px; display:grid; grid-template-columns:minmax(0,1fr) minmax(0,1fr); gap:8px; }
.file-pane { min-width:0; min-height:0; display:grid; grid-template-rows:31px 39px 31px 30px minmax(0,1fr) 27px; overflow:hidden; border:1px solid var(--line); border-radius:8px; background:#fff; box-shadow:0 5px 18px rgba(34,49,58,.06); }
.file-pane.active { border-color:rgba(45,110,163,.7); box-shadow:0 0 0 1px rgba(45,110,163,.2),0 6px 22px rgba(34,64,85,.09); }
.pane-tabbar { padding:4px 6px 0; display:flex; align-items:flex-end; gap:3px; border-bottom:1px solid var(--line); background:#edf0f2; }
.pane-tab { min-width:0; max-width:210px; height:27px; padding:0 9px; display:flex; align-items:center; gap:6px; border:1px solid var(--line); border-bottom-color:#fff; border-radius:6px 6px 0 0; background:#fff; font-size:11px; }
.pane-tab span { min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.pane-tab small { margin-left:auto; color:#9ba4aa; }
.pane-tabbar>button { margin-bottom:4px; border:0; background:transparent; color:#7a878f; cursor:pointer; }
.pane-pathbar { min-width:0; padding:4px 6px; display:flex; align-items:center; gap:3px; border-bottom:1px solid var(--line); }
.history-controls { display:flex; gap:1px; }
.pane-pathbar button { width:27px; height:27px; padding:0; display:grid; place-items:center; flex:0 0 auto; border:0; border-radius:5px; background:transparent; color:#566873; cursor:pointer; }
.pane-pathbar button:hover:not(:disabled) { background:#eaf0f3; }
.pane-pathbar button:disabled { opacity:.3; }
.pane-breadcrumbs { min-width:0; flex:1; display:flex; align-items:center; overflow:auto; white-space:nowrap; scrollbar-width:none; }
.pane-breadcrumbs>span { color:#b1b9be; }
.pane-breadcrumbs button { width:auto; max-width:130px; padding:0 5px; display:block; overflow:hidden; color:#405865; font-size:11px; text-overflow:ellipsis; white-space:nowrap; }
.pane-breadcrumbs button:disabled { color:#203846; font-weight:700; opacity:1; }
.pane-path-input { min-width:0; height:27px; flex:1; padding:0 7px; border:1px solid #7ba6c4; border-radius:5px; outline:none; font:11px ui-monospace,monospace; }
.spinning :deep(svg) { animation:spin .8s linear infinite; }
@keyframes spin { to { transform:rotate(360deg); } }
.pane-filterbar { padding:4px 9px; display:flex; align-items:center; gap:6px; border-bottom:1px solid var(--line); color:#8a969d; background:#fafbfb; }
.pane-filterbar input { min-width:0; flex:1; border:0; outline:0; background:transparent; color:#34454f; font-size:11px; }
.pane-filterbar button { border:0; background:transparent; color:#87939a; cursor:pointer; }
.pane-columns,.file-row { display:grid; grid-template-columns:minmax(160px,1fr) 78px 118px; align-items:center; }
.pane-columns { padding:0 9px; border-bottom:1px solid var(--line); background:#f2f4f5; }
.pane-columns button { padding:0; border:0; background:transparent; color:#697780; font-size:9px; font-weight:700; text-align:left; text-transform:uppercase; cursor:pointer; }
.pane-content { position:relative; min-height:0; overflow:auto; background:#fff; }
.file-row { min-height:31px; padding:0 9px; border-bottom:1px solid rgba(38,54,65,.065); color:#44545e; font-size:10px; user-select:none; }
.file-row:hover { background:#f0f5f8; }
.file-row.selected { background:#dbeaf4; color:#163f5c; }
.file-pane.active .file-row.selected { background:#c9e0ef; }
.file-name-cell { min-width:0; display:flex; align-items:center; gap:7px; }
.file-name-cell :deep(.n-icon) { flex:0 0 auto; color:#477795; }
.file-name-cell span { overflow:hidden; font-size:11px; font-weight:600; text-overflow:ellipsis; white-space:nowrap; }
.file-row>span { color:#78858d; font-variant-numeric:tabular-nums; }
.pane-placeholder { height:100%; min-height:170px; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:7px; color:#9aa4aa; text-align:center; }
.pane-placeholder strong { color:#64737c; font-size:12px; }
.pane-placeholder span { max-width:80%; font-size:10px; }
.pane-placeholder.error { color:#ad5656; }
.pane-placeholder button { border:1px solid var(--line); border-radius:5px; background:#fff; cursor:pointer; }
.pane-drop-target { position:absolute; inset:8px; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:8px; border:2px dashed #4d8bb4; border-radius:8px; background:rgba(230,242,249,.94); color:#28658c; pointer-events:none; }
.pane-statusbar { padding:0 9px; display:flex; align-items:center; justify-content:space-between; border-top:1px solid var(--line); background:#f5f7f8; color:#7b888f; font-size:9px; }

.activity-popover { position:fixed; right:14px; bottom:14px; z-index:15; width:min(360px,calc(100vw - 28px)); max-height:220px; padding:10px; display:grid; gap:8px; overflow:auto; border:1px solid var(--line); border-radius:9px; background:rgba(255,255,255,.97); box-shadow:0 14px 44px rgba(25,40,50,.22); }
.activity-popover header,.activity-row>div { display:flex; justify-content:space-between; gap:8px; }
.activity-popover header button { border:0; background:transparent; color:var(--blue); cursor:pointer; }
.activity-row span { overflow:hidden; font-size:10px; font-weight:700; text-overflow:ellipsis; white-space:nowrap; }
.activity-row small { flex:0 0 auto; color:#7c8991; font-size:9px; }
.activity-row small.error { color:#bd4141; }
.context-menu { position:fixed; z-index:30; width:185px; padding:5px; display:grid; border:1px solid rgba(39,55,66,.2); border-radius:7px; background:rgba(255,255,255,.98); box-shadow:0 12px 35px rgba(22,36,45,.23); }
.context-menu button { height:28px; padding:0 9px; border:0; border-radius:4px; background:transparent; color:#344650; font-size:11px; text-align:left; cursor:pointer; }
.context-menu button:hover { background:#e5eff5; }
.context-menu button.danger { color:#ac4242; }
.context-menu>span { height:1px; margin:4px; background:var(--line); }
.commander-dialog { width:min(440px,calc(100vw - 28px)); }
.dialog-actions { display:flex; justify-content:flex-end; gap:8px; }
.conflict-dialog p { color:#667680; font-size:12px; }
.conflict-actions { display:grid; gap:7px; }
.conflict-actions button { padding:10px 12px; display:grid; gap:2px; border:1px solid var(--line); border-radius:7px; background:#f8fafb; color:#304550; text-align:left; cursor:pointer; }
.conflict-actions button:hover { border-color:#7ca5bf; background:#eef5f8; }
.conflict-actions button.danger { color:#9b3c3c; }
.conflict-actions span { color:#839099; font-size:10px; }

@media (max-width:950px) { .commander-main{grid-template-columns:150px minmax(0,1fr)} .toolbar-brand{min-width:auto} .toolbar-brand>div{display:none} .toolbar-commands button{min-width:48px} .toolbar-commands button span{font-size:0} .toolbar-commands button span::first-letter{font-size:0} .pane-columns,.file-row{grid-template-columns:minmax(130px,1fr) 65px 92px} .file-row>span:last-child{font-size:9px} }
@media (max-width:720px) { .commander{grid-template-rows:54px 34px minmax(0,1fr)} .commander-toolbar{gap:5px;padding:5px 7px} .commander-sidebar{display:none} .commander-main{grid-template-columns:1fr} .toolbar-separator{margin-inline:1px} .toolbar-commands button:nth-of-type(5),.toolbar-commands button:nth-of-type(6){display:none} .toolbar-tail{display:none} .mobile-pane-switcher{display:grid;grid-template-columns:1fr 1fr;padding:3px 6px;gap:4px;border-bottom:1px solid var(--line);background:#f4f6f7} .mobile-pane-switcher button{overflow:hidden;border:0;border-radius:5px;background:transparent;color:#667680;font-size:10px;text-overflow:ellipsis;white-space:nowrap} .mobile-pane-switcher button.active{background:#d8e7f0;color:#1d5c84;font-weight:700} .pane-deck{grid-template-columns:1fr;padding:5px}.file-pane:not(.active){display:none} }
</style>
