<template>
  <div class="serial-app" :class="{ 'serial-app--session-open': activeView && !mobileSetup }" :style="mobileViewportStyle">
    <aside class="serial-sidebar">
      <div class="serial-brand">
        <router-link to="/" aria-label="返回首页">SS</router-link>
        <div><strong>串口终端</strong><small>本机 / 共享 / 服务器</small></div>
      </div>

      <div class="serial-source-switch">
        <button type="button" :class="{ active: source === 'browser' }" @click="source = 'browser'">浏览器串口</button>
        <button type="button" :class="{ active: source === 'shared' }" @click="source = 'shared'">他人共享</button>
        <button type="button" :class="{ active: source === 'server' }" @click="source = 'server'">服务器串口</button>
      </div>

      <n-alert v-if="source === 'browser' && !browserSupported" type="warning" :show-icon="false">
        当前浏览器不支持 Web Serial，请使用桌面版 Chrome 或 Edge，或改用服务器串口。
      </n-alert>
      <template v-if="source === 'browser'">
        <div class="serial-field-heading">
          <span>已授权设备</span>
          <n-button size="tiny" secondary :disabled="!browserSupported" @click="refreshBrowserPorts">刷新</n-button>
        </div>
        <n-select v-model:value="browserPortId" :options="browserPortOptions" placeholder="先选择一个串口" />
        <n-button type="primary" secondary :disabled="!browserSupported" @click="requestBrowserPort">选择浏览器串口</n-button>
        <p class="serial-hint">浏览器只允许网页访问你主动选择的设备，授权由当前浏览器和站点管理。</p>
        <div class="serial-share-settings">
          <n-checkbox v-model:checked="browserShareSettings.enabled">共享给其他客户端</n-checkbox>
          <n-input v-if="browserShareSettings.enabled" v-model:value="browserShareSettings.name" size="small" maxlength="120" placeholder="共享名称" />
          <n-checkbox v-if="browserShareSettings.enabled" v-model:checked="browserShareSettings.writeEnabled">允许其他客户端写入</n-checkbox>
          <p v-if="browserShareSettings.enabled" class="serial-share-status" :class="browserShareStatus.kind">
            <span aria-hidden="true" />{{ browserShareStatus.text }}
          </p>
          <p v-if="browserShareSettings.enabled" class="serial-hint">默认建议只读。必须先打开浏览器串口才会发布；拥有者页面关闭或本机串口断开后，共享立即离线。</p>
        </div>
      </template>
      <template v-else-if="source === 'shared'">
        <div class="serial-field-heading">
          <span>在线浏览器共享</span>
          <n-button size="tiny" secondary :loading="loadingSharedPorts" @click="refreshSharedPorts">刷新</n-button>
        </div>
        <n-select v-model:value="sharedPortId" filterable :options="sharedPortOptions" placeholder="选择其他客户端共享的串口" />
        <p class="serial-shared-refresh-state">
          {{ sharedPorts.length ? `已发现 ${sharedPorts.length} 个在线共享` : "暂无在线共享" }} · 每 2 秒自动刷新
        </p>
        <p class="serial-hint">数据通过 Space Station 后端中继；只读共享不能从此客户端发送数据。</p>
      </template>
      <template v-else>
        <div class="serial-field-heading">
          <span>后端主机设备</span>
          <n-button size="tiny" secondary :loading="loadingServerPorts" @click="refreshServerPorts">刷新</n-button>
        </div>
        <n-select v-model:value="serverPortId" filterable :options="serverPortOptions" placeholder="选择服务器串口" />
        <p class="serial-hint">设备位于运行 Space Station 后端的主机，不一定是当前浏览器所在设备。</p>
      </template>

      <div v-if="source !== 'shared'" class="serial-divider" />
      <div v-if="source !== 'shared'" class="serial-grid">
        <label><span>波特率</span><n-select v-model:value="settings.baudRate" filterable tag :options="baudOptions" /></label>
        <label><span>数据位</span><n-select v-model:value="settings.dataBits" :options="dataBitsOptions" /></label>
        <label><span>停止位</span><n-select v-model:value="settings.stopBits" :options="stopBitsOptions" /></label>
        <label><span>校验位</span><n-select v-model:value="settings.parity" :options="parityOptions" /></label>
      </div>
      <label v-if="source !== 'shared'" class="serial-flow"><span>流控制</span><n-select v-model:value="settings.flowControl" :options="flowOptions" /></label>
      <n-button class="serial-open" type="primary" :disabled="!selectedPortAvailable" @click="openSelectedPort">
        打开串口
      </n-button>

      <section v-if="views.length" class="serial-mobile-sessions" aria-label="已打开串口">
        <div class="serial-mobile-sessions-title">
          <strong>已打开串口</strong>
          <span>{{ views.length }}</span>
        </div>
        <div class="serial-mobile-session-list">
          <div v-for="view in views" :key="`mobile-${view.id}`" class="serial-mobile-session">
            <button type="button" @click="activateView(view.id)">
              <span class="serial-status-dot" :class="sessionFor(view)?.status" />
              <span>{{ view.title }}</span>
            </button>
            <button type="button" aria-label="关闭串口" @click="closeView(view.id)">×</button>
          </div>
        </div>
      </section>

      <div class="serial-rules">
        <strong>占用规则</strong>
        <p>当前页面内每个串口只保留一个终端，重复打开会直接切换到已有标签。不同浏览器可以共享同一个后端串口，最后一个客户端退出后才释放设备。</p>
      </div>
    </aside>

    <main class="serial-workspace">
      <header class="serial-tabs">
        <n-button class="serial-mobile-menu" secondary size="small" @click="mobileSetup = true">串口</n-button>
        <div class="serial-tab-list">
          <button
            v-for="view in views"
            :key="view.id"
            type="button"
            class="serial-tab"
            :class="{ active: view.id === activeViewId }"
            @click="activateView(view.id)"
          >
            <span class="serial-status-dot" :class="sessionFor(view)?.status" />
            <span>{{ view.title }}</span>
            <i @click.stop="closeView(view.id)">×</i>
          </button>
        </div>
        <div v-if="activeSession" class="serial-tab-actions">
          <terminal-action-bar
            class="serial-desktop-actions"
            :recording="Boolean(activeView?.recording)"
            settings
            @search="openSearch"
            @snippets="showSnippets = true"
            @recording="activeView && toggleRecording(activeView)"
            @settings="openTerminalSettings"
          />
          <terminal-plugin-entry
            :transport="activePluginContext.transport"
            :target="activePluginContext.target"
            :unavailable-hint="activePluginContext.hint"
          />
          <n-button v-if="activeSession.status === 'error' || activeSession.status === 'closed'" size="tiny" secondary @click="reconnectActive">重连</n-button>
          <span class="serial-location">{{ locationText(activeSession.location) }}</span>
          <span class="serial-status-label" :class="activeSession.status">{{ statusText(activeSession) }}</span>
          <terminal-renderer-badge v-if="activeView" class="serial-renderer" :renderer="activeView.renderer" />
          <n-dropdown v-if="mobileActionOptions.length" trigger="click" :options="mobileActionOptions" @select="handleMobileAction">
            <n-button class="serial-mobile-more" secondary size="tiny">更多</n-button>
          </n-dropdown>
        </div>
      </header>

      <terminal-search-bar
        v-if="activeView && showSearch"
        ref="searchInput"
        v-model:query="searchQuery"
        v-model:target-index="searchTargetIndex"
        v-model:case-sensitive="searchCaseSensitive"
        v-model:whole-word="searchWholeWord"
        v-model:regex="searchRegex"
        class="serial-search-bar"
        :result-count="activeView.searchResultCount"
        :count-text="searchCountText(activeView)"
        @index-focus="searchIndexEditing = $event"
        @jump="jumpToSearchIndex"
        @previous="searchTerminal(true)"
        @next="searchTerminal(false)"
        @close="closeSearch"
      />

      <section v-if="views.length === 0" class="serial-empty">
        <div>›_</div>
        <h1>打开一个串口开始通信</h1>
        <p>每个串口对应一个终端标签，可以同时打开多个不同串口。</p>
        <n-button class="serial-empty-mobile" type="primary" @click="mobileSetup = true">选择串口</n-button>
      </section>

      <template v-for="view in views" :key="view.id">
        <section v-show="view.id === activeViewId" class="serial-terminal-section">
          <web-terminal
            class="serial-terminal"
            :scrollback="terminalSettings.scrollbackLines"
            :font-size="terminalSettings.fontSize"
            :line-height="terminalSettings.lineHeight"
            :letter-spacing="terminalSettings.letterSpacing"
            @ready="handleTerminalReady(view, $event)"
            @data="handleTerminalData(view, $event)"
            @renderer="view.renderer = $event"
            @search-results="updateSearchResults(view, $event)"
          />
          <div v-if="sessionFor(view)?.error" class="serial-error">{{ sessionFor(view)?.error }}</div>
          <terminal-command-panel
            :show-quick="showQuickSnippets"
            :show-composer="terminalSettings.showCommandComposer"
            :snippets="pinnedSnippets"
            label="发送框"
            @toggle-quick="showQuickSnippets = !showQuickSnippets"
            @toggle-composer="toggleCommandComposer"
            @use-snippet="useSnippet(view, $event)"
          >
            <template #composer><div class="serial-composer">
            <n-select v-model:value="view.sendMode" class="serial-send-mode" :options="sendModeOptions" size="small" />
            <n-input
              v-model:value="view.command"
              class="serial-command"
              :placeholder="view.sendMode === 'hex' ? '例如：48 65 6C 6C 6F' : '输入要发送的内容'"
              @keyup.ctrl.enter="sendFromComposer(view)"
              @keyup.meta.enter="sendFromComposer(view)"
            />
            <n-select v-if="view.sendMode === 'text'" v-model:value="view.lineEnding" class="serial-line-ending" :options="lineEndingOptions" size="small" />
            <n-button type="primary" :disabled="!canWrite(view)" @click="sendFromComposer(view)">发送</n-button>
            </div></template>
          </terminal-command-panel>
        </section>
      </template>
    </main>

    <n-modal v-model:show="showSnippets" preset="card" title="命令片段" :style="dialogStyle">
      <n-tabs v-model:value="snippetLibraryTab" type="line" animated>
        <n-tab-pane name="local" tab="本地片段">
          <div class="serial-snippet-editor">
            <n-input v-model:value="snippetDraft.name" placeholder="名称，例如：查询版本" />
            <n-input v-model:value="snippetDraft.command" type="textarea" :rows="3" placeholder="要发送的文本或 HEX" />
            <n-select v-model:value="snippetDraft.action" :options="snippetActionOptions" />
            <n-checkbox v-model:checked="snippetDraft.pinned">显示为快捷按钮</n-checkbox>
            <n-button type="primary" @click="saveSnippet">{{ editingSnippetId ? '保存修改' : '添加片段' }}</n-button>
          </div>
          <div class="serial-snippet-list">
            <p v-if="snippets.length" class="serial-snippet-order-hint">SSH 与串口共用此本地列表；拖动左侧手柄调整顺序。</p>
            <div
              v-for="snippet in snippets"
              :key="snippet.id"
              class="serial-snippet-row"
              :data-snippet-id="snippet.id"
              :class="{
                'serial-snippet-row--dragging': draggedSnippetId === snippet.id,
                'serial-snippet-row--drop-before': snippetDropTargetId === snippet.id && snippetDropPosition === 'before',
                'serial-snippet-row--drop-after': snippetDropTargetId === snippet.id && snippetDropPosition === 'after',
              }"
            >
              <button
                class="serial-snippet-drag-handle"
                type="button"
                aria-label="拖动调整片段顺序"
                title="按住拖动排序"
                @pointerdown="startSnippetPointerDrag($event, snippet.id)"
                @pointermove="updateSnippetPointerDrag"
                @pointerup="dropSnippetPointerDrag"
                @pointercancel="cancelSnippetPointerDrag"
              >⠿</button>
              <button class="serial-snippet-content" type="button" @click="activeView && useSnippet(activeView, snippet)">
                <span class="serial-snippet-title"><strong>{{ snippet.name }}</strong><small>{{ snippet.action === 'insert' ? '插入' : '发送' }}</small></span>
                <code>{{ snippet.command }}</code>
              </button>
              <n-button text type="primary" :disabled="sharedActionId === snippet.id || localShareStatus(snippet) === '已共享'" @click="storeSnippetInLibrary(snippet)">{{ localShareStatus(snippet) }}</n-button>
              <n-button text type="primary" @click="editSnippet(snippet)">编辑</n-button>
              <n-button text type="error" @click="deleteSnippet(snippet.id)">删除</n-button>
            </div>
            <p v-if="snippets.length === 0" class="serial-hint">还没有本地片段，可新建或从共享库添加。</p>
          </div>
        </n-tab-pane>
        <n-tab-pane name="shared" tab="共享片段库">
          <div class="serial-shared-snippet-heading">
            <p>共享库保存在服务器；按需把单条片段添加到当前浏览器。</p>
            <n-button size="tiny" secondary :loading="libraryLoading" @click="loadSharedSnippetLibrary(true)">刷新</n-button>
          </div>
          <n-alert type="warning" :show-icon="false">共享片段对能访问本服务的客户端可见，请勿保存口令、令牌等秘密。</n-alert>
          <div class="serial-snippet-list serial-shared-snippet-list">
            <div v-for="snippet in librarySnippets" :key="snippet.id" class="serial-snippet-row">
              <div class="serial-snippet-content">
                <span class="serial-snippet-title"><strong>{{ snippet.name }}</strong><small>{{ snippet.action === 'insert' ? '插入' : '发送' }}</small></span>
                <code>{{ snippet.command }}</code>
              </div>
              <n-button text type="primary" :disabled="sharedActionId === snippet.id || sharedImportStatus(snippet) === '已在本地'" @click="addSharedSnippetToLocal(snippet)">{{ sharedImportStatus(snippet) }}</n-button>
              <n-popconfirm @positive-click="deleteSnippetFromLibrary(snippet.id)">
                <template #trigger><n-button text type="error" :disabled="sharedActionId === snippet.id">删除</n-button></template>
                只会从共享库删除，不影响各浏览器已有的本地片段。确认删除吗？
              </n-popconfirm>
            </div>
            <p v-if="libraryLoading && librarySnippets.length === 0" class="serial-hint">正在读取共享片段…</p>
            <p v-else-if="librarySnippets.length === 0" class="serial-hint">共享库还没有片段。</p>
          </div>
        </n-tab-pane>
      </n-tabs>
    </n-modal>

    <n-modal v-model:show="showRecordingOptions" preset="card" title="开始串口录制" :style="dialogStyle">
      <div class="serial-recording-options">
        <n-checkbox v-model:checked="recordingDraft.stripAnsi">过滤 ANSI 颜色和控制字符</n-checkbox>
        <n-checkbox v-model:checked="recordingDraft.timestamps">为每个数据块添加时间戳</n-checkbox>
        <p>录制内容只保存在当前浏览器标签，停止后下载日志。</p>
      </div>
      <template #footer><div class="serial-dialog-actions"><n-button @click="showRecordingOptions = false">取消</n-button><n-button type="primary" @click="startRecording">开始录制</n-button></div></template>
    </n-modal>

    <n-modal v-model:show="showTerminalSettings" preset="card" title="终端设置（SSH 与串口共用）" :style="dialogStyle">
      <n-form label-placement="top">
        <div class="serial-settings-grid">
          <n-form-item label="回滚缓冲区（输出行）">
            <n-input-number v-model:value="terminalSettingsDraft.scrollbackLines" :min="1000" :max="500000" :step="10000" />
            <template #feedback>按 120 列终端折算；窄屏会自动增加内部屏幕行容量。</template>
          </n-form-item>
          <n-form-item label="字体大小"><n-input-number v-model:value="terminalSettingsDraft.fontSize" :min="10" :max="28" /></n-form-item>
          <n-form-item label="行高"><n-input-number v-model:value="terminalSettingsDraft.lineHeight" :min="1" :max="2" :step="0.05" /></n-form-item>
          <n-form-item label="字符间距"><n-input-number v-model:value="terminalSettingsDraft.letterSpacing" :min="0" :max="4" :step="0.5" /></n-form-item>
          <n-form-item label="录制缓冲区上限（MiB）"><n-input-number v-model:value="terminalSettingsDraft.recordingMaxMiB" :min="1" :max="500" :step="10" /></n-form-item>
        </div>
        <n-form-item label="公共交互">
          <div class="serial-setting-switches">
            <n-checkbox v-model:checked="terminalSettingsDraft.showCommandComposer">显示底部命令编辑和发送框</n-checkbox>
            <n-checkbox v-model:checked="terminalSettingsDraft.copyOnSelect">选中终端文本后自动复制</n-checkbox>
            <n-checkbox v-model:checked="terminalSettingsDraft.pasteOnRightClick">在终端内右键时自动粘贴</n-checkbox>
            <div class="serial-clipboard-permission">
              <span>读取权限：{{ clipboardPermissionLabel }}</span>
              <n-button size="small" secondary :loading="clipboardPermissionState === 'checking'" @click="requestClipboardAccess">检测/授权</n-button>
            </div>
          </div>
        </n-form-item>
        <p class="serial-settings-hint">已明确授权时右键自动粘贴；Safari 或无权限时直接显示浏览器原生右键菜单。</p>
      </n-form>
      <template #footer><div class="serial-dialog-actions"><n-button @click="showTerminalSettings = false">取消</n-button><n-button type="primary" @click="saveTerminalSettings">保存</n-button></div></template>
    </n-modal>
  </div>
</template>

<script setup lang="ts">
import { NAlert, NButton, NCheckbox, NDropdown, NForm, NFormItem, NInput, NInputNumber, NModal, NPopconfirm, NSelect, NTabPane, NTabs, useMessage } from "naive-ui";
import { computed, markRaw, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { fetchBackendSerialPorts, fetchBrowserSerialShares, type BackendSerialPort, type BrowserSerialShare, type SharedCommandSnippet } from "@/api";
import WebTerminal from "../terminal/WebTerminal.vue";
import TerminalActionBar from "../terminal/TerminalActionBar.vue";
import TerminalSearchBar from "../terminal/TerminalSearchBar.vue";
import TerminalCommandPanel from "../terminal/TerminalCommandPanel.vue";
import TerminalRendererBadge from "../terminal/TerminalRendererBadge.vue";
import TerminalPluginEntry from "../terminal/TerminalPluginEntry.vue";
import type { TerminalRenderer, WebTerminalHandle, WebTerminalReadyEvent, WebTerminalSearchResult } from "../terminal/WebTerminal.types";
import { loadTerminalPreferences, normalizeTerminalPreferences, saveTerminalPreferences, type TerminalPreferences } from "../terminal/terminalPreferences";
import { attachTerminalClipboard } from "../terminal/terminalClipboard";
import { describeTerminalClipboardError, useTerminalClipboardPermission } from "../terminal/useTerminalClipboardPermission";
import { useMobileVisualViewport } from "../terminal/useMobileVisualViewport";
import {
  persistCommandSnippets,
  useCommandSnippets,
  useCommandSnippetReorder,
  useSharedCommandSnippetLibrary,
  type CommandSnippet,
  type SnippetAction,
} from "@/utils/commandSnippets";

type Location = "browser" | "shared" | "server";
type SessionStatus = "connecting" | "open" | "closed" | "error";
type SendMode = "text" | "hex";

interface SerialSettings {
  baudRate: number;
  dataBits: 7 | 8;
  stopBits: 1 | 2;
  parity: "none" | "even" | "odd";
  flowControl: "none" | "hardware";
}

interface BrowserPortItem {
  id: string;
  label: string;
  pluginTarget: string;
  port: SerialPort;
}

interface SerialSession {
  key: string;
  location: Location;
  portId: string;
  name: string;
  settings: SerialSettings;
  status: SessionStatus;
  error: string;
  closing: boolean;
  reconnecting: boolean;
  chunks: Uint8Array[];
  bufferedBytes: number;
  socket?: WebSocket;
  socketOpened: boolean;
  keepaliveTimer?: number;
  retryTimer?: number;
  viewers: number;
  writeEnabled: boolean;
  relaySocket?: WebSocket;
  relayKeepaliveTimer?: number;
  relayRetryTimer?: number;
  relayRetryAttempt?: number;
  shareId?: string;
  pluginTarget?: string;
  bridgeConnected: boolean;
  sharing: boolean;
  readOnlyHintShown: boolean;
  browserPort?: SerialPort;
  reader?: ReadableStreamDefaultReader<Uint8Array>;
  readTask?: Promise<void>;
  writeChain: Promise<void>;
}

interface SerialView {
  id: string;
  sessionKey: string;
  title: string;
  command: string;
  sendMode: SendMode;
  lineEnding: "none" | "lf" | "crlf";
  terminalView?: WebTerminalHandle;
  clipboardCleanup?: () => void;
  renderer: TerminalRenderer;
  searchResultIndex: number;
  searchResultCount: number;
  searchResultLimited: boolean;
  recording: boolean;
  recordingStartedAt?: string;
  recordingContent: string;
  recordingEntries: Array<{ at: Date; data: string }>;
  recordingDecoder?: TextDecoder;
  recordingStripAnsi: boolean;
  recordingTimestamps: boolean;
  recordingSizeBytes: number;
  recordingLimitReached: boolean;
}

const message = useMessage();
const { mobileViewportStyle } = useMobileVisualViewport();
const {
  clipboardPermissionState,
  clipboardPermissionLabel,
  refreshClipboardPermission,
  requestClipboardPermission,
  canReadClipboardAutomatically,
  markClipboardReadFailed,
  disposeClipboardPermission,
} = useTerminalClipboardPermission();
const dialogStyle = { width: "min(680px, calc(100vw - 32px))", maxHeight: "calc(100dvh - 32px)" };
const source = ref<Location>("server");
const browserSupported = "serial" in navigator;
const browserPorts = ref<BrowserPortItem[]>([]);
const browserPortId = ref<string | null>(null);
const serverPorts = ref<BackendSerialPort[]>([]);
const serverPortId = ref<string | null>(null);
const loadingServerPorts = ref(false);
const sharedPorts = ref<BrowserSerialShare[]>([]);
const sharedPortId = ref<string | null>(null);
const loadingSharedPorts = ref(false);
const sessions = reactive(new Map<string, SerialSession>());
const views = reactive<SerialView[]>([]);
const activeViewId = ref("");
const mobileSetup = ref(false);
const terminalSettings = reactive<TerminalPreferences>(loadTerminalPreferences());
const terminalSettingsDraft = reactive<TerminalPreferences>({ ...terminalSettings });
const showTerminalSettings = ref(false);
const showSearch = ref(false);
const searchQuery = ref("");
const searchCaseSensitive = ref(false);
const searchWholeWord = ref(false);
const searchRegex = ref(false);
const searchTargetIndex = ref<number | null>(null);
const searchIndexEditing = ref(false);
const searchInput = ref<{ focus: () => void } | null>(null);
const showSnippets = ref(false);
const snippets = useCommandSnippets();
const snippetDraft = reactive<{ name: string; command: string; pinned: boolean; action: SnippetAction }>({
  name: "",
  command: "",
  pinned: true,
  action: "insert",
});
const editingSnippetId = ref("");
const {
  draggedSnippetId,
  snippetDropTargetId,
  snippetDropPosition,
  startSnippetPointerDrag,
  updateSnippetPointerDrag,
  dropSnippetPointerDrag,
  cancelSnippetPointerDrag,
} = useCommandSnippetReorder(snippets, ".serial-snippet-row");
const snippetLibraryTab = ref<"local" | "shared">("local");
const sharedActionId = ref("");
const {
  librarySnippets,
  libraryLoading,
  refreshSharedSnippets,
  storeSharedSnippet,
  removeSharedSnippet,
} = useSharedCommandSnippetLibrary();
const showQuickSnippets = ref(localStorage.getItem("serial-show-quick-snippets") !== "false");
const showRecordingOptions = ref(false);
const recordingTargetId = ref("");
const recordingDraft = reactive({ stripAnsi: true, timestamps: false });
const browserIds = new WeakMap<SerialPort, string>();
let browserSequence = 0;
let searchInputTimer: number | undefined;
let sharedRefreshTimer: number | undefined;
let shareSettingsTimer: number | undefined;
let clipboardWarningShown = false;
let clipboardFallbackHintShown = false;
const sharedRefreshIntervalMs = 2000;
const snippetActionOptions = [
  { label: "插入发送框，可编辑后发送", value: "insert" },
  { label: "点击后立即发送", value: "run" },
];

const storedBrowserShareSettings = (() => {
  try { return JSON.parse(localStorage.getItem("space-station:browser-serial-share") || "{}"); }
  catch { return {}; }
})();
const browserShareSettings = reactive({
  enabled: storedBrowserShareSettings.enabled === true,
  name: String(storedBrowserShareSettings.name || "我的浏览器串口"),
  writeEnabled: storedBrowserShareSettings.writeEnabled === true,
});

const pinnedSnippets = computed(() => snippets.value.filter((snippet) => snippet.pinned !== false));
const mobileActionOptions = computed(() => {
  const view = activeView.value;
  const session = activeSession.value;
  if (!view || !session) return [];
  const options: Array<{ label: string; key: string; disabled?: boolean }> = [];
  if (session.status === "closed" || session.status === "error") options.push({ label: "重新连接", key: "reconnect" });
  options.push(
    { label: "搜索终端", key: "search" },
    { label: "命令片段", key: "snippets" },
    { label: view.recording ? "停止录制" : "开始录制", key: "recording" },
    { label: "终端设置", key: "settings" },
    { label: `位置：${locationText(session.location)}`, key: "location", disabled: true },
    { label: `状态：${statusText(session)}`, key: "status", disabled: true },
    { label: `渲染：${view.renderer === "webgl" ? "GPU" : "Canvas"}`, key: "renderer", disabled: true },
  );
  return options;
});

const storedSettings = (() => {
  try { return JSON.parse(localStorage.getItem("space-station:serial-settings") || "{}"); }
  catch { return {}; }
})();
const settings = reactive<SerialSettings>({
  baudRate: Number(storedSettings.baudRate) || 115200,
  dataBits: storedSettings.dataBits === 7 ? 7 : 8,
  stopBits: storedSettings.stopBits === 2 ? 2 : 1,
  parity: ["even", "odd"].includes(storedSettings.parity) ? storedSettings.parity : "none",
  flowControl: storedSettings.flowControl === "hardware" ? "hardware" : "none",
});

const baudOptions = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600].map((value) => ({ label: String(value), value }));
const dataBitsOptions = [7, 8].map((value) => ({ label: String(value), value }));
const stopBitsOptions = [1, 2].map((value) => ({ label: String(value), value }));
const parityOptions = [{ label: "无", value: "none" }, { label: "偶校验", value: "even" }, { label: "奇校验", value: "odd" }];
const flowOptions = [{ label: "无", value: "none" }, { label: "硬件 RTS/CTS", value: "hardware" }];
const sendModeOptions = [{ label: "文本", value: "text" }, { label: "HEX", value: "hex" }];
const lineEndingOptions = [{ label: "不追加", value: "none" }, { label: "LF", value: "lf" }, { label: "CRLF", value: "crlf" }];

const browserPortOptions = computed(() => browserPorts.value.map((item) => ({ label: item.label, value: item.id })));
const serverPortOptions = computed(() => serverPorts.value.map((item) => ({ label: item.name === item.path ? item.path : `${item.name} · ${item.path}`, value: item.id })));
const sharedPortOptions = computed(() => sharedPorts.value.map((item) => ({
  label: `${item.name} · ${item.portLabel}${item.writeEnabled ? " · 可写" : " · 只读"}`,
  value: item.id,
})));
const browserShareStatus = computed<{ kind: "online" | "pending" | "offline"; text: string }>(() => {
  const browserSessions = Array.from(sessions.values()).filter((session) => session.location === "browser" && session.status === "open" && !session.closing);
  const onlineCount = browserSessions.filter((session) => session.sharing).length;
  if (onlineCount > 0) {
    return { kind: "online", text: `${onlineCount} 个串口已发布，其他客户端将在 2 秒内看到` };
  }
  if (browserSessions.some((session) => session.relaySocket?.readyState === WebSocket.CONNECTING)) {
    return { kind: "pending", text: "正在发布共享…" };
  }
  if (browserSessions.length > 0) {
    return { kind: "offline", text: "共享通道暂时离线，正在自动重连…" };
  }
  return { kind: "pending", text: "请先打开浏览器串口，共享才会发布" };
});
const selectedPortAvailable = computed(() => source.value === "browser"
  ? Boolean(browserPortId.value)
  : source.value === "shared" ? Boolean(sharedPortId.value) : Boolean(serverPortId.value));
const activeView = computed(() => views.find((view) => view.id === activeViewId.value));
const activeSession = computed(() => activeView.value ? sessions.get(activeView.value.sessionKey) : undefined);
const activePluginContext = computed(() => {
  const session = activeSession.value;
  if (!session) return { transport: undefined, target: undefined, hint: "当前没有打开终端。" };
  if (session.location === "server") return { transport: "serial", target: session.portId, hint: "" };
  if (session.location === "shared") return { transport: "browser-serial", target: session.pluginTarget || session.portId, hint: "" };
  if (session.shareId && session.bridgeConnected) {
    return { transport: "browser-serial", target: session.pluginTarget || session.shareId, hint: "" };
  }
  return {
    transport: undefined,
    target: undefined,
    hint: "后端插件通道正在连接；本地串口不受影响。",
  };
});

function browserPortLabel(port: SerialPort, index: number) {
  const info = port.getInfo();
  const ids = info.usbVendorId === undefined ? "已授权设备" : `USB ${info.usbVendorId.toString(16).padStart(4, "0")}:${(info.usbProductId ?? 0).toString(16).padStart(4, "0")}`;
  return `${ids} · 串口 ${index + 1}`;
}

function browserPluginTarget(port: SerialPort, index: number) {
  const info = port.getInfo();
  if (info.usbVendorId !== undefined) {
    const vendor = info.usbVendorId.toString(16).padStart(4, "0");
    const product = (info.usbProductId ?? 0).toString(16).padStart(4, "0");
    return `browser-usb:${vendor}:${product}`;
  }
  return `browser-port:${index + 1}`;
}

function registerBrowserPorts(ports: SerialPort[]) {
  browserPorts.value = ports.map((port, index) => {
    let id = browserIds.get(port);
    if (!id) { id = `browser-${++browserSequence}`; browserIds.set(port, id); }
    return { id, label: browserPortLabel(port, index), pluginTarget: browserPluginTarget(port, index), port };
  });
  if (!browserPorts.value.some((item) => item.id === browserPortId.value)) browserPortId.value = browserPorts.value[0]?.id ?? null;
}

async function refreshBrowserPorts() {
  if (!navigator.serial) return;
  try { registerBrowserPorts(await navigator.serial.getPorts()); }
  catch (error) { message.error(errorMessage(error)); }
}

async function requestBrowserPort() {
  if (!navigator.serial) return;
  try {
    const selected = await navigator.serial.requestPort();
    const ports = await navigator.serial.getPorts();
    registerBrowserPorts(ports.includes(selected) ? ports : [...ports, selected]);
    browserPortId.value = browserIds.get(selected) ?? browserPortId.value;
  } catch (error) {
    if ((error as DOMException)?.name !== "NotFoundError") message.error(errorMessage(error));
  }
}

async function refreshServerPorts() {
  loadingServerPorts.value = true;
  try {
    const result = await fetchBackendSerialPorts();
    serverPorts.value = result.ports;
    if (!serverPorts.value.some((item) => item.id === serverPortId.value)) serverPortId.value = serverPorts.value[0]?.id ?? null;
  } catch (error) { message.error(`读取服务器串口失败：${errorMessage(error)}`); }
  finally { loadingServerPorts.value = false; }
}

async function refreshSharedPorts() {
  if (loadingSharedPorts.value) return;
  loadingSharedPorts.value = true;
  try {
    const result = await fetchBrowserSerialShares();
    const ownedShareIds = new Set(Array.from(sessions.values())
      .filter((session) => session.location === "browser" && session.shareId)
      .map((session) => session.shareId!));
    sharedPorts.value = result.shares.filter((item) => !ownedShareIds.has(item.id));
    if (!sharedPorts.value.some((item) => item.id === sharedPortId.value)) sharedPortId.value = sharedPorts.value[0]?.id ?? null;
  } catch (error) { message.error(`读取浏览器串口共享失败：${errorMessage(error)}`); }
  finally { loadingSharedPorts.value = false; }
}

function refreshSharedPortsWhenVisible() {
  if (source.value === "shared" && document.visibilityState === "visible") void refreshSharedPorts();
}

function settingsMatch(left: SerialSettings, right: SerialSettings) {
  return left.baudRate === right.baudRate && left.dataBits === right.dataBits && left.stopBits === right.stopBits && left.parity === right.parity && left.flowControl === right.flowControl;
}

async function openSelectedPort() {
  const portId = source.value === "browser" ? browserPortId.value : source.value === "shared" ? sharedPortId.value : serverPortId.value;
  if (!portId) return;
  const key = `${source.value}:${portId}`;
  const existing = sessions.get(key);
  if (existing) {
    const existingView = views.find((view) => view.sessionKey === existing.key);
    if (existingView) activateView(existingView.id);
    if (existing.location !== "shared" && !settingsMatch(existing.settings, settings)) {
      message.warning(`已切换到现有终端；该串口使用 ${existing.settings.baudRate}-${existing.settings.dataBits}-${existing.settings.parity}，关闭后才能修改参数。`);
      return;
    }
    return;
  }

  const browserItem = browserPorts.value.find((item) => item.id === portId);
  const serverItem = serverPorts.value.find((item) => item.id === portId);
  const sharedItem = sharedPorts.value.find((item) => item.id === portId);
  const session = reactive<SerialSession>({
    key,
    location: source.value,
    portId,
    name: browserItem?.label ?? sharedItem?.name ?? serverItem?.name ?? portId,
    settings: { ...settings },
    status: "connecting",
    error: "",
    closing: false,
    reconnecting: false,
    chunks: [],
    bufferedBytes: 0,
    socketOpened: false,
    viewers: 0,
    writeEnabled: sharedItem?.writeEnabled ?? true,
    bridgeConnected: false,
    sharing: false,
    readOnlyHintShown: false,
    shareId: source.value === "browser" ? crypto.randomUUID() : sharedItem?.id,
    pluginTarget: browserItem?.pluginTarget ?? sharedItem?.pluginTarget,
    browserPort: browserItem ? markRaw(browserItem.port) : undefined,
    writeChain: Promise.resolve(),
  });
  sessions.set(key, session);
  await addView(session);
  void startSession(session);
}

async function startSession(session: SerialSession) {
  session.status = "connecting";
  session.error = "";
  session.closing = false;
  try {
    if (session.location === "browser") await openBrowserSession(session);
    else if (session.location === "shared") openSharedSession(session);
    else openServerSession(session);
  } catch (error) {
    failSession(session, errorMessage(error));
  }
}

async function openBrowserSession(session: SerialSession) {
  if (!session.browserPort) throw new Error("浏览器串口授权已失效，请重新选择设备。");
  await session.browserPort.open({ ...session.settings, bufferSize: 1024 * 1024 });
  session.status = "open";
  broadcastNotice(session, `\r\n\x1b[36m[已连接浏览器串口 ${session.name}]\x1b[0m\r\n`);
  if (session.shareId) openBrowserPluginBridge(session);
  const readTask = (async () => {
    if (!session.browserPort?.readable) return;
    const reader = session.browserPort.readable.getReader();
    session.reader = markRaw(reader);
    try {
      while (!session.closing) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value?.byteLength) {
          receiveData(session, value);
          if (session.relaySocket?.readyState === WebSocket.OPEN) session.relaySocket.send(value);
        }
      }
    } finally {
      reader.releaseLock();
      if (session.reader === reader) session.reader = undefined;
    }
  })();
  session.readTask = readTask;
  await readTask;
  if (session.readTask === readTask) session.readTask = undefined;
  if (!session.closing) {
    closeBrowserPluginBridge(session);
    session.status = "closed";
    broadcastNotice(session, "\r\n\x1b[33m[串口读取已结束，按任意键重连]\x1b[0m\r\n");
  }
}

function websocketUrl(path: string) {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  return `${protocol}//${window.location.host}${path}`;
}

function openBrowserPluginBridge(session: SerialSession) {
  if (!session.shareId) return;
  if (session.relaySocket?.readyState === WebSocket.OPEN || session.relaySocket?.readyState === WebSocket.CONNECTING) return;
  window.clearTimeout(session.relayRetryTimer);
  session.relayRetryTimer = undefined;
  const socket = markRaw(new WebSocket(websocketUrl("/api/tools/serial/browser-share")));
  socket.binaryType = "arraybuffer";
  session.relaySocket = socket;
  socket.onopen = () => {
    socket.send(JSON.stringify({
      type: "publish",
      shareId: session.shareId,
      name: browserShareSettings.name.trim() || session.name,
      portLabel: session.name,
      writeEnabled: browserShareSettings.writeEnabled,
      discoverable: browserShareSettings.enabled,
      pluginTarget: session.pluginTarget || session.shareId,
    }));
    window.clearInterval(session.relayKeepaliveTimer);
    session.relayKeepaliveTimer = window.setInterval(() => {
      if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "ping", at: Date.now() }));
    }, 15000);
  };
  socket.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) {
      void writeSession(session, new Uint8Array(event.data)).catch((error) => broadcastNotice(session, `\r\n\x1b[31m[插件或远程写入失败：${errorMessage(error)}]\x1b[0m\r\n`));
      return;
    }
    if (event.data instanceof Blob) {
      void event.data.arrayBuffer().then((data) => writeSession(session, new Uint8Array(data))).catch(() => undefined);
      return;
    }
    try {
      const payload = JSON.parse(String(event.data));
      if (payload.type === "published") {
        session.bridgeConnected = true;
        session.sharing = payload.discoverable === true;
        session.relayRetryAttempt = 0;
        if (session.sharing) broadcastNotice(session, `\r\n\x1b[36m[已共享为“${browserShareSettings.name.trim() || session.name}” · ${browserShareSettings.writeEnabled ? "允许远程写入" : "只读"}]\x1b[0m\r\n`);
      } else if (payload.type === "share-state") {
        const wasSharing = session.sharing;
        session.bridgeConnected = true;
        session.sharing = payload.discoverable === true;
        session.viewers = Number(payload.viewers) || 0;
        if (!wasSharing && session.sharing) {
          broadcastNotice(session, `\r\n\x1b[36m[已共享为“${browserShareSettings.name.trim() || session.name}” · ${browserShareSettings.writeEnabled ? "允许远程写入" : "只读"}]\x1b[0m\r\n`);
        } else if (wasSharing && !session.sharing) {
          broadcastNotice(session, "\r\n\x1b[33m[已停止客户端共享，后端插件继续运行]\x1b[0m\r\n");
        }
      } else if (payload.type === "error") {
        session.bridgeConnected = false;
        session.sharing = false;
        broadcastNotice(session, `\r\n\x1b[31m[后端插件通道失败：${payload.message || "未知错误"}；本地串口仍可使用]\x1b[0m\r\n`);
      }
    } catch { /* Ignore non-protocol text. */ }
  };
  socket.onerror = () => { session.bridgeConnected = false; session.sharing = false; };
  socket.onclose = () => {
    window.clearInterval(session.relayKeepaliveTimer);
    session.relayKeepaliveTimer = undefined;
    if (session.relaySocket === socket) session.relaySocket = undefined;
    const wasSharing = session.sharing;
    session.bridgeConnected = false;
    if (wasSharing && !session.closing) broadcastNotice(session, "\r\n\x1b[33m[浏览器串口共享通道已断开，本地串口仍可使用]\x1b[0m\r\n");
    session.sharing = false;
    if (!session.closing && session.status === "open") {
      const attempt = (session.relayRetryAttempt ?? 0) + 1;
      session.relayRetryAttempt = attempt;
      const delay = Math.min(10000, 1000 * (2 ** Math.min(attempt - 1, 3)));
      session.relayRetryTimer = window.setTimeout(() => {
        session.relayRetryTimer = undefined;
        if (!session.closing && session.status === "open") openBrowserPluginBridge(session);
      }, delay);
    }
  };
}

function openSharedSession(session: SerialSession) {
  const socket = markRaw(new WebSocket(websocketUrl("/api/tools/serial/browser-share")));
  socket.binaryType = "arraybuffer";
  session.socket = socket;
  session.socketOpened = false;
  socket.onopen = () => {
    session.socketOpened = true;
    socket.send(JSON.stringify({ type: "subscribe", shareId: session.portId }));
    window.clearInterval(session.keepaliveTimer);
    session.keepaliveTimer = window.setInterval(() => {
      if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "ping", at: Date.now() }));
    }, 15000);
  };
  socket.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) { receiveData(session, new Uint8Array(event.data)); return; }
    if (event.data instanceof Blob) { void event.data.arrayBuffer().then((data) => receiveData(session, new Uint8Array(data))); return; }
    try {
      const payload = JSON.parse(String(event.data));
      if (payload.type === "subscribed") {
        session.status = "open";
        session.writeEnabled = payload.writeEnabled === true;
        session.viewers = Number(payload.viewers) || 1;
        broadcastNotice(session, `\r\n\x1b[36m[已连接浏览器共享串口“${session.name}” · ${session.writeEnabled ? "可写" : "只读"}]\x1b[0m\r\n`);
      } else if (payload.type === "share-state") {
        session.viewers = Number(payload.viewers) || session.viewers;
        session.writeEnabled = payload.writeEnabled === true;
        if (session.writeEnabled) session.readOnlyHintShown = false;
      } else if (payload.type === "write-error") {
        message.warning(payload.message || "共享串口写入失败");
      } else if (payload.type === "unavailable") {
        failSession(session, payload.message || "浏览器串口共享已经离线");
      } else if (payload.type === "error") failSession(session, payload.message || "浏览器串口共享连接失败");
    } catch { /* Ignore non-protocol text. */ }
  };
  socket.onerror = () => {
    if (!session.error) failSession(session, session.socketOpened ? "浏览器串口共享连接异常中断。" : "浏览器串口共享 WebSocket 握手失败。");
  };
  socket.onclose = () => {
    window.clearInterval(session.keepaliveTimer);
    session.keepaliveTimer = undefined;
    if (!session.closing && session.status !== "error") {
      session.status = "closed";
      broadcastNotice(session, "\r\n\x1b[33m[浏览器串口共享连接已关闭，按任意键重连]\x1b[0m\r\n");
    }
  };
}

function openServerSession(session: SerialSession, attempt = 0) {
  const socket = markRaw(new WebSocket(websocketUrl("/api/tools/serial/session")));
  let transportError = false;
  socket.binaryType = "arraybuffer";
  session.socket = socket;
  session.socketOpened = false;
  window.clearTimeout(session.retryTimer);
  session.retryTimer = undefined;
  socket.onopen = () => {
    session.socketOpened = true;
    socket.send(JSON.stringify({ type: "open", port: session.portId, options: {
      baudRate: session.settings.baudRate,
      dataBits: session.settings.dataBits,
      stopBits: session.settings.stopBits,
      parity: session.settings.parity,
      flowControl: session.settings.flowControl,
    } }));
    window.clearInterval(session.keepaliveTimer);
    session.keepaliveTimer = window.setInterval(() => {
      if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "ping", at: Date.now() }));
    }, 15000);
  };
  socket.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) { receiveData(session, new Uint8Array(event.data)); return; }
    if (event.data instanceof Blob) { void event.data.arrayBuffer().then((data) => receiveData(session, new Uint8Array(data))); return; }
    try {
      const payload = JSON.parse(String(event.data));
      if (payload.type === "opened") {
        session.status = "open";
        session.viewers = Number(payload.viewers) || 1;
        broadcastNotice(session, `\r\n\x1b[36m[已连接服务器串口 ${session.name} · ${payload.viewers ?? 1} 个订阅]\x1b[0m\r\n`);
      } else if (payload.type === "state" && payload.status) {
        session.status = payload.status === "open" ? "open" : "closed";
        session.viewers = Number(payload.viewers) || session.viewers;
      } else if (payload.type === "error") failSession(session, payload.message || "服务器串口连接失败");
    } catch { /* Ignore non-protocol text. */ }
  };
  socket.onerror = () => {
    transportError = true;
    if (session.socketOpened && !session.error) failSession(session, "串口 WebSocket 连接异常中断，请检查网络或后端日志。");
  };
  socket.onclose = (event) => {
    window.clearInterval(session.keepaliveTimer);
    session.keepaliveTimer = undefined;
    if (session.socket !== socket || session.closing) return;
    if (!session.socketOpened && attempt < 2) {
      session.status = "connecting";
      session.error = "";
      const delay = attempt === 0 ? 350 : 1000;
      session.retryTimer = window.setTimeout(() => {
        session.retryTimer = undefined;
        if (!session.closing) openServerSession(session, attempt + 1);
      }, delay);
      return;
    }
    if (!session.socketOpened) {
      failSession(session, `串口 WebSocket 握手失败（关闭代码 ${event.code}${transportError ? "，网络错误" : ""}），请检查访问地址、客户端证书或后端状态。`);
      return;
    }
    if (!session.closing && session.status !== "error") {
      session.status = "closed";
      const detail = event.reason ? `：${event.reason}` : event.code !== 1000 ? `（代码 ${event.code}）` : "";
      broadcastNotice(session, `\r\n\x1b[33m[服务器串口连接已关闭${detail}，按任意键重连]\x1b[0m\r\n`);
    }
  };
}

function failSession(session: SerialSession, reason: string) {
  session.status = "error";
  session.error = reason;
  broadcastNotice(session, `\r\n\x1b[31m[连接失败：${reason}]\x1b[0m\r\n\x1b[33m[按任意键重连]\x1b[0m\r\n`);
}

function receiveData(session: SerialSession, input: Uint8Array) {
  const data = input.slice();
  session.chunks.push(data);
  session.bufferedBytes += data.byteLength;
  while (session.bufferedBytes > 512 * 1024 && session.chunks.length > 1) {
    const removed = session.chunks.shift();
    session.bufferedBytes -= removed?.byteLength ?? 0;
  }
  views.filter((view) => view.sessionKey === session.key).forEach((view) => {
    view.terminalView?.write(data);
    appendRecording(view, data);
  });
}

function broadcastNotice(session: SerialSession, text: string) {
  views.filter((view) => view.sessionKey === session.key).forEach((view) => view.terminalView?.write(text));
}

async function writeSession(session: SerialSession, data: Uint8Array) {
  if (session.status !== "open") throw new Error("串口尚未连接。");
  session.writeChain = session.writeChain.then(async () => {
    if (session.location === "server" || session.location === "shared") {
      if (session.socket?.readyState !== WebSocket.OPEN) throw new Error("服务器串口连接已断开。");
      if (session.location === "shared" && !session.writeEnabled) throw new Error("该浏览器共享串口为只读，拥有者未允许远程写入。");
      session.socket.send(data);
      return;
    }
    if (!session.browserPort?.writable) throw new Error("浏览器串口不可写。");
    const writer = session.browserPort.writable.getWriter();
    try { await writer.write(data); }
    finally { writer.releaseLock(); }
  });
  await session.writeChain.catch((error) => {
    session.writeChain = Promise.resolve();
    throw error;
  });
}

async function addView(session: SerialSession) {
  const view = reactive<SerialView>({
    id: crypto.randomUUID(),
    sessionKey: session.key,
    title: session.name,
    command: "",
    sendMode: "text",
    lineEnding: "none",
    renderer: "canvas",
    searchResultIndex: -1,
    searchResultCount: 0,
    searchResultLimited: false,
    recording: false,
    recordingContent: "",
    recordingEntries: [],
    recordingStripAnsi: true,
    recordingTimestamps: false,
    recordingSizeBytes: 0,
    recordingLimitReached: false,
  });
  views.push(view);
  activeViewId.value = view.id;
  mobileSetup.value = false;
  await nextTick();
}

function handleTerminalReady(view: SerialView, event: WebTerminalReadyEvent) {
  const session = sessions.get(view.sessionKey);
  if (!session) return;
  view.terminalView = event.handle;
  event.terminal.attachCustomKeyEventHandler((keyboardEvent) => keyboardEvent.key !== "F12");
  view.clipboardCleanup?.();
  view.clipboardCleanup = attachTerminalClipboard(event.terminal, event.element, {
    copyOnSelect: () => terminalSettings.copyOnSelect,
    pasteOnRightClick: () => terminalSettings.pasteOnRightClick,
    canPaste: () => session.status === "open",
    canReadClipboard: canReadClipboardAutomatically,
    onCopyError: () => warnClipboardAccess("浏览器不允许自动写入剪贴板，请检查站点权限"),
    onPasteUnavailable: () => {
      if (clipboardFallbackHintShown) return;
      clipboardFallbackHintShown = true;
      message.info("未获得自动读取权限，已保留浏览器原生右键菜单");
    },
    onPasteError: (error) => {
      markClipboardReadFailed();
      message.error(`${describeTerminalClipboardError(error)}；下次右键将显示原生菜单`);
    },
  });
  session.chunks.forEach((chunk) => event.terminal.write(chunk));
}

function handleTerminalData(view: SerialView, data: string) {
  const session = sessions.get(view.sessionKey);
  if (!session) return;
  if (session.status === "closed" || session.status === "error") {
    void reconnectSession(session);
    return;
  }
  if (session.status !== "open") return;
  if (session.location === "shared" && !session.writeEnabled) {
    if (!session.readOnlyHintShown) {
      session.readOnlyHintShown = true;
      message.info("该浏览器串口共享为只读，拥有者需要开启“允许其他客户端写入”");
    }
    return;
  }
  void writeSession(session, new TextEncoder().encode(data)).catch((error) => message.error(errorMessage(error)));
}

function activateView(viewId: string) {
  activeViewId.value = viewId;
  mobileSetup.value = false;
  nextTick(() => views.find((view) => view.id === viewId)?.terminalView?.fit());
}

async function closeView(viewId: string) {
  const index = views.findIndex((view) => view.id === viewId);
  if (index < 0) return;
  const [view] = views.splice(index, 1);
  view.clipboardCleanup?.();
  if (!views.some((candidate) => candidate.sessionKey === view.sessionKey)) {
    const session = sessions.get(view.sessionKey);
    if (session) await closeSession(session);
    sessions.delete(view.sessionKey);
  }
  if (activeViewId.value === viewId) activeViewId.value = views[Math.min(index, views.length - 1)]?.id ?? "";
}

async function closeSession(session: SerialSession) {
  session.closing = true;
  session.status = "closed";
  if (session.location === "server" || session.location === "shared") {
    window.clearInterval(session.keepaliveTimer);
    session.keepaliveTimer = undefined;
    window.clearTimeout(session.retryTimer);
    session.retryTimer = undefined;
    const socket = session.socket;
    if (session.location === "server" && socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "close" }));
    if (socket) {
      socket.onopen = null;
      socket.onmessage = null;
      socket.onerror = null;
      socket.onclose = null;
      socket.close();
    }
    session.socket = undefined;
    return;
  }
  closeBrowserPluginBridge(session);
  await session.reader?.cancel().catch(() => undefined);
  await session.readTask?.catch(() => undefined);
  await session.browserPort?.close().catch(() => undefined);
}

function closeBrowserPluginBridge(session: SerialSession) {
  window.clearInterval(session.relayKeepaliveTimer);
  session.relayKeepaliveTimer = undefined;
  window.clearTimeout(session.relayRetryTimer);
  session.relayRetryTimer = undefined;
  session.relayRetryAttempt = 0;
  const relay = session.relaySocket;
  if (relay) {
    relay.onopen = null;
    relay.onmessage = null;
    relay.onerror = null;
    relay.onclose = null;
    relay.close();
  }
  session.relaySocket = undefined;
  session.bridgeConnected = false;
  session.sharing = false;
  session.viewers = 0;
}

async function reconnectActive() {
  const session = activeSession.value;
  if (!session) return;
  await reconnectSession(session);
}

async function reconnectSession(session: SerialSession) {
  if (session.reconnecting || session.status === "open") return;
  session.reconnecting = true;
  session.error = "";
  broadcastNotice(session, "\r\n\x1b[36m[正在重新连接串口…]\x1b[0m\r\n");
  try {
    const closing = closeSession(session);
    session.status = "connecting";
    await closing;
    await startSession(session);
  } finally {
    session.reconnecting = false;
  }
}

function sessionFor(view: SerialView) { return sessions.get(view.sessionKey); }
function statusText(session: SerialSession) {
  if (session.status === "connecting") return "连接中";
  if (session.status === "error") return "连接失败";
  if (session.status === "closed") return "已断开";
  if (session.location === "server") return `已连接 · ${session.viewers || 1} 个客户端`;
  if (session.location === "shared") return `已连接 · ${session.viewers || 1} 个订阅`;
  return session.sharing ? `已连接 · 已共享 · ${session.viewers} 个远程客户端` : "已连接";
}

function locationText(location: Location) {
  return location === "browser" ? "本机浏览器" : location === "shared" ? "浏览器共享" : "服务器";
}

function canWrite(view: SerialView) {
  const session = sessions.get(view.sessionKey);
  return session?.status === "open" && (session.location !== "shared" || session.writeEnabled);
}

function handleMobileAction(key: string) {
  const view = activeView.value;
  if (!view) return;
  if (key === "reconnect") void reconnectActive();
  else if (key === "search") openSearch();
  else if (key === "snippets") showSnippets.value = true;
  else if (key === "recording") toggleRecording(view);
  else if (key === "settings") openTerminalSettings();
}

async function sendFromComposer(view: SerialView) {
  const session = sessions.get(view.sessionKey);
  if (!session || !view.command) return;
  try {
    let data: Uint8Array;
    if (view.sendMode === "hex") {
      const compact = view.command.replace(/(?:0x)?([0-9a-f]{2})/gi, "$1").replace(/[\s,;:-]/g, "");
      if (!compact || compact.length % 2 || !/^[0-9a-f]+$/i.test(compact)) throw new Error("HEX 内容必须由完整的两位十六进制字节组成。");
      data = Uint8Array.from(compact.match(/.{2}/g)!.map((part) => Number.parseInt(part, 16)));
    } else {
      const ending = view.lineEnding === "lf" ? "\n" : view.lineEnding === "crlf" ? "\r\n" : "";
      data = new TextEncoder().encode(view.command + ending);
    }
    await writeSession(session, data);
    view.command = "";
  } catch (error) { message.error(errorMessage(error)); }
}

function errorMessage(error: unknown) {
  if (error instanceof Error) return error.message;
  return String(error);
}

function warnClipboardAccess(content: string) {
  if (clipboardWarningShown) return;
  clipboardWarningShown = true;
  message.warning(content);
}

async function requestClipboardAccess() {
  try {
    const state = await requestClipboardPermission();
    clipboardFallbackHintShown = false;
    message.success(state === "granted"
      ? "剪贴板读取权限已允许，右键可以自动粘贴"
      : "剪贴板读取成功；此浏览器仍会使用原生右键菜单按次粘贴");
  } catch (error) {
    await refreshClipboardPermission();
    message.error(describeTerminalClipboardError(error));
  }
}

function updateSearchResults(view: SerialView, event: WebTerminalSearchResult) {
  view.searchResultIndex = event.resultIndex;
  view.searchResultCount = event.resultCount;
  view.searchResultLimited = event.limited;
  if (view.id === activeViewId.value && !searchIndexEditing.value) searchTargetIndex.value = event.resultIndex >= 0 ? event.resultIndex + 1 : null;
}

function openSearch() {
  showSearch.value = true;
  searchTargetIndex.value = activeView.value && activeView.value.searchResultIndex >= 0 ? activeView.value.searchResultIndex + 1 : null;
  nextTick(() => { searchInput.value?.focus(); if (searchQuery.value) searchTerminal(true, true); });
}

function closeSearch() {
  if (searchInputTimer) window.clearTimeout(searchInputTimer);
  showSearch.value = false;
  searchQuery.value = "";
  views.forEach((view) => {
    view.terminalView?.clearSearch();
    view.searchResultIndex = -1;
    view.searchResultCount = 0;
    view.searchResultLimited = false;
  });
  searchTargetIndex.value = null;
  activeView.value?.terminalView?.focus();
}

function searchTerminal(previous: boolean, incremental = false) {
  if (searchInputTimer) { window.clearTimeout(searchInputTimer); searchInputTimer = undefined; }
  if (!activeView.value || !searchQuery.value) return;
  activeView.value.terminalView?.search(
    searchQuery.value,
    previous,
    incremental,
    searchCaseSensitive.value,
    searchWholeWord.value,
    searchRegex.value,
  );
}

function jumpToSearchIndex() {
  const view = activeView.value;
  const target = Math.trunc(Number(searchTargetIndex.value));
  if (!view || !Number.isFinite(target) || target < 1 || target > view.searchResultCount) {
    message.warning(view?.searchResultCount ? `请输入 1 到 ${view.searchResultCount} 之间的序号` : "当前没有搜索结果");
    return;
  }
  view.terminalView?.jumpToSearchIndex(target - 1);
}

function searchCountText(view: SerialView) {
  const current = view.searchResultIndex >= 0 ? view.searchResultIndex + 1 : view.searchResultCount ? "?" : 0;
  return `${current}/${view.searchResultCount}`;
}

function handleGlobalShortcut(event: KeyboardEvent) {
  if (!(event.ctrlKey || event.metaKey) || event.key.toLowerCase() !== "f" || !activeView.value) return;
  event.preventDefault();
  event.stopPropagation();
  if (showSearch.value) closeSearch(); else openSearch();
}

function openTerminalSettings() {
  Object.assign(terminalSettingsDraft, terminalSettings);
  showTerminalSettings.value = true;
}

function saveTerminalSettings() {
  Object.assign(terminalSettings, saveTerminalPreferences(normalizeTerminalPreferences(terminalSettingsDraft)));
  views.forEach((view) => view.terminalView?.setAppearance(terminalSettings));
  showTerminalSettings.value = false;
  message.success("终端设置已同步应用到 SSH 与串口；回滚行数对新终端生效");
}

function toggleCommandComposer() {
  terminalSettings.showCommandComposer = !terminalSettings.showCommandComposer;
  saveTerminalPreferences(terminalSettings);
  nextTick(() => activeView.value?.terminalView?.fit());
}

function persistSnippets() { persistCommandSnippets(snippets.value); }

function snippetContentsMatch(left: Pick<CommandSnippet, "name" | "command" | "action">, right: Pick<CommandSnippet, "name" | "command" | "action">) {
  return left.name === right.name && left.command === right.command && left.action === right.action;
}

function localShareStatus(snippet: CommandSnippet) {
  const shared = librarySnippets.value.find((item) => item.id === snippet.id);
  if (!shared) return "存入共享";
  return snippetContentsMatch(snippet, shared) ? "已共享" : "更新共享";
}

function sharedImportStatus(snippet: SharedCommandSnippet) {
  const local = snippets.value.find((item) => item.id === snippet.id)
    ?? snippets.value.find((item) => snippetContentsMatch(item, snippet));
  if (!local) return "添加到本地";
  return snippetContentsMatch(local, snippet) ? "已在本地" : "更新本地";
}

async function loadSharedSnippetLibrary(force = false) {
  try {
    await refreshSharedSnippets(force);
  } catch (error) {
    message.error(`共享片段读取失败：${errorMessage(error)}`);
  }
}

async function storeSnippetInLibrary(snippet: CommandSnippet) {
  const duplicate = librarySnippets.value.find((item) => item.id !== snippet.id && snippetContentsMatch(item, snippet));
  if (duplicate) return message.info(`共享库已有相同片段“${duplicate.name}”`);
  const updating = librarySnippets.value.some((item) => item.id === snippet.id);
  sharedActionId.value = snippet.id;
  try {
    await storeSharedSnippet(snippet);
    message.success(updating ? "共享片段已更新" : "片段已存入共享库");
  } catch (error) {
    message.error(`共享片段保存失败：${errorMessage(error)}`);
  } finally {
    sharedActionId.value = "";
  }
}

function addSharedSnippetToLocal(snippet: SharedCommandSnippet) {
  const localIndex = snippets.value.findIndex((item) => item.id === snippet.id);
  if (localIndex >= 0) {
    const local = snippets.value[localIndex];
    snippets.value[localIndex] = { ...snippet, pinned: local.pinned !== false };
    message.success("本地片段已更新");
  } else {
    const duplicate = snippets.value.find((item) => snippetContentsMatch(item, snippet));
    if (duplicate) return message.info(`本地已有相同片段“${duplicate.name}”`);
    snippets.value.push({ ...snippet, pinned: true });
    message.success("已添加到本地片段");
  }
  persistSnippets();
}

async function deleteSnippetFromLibrary(id: string) {
  sharedActionId.value = id;
  try {
    await removeSharedSnippet(id);
    message.success("已从共享库删除；本地片段不受影响");
  } catch (error) {
    message.error(`共享片段删除失败：${errorMessage(error)}`);
  } finally {
    sharedActionId.value = "";
  }
}

function resetSnippetDraft() {
  editingSnippetId.value = "";
  Object.assign(snippetDraft, { name: "", command: "", pinned: true, action: "insert" });
}

function saveSnippet() {
  if (!snippetDraft.name.trim() || !snippetDraft.command.trim()) { message.warning("名称和内容不能为空"); return; }
  const value: CommandSnippet = {
    id: editingSnippetId.value || crypto.randomUUID(),
    name: snippetDraft.name.trim(),
    command: snippetDraft.command,
    pinned: snippetDraft.pinned,
    action: snippetDraft.action,
  };
  if (editingSnippetId.value) snippets.value = snippets.value.map((item) => item.id === editingSnippetId.value ? value : item);
  else snippets.value.push(value);
  persistSnippets();
  resetSnippetDraft();
}

function editSnippet(snippet: CommandSnippet) {
  editingSnippetId.value = snippet.id;
  Object.assign(snippetDraft, {
    name: snippet.name,
    command: snippet.command,
    pinned: snippet.pinned !== false,
    action: snippet.action,
  });
}

function deleteSnippet(id: string) {
  snippets.value = snippets.value.filter((item) => item.id !== id);
  persistSnippets();
  if (editingSnippetId.value === id) resetSnippetDraft();
}

async function useSnippet(view: SerialView, snippet: CommandSnippet) {
  view.command = snippet.command;
  showSnippets.value = false;
  if (snippet.action === "run") await sendFromComposer(view);
}

function appendRecording(view: SerialView, data: Uint8Array) {
  if (!view.recording) return;
  const limit = terminalSettings.recordingMaxMiB * 1024 * 1024;
  if (view.recordingSizeBytes + data.byteLength > limit) {
    if (!view.recordingLimitReached) message.warning(`录制缓冲区已达到 ${terminalSettings.recordingMaxMiB} MiB 上限`);
    view.recordingLimitReached = true;
    return;
  }
  const decoded = view.recordingDecoder?.decode(data, { stream: true }) ?? new TextDecoder().decode(data);
  view.recordingContent += decoded;
  view.recordingEntries.push({ at: new Date(), data: decoded });
  view.recordingSizeBytes += data.byteLength;
}

function toggleRecording(view: SerialView) {
  if (!view.recording) {
    recordingTargetId.value = view.id;
    Object.assign(recordingDraft, { stripAnsi: true, timestamps: false });
    showRecordingOptions.value = true;
    return;
  }
  stopRecording(view);
}

function startRecording() {
  const view = views.find((item) => item.id === recordingTargetId.value);
  if (!view) return;
  view.recording = true;
  view.recordingStartedAt = new Date().toISOString();
  view.recordingContent = "";
  view.recordingEntries = [];
  view.recordingDecoder = new TextDecoder();
  view.recordingStripAnsi = recordingDraft.stripAnsi;
  view.recordingTimestamps = recordingDraft.timestamps;
  view.recordingSizeBytes = 0;
  view.recordingLimitReached = false;
  showRecordingOptions.value = false;
}

function stopRecording(view: SerialView) {
  view.recording = false;
  const tail = view.recordingDecoder?.decode() ?? "";
  if (tail) { view.recordingContent += tail; view.recordingEntries.push({ at: new Date(), data: tail }); }
  const clean = (value: string) => normalizeRecordingText(view.recordingStripAnsi ? stripAnsi(value) : value);
  const body = view.recordingTimestamps
    ? view.recordingEntries.map((entry) => `[${formatLogTime(entry.at)}] ${clean(entry.data)}`).join("")
    : clean(view.recordingContent);
  const startedAt = view.recordingStartedAt ? new Date(view.recordingStartedAt) : new Date();
  downloadText(`${safeFilename(view.title)}-${formatFilenameTime(startedAt)}.log`, body);
  view.recordingContent = "";
  view.recordingEntries = [];
  view.recordingDecoder = undefined;
  view.recordingSizeBytes = 0;
}

function stripAnsi(value: string) {
  return value.replace(/[\u001B\u009B][[\]()#;?]*(?:(?:(?:[a-zA-Z\d]*(?:;[-a-zA-Z\d/#&.:=?%@~_]+)*)?\u0007)|(?:(?:\d{1,4}(?:[;:]\d{0,4})*)?[\dA-PR-TZcf-nq-uy=><~]))/g, "");
}
function normalizeRecordingText(value: string) { return value.replace(/\r\n/g, "\n").replace(/\r/g, ""); }
function formatLogTime(value: Date) { return value.toISOString(); }
function formatFilenameTime(value: Date) { return value.toISOString().replace(/:/g, "-"); }
function safeFilename(value: string) { return value.replace(/[\\/:*?"<>|]/g, "_"); }
function downloadText(filename: string, content: string) {
  const url = URL.createObjectURL(new Blob([content], { type: "text/plain;charset=utf-8" }));
  const link = document.createElement("a"); link.href = url; link.download = filename; link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

watch(settings, (value) => localStorage.setItem("space-station:serial-settings", JSON.stringify(value)), { deep: true });
watch(browserShareSettings, (value) => {
  localStorage.setItem("space-station:browser-serial-share", JSON.stringify(value));
  window.clearTimeout(shareSettingsTimer);
  shareSettingsTimer = window.setTimeout(() => {
    sessions.forEach((session) => {
      if (session.location !== "browser" || session.status !== "open" || session.closing) return;
      if (session.bridgeConnected && session.relaySocket?.readyState === WebSocket.OPEN) {
        session.relaySocket.send(JSON.stringify({
          type: "update",
          name: browserShareSettings.name.trim() || session.name,
          writeEnabled: browserShareSettings.writeEnabled,
          discoverable: browserShareSettings.enabled,
        }));
      } else {
        session.shareId ||= crypto.randomUUID();
        openBrowserPluginBridge(session);
      }
    });
  }, 250);
}, { deep: true });
watch(source, (value) => {
  window.clearInterval(sharedRefreshTimer);
  sharedRefreshTimer = undefined;
  if (value === "server") void refreshServerPorts();
  else if (value === "shared") {
    void refreshSharedPorts();
    sharedRefreshTimer = window.setInterval(refreshSharedPortsWhenVisible, sharedRefreshIntervalMs);
  }
  else void refreshBrowserPorts();
});
watch(showQuickSnippets, (value) => localStorage.setItem("serial-show-quick-snippets", String(value)));
watch(showSnippets, (visible) => {
  if (visible) void loadSharedSnippetLibrary(true);
});
watch([searchQuery, searchCaseSensitive, searchWholeWord, searchRegex], ([value]) => {
  if (!showSearch.value) return;
  if (searchInputTimer) window.clearTimeout(searchInputTimer);
  if (!value) { activeView.value?.terminalView?.clearSearch(); return; }
  searchInputTimer = window.setTimeout(() => searchTerminal(true, true), 200);
});

onMounted(() => {
  void refreshServerPorts();
  void refreshBrowserPorts();
  void refreshSharedPorts();
  void refreshClipboardPermission();
  window.addEventListener("keydown", handleGlobalShortcut, true);
  window.addEventListener("focus", refreshSharedPortsWhenVisible);
  document.addEventListener("visibilitychange", refreshSharedPortsWhenVisible);
});

onBeforeUnmount(() => {
  if (searchInputTimer) window.clearTimeout(searchInputTimer);
  window.clearInterval(sharedRefreshTimer);
  window.clearTimeout(shareSettingsTimer);
  disposeClipboardPermission();
  window.removeEventListener("keydown", handleGlobalShortcut, true);
  window.removeEventListener("focus", refreshSharedPortsWhenVisible);
  document.removeEventListener("visibilitychange", refreshSharedPortsWhenVisible);
  views.forEach((view) => view.clipboardCleanup?.());
  sessions.forEach((session) => void closeSession(session));
});
</script>

<style scoped>
.serial-app { height: 100dvh; min-width: 0; display: grid; grid-template-columns: 310px minmax(0, 1fr); overflow: hidden; background: #0d1318; color: #d7e0e7; }
.serial-sidebar { min-height: 0; padding: 18px; display: flex; flex-direction: column; gap: 13px; overflow: auto; background: #151d23; border-right: 1px solid #2d3b45; scrollbar-color: #445865 #151d23; scrollbar-width: thin; }
.serial-sidebar::-webkit-scrollbar, .serial-tab-list::-webkit-scrollbar { width: 8px; height: 8px; }
.serial-sidebar::-webkit-scrollbar-thumb, .serial-tab-list::-webkit-scrollbar-thumb { background: #445865; border: 2px solid #151d23; border-radius: 8px; }
.serial-brand { display: flex; align-items: center; gap: 12px; margin-bottom: 3px; }
.serial-brand > a { width: 42px; height: 42px; display: grid; place-items: center; border-radius: 10px; background: #85b8b5; color: #102128; font-weight: 900; text-decoration: none; }
.serial-brand > div { display: grid; gap: 2px; }
.serial-brand strong { font-size: 18px; }
.serial-brand small, .serial-hint { color: #8fa0ad; }
.serial-source-switch { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); padding: 3px; border-radius: 8px; background: #0e151a; }
.serial-source-switch button { min-height: 34px; border: 0; border-radius: 6px; background: transparent; color: #9caab4; cursor: pointer; }
.serial-source-switch button.active { background: #283842; color: #e9f0f4; font-weight: 700; }
.serial-share-settings { padding: 10px; display: grid; gap: 9px; border: 1px solid #30414b; border-radius: 8px; background: #11191f; }
.serial-share-settings :deep(.n-checkbox__label) { color: #e2ebf0 !important; font-weight: 600; }
.serial-share-settings :deep(.n-checkbox-box__border) { border-color: #718692 !important; }
.serial-share-settings .serial-hint { margin: 0; color: #aebdc6; }
.serial-share-status, .serial-shared-refresh-state { margin: 0; color: #b9c8d1; font-size: 12px; line-height: 1.5; }
.serial-share-status { display: flex; align-items: center; gap: 7px; }
.serial-share-status > span { width: 7px; height: 7px; flex: none; border-radius: 50%; background: #d8a84f; box-shadow: 0 0 0 3px rgba(216, 168, 79, .12); }
.serial-share-status.online { color: #82dba8; }
.serial-share-status.online > span { background: #4fc583; box-shadow: 0 0 0 3px rgba(79, 197, 131, .14); }
.serial-share-status.offline { color: #f0a0a0; }
.serial-share-status.offline > span { background: #e26969; box-shadow: 0 0 0 3px rgba(226, 105, 105, .14); }
.serial-shared-refresh-state { color: #b4c3cc; }
.serial-field-heading { display: flex; align-items: center; justify-content: space-between; min-height: 24px; color: #c9d3da; font-size: 13px; font-weight: 700; }
.serial-field-heading :deep(.n-button), .serial-tab-actions > :deep(.n-button) { color: #e1eaef; background: #2a3a44; border-color: #526671; }
.serial-field-heading :deep(.n-button:hover), .serial-tab-actions > :deep(.n-button:hover) { color: #102027; background: #91bfbd; border-color: #91bfbd; }
.serial-hint { margin: -4px 0 0; font-size: 12px; line-height: 1.55; }
.serial-divider { height: 1px; background: #2a3841; }
.serial-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.serial-grid label, .serial-flow { min-width: 0; display: grid; gap: 5px; }
.serial-grid label > span, .serial-flow > span { color: #aebbc4; font-size: 12px; }
.serial-open { width: 100%; }
.serial-rules { margin-top: auto; padding: 12px; border: 1px solid #2c3d47; border-radius: 8px; background: #11191f; }
.serial-rules strong { font-size: 13px; }
.serial-rules p { margin: 6px 0 0; color: #8fa0ad; font-size: 12px; line-height: 1.55; }
.serial-mobile-sessions { display: none; }
.serial-workspace { position: relative; min-width: 0; min-height: 0; display: flex; flex-direction: column; overflow: hidden; }
.serial-search-bar { top: 55px; }
.serial-tabs { min-height: 48px; display: flex; align-items: stretch; border-bottom: 1px solid #2c3942; background: #151d23; }
.serial-tab-list { min-width: 0; flex: 1; display: flex; align-items: stretch; overflow-x: auto; }
.serial-tab { flex: 0 0 auto; min-width: 130px; max-width: 230px; padding: 0 12px; display: flex; align-items: center; gap: 8px; border: 0; border-right: 1px solid #2b3841; background: #151d23; color: #9eacb6; cursor: pointer; }
.serial-tab.active { background: #0d1318; color: #eef4f7; }
.serial-tab > span:nth-child(2) { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.serial-tab i { margin-left: auto; color: #82919c; font-size: 18px; font-style: normal; }
.serial-status-dot { width: 8px; height: 8px; flex: 0 0 auto; border-radius: 50%; background: #73818b; }
.serial-status-dot.open { background: #4fc583; box-shadow: 0 0 0 3px rgba(79, 197, 131, .12); }
.serial-status-dot.connecting { background: #e1ad55; }
.serial-status-dot.error { background: #e26969; }
.serial-tab-actions { min-width: 0; flex: 0 0 auto; padding: 0 12px; display: flex; align-items: center; gap: 8px; }
.serial-location, .serial-status-label { padding: 3px 8px; border-radius: 999px; background: #263640; color: #b9c8d1; font-size: 11px; white-space: nowrap; }
.serial-renderer { order: 10; }
.serial-mobile-more { display: none; }
.serial-status-label.open { background: rgba(39, 135, 83, .26); color: #78d7a1; }
.serial-status-label.error { background: rgba(169, 62, 62, .28); color: #f0a0a0; }
.serial-mobile-menu { display: none; align-self: center; margin-left: 8px; }
.serial-empty { flex: 1; display: grid; place-content: center; justify-items: center; padding: 30px; text-align: center; }
.serial-empty > div { color: #85b8b5; font: 700 54px/1 monospace; }
.serial-empty h1 { margin: 20px 0 8px; font-size: 24px; }
.serial-empty p { margin: 0; color: #83939e; }
.serial-empty-mobile { display: none; margin-top: 18px; }
.serial-terminal-section { position: relative; min-height: 0; flex: 1; display: flex; flex-direction: column; }
.serial-terminal { min-height: 0; flex: 1; overflow: hidden; background: #101418; }
.serial-error { padding: 8px 12px; border-top: 1px solid #653b3b; background: #3a2222; color: #f3b3b3; font-size: 13px; }
.serial-command-toolbar { padding: 7px 12px; display: flex; align-items: center; gap: 7px; border-top: 1px solid #2c3942; background: #151d23; }
.serial-command-toolbar :deep(.n-button) { color: #dbe5ea; }
.serial-quick-snippets { min-width: 0; flex: 1; display: flex; gap: 6px; overflow-x: auto; scrollbar-width: none; }
.serial-quick-snippets::-webkit-scrollbar { display: none; }
.serial-quick-snippets > span { align-self: center; color: #83939e; font-size: 12px; }
.serial-composer { padding: 10px 12px; display: flex; align-items: center; gap: 8px; border-top: 1px solid #2c3942; background: #151d23; }
.serial-send-mode { width: 84px; flex: 0 0 auto; }
.serial-command { min-width: 100px; flex: 1; }
.serial-line-ending { width: 100px; flex: 0 0 auto; }
.serial-snippet-editor { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 10px; align-items: center; }
.serial-snippet-editor > :nth-child(2) { grid-column: 1 / -1; }
.serial-snippet-editor > :last-child { grid-column: 2; }
.serial-snippet-list { margin-top: 16px; display: grid; gap: 7px; max-height: 280px; overflow: auto; scrollbar-width: none; }
.serial-snippet-list::-webkit-scrollbar { display: none; }
.serial-snippet-order-hint { margin: 0 0 2px; color: #71808b; font-size: 12px; }
.serial-shared-snippet-heading { margin-bottom: 10px; display: flex; align-items: center; justify-content: space-between; gap: 12px; }
.serial-shared-snippet-heading p { margin: 0; color: #687783; font-size: 12px; line-height: 1.5; }
.serial-shared-snippet-list { max-height: 390px; }
.serial-snippet-row { position: relative; padding: 8px 10px; display: flex; align-items: center; gap: 8px; border: 1px solid #dce3e7; border-radius: 7px; }
.serial-snippet-row--dragging { opacity: .45; }
.serial-snippet-row--drop-before::before, .serial-snippet-row--drop-after::after { position: absolute; right: 4px; left: 4px; height: 2px; border-radius: 2px; background: #18a058; content: ""; }
.serial-snippet-row--drop-before::before { top: -5px; }
.serial-snippet-row--drop-after::after { bottom: -5px; }
.serial-snippet-drag-handle { flex: 0 0 auto; width: 34px; height: 34px; padding: 0; border: 0; border-radius: 6px; background: transparent; color: #82909a; font-size: 22px; line-height: 1; cursor: grab; touch-action: none; user-select: none; }
.serial-snippet-drag-handle:hover { background: #f0f3f5; color: #53636e; }
.serial-snippet-drag-handle:active { cursor: grabbing; }
.serial-snippet-content { min-width: 0; flex: 1; display: grid; gap: 3px; border: 0; background: transparent; text-align: left; cursor: pointer; }
.serial-snippet-title { display: flex; align-items: center; gap: 7px; }
.serial-snippet-title small { padding: 1px 6px; border-radius: 999px; background: #e8eef1; color: #64737d; font-size: 10px; }
.serial-snippet-row code { overflow: hidden; color: #687783; text-overflow: ellipsis; white-space: nowrap; }
.serial-recording-options, .serial-setting-switches { display: grid; gap: 12px; }
.serial-recording-options p { margin: 0; color: #687783; font-size: 12px; }
.serial-clipboard-permission { padding-top: 2px; display: flex; align-items: center; justify-content: space-between; gap: 12px; color: #687783; font-size: 12px; }
.serial-settings-hint { margin: -10px 0 10px; color: #687783; font-size: 12px; line-height: 1.5; }
.serial-settings-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 0 14px; }
.serial-dialog-actions { display: flex; justify-content: flex-end; gap: 8px; }

@media (max-width: 760px) {
  .serial-app {
    position: fixed;
    top: var(--terminal-visual-top, 0px);
    left: var(--terminal-visual-left, 0px);
    width: var(--terminal-visual-width, 100%);
    max-width: var(--terminal-visual-width, 100%);
    height: var(--terminal-visual-height, 100dvh);
    max-height: var(--terminal-visual-height, 100dvh);
    grid-template-columns: 1fr;
    overflow: hidden;
  }
  .serial-sidebar { width: 100%; border-right: 0; padding: max(14px, env(safe-area-inset-top)) 14px max(14px, env(safe-area-inset-bottom)); }
  .serial-app--session-open .serial-sidebar { display: none; }
  .serial-workspace { display: none; }
  .serial-app--session-open .serial-workspace { display: flex; }
  .serial-mobile-menu {
    display: inline-flex;
    min-height: 34px;
    border: 1px solid #647985 !important;
    background: #30434e !important;
    color: #f4f9fb !important;
    font-weight: 700;
  }
  .serial-mobile-menu:hover, .serial-mobile-menu:active {
    border-color: #9bc7c4 !important;
    background: #9bc7c4 !important;
    color: #102027 !important;
  }
  .serial-mobile-menu:focus-visible { outline: 2px solid #b9d9d7; outline-offset: 1px; }
  .serial-tabs { padding-top: env(safe-area-inset-top); min-height: calc(46px + env(safe-area-inset-top)); }
  .serial-tab-list .serial-tab:not(.active) { display: none; }
  .serial-tab-list .serial-tab.active { min-width: 0; max-width: none; flex: 1; }
  .serial-tab-actions .serial-location, .serial-tab-actions > button:not(.serial-mobile-more), .serial-desktop-actions, .serial-status-label, .serial-renderer { display: none; }
  .serial-tab-actions { padding: 0 7px; }
  .serial-mobile-more { display: inline-flex; }
  .serial-mobile-sessions { padding: 9px; display: grid; gap: 8px; border: 1px solid #34434d; border-radius: 8px; background: #1d262c; }
  .serial-mobile-sessions-title { display: flex; align-items: center; justify-content: space-between; color: #b9c7cf; font-size: 12px; }
  .serial-mobile-sessions-title span { min-width: 24px; padding: 1px 7px; border-radius: 999px; background: #30404a; color: #dce6eb; text-align: center; }
  .serial-mobile-session-list { display: flex; gap: 7px; overflow-x: auto; scrollbar-width: none; }
  .serial-mobile-session-list::-webkit-scrollbar { display: none; }
  .serial-mobile-session { flex: 0 0 auto; display: flex; align-items: stretch; overflow: hidden; border: 1px solid #42535e; border-radius: 7px; background: #27343c; }
  .serial-mobile-session > button { min-height: 34px; padding: 0 9px; display: flex; align-items: center; gap: 7px; border: 0; background: transparent; color: #e0e8ed; }
  .serial-mobile-session > button:first-child { max-width: 170px; }
  .serial-mobile-session > button:first-child span:last-child { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .serial-mobile-session > button:last-child { padding: 0 10px; border-left: 1px solid #42535e; color: #aebbc4; font-size: 18px; }
  .serial-empty-mobile { display: inline-flex; }
  .serial-composer { padding-bottom: max(10px, env(safe-area-inset-bottom)); flex-wrap: wrap; }
  .serial-command { order: -1; flex-basis: 100%; }
  .serial-send-mode { flex: 1; }
  .serial-line-ending { flex: 1; }
  .serial-command-toolbar { flex-wrap: wrap; }
  .serial-quick-snippets { order: 3; flex-basis: 100%; }
  .serial-snippet-row { align-items: flex-start; flex-wrap: wrap; }
  .serial-snippet-row > .serial-snippet-content { flex-basis: calc(100% - 50px); }
  .serial-shared-snippet-list .serial-snippet-content { flex-basis: 100%; }
  .serial-settings-grid { grid-template-columns: 1fr; }
}
</style>
