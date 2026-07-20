<template>
  <div class="ssh-app" :class="{ 'ssh-app--terminal-open': activeTabId }">
    <aside class="ssh-sidebar">
      <div class="ssh-brand-row">
        <router-link class="ssh-home" to="/" aria-label="返回首页">SS</router-link>
        <div>
          <strong>SSH 终端</strong>
          <small>{{ hosts.length }} 台主机</small>
        </div>
        <n-button type="primary" size="small" aria-label="添加主机" @click="openHostEditor()">
          <template #icon><n-icon><AddOutline /></n-icon></template>
          添加
        </n-button>
      </div>

      <n-input v-model:value="keyword" clearable size="small" placeholder="搜索主机" />

      <div class="ssh-config-actions">
        <n-button secondary size="small" @click="openPortForwards">端口转发</n-button>
        <n-button secondary size="small" @click="openTerminalSettings">终端设置</n-button>
        <n-button secondary size="small" @click="exportConfiguration">导出</n-button>
        <n-button secondary size="small" @click="configurationInput?.click()">导入</n-button>
        <input ref="configurationInput" type="file" accept="application/json,.json" @change="importConfiguration" />
      </div>

      <n-scrollbar class="ssh-host-scroll" trigger="none" :theme-overrides="hostScrollbarTheme">
        <div class="ssh-host-list">
          <div v-if="loading" class="ssh-empty">正在载入主机…</div>
          <div v-else-if="filteredHosts.length === 0" class="ssh-empty">
            暂无主机
            <n-button text type="primary" @click="openHostEditor()">添加第一台</n-button>
          </div>
          <button
            v-for="host in filteredHosts"
            :key="host.id"
            class="ssh-host"
            type="button"
            @click="openCredentials(host)"
            @contextmenu.prevent="openHostEditor(host)"
          >
            <span class="ssh-host-icon">{{ host.name.slice(0, 1).toUpperCase() }}</span>
            <span class="ssh-host-copy">
              <strong>{{ host.name }}</strong>
              <small>{{ host.username }}@{{ host.host }}:{{ host.port }}</small>
            </span>
            <n-button class="ssh-edit-button" secondary circle size="small" aria-label="编辑主机" @click.stop="openHostEditor(host)">
              <template #icon><n-icon><CreateOutline /></n-icon></template>
            </n-button>
          </button>
        </div>
      </n-scrollbar>
    </aside>

    <main class="ssh-workspace">
      <div class="ssh-tabs">
        <div class="ssh-tab-list" @wheel="handleTabListWheel" @dragover.prevent="handleTabListDragOver">
          <n-button class="ssh-mobile-hosts" quaternary size="small" @click="activeTabId = ''">
            <template #icon><n-icon><MenuOutline /></n-icon></template>
            主机
          </n-button>
          <button
            v-for="tab in tabs"
            :key="tab.id"
            type="button"
            class="ssh-tab"
            :class="{ active: tab.id === activeTabId, dragging: draggedTabId === tab.id }"
            draggable="true"
            @click="activateTab(tab.id)"
            @dragstart="handleTabDragStart($event, tab.id)"
            @dragover.prevent
            @drop.prevent="handleTabDrop($event, tab.id)"
            @dragend="draggedTabId = ''"
          >
            <span class="ssh-status-dot" :class="tab.status" />
            <span>{{ tab.host.name }}</span>
            <n-icon class="ssh-tab-close" size="14" @click.stop="closeTab(tab.id)"><CloseOutline /></n-icon>
          </button>
        </div>
        <div v-if="activeTab" class="ssh-tab-actions">
          <div class="ssh-pane-switch">
            <button type="button" :class="{ active: activePane === 'terminal' }" @click="showTerminalPane">终端</button>
            <button type="button" :class="{ active: activePane === 'sftp' }" @click="activePane = 'sftp'">文件</button>
          </div>
          <n-button v-if="activeTab.status === 'closed' || activeTab.status === 'error'" secondary size="tiny" @click="reconnectTab(activeTab)">重连</n-button>
          <n-button v-if="activePane === 'terminal'" secondary size="tiny" @click="openSearch">搜索</n-button>
          <n-button v-if="activePane === 'terminal'" secondary size="tiny" @click="openSnippets">片段</n-button>
          <n-button
            v-if="activePane === 'terminal'"
            secondary
            size="tiny"
            :type="activeTab.recording ? 'error' : 'default'"
            @click="toggleRecording(activeTab)"
          >{{ activeTab.recording ? '停止录制' : '录制' }}</n-button>
          <span v-if="activePane === 'terminal'" class="ssh-renderer" :class="activeTab.renderer">{{ activeTab.renderer === 'webgl' ? 'GPU' : 'Canvas' }}</span>
          <span class="ssh-status-text" :class="activeTab.status">{{ activeTab.message }}</span>
        </div>
      </div>

      <div v-if="activeTab && activePane === 'terminal' && showSearch" class="ssh-search-bar">
        <n-input ref="searchInput" v-model:value="searchQuery" size="small" clearable placeholder="输入文字，Enter 查找下一个" @keyup.enter="searchTerminal(false)" />
        <span class="ssh-search-count">{{ activeTab.searchResultCount ? activeTab.searchResultIndex + 1 : 0 }}/{{ activeTab.searchResultCount }}</span>
        <n-input-number
          v-model:value="searchTargetIndex"
          class="ssh-search-index"
          size="small"
          :min="1"
          :max="Math.max(1, activeTab.searchResultCount)"
          :show-button="false"
          placeholder="序号"
          @keyup.enter="jumpToSearchIndex"
        />
        <n-button size="small" @click="jumpToSearchIndex">跳转</n-button>
        <n-button size="small" @click="searchTerminal(true)">上一个</n-button>
        <n-button size="small" @click="searchTerminal(false)">下一个</n-button>
        <n-button size="small" @click="closeSearch">关闭</n-button>
      </div>

      <section v-if="tabs.length === 0" class="ssh-welcome">
        <div class="ssh-welcome-mark">›_</div>
        <template v-if="hosts.length === 0">
          <h1>添加第一台 SSH 主机</h1>
          <p>保存主机地址和用户名后，即可打开远程终端。</p>
          <n-button class="ssh-welcome-action" type="primary" size="large" @click="openHostEditor()">
            <template #icon><n-icon><AddOutline /></n-icon></template>
            添加 SSH 主机
          </n-button>
        </template>
        <template v-else>
          <h1>选择一台主机开始连接</h1>
          <p>凭据只用于本次连接，不会保存到服务器。</p>
        </template>
      </section>
      <div
        v-for="tab in tabs"
        v-show="tab.id === activeTabId && activePane === 'terminal'"
        :key="tab.id"
        class="ssh-terminal-pane"
      >
        <section
          :ref="(element) => setTerminalElement(tab.id, element as HTMLElement | null)"
          class="ssh-terminal"
        />
        <div class="ssh-command-panel">
          <div class="ssh-command-toolbar">
            <n-button secondary size="tiny" @click="toggleQuickSnippets">
              {{ showQuickSnippets ? '隐藏快捷片段' : '显示快捷片段' }}
            </n-button>
            <div v-if="showQuickSnippets" class="ssh-quick-snippets">
              <n-button v-for="snippet in pinnedSnippets" :key="snippet.id" secondary size="tiny" @click="sendSnippet(snippet, false)">
                {{ snippet.name }}
              </n-button>
              <span v-if="pinnedSnippets.length === 0">暂无快捷片段</span>
            </div>
            <n-button text size="tiny" @click="openSnippets">管理片段</n-button>
          </div>
          <div class="ssh-command-editor">
            <n-input
              v-model:value="commandDraft"
              type="textarea"
              :autosize="{ minRows: 1, maxRows: 4 }"
              placeholder="输入要发送的命令，Ctrl/⌘ + Enter 发送"
              @keydown="handleCommandKeydown"
            />
            <n-button type="primary" :disabled="!commandDraft.trim()" @click="sendCommand">发送</n-button>
          </div>
        </div>
      </div>
      <sftp-panel
        v-if="activeTab && activePane === 'sftp'"
        :host="activeTab.host"
        @request-credentials="requestPersistentCredentials(activeTab.host)"
      />
    </main>

    <n-modal
      v-model:show="showHostEditor"
      preset="card"
      :title="editingId ? '编辑 SSH 主机' : '添加 SSH 主机'"
      class="ssh-dialog"
      :style="dialogStyle"
    >
      <n-form label-placement="top">
        <div class="ssh-form-grid">
          <n-form-item label="名称"><n-input v-model:value="hostDraft.name" placeholder="生产服务器" /></n-form-item>
          <n-form-item label="分组"><n-input v-model:value="hostDraft.group" placeholder="可选" /></n-form-item>
        </div>
        <n-form-item label="主机"><n-input v-model:value="hostDraft.host" placeholder="example.com 或 IP" /></n-form-item>
        <div class="ssh-form-grid ssh-form-grid--connection">
          <n-form-item label="端口"><n-input-number v-model:value="hostDraft.port" :min="1" :max="65535" /></n-form-item>
          <n-form-item label="用户名"><n-input v-model:value="hostDraft.username" placeholder="root" /></n-form-item>
        </div>
        <n-checkbox v-model:checked="hostDraft.useAgent">默认使用服务端 SSH Agent 认证</n-checkbox>
        <n-form-item label="跳板机（可选）">
          <n-select v-model:value="hostDraft.jumpHostId" clearable :options="jumpHostOptions" placeholder="直接连接" />
        </n-form-item>
        <n-form-item v-if="hostDraft.hostKeySha256" label="已信任主机指纹">
          <n-input :value="hostDraft.hostKeySha256" readonly />
          <n-button class="ssh-reset-key" size="small" @click="hostDraft.hostKeySha256 = ''">重置信任</n-button>
        </n-form-item>
        <n-alert v-if="hostDraft.hasCredential" type="success" :show-icon="true">
          这台主机已保存加密凭据。
          <n-button class="ssh-forget-credential" text type="error" @click="forgetCredential">删除已保存凭据</n-button>
        </n-alert>
      </n-form>
      <template #footer>
        <div class="ssh-dialog-actions">
          <n-button v-if="editingId" type="error" secondary @click="deleteHost">删除</n-button>
          <span class="ssh-dialog-spacer" />
          <n-button @click="showHostEditor = false">取消</n-button>
          <n-button type="primary" :loading="saving" @click="saveHost">保存</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showCredentials" preset="card" title="连接认证" class="ssh-dialog" :style="dialogStyle">
      <p class="ssh-connect-target">{{ selectedHost?.username }}@{{ selectedHost?.host }}:{{ selectedHost?.port }}</p>
      <n-tabs v-model:value="credentialDraft.method" type="segment">
        <n-tab-pane name="password" tab="密码">
          <n-form-item label="密码">
            <n-input v-model:value="credentialDraft.password" type="password" show-password-on="click" @keyup.enter="connect" />
          </n-form-item>
        </n-tab-pane>
        <n-tab-pane name="privateKey" tab="私钥">
          <n-upload :show-file-list="false" @change="loadPrivateKey">
            <n-button size="small">读取私钥文件</n-button>
          </n-upload>
          <p class="ssh-field-hint">支持无后缀的 id_rsa、id_ed25519，以及 PEM、KEY 等私钥文件。</p>
          <n-input v-model:value="credentialDraft.privateKey" class="ssh-private-key" type="textarea" :rows="7" placeholder="-----BEGIN OPENSSH PRIVATE KEY-----" />
          <n-form-item label="私钥口令（可选）">
            <n-input v-model:value="credentialDraft.passphrase" type="password" show-password-on="click" />
          </n-form-item>
        </n-tab-pane>
        <n-tab-pane name="agent" tab="SSH Agent">
          <n-alert type="info" :show-icon="true">
            使用运行 Space Station 服务进程可访问的 SSH Agent（SSH_AUTH_SOCK）进行认证，无需上传私钥。
          </n-alert>
        </n-tab-pane>
      </n-tabs>
      <n-checkbox v-if="credentialDraft.method !== 'agent'" v-model:checked="credentialDraft.remember">本次打开期间记住凭据</n-checkbox>
      <n-checkbox v-if="credentialDraft.method !== 'agent'" v-model:checked="credentialDraft.persist" class="ssh-persist-credential">
        加密保存凭据，刷新页面后仍可直接连接
      </n-checkbox>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="showCredentials = false">取消</n-button>
          <n-button type="primary" @click="connect">连接</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showFingerprint" preset="card" title="确认主机身份" class="ssh-dialog" :style="dialogStyle">
      <n-alert type="warning" :show-icon="true">
        这是第一次连接该主机。请核对服务器上的主机密钥指纹后再信任。
      </n-alert>
      <dl class="ssh-fingerprint">
        <dt>主机</dt><dd>{{ fingerprintRequest?.host }}:{{ fingerprintRequest?.port }}</dd>
        <dt>SHA256</dt><dd>{{ fingerprintRequest?.fingerprint }}</dd>
      </dl>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="answerFingerprint(false)">拒绝</n-button>
          <n-button type="warning" @click="answerFingerprint(true)">信任并连接</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showSnippets" preset="card" title="命令片段" class="ssh-dialog" :style="dialogStyle">
      <div class="ssh-snippet-editor">
        <n-input v-model:value="snippetDraft.name" placeholder="名称，例如：查看磁盘" />
        <n-input v-model:value="snippetDraft.command" type="textarea" :rows="3" placeholder="df -h" />
        <n-checkbox v-model:checked="snippetDraft.pinned">显示为终端快捷按钮</n-checkbox>
        <div class="ssh-snippet-editor-actions">
          <n-button v-if="editingSnippetId" @click="cancelSnippetEdit">取消编辑</n-button>
          <n-button type="primary" @click="saveSnippet">{{ editingSnippetId ? '保存修改' : '添加片段' }}</n-button>
        </div>
      </div>
      <div class="ssh-snippet-list">
        <div v-for="snippet in snippets" :key="snippet.id" class="ssh-snippet-row">
          <button type="button" @click="sendSnippet(snippet)"><strong>{{ snippet.name }}</strong><code>{{ snippet.command }}</code></button>
          <n-button text type="primary" @click="editSnippet(snippet)">编辑</n-button>
          <n-button text type="error" @click="deleteSnippet(snippet.id)">删除</n-button>
        </div>
        <p v-if="snippets.length === 0" class="ssh-field-hint">还没有命令片段。</p>
      </div>
    </n-modal>

    <n-modal v-model:show="showPortForwards" preset="card" title="本地端口转发" class="ssh-dialog" :style="dialogStyle">
      <n-alert type="info" :show-icon="true">监听地址固定为服务端的 127.0.0.1，不会暴露到公网。</n-alert>
      <div class="ssh-forward-form">
        <n-select v-model:value="forwardDraft.hostId" :options="hostOptions" placeholder="SSH 主机" />
        <n-input-number v-model:value="forwardDraft.localPort" :min="1" :max="65535" placeholder="本地端口" />
        <n-input v-model:value="forwardDraft.remoteHost" placeholder="远端地址，例如 127.0.0.1" />
        <n-input-number v-model:value="forwardDraft.remotePort" :min="1" :max="65535" placeholder="远端端口" />
        <n-button type="primary" @click="startForward">启动转发</n-button>
      </div>
      <div class="ssh-forward-list">
        <div v-for="forward in portForwards" :key="forward.id" class="ssh-forward-row">
          <div><strong>127.0.0.1:{{ forward.localPort }}</strong><small> → {{ forward.remoteHost }}:{{ forward.remotePort }} · {{ forward.message }}</small></div>
          <n-button text type="error" @click="stopForward(forward.id)">停止</n-button>
        </div>
        <p v-if="portForwards.length === 0" class="ssh-field-hint">没有运行中的端口转发。</p>
      </div>
    </n-modal>

    <n-modal v-model:show="showRecordingOptions" preset="card" title="开始会话录制" class="ssh-dialog" :style="dialogStyle">
      <div class="ssh-recording-options">
        <n-checkbox v-model:checked="recordingDraft.stripAnsi">过滤 ANSI 颜色和终端控制字符（推荐）</n-checkbox>
        <n-checkbox v-model:checked="recordingDraft.timestamps">为每行添加时间戳</n-checkbox>
        <p>录制内容仅缓存在当前浏览器标签内，停止后下载为日志文件。</p>
      </div>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="showRecordingOptions = false">取消</n-button>
          <n-button type="primary" @click="startRecording">开始录制</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showTerminalSettings" preset="card" title="终端设置" class="ssh-dialog" :style="dialogStyle">
      <n-form label-placement="top">
        <n-form-item label="终端回滚缓冲区（行）">
          <n-input-number v-model:value="terminalSettingsDraft.scrollbackLines" :min="1000" :max="500000" :step="10000" />
        </n-form-item>
        <p class="ssh-field-hint">保存后对新打开或重新连接的终端生效。</p>
        <n-form-item class="ssh-settings-recording" label="单次录制缓冲区上限（MiB）">
          <n-input-number v-model:value="terminalSettingsDraft.recordingMaxMiB" :min="1" :max="500" :step="10" />
        </n-form-item>
        <p class="ssh-field-hint">录制内容仅保存在当前浏览器标签内；达到上限后会停止追加并提示。</p>
        <n-form-item class="ssh-settings-recording" label="剪贴板操作">
          <div class="ssh-settings-switches">
            <n-checkbox v-model:checked="terminalSettingsDraft.copyOnSelect">选中终端文本后自动复制</n-checkbox>
            <n-checkbox v-model:checked="terminalSettingsDraft.pasteOnRightClick">在终端内右键时自动粘贴</n-checkbox>
            <div class="ssh-clipboard-permission">
              <span>读取权限：{{ clipboardPermissionLabel }}</span>
              <n-button size="small" secondary :loading="clipboardPermissionState === 'checking'" @click="requestClipboardAccess">检测/授权</n-button>
            </div>
          </div>
        </n-form-item>
        <p class="ssh-field-hint">已授权时右键自动粘贴；无权限时保留浏览器原生右键菜单。不会额外发送回车，但多行内容仍可能被远程 Shell 执行。</p>
      </n-form>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="showTerminalSettings = false">取消</n-button>
          <n-button type="primary" @click="saveTerminalSettings">保存</n-button>
        </div>
      </template>
    </n-modal>
  </div>
</template>

<script setup lang="ts">
import "@xterm/xterm/css/xterm.css";
import { FitAddon } from "@xterm/addon-fit";
import { SearchAddon } from "@xterm/addon-search";
import { SerializeAddon } from "@xterm/addon-serialize";
import { WebLinksAddon } from "@xterm/addon-web-links";
import { WebglAddon } from "@xterm/addon-webgl";
import { Terminal } from "@xterm/xterm";
import { AddOutline, CloseOutline, CreateOutline, MenuOutline } from "@vicons/ionicons5";
import {
  NAlert,
  NButton,
  NCheckbox,
  NForm,
  NFormItem,
  NIcon,
  NInput,
  NInputNumber,
  NModal,
  NScrollbar,
  NSelect,
  NTabPane,
  NTabs,
  NUpload,
  useMessage,
  type InputInst,
  type UploadFileInfo,
} from "naive-ui";
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { writeClipboard } from "@/utils/clipboard";
import {
  deleteSshCredential,
  fetchSshHosts,
  fetchSshPortForwards,
  saveSshHosts,
  startSshPortForward,
  stopSshPortForward,
  type SshHost,
  type SshPortForward,
} from "@/api";
import SftpPanel from "./SftpPanel.vue";

type ConnectionStatus = "connecting" | "authenticating" | "connected" | "closed" | "error";

interface TerminalTab {
  id: string;
  host: SshHost;
  status: ConnectionStatus;
  message: string;
  socket: WebSocket;
  terminal?: Terminal;
  fitAddon?: FitAddon;
  searchAddon?: SearchAddon;
  serializeAddon?: SerializeAddon;
  webglAddon?: WebglAddon;
  resizeObserver?: ResizeObserver;
  connectPayload: Record<string, unknown>;
  pendingCredential?: CredentialData;
  rememberCredential: boolean;
  persistCredential: boolean;
  usedStoredCredential: boolean;
  restoreBuffer?: string;
  recording: boolean;
  recordingStartedAt?: string;
  recordingContent: string;
  recordingEntries: Array<{ at: Date; data: string }>;
  recordingDecoder?: TextDecoder;
  recordingStripAnsi: boolean;
  recordingTimestamps: boolean;
  recordingSizeBytes: number;
  recordingLimitReached: boolean;
  renderer: "webgl" | "canvas";
  searchResultIndex: number;
  searchResultCount: number;
  searchTerm: string;
  searchCountTimer?: number;
  searchMatches: Array<Array<{ row: number; column: number; width: number }>>;
  searchOverlay?: HTMLElement;
  clipboardCleanup?: () => void;
}

interface CommandSnippet { id: string; name: string; command: string; pinned?: boolean }

interface TerminalSettings {
  scrollbackLines: number;
  recordingMaxMiB: number;
  copyOnSelect: boolean;
  pasteOnRightClick: boolean;
}

interface CredentialData {
  method: string;
  password: string;
  privateKey: string;
  passphrase: string;
}

interface FingerprintRequest {
  tabId: string;
  host: string;
  port: number;
  fingerprint: string;
}

type ClipboardReadState = PermissionState | "checking" | "available" | "unsupported" | "insecure";

const message = useMessage();
const hosts = ref<SshHost[]>([]);
const tabs = ref<TerminalTab[]>([]);
const activeTabId = ref("");
const draggedTabId = ref("");
const activePane = ref<"terminal" | "sftp">("terminal");
const showSearch = ref(false);
const searchQuery = ref("");
const searchTargetIndex = ref<number | null>(null);
const searchInput = ref<InputInst | null>(null);
const showSnippets = ref(false);
const snippets = ref<CommandSnippet[]>(loadSnippets());
const snippetDraft = reactive({ name: "", command: "", pinned: true });
const editingSnippetId = ref("");
const commandDraft = ref("");
const showQuickSnippets = ref(localStorage.getItem("ssh-show-quick-snippets") !== "false");
const configurationInput = ref<HTMLInputElement | null>(null);
const showPortForwards = ref(false);
const portForwards = ref<SshPortForward[]>([]);
const forwardDraft = reactive({ hostId: "", localPort: 8080, remoteHost: "127.0.0.1", remotePort: 80 });
const showRecordingOptions = ref(false);
const recordingTargetId = ref("");
const recordingDraft = reactive({ stripAnsi: true, timestamps: false });
const terminalSettings = reactive<TerminalSettings>(loadTerminalSettings());
const terminalSettingsDraft = reactive<TerminalSettings>({ ...terminalSettings });
const showTerminalSettings = ref(false);
const terminalElements = new Map<string, HTMLElement>();
const credentialCache = new Map<string, CredentialData>();
let clipboardWarningShown = false;
let clipboardFallbackHintShown = false;
let clipboardPermissionStatus: PermissionStatus | undefined;
const clipboardPermissionState = ref<ClipboardReadState>("checking");
const keyword = ref("");
const loading = ref(true);
const saving = ref(false);
const showHostEditor = ref(false);
const showCredentials = ref(false);
const showFingerprint = ref(false);
const editingId = ref("");
const selectedHost = ref<SshHost | null>(null);
const fingerprintRequest = ref<FingerprintRequest | null>(null);
const hostDraft = reactive<SshHost>(emptyHost());
const credentialDraft = reactive({
  method: "password",
  password: "",
  privateKey: "",
  passphrase: "",
  remember: true,
  persist: false,
});
const dialogStyle = { width: "var(--ssh-dialog-width)" };
const hostScrollbarTheme = {
  width: "7px",
  borderRadius: "999px",
  color: "#527c7a",
  colorHover: "#83b3af",
  railColor: "#151b20",
};
const terminalSearchOptions = {
  caseSensitive: false,
};

const filteredHosts = computed(() => {
  const query = keyword.value.trim().toLowerCase();
  if (!query) return hosts.value;
  return hosts.value.filter((host) => [host.name, host.host, host.username, host.group].some((part) => part.toLowerCase().includes(query)));
});
const activeTab = computed(() => tabs.value.find((tab) => tab.id === activeTabId.value));
const jumpHostOptions = computed(() => hosts.value
  .filter((host) => host.id !== editingId.value && !host.jumpHostId)
  .map((host) => ({ label: `${host.name} (${host.username}@${host.host})`, value: host.id })));
const hostOptions = computed(() => hosts.value.map((host) => ({ label: host.name, value: host.id })));
const pinnedSnippets = computed(() => snippets.value.filter((snippet) => snippet.pinned !== false));
const clipboardPermissionLabel = computed(() => ({
  checking: "检测中",
  granted: "已允许",
  available: "当前会话可用",
  prompt: "等待授权",
  denied: "已拒绝",
  unsupported: "浏览器按次确认",
  insecure: "当前页面不安全",
})[clipboardPermissionState.value]);

watch(searchQuery, (value) => {
  if (!showSearch.value) return;
  if (!value) {
    activeTab.value?.searchAddon?.clearDecorations();
    resetSearchResults(activeTab.value);
    return;
  }
  searchTerminal(false, true);
});

onMounted(() => {
  void loadHosts();
  void refreshClipboardPermission();
  window.addEventListener("keydown", handleGlobalShortcut);
});
onBeforeUnmount(() => {
  if (clipboardPermissionStatus) clipboardPermissionStatus.onchange = null;
  window.removeEventListener("keydown", handleGlobalShortcut);
  tabs.value.forEach(disposeTab);
});

function emptyHost(): SshHost {
  return { id: "", name: "", host: "", port: 22, username: "root", group: "", hostKeySha256: "", useAgent: false, jumpHostId: "" };
}

async function loadHosts() {
  try {
    hosts.value = (await fetchSshHosts()).hosts;
  } catch (error) {
    message.error(error instanceof Error ? error.message : "SSH 主机加载失败");
  } finally {
    loading.value = false;
  }
}

function openHostEditor(host?: SshHost) {
  editingId.value = host?.id ?? "";
  Object.assign(hostDraft, host ? { ...host } : emptyHost());
  showHostEditor.value = true;
}

async function saveHost() {
  if (!hostDraft.name.trim() || !hostDraft.host.trim() || !hostDraft.username.trim()) {
    message.warning("名称、主机和用户名不能为空");
    return;
  }
  saving.value = true;
  try {
    const value: SshHost = {
      ...hostDraft,
      id: editingId.value || crypto.randomUUID(),
      name: hostDraft.name.trim(),
      host: hostDraft.host.trim(),
      username: hostDraft.username.trim(),
      group: hostDraft.group.trim(),
      port: Math.max(1, Math.min(65535, Number(hostDraft.port) || 22)),
    };
    const next = editingId.value ? hosts.value.map((host) => (host.id === editingId.value ? value : host)) : [...hosts.value, value];
    await saveSshHosts(next);
    hosts.value = next;
    showHostEditor.value = false;
  } catch (error) {
    message.error(error instanceof Error ? error.message : "SSH 主机保存失败");
  } finally {
    saving.value = false;
  }
}

async function deleteHost() {
  if (!window.confirm(`确定删除 SSH 主机“${hostDraft.name}”吗？保存的凭据也会一并删除。`)) return;
  const next = hosts.value.filter((host) => host.id !== editingId.value);
  await saveSshHosts(next);
  hosts.value = next;
  showHostEditor.value = false;
}

async function forgetCredential() {
  if (!editingId.value) return;
  try {
    await deleteSshCredential(editingId.value);
    hostDraft.hasCredential = false;
    const host = hosts.value.find((item) => item.id === editingId.value);
    if (host) host.hasCredential = false;
    credentialCache.delete(editingId.value);
    message.success("已删除保存的凭据");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "凭据删除失败");
  }
}

function openCredentials(host: SshHost) {
  if (host.useAgent) {
    openTerminal(host, { method: "agent", password: "", privateKey: "", passphrase: "" }, false, false);
    return;
  }
  const cached = credentialCache.get(host.id);
  if (cached) {
    openTerminal(host, { ...cached }, true, false);
    return;
  }
  if (host.hasCredential) {
    openTerminal(host, { method: "stored", password: "", privateKey: "", passphrase: "" }, false, false);
    return;
  }
  selectedHost.value = host;
  Object.assign(credentialDraft, {
    method: "password",
    password: "",
    privateKey: "",
    passphrase: "",
    remember: true,
    persist: false,
  });
  showCredentials.value = true;
}

function requestPersistentCredentials(host: SshHost) {
  selectedHost.value = host;
  Object.assign(credentialDraft, {
    method: "password",
    password: "",
    privateKey: "",
    passphrase: "",
    remember: true,
    persist: true,
  });
  showCredentials.value = true;
}

async function loadPrivateKey(options: { file: UploadFileInfo }) {
  const file = options.file.file;
  if (!file) return;
  if (file.size > 1024 * 1024) {
    message.error("私钥文件不能超过 1 MiB");
    return;
  }
  const content = (await file.text()).trim();
  if (!/^-----BEGIN (?:OPENSSH |RSA |EC |DSA |ENCRYPTED )?PRIVATE KEY-----/.test(content)) {
    message.error("所选文件不是支持的 SSH 私钥");
    return;
  }
  credentialDraft.privateKey = `${content}\n`;
  message.success(`已读取 ${file.name}`);
}

async function connect() {
  const host = selectedHost.value;
  const credential = credentialDraft.method === "privateKey" ? credentialDraft.privateKey : credentialDraft.password;
  if (!host || (credentialDraft.method !== "agent" && !credential)) {
    message.warning("请输入认证凭据");
    return;
  }
  showCredentials.value = false;
  openTerminal(host, {
    method: credentialDraft.method,
    password: credentialDraft.password,
    privateKey: credentialDraft.privateKey,
    passphrase: credentialDraft.passphrase,
  }, credentialDraft.method === "agent" ? false : credentialDraft.remember || credentialDraft.persist,
  credentialDraft.method === "agent" ? false : credentialDraft.persist);
  Object.assign(credentialDraft, { password: "", privateKey: "", passphrase: "" });
}

async function openTerminal(
  host: SshHost,
  credential: CredentialData,
  rememberCredential: boolean,
  persistCredential: boolean,
) {
  const id = crypto.randomUUID();
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(`${protocol}//${window.location.host}/api/tools/ssh/terminal`);
  socket.binaryType = "arraybuffer";
  const tab: TerminalTab = {
    id,
    host,
    status: "connecting",
    message: "正在打开连接…",
    socket,
    connectPayload: {
      type: "connect",
      hostId: host.id,
      password: credential.method === "password" ? credential.password : "",
      privateKey: credential.method === "privateKey" ? credential.privateKey : "",
      passphrase: credential.passphrase,
      useAgent: credential.method === "agent",
      saveCredential: persistCredential,
    },
    pendingCredential: credential,
    rememberCredential,
    persistCredential,
    usedStoredCredential: credential.method === "stored",
    recording: false,
    recordingContent: "",
    recordingEntries: [],
    recordingStripAnsi: true,
    recordingTimestamps: false,
    recordingSizeBytes: 0,
    recordingLimitReached: false,
    renderer: "canvas",
    searchResultIndex: 0,
    searchResultCount: 0,
    searchTerm: "",
    searchMatches: [],
  };
  tabs.value.push(tab);
  activeTabId.value = id;
  activePane.value = "terminal";
  await nextTick();
  initializeTerminal(tab);
  socket.onmessage = (event) => handleSocketMessage(tab, event);
  socket.onerror = () => updateTab(tab, "error", "WebSocket 连接失败");
  socket.onclose = () => {
    if (tab.status !== "closed" && tab.status !== "error") updateTab(tab, "closed", "连接已关闭");
  };
}

function initializeTerminal(tab: TerminalTab) {
  const element = terminalElements.get(tab.id);
  if (!element) return;
  const terminal = new Terminal({
    cursorBlink: true,
    fontFamily: '"SFMono-Regular", Consolas, "Liberation Mono", monospace',
    fontSize: 14,
    scrollback: terminalSettings.scrollbackLines,
    allowProposedApi: false,
    theme: {
      background: "#101418",
      foreground: "#d8dee9",
      cursor: "#8fbcbb",
      selectionBackground: "#d96820",
      selectionInactiveBackground: "#d96820",
      selectionForeground: "#ffffff",
    },
  });
  const fitAddon = new FitAddon();
  const searchAddon = new SearchAddon();
  const serializeAddon = new SerializeAddon();
  terminal.loadAddon(fitAddon);
  terminal.loadAddon(new WebLinksAddon());
  terminal.loadAddon(searchAddon);
  terminal.loadAddon(serializeAddon);
  terminal.open(element);
  const screen = element.querySelector<HTMLElement>(".xterm-screen");
  if (screen) {
    const overlay = document.createElement("div");
    overlay.className = "ssh-search-overlay";
    screen.appendChild(overlay);
    tab.searchOverlay = overlay;
  }
  let webglAddon: WebglAddon | undefined;
  try {
    webglAddon = new WebglAddon();
    webglAddon.onContextLoss(() => {
      webglAddon?.dispose();
      updateRenderer(tab, "canvas");
    });
    terminal.loadAddon(webglAddon);
    updateRenderer(tab, "webgl");
  } catch {
    webglAddon = undefined;
  }
  if (tab.restoreBuffer) terminal.write(tab.restoreBuffer);
  fitAddon.fit();
  terminal.focus();
  terminal.onData((data) => {
    if (tab.socket.readyState === WebSocket.OPEN) tab.socket.send(new TextEncoder().encode(data));
  });
  setupTerminalClipboard(tab, terminal, element);
  terminal.onScroll(() => renderSearchOverlay(tab));
  const resizeObserver = new ResizeObserver(() => {
    fitAddon.fit();
    sendResize(tab);
    renderSearchOverlay(tab);
  });
  resizeObserver.observe(element);
  tab.terminal = terminal;
  tab.fitAddon = fitAddon;
  tab.searchAddon = searchAddon;
  tab.serializeAddon = serializeAddon;
  tab.webglAddon = webglAddon;
  tab.resizeObserver = resizeObserver;
}

function setupTerminalClipboard(tab: TerminalTab, terminal: Terminal, element: HTMLElement) {
  const copySelection = (event: PointerEvent) => {
    if (event.button !== 0) return;
    if (!terminalSettings.copyOnSelect || !terminal.hasSelection()) return;
    const selection = terminal.getSelection();
    if (!selection) return;
    void writeClipboard(selection).then((success) => {
      if (!success) warnClipboardAccess("浏览器不允许自动写入剪贴板，请检查站点权限");
    });
  };
  const pasteClipboard = (event: MouseEvent) => {
    if (!terminalSettings.pasteOnRightClick) return;
    if (tab.socket.readyState !== WebSocket.OPEN || tab.status !== "connected") {
      message.warning("SSH 尚未连接，无法粘贴");
      return;
    }
    if (!["granted", "available"].includes(clipboardPermissionState.value) || !navigator.clipboard?.readText) {
      terminal.focus();
      if (!clipboardFallbackHintShown) {
        clipboardFallbackHintShown = true;
        message.info("剪贴板读取尚未授权，已保留原生右键菜单，请选择“粘贴”或使用粘贴快捷键");
      }
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    void navigator.clipboard.readText()
      .then((text) => {
        if (text) terminal.paste(text);
        if (clipboardPermissionState.value !== "granted") clipboardPermissionState.value = "available";
        terminal.focus();
      })
      .catch((error: unknown) => {
        clipboardPermissionState.value = "prompt";
        message.error(`${describeClipboardError(error)}；下次右键将使用浏览器原生粘贴菜单`);
      });
  };
  element.addEventListener("pointerup", copySelection);
  element.addEventListener("contextmenu", pasteClipboard, true);
  tab.clipboardCleanup = () => {
    element.removeEventListener("pointerup", copySelection);
    element.removeEventListener("contextmenu", pasteClipboard, true);
  };
}

function warnClipboardAccess(content: string) {
  if (clipboardWarningShown) return;
  clipboardWarningShown = true;
  message.warning(content);
}

function describeClipboardError(error: unknown) {
  if (error instanceof DOMException) return `剪贴板读取失败（${error.name}: ${error.message}）`;
  if (error instanceof Error) return `剪贴板读取失败（${error.message}）`;
  return "浏览器不允许读取剪贴板";
}

async function refreshClipboardPermission() {
  if (clipboardPermissionStatus) clipboardPermissionStatus.onchange = null;
  clipboardPermissionStatus = undefined;
  if (!window.isSecureContext) {
    clipboardPermissionState.value = "insecure";
    return;
  }
  if (!navigator.clipboard?.readText || !navigator.permissions?.query) {
    clipboardPermissionState.value = "unsupported";
    return;
  }
  clipboardPermissionState.value = "checking";
  try {
    const status = await navigator.permissions.query({ name: "clipboard-read" as PermissionName });
    clipboardPermissionStatus = status;
    clipboardPermissionState.value = status.state;
    status.onchange = () => {
      clipboardPermissionState.value = status.state;
      if (status.state === "granted") clipboardFallbackHintShown = false;
    };
  } catch {
    clipboardPermissionState.value = "unsupported";
  }
}

async function requestClipboardAccess() {
  if (!window.isSecureContext) {
    message.error("当前页面不是浏览器认可的安全上下文，请使用受信任的 HTTPS 地址");
    return;
  }
  if (!navigator.clipboard?.readText) {
    message.error("当前浏览器不支持读取剪贴板，请使用原生粘贴菜单或快捷键");
    return;
  }
  try {
    await navigator.clipboard.readText();
    await refreshClipboardPermission();
    if (clipboardPermissionState.value === "granted") {
      clipboardFallbackHintShown = false;
      message.success("剪贴板读取权限已允许，右键可以自动粘贴");
    } else {
      clipboardPermissionState.value = "available";
      clipboardFallbackHintShown = false;
      message.success("剪贴板读取成功，当前页面会话中将优先尝试右键自动粘贴");
    }
  } catch (error) {
    await refreshClipboardPermission();
    message.error(describeClipboardError(error));
  }
}

function handleSocketMessage(tab: TerminalTab, event: MessageEvent) {
  if (event.data instanceof ArrayBuffer) {
    const bytes = new Uint8Array(event.data);
    tab.terminal?.write(bytes, () => scheduleSearchCount(tab));
    const recordingLimit = terminalSettings.recordingMaxMiB * 1024 * 1024;
    if (tab.recording && tab.recordingSizeBytes < recordingLimit) {
      if (tab.recordingSizeBytes + bytes.byteLength <= recordingLimit) {
        const decoded = tab.recordingDecoder?.decode(bytes, { stream: true }) ?? new TextDecoder().decode(bytes);
        tab.recordingContent += decoded;
        tab.recordingEntries.push({ at: new Date(), data: decoded });
        tab.recordingSizeBytes += bytes.byteLength;
      } else if (!tab.recordingLimitReached) {
        tab.recordingLimitReached = true;
        tab.recordingSizeBytes = recordingLimit;
        message.warning(`录制缓冲区已达到 ${terminalSettings.recordingMaxMiB} MiB 上限，已停止追加`);
      }
    }
    return;
  }
  const payload = JSON.parse(String(event.data)) as Record<string, unknown>;
  if (payload.type === "ready") {
    tab.connectPayload.columns = tab.terminal?.cols ?? 100;
    tab.connectPayload.rows = tab.terminal?.rows ?? 30;
    tab.socket.send(JSON.stringify(tab.connectPayload));
    tab.connectPayload = {};
  } else if (payload.type === "status") {
    updateTab(tab, String(payload.status) as ConnectionStatus, String(payload.message ?? ""));
    if (payload.status === "connected") {
      if (tab.rememberCredential && tab.pendingCredential) credentialCache.set(tab.host.id, { ...tab.pendingCredential });
      if (tab.persistCredential) {
        const host = hosts.value.find((item) => item.id === tab.host.id);
        if (host) host.hasCredential = true;
      }
      tab.terminal?.focus();
    }
  } else if (payload.type === "host-key") {
    fingerprintRequest.value = {
      tabId: tab.id,
      host: String(payload.host),
      port: Number(payload.port),
      fingerprint: String(payload.fingerprint),
    };
    showFingerprint.value = true;
  } else if (payload.type === "warning") {
    message.warning(String(payload.message ?? "SSH 操作未完全成功"));
  } else if (payload.type === "host-key-mismatch" || payload.type === "error") {
    credentialCache.delete(tab.host.id);
    if (tab.usedStoredCredential && /authentication|认证/i.test(String(payload.message ?? ""))) {
      void deleteSshCredential(tab.host.id).then(() => {
        const host = hosts.value.find((item) => item.id === tab.host.id);
        if (host) host.hasCredential = false;
      });
    }
    updateTab(tab, "error", String(payload.message ?? "SSH 连接失败"));
    tab.terminal?.writeln(`\r\n\x1b[31m${String(payload.message ?? "SSH 连接失败")}\x1b[0m`);
  }
}

function answerFingerprint(trusted: boolean) {
  const request = fingerprintRequest.value;
  const tab = tabs.value.find((item) => item.id === request?.tabId);
  if (tab?.socket.readyState === WebSocket.OPEN) tab.socket.send(JSON.stringify({ type: "trust-host", trusted }));
  if (trusted && tab) {
    const host = hosts.value.find((item) => item.id === tab.host.id);
    if (host && request) host.hostKeySha256 = request.fingerprint;
  }
  showFingerprint.value = false;
  fingerprintRequest.value = null;
}

function sendResize(tab: TerminalTab) {
  if (tab.socket.readyState === WebSocket.OPEN && tab.terminal) {
    tab.socket.send(JSON.stringify({ type: "resize", columns: tab.terminal.cols, rows: tab.terminal.rows }));
  }
}

function updateTab(tab: TerminalTab, status: ConnectionStatus, statusMessage: string) {
  const reactiveTab = tabs.value.find((item) => item.id === tab.id);
  if (!reactiveTab) return;
  reactiveTab.status = status;
  reactiveTab.message = statusMessage;
}

function activateTab(id: string) {
  activeTabId.value = id;
  nextTick(() => {
    const tab = tabs.value.find((item) => item.id === id);
    tab?.fitAddon?.fit();
    tab?.terminal?.focus();
    if (tab) sendResize(tab);
    if (tab && showSearch.value && searchQuery.value) searchTerminal(false, true);
  });
}

function handleTabDragStart(event: DragEvent, id: string) {
  draggedTabId.value = id;
  if (event.dataTransfer) {
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", id);
  }
}

function handleTabListWheel(event: WheelEvent) {
  const element = event.currentTarget as HTMLElement;
  if (element.scrollWidth <= element.clientWidth || Math.abs(event.deltaX) >= Math.abs(event.deltaY)) return;
  event.preventDefault();
  element.scrollLeft += event.deltaY;
}

function handleTabListDragOver(event: DragEvent) {
  const element = event.currentTarget as HTMLElement;
  const bounds = element.getBoundingClientRect();
  const edgeSize = Math.min(48, bounds.width / 4);
  if (event.clientX < bounds.left + edgeSize) element.scrollLeft -= 18;
  else if (event.clientX > bounds.right - edgeSize) element.scrollLeft += 18;
}

function handleTabDrop(event: DragEvent, targetId: string) {
  const sourceId = draggedTabId.value || event.dataTransfer?.getData("text/plain") || "";
  if (!sourceId || sourceId === targetId) return;
  const sourceIndex = tabs.value.findIndex((tab) => tab.id === sourceId);
  if (sourceIndex < 0) return;
  const targetElement = event.currentTarget as HTMLElement;
  const targetBounds = targetElement.getBoundingClientRect();
  const insertAfter = event.clientX > targetBounds.left + targetBounds.width / 2;
  const [sourceTab] = tabs.value.splice(sourceIndex, 1);
  let targetIndex = tabs.value.findIndex((tab) => tab.id === targetId);
  if (insertAfter) targetIndex += 1;
  tabs.value.splice(Math.max(0, targetIndex), 0, sourceTab);
  draggedTabId.value = "";
}

function showTerminalPane() {
  activePane.value = "terminal";
  nextTick(() => {
    const tab = activeTab.value;
    tab?.fitAddon?.fit();
    tab?.terminal?.focus();
    if (tab) sendResize(tab);
  });
}

function reconnectTab(tab: TerminalTab) {
  const credential = tab.pendingCredential ? { ...tab.pendingCredential } : { method: "stored", password: "", privateKey: "", passphrase: "" };
  const restoreBuffer = tab.serializeAddon?.serialize();
  const host = tab.host;
  const remember = tab.rememberCredential;
  const persist = tab.persistCredential;
  closeTab(tab.id);
  void openTerminal(host, credential, remember, persist).then(() => {
    const replacement = tabs.value.find((item) => item.id === activeTabId.value);
    if (replacement && restoreBuffer) replacement.terminal?.write(restoreBuffer);
  });
}

function searchTerminal(previous: boolean, incremental = false) {
  const tab = activeTab.value;
  const addon = tab?.searchAddon;
  if (!tab || !addon || !searchQuery.value) return;
  const termChanged = tab.searchTerm !== searchQuery.value;
  updateSearchCount(tab, searchQuery.value);
  if (tab.searchResultCount > 0) {
    if (termChanged || incremental) tab.searchResultIndex = 0;
    else if (previous) tab.searchResultIndex = (tab.searchResultIndex - 1 + tab.searchResultCount) % tab.searchResultCount;
    else tab.searchResultIndex = (tab.searchResultIndex + 1) % tab.searchResultCount;
  }
  tab.searchTerm = searchQuery.value;
  updateSearchCount(tab, searchQuery.value);
  searchTargetIndex.value = tab.searchResultCount ? tab.searchResultIndex + 1 : null;
  const options = { ...terminalSearchOptions, incremental };
  if (previous) addon.findPrevious(searchQuery.value, options);
  else addon.findNext(searchQuery.value, options);
}

function openSearch() {
  showSearch.value = true;
  searchTargetIndex.value = activeTab.value?.searchResultCount ? activeTab.value.searchResultIndex + 1 : null;
  nextTick(() => {
    searchInput.value?.focus();
    if (searchQuery.value) searchTerminal(false, true);
  });
}

function closeSearch() {
  showSearch.value = false;
  tabs.value.forEach((tab) => {
    tab.searchAddon?.clearDecorations();
    resetSearchResults(tab);
  });
  activeTab.value?.terminal?.focus();
}

function resetSearchResults(tab?: TerminalTab) {
  if (!tab) return;
  tab.searchResultIndex = 0;
  tab.searchResultCount = 0;
  tab.searchTerm = "";
  tab.searchMatches = [];
  if (tab.id === activeTabId.value) searchTargetIndex.value = null;
  renderSearchOverlay(tab);
}

function updateSearchCount(tab: TerminalTab, term: string) {
  const buffer = tab.terminal?.buffer.active;
  if (!buffer || !term) {
    resetSearchResults(tab);
    return;
  }
  const terminal = tab.terminal!;
  const needle = term.toLocaleLowerCase();
  const matches: Array<Array<{ row: number; column: number; width: number }>> = [];
  let logicalText = "";
  let cells: Array<{ start: number; end: number; row: number; column: number; width: number }> = [];
  const collectLine = () => {
    const value = logicalText.toLocaleLowerCase();
    let offset = 0;
    while ((offset = value.indexOf(needle, offset)) >= 0) {
      const end = offset + needle.length;
      const covered = cells.filter((cell) => cell.end > offset && cell.start < end);
      const ranges: Array<{ row: number; column: number; width: number }> = [];
      for (const cell of covered) {
        const last = ranges[ranges.length - 1];
        if (last && last.row === cell.row && last.column + last.width === cell.column) last.width += cell.width;
        else ranges.push({ row: cell.row, column: cell.column, width: cell.width });
      }
      if (ranges.length) matches.push(ranges);
      offset += Math.max(1, needle.length);
    }
    logicalText = "";
    cells = [];
  };
  for (let index = 0; index < buffer.length; ++index) {
    const line = buffer.getLine(index);
    if (!line) continue;
    if (!line.isWrapped && logicalText) collectLine();
    for (let column = 0; column < terminal.cols; ++column) {
      const cell = line.getCell(column);
      if (!cell || cell.getWidth() === 0) continue;
      const value = cell.getChars() || " ";
      const start = logicalText.length;
      logicalText += value;
      cells.push({ start, end: logicalText.length, row: index, column, width: Math.max(1, cell.getWidth()) });
    }
  }
  if (logicalText) collectLine();
  tab.searchResultCount = matches.length;
  tab.searchMatches = matches;
  if (!matches.length) tab.searchResultIndex = 0;
  else tab.searchResultIndex = Math.min(tab.searchResultIndex, matches.length - 1);
  if (tab.id === activeTabId.value) searchTargetIndex.value = matches.length ? tab.searchResultIndex + 1 : null;
  renderSearchOverlay(tab);
}

function jumpToSearchIndex() {
  const tab = activeTab.value;
  if (!tab?.terminal || !tab.searchResultCount) {
    message.warning("当前没有搜索结果");
    return;
  }
  const target = Math.trunc(Number(searchTargetIndex.value));
  if (!Number.isFinite(target) || target < 1 || target > tab.searchResultCount) {
    message.warning(`请输入 1 到 ${tab.searchResultCount} 之间的序号`);
    return;
  }
  tab.searchResultIndex = target - 1;
  const firstRange = tab.searchMatches[tab.searchResultIndex]?.[0];
  if (firstRange) {
    tab.terminal.scrollToLine(Math.max(0, firstRange.row - Math.floor(tab.terminal.rows / 2)));
    tab.terminal.select(firstRange.column, firstRange.row, firstRange.width);
  }
  renderSearchOverlay(tab);
  tab.terminal.focus();
}

function renderSearchOverlay(tab: TerminalTab) {
  const terminal = tab.terminal;
  const overlay = tab.searchOverlay;
  if (!terminal || !overlay) return;
  overlay.replaceChildren();
  if (!showSearch.value || !searchQuery.value) return;
  const viewportY = terminal.buffer.active.viewportY;
  const cellWidth = overlay.parentElement!.clientWidth / terminal.cols;
  const cellHeight = overlay.parentElement!.clientHeight / terminal.rows;
  tab.searchMatches.forEach((ranges, matchIndex) => ranges.forEach((range) => {
    const viewportRow = range.row - viewportY;
    if (viewportRow < 0 || viewportRow >= terminal.rows) return;
    const highlight = document.createElement("span");
    highlight.className = matchIndex === tab.searchResultIndex ? "ssh-search-match active" : "ssh-search-match";
    highlight.style.left = `${range.column * cellWidth}px`;
    highlight.style.top = `${viewportRow * cellHeight}px`;
    highlight.style.width = `${range.width * cellWidth}px`;
    highlight.style.height = `${cellHeight}px`;
    overlay.appendChild(highlight);
  }));
}

function scheduleSearchCount(tab: TerminalTab) {
  if (!showSearch.value || tab.id !== activeTabId.value || !searchQuery.value) return;
  if (tab.searchCountTimer) window.clearTimeout(tab.searchCountTimer);
  tab.searchCountTimer = window.setTimeout(() => updateSearchCount(tab, searchQuery.value), 200);
}

function handleGlobalShortcut(event: KeyboardEvent) {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "f" && activeTab.value && activePane.value === "terminal") {
    event.preventDefault();
    openSearch();
  }
}

function updateRenderer(tab: TerminalTab, renderer: "webgl" | "canvas") {
  const reactiveTab = tabs.value.find((item) => item.id === tab.id);
  if (reactiveTab) reactiveTab.renderer = renderer;
}

function toggleRecording(tab: TerminalTab) {
  if (!tab.recording) {
    recordingTargetId.value = tab.id;
    Object.assign(recordingDraft, { stripAnsi: true, timestamps: false });
    showRecordingOptions.value = true;
    return;
  }
  tab.recording = false;
  const decoderTail = tab.recordingDecoder?.decode() ?? "";
  if (decoderTail) {
    tab.recordingContent += decoderTail;
    tab.recordingEntries.push({ at: new Date(), data: decoderTail });
  }
  const body = tab.recordingTimestamps
    ? formatTimestampedRecording(tab.recordingEntries, tab.recordingStripAnsi)
    : normalizeRecordingText(tab.recordingStripAnsi ? stripAnsi(tab.recordingContent) : tab.recordingContent);
  const startedAt = tab.recordingStartedAt ? formatRecordingTime(new Date(tab.recordingStartedAt)) : "";
  const content = `# ${tab.host.name} ${startedAt}\n${body}`;
  const filenameTime = formatFilenameTimestamp(tab.recordingStartedAt ? new Date(tab.recordingStartedAt) : new Date());
  downloadText(`${safeFilename(tab.host.name)}-${filenameTime}.log`, content, "text/plain");
  tab.recordingContent = "";
  tab.recordingEntries = [];
  tab.recordingDecoder = undefined;
  tab.recordingSizeBytes = 0;
  tab.recordingLimitReached = false;
  message.success("录制已停止并下载");
}

function startRecording() {
  const tab = tabs.value.find((item) => item.id === recordingTargetId.value);
  if (!tab) return;
  tab.recording = true;
  tab.recordingStartedAt = new Date().toISOString();
  tab.recordingContent = "";
  tab.recordingEntries = [];
  tab.recordingDecoder = new TextDecoder();
  tab.recordingStripAnsi = recordingDraft.stripAnsi;
  tab.recordingTimestamps = recordingDraft.timestamps;
  tab.recordingSizeBytes = 0;
  tab.recordingLimitReached = false;
  showRecordingOptions.value = false;
  message.success("会话录制已开始");
}

function stripAnsi(value: string) {
  return value
    .replace(/\x1B(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1B\\)|[@-_])/g, "")
    .replace(/\x9B[0-?]*[ -/]*[@-~]/g, "")
    .replace(/[\x00-\x08\x0B\x0C\x0E-\x1A\x1C-\x1F\x7F]/g, "");
}

function formatTimestampedRecording(entries: Array<{ at: Date; data: string }>, filterAnsi: boolean) {
  let output = "";
  let pending = "";
  let pendingTime = "";
  for (const entry of entries) {
    const value = normalizeRecordingText(filterAnsi ? stripAnsi(entry.data) : entry.data);
    const parts = value.split("\n");
    for (let index = 0; index < parts.length; ++index) {
      if (!pendingTime && parts[index]) pendingTime = formatRecordingTime(entry.at);
      pending += parts[index];
      if (index < parts.length - 1) {
        output += `${pendingTime ? `[${pendingTime}] ` : ""}${pending}\n`;
        pending = "";
        pendingTime = "";
      }
    }
  }
  if (pending) output += `${pendingTime ? `[${pendingTime}] ` : ""}${pending}`;
  return output;
}

function formatRecordingTime(value: Date) {
  const pad = (part: number, length = 2) => String(part).padStart(length, "0");
  return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())} ${pad(value.getHours())}:${pad(value.getMinutes())}:${pad(value.getSeconds())}.${pad(value.getMilliseconds(), 3)}`;
}

function formatFilenameTimestamp(value: Date) {
  const pad = (part: number, length = 2) => String(part).padStart(length, "0");
  const offsetMinutes = -value.getTimezoneOffset();
  const offsetSign = offsetMinutes >= 0 ? "+" : "-";
  const offsetHours = Math.floor(Math.abs(offsetMinutes) / 60);
  const offsetRemainder = Math.abs(offsetMinutes) % 60;
  return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())}T${pad(value.getHours())}-${pad(value.getMinutes())}-${pad(value.getSeconds())}.${pad(value.getMilliseconds(), 3)}${offsetSign}${pad(offsetHours)}-${pad(offsetRemainder)}`;
}

function normalizeRecordingText(value: string) {
  return value.replace(/\r\n/g, "\n").replace(/\r/g, "");
}

function loadTerminalSettings(): TerminalSettings {
  const defaults: TerminalSettings = { scrollbackLines: 50000, recordingMaxMiB: 50, copyOnSelect: false, pasteOnRightClick: false };
  try {
    const saved = JSON.parse(localStorage.getItem("ssh-terminal-settings") || "{}") as Partial<TerminalSettings>;
    return {
      scrollbackLines: clampNumber(saved.scrollbackLines, 1000, 500000, defaults.scrollbackLines),
      recordingMaxMiB: clampNumber(saved.recordingMaxMiB, 1, 500, defaults.recordingMaxMiB),
      copyOnSelect: saved.copyOnSelect === true,
      pasteOnRightClick: saved.pasteOnRightClick === true,
    };
  } catch {
    return defaults;
  }
}

function clampNumber(value: unknown, min: number, max: number, fallback: number) {
  const number = Math.trunc(Number(value));
  return Number.isFinite(number) ? Math.max(min, Math.min(max, number)) : fallback;
}

function openTerminalSettings() {
  Object.assign(terminalSettingsDraft, terminalSettings);
  showTerminalSettings.value = true;
  void refreshClipboardPermission();
}

function saveTerminalSettings() {
  terminalSettings.scrollbackLines = clampNumber(terminalSettingsDraft.scrollbackLines, 1000, 500000, 50000);
  terminalSettings.recordingMaxMiB = clampNumber(terminalSettingsDraft.recordingMaxMiB, 1, 500, 50);
  terminalSettings.copyOnSelect = terminalSettingsDraft.copyOnSelect === true;
  terminalSettings.pasteOnRightClick = terminalSettingsDraft.pasteOnRightClick === true;
  Object.assign(terminalSettingsDraft, terminalSettings);
  localStorage.setItem("ssh-terminal-settings", JSON.stringify(terminalSettings));
  showTerminalSettings.value = false;
  message.success("终端设置已保存；回滚行数将在新终端中生效");
}

function loadSnippets(): CommandSnippet[] {
  try { return JSON.parse(localStorage.getItem("ssh-command-snippets") || "[]") as CommandSnippet[]; }
  catch { return []; }
}

function persistSnippets() {
  localStorage.setItem("ssh-command-snippets", JSON.stringify(snippets.value));
}

function saveSnippet() {
  if (!snippetDraft.name.trim() || !snippetDraft.command.trim()) return message.warning("名称和命令不能为空");
  const value: CommandSnippet = {
    id: editingSnippetId.value || crypto.randomUUID(),
    name: snippetDraft.name.trim(),
    command: snippetDraft.command.trim(),
    pinned: snippetDraft.pinned,
  };
  if (editingSnippetId.value) snippets.value = snippets.value.map((snippet) => snippet.id === editingSnippetId.value ? value : snippet);
  else snippets.value.push(value);
  cancelSnippetEdit();
  persistSnippets();
}

function openSnippets() {
  showSnippets.value = true;
}

function editSnippet(snippet: CommandSnippet) {
  editingSnippetId.value = snippet.id;
  Object.assign(snippetDraft, { name: snippet.name, command: snippet.command, pinned: snippet.pinned !== false });
}

function cancelSnippetEdit() {
  editingSnippetId.value = "";
  Object.assign(snippetDraft, { name: "", command: "", pinned: true });
}

function deleteSnippet(id: string) {
  snippets.value = snippets.value.filter((item) => item.id !== id);
  if (editingSnippetId.value === id) cancelSnippetEdit();
  persistSnippets();
}

function sendSnippet(snippet: CommandSnippet, closeModal = true) {
  const tab = activeTab.value;
  if (!tab || tab.socket.readyState !== WebSocket.OPEN || tab.status !== "connected") return message.warning("SSH 尚未连接");
  tab.socket.send(new TextEncoder().encode(`${snippet.command}\n`));
  if (closeModal) showSnippets.value = false;
  tab.terminal?.focus();
}

function toggleQuickSnippets() {
  showQuickSnippets.value = !showQuickSnippets.value;
  localStorage.setItem("ssh-show-quick-snippets", String(showQuickSnippets.value));
  nextTick(() => activeTab.value?.fitAddon?.fit());
}

function sendCommand() {
  const tab = activeTab.value;
  const command = commandDraft.value.trimEnd();
  if (!command) return;
  if (!tab || tab.socket.readyState !== WebSocket.OPEN || tab.status !== "connected") {
    message.warning("SSH 尚未连接");
    return;
  }
  tab.socket.send(new TextEncoder().encode(`${command}\n`));
  commandDraft.value = "";
  tab.terminal?.focus();
}

function handleCommandKeydown(event: KeyboardEvent) {
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
    event.preventDefault();
    sendCommand();
  }
}

function exportConfiguration() {
  downloadText("space-station-ssh.json", JSON.stringify({ version: 1, hosts: hosts.value, snippets: snippets.value, terminalSettings }, null, 2), "application/json");
}

async function importConfiguration(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) return;
  try {
    const data = JSON.parse(await file.text()) as { hosts?: SshHost[]; snippets?: CommandSnippet[]; terminalSettings?: Partial<TerminalSettings> };
    if (!Array.isArray(data.hosts)) throw new Error("配置中缺少主机列表");
    const importedHosts = data.hosts.map((host) => ({ ...host, hasCredential: false }));
    await saveSshHosts(importedHosts);
    hosts.value = importedHosts;
    if (Array.isArray(data.snippets)) snippets.value = data.snippets;
    if (data.terminalSettings) {
      terminalSettings.scrollbackLines = clampNumber(data.terminalSettings.scrollbackLines, 1000, 500000, terminalSettings.scrollbackLines);
      terminalSettings.recordingMaxMiB = clampNumber(data.terminalSettings.recordingMaxMiB, 1, 500, terminalSettings.recordingMaxMiB);
      if (typeof data.terminalSettings.copyOnSelect === "boolean") terminalSettings.copyOnSelect = data.terminalSettings.copyOnSelect;
      if (typeof data.terminalSettings.pasteOnRightClick === "boolean") terminalSettings.pasteOnRightClick = data.terminalSettings.pasteOnRightClick;
      localStorage.setItem("ssh-terminal-settings", JSON.stringify(terminalSettings));
    }
    persistSnippets();
    message.success("SSH 配置已导入（凭据不会导入）");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "配置导入失败");
  } finally {
    input.value = "";
  }
}

function safeFilename(value: string) {
  return value.replace(/[\\/:*?"<>|]/g, "_") || "ssh-session";
}

function downloadText(filename: string, content: string, type: string) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}

async function openPortForwards() {
  showPortForwards.value = true;
  if (!forwardDraft.hostId && hosts.value[0]) forwardDraft.hostId = hosts.value[0].id;
  await refreshForwards();
}

async function refreshForwards() {
  try { portForwards.value = (await fetchSshPortForwards()).forwards; }
  catch (error) { message.error(error instanceof Error ? error.message : "端口转发状态读取失败"); }
}

async function startForward() {
  if (!forwardDraft.hostId || !forwardDraft.remoteHost || !forwardDraft.localPort || !forwardDraft.remotePort) {
    message.warning("请填写完整的转发参数");
    return;
  }
  try {
    await startSshPortForward({ ...forwardDraft });
    await new Promise((resolve) => window.setTimeout(resolve, 150));
    await refreshForwards();
  } catch (error) {
    message.error(error instanceof Error ? error.message : "端口转发启动失败");
  }
}

async function stopForward(id: string) {
  await stopSshPortForward(id);
  await refreshForwards();
}

function setTerminalElement(id: string, element: HTMLElement | null) {
  if (element) terminalElements.set(id, element);
  else terminalElements.delete(id);
}

function closeTab(id: string) {
  const index = tabs.value.findIndex((tab) => tab.id === id);
  if (index < 0) return;
  const [tab] = tabs.value.splice(index, 1);
  disposeTab(tab);
  if (activeTabId.value === id) activeTabId.value = tabs.value[Math.min(index, tabs.value.length - 1)]?.id ?? "";
}

function disposeTab(tab: TerminalTab) {
  if (tab.searchCountTimer) window.clearTimeout(tab.searchCountTimer);
  tab.clipboardCleanup?.();
  tab.searchOverlay?.remove();
  tab.resizeObserver?.disconnect();
  tab.webglAddon?.dispose();
  tab.terminal?.dispose();
  if (tab.socket.readyState === WebSocket.OPEN || tab.socket.readyState === WebSocket.CONNECTING) tab.socket.close();
  if (tab.pendingCredential) {
    tab.pendingCredential.password = "";
    tab.pendingCredential.privateKey = "";
    tab.pendingCredential.passphrase = "";
  }
  terminalElements.delete(tab.id);
}
</script>

<style scoped>
.ssh-app { height: 100dvh; min-height: 0; display: grid; grid-template-columns: 280px minmax(0, 1fr); overflow: hidden; background: #101418; color: #d8dee9; }
.ssh-sidebar { box-sizing: border-box; min-width: 0; min-height: 0; height: 100%; padding: 14px; display: flex; flex-direction: column; gap: 14px; overflow: hidden; border-right: 1px solid #27313a; background: #171d22; }
.ssh-brand-row { display: grid; grid-template-columns: 38px minmax(0, 1fr) auto; align-items: center; gap: 10px; }
.ssh-brand-row strong, .ssh-brand-row small { display: block; }
.ssh-brand-row small { margin-top: 2px; color: #7f8d99; font-size: 11px; }
.ssh-home { width: 38px; height: 38px; display: grid; place-items: center; border-radius: 8px; background: #79a8a5; color: #101418; font-weight: 900; text-decoration: none; }
.ssh-host-scroll { min-height: 0; flex: 1 1 0; }
.ssh-host-list { min-height: 100%; padding-right: 9px; display: flex; flex-direction: column; gap: 5px; }
.ssh-host { width: 100%; flex: 0 0 auto; padding: 9px; display: grid; grid-template-columns: 34px minmax(0, 1fr) 30px; align-items: center; gap: 9px; border: 0; border-radius: 7px; background: transparent; color: inherit; text-align: left; cursor: pointer; }
.ssh-host:hover { background: #222b32; }
.ssh-host-icon { width: 34px; height: 34px; display: grid; place-items: center; border-radius: 7px; background: #293740; color: #9fc4c2; font-weight: 800; }
.ssh-host-copy { min-width: 0; }
.ssh-host-copy strong, .ssh-host-copy small { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ssh-host-copy small { margin-top: 3px; color: #83919c; font-size: 11px; }
.ssh-edit-button { color: #c8d4dc; background: #2b3740; }
.ssh-edit-button:hover { color: #101418; background: #9bc7c4; }
.ssh-empty { padding: 28px 8px; display: grid; justify-items: center; gap: 8px; color: #7f8d99; font-size: 13px; }
.ssh-workspace { position: relative; min-width: 0; min-height: 0; display: grid; grid-template-rows: 42px minmax(0, 1fr); }
.ssh-tabs { min-width: 0; display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: stretch; border-bottom: 1px solid #27313a; background: #151a1f; overflow: hidden; }
.ssh-tabs :deep(.n-button--secondary) { color: #e4edf2; background: #34434e; border-color: #536570; }
.ssh-tabs :deep(.n-button--secondary:hover) { color: #101418; background: #9bc7c4; }
.ssh-tab-list { min-width: 0; display: flex; overflow-x: auto; overscroll-behavior-x: contain; scrollbar-width: none; }
.ssh-tab-list::-webkit-scrollbar, .ssh-tab-actions::-webkit-scrollbar { display: none; }
.ssh-tab-actions { max-width: 70vw; padding: 0 4px; display: flex; align-items: center; gap: 4px; overflow-x: auto; scrollbar-width: none; background: #151a1f; box-shadow: -8px 0 12px #101418aa; }
.ssh-mobile-hosts { display: none; }
.ssh-tab { min-width: 130px; max-width: 220px; padding: 0 12px; display: flex; align-items: center; gap: 8px; border: 0; border-right: 1px solid #27313a; border-bottom: 2px solid transparent; background: transparent; color: #8997a2; cursor: pointer; }
.ssh-tab.active { border-bottom-color: #79a8a5; background: #101418; color: #e5e9ef; }
.ssh-tab.dragging { opacity: .45; }
.ssh-tab span:nth-child(2) { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ssh-tab-close { margin-left: auto; flex: 0 0 auto; }
.ssh-status-dot { width: 7px; height: 7px; flex: 0 0 auto; border-radius: 50%; background: #87909a; }
.ssh-status-dot.connecting, .ssh-status-dot.authenticating { background: #e4b860; }
.ssh-status-dot.connected { background: #66bd83; }
.ssh-status-dot.error { background: #e06c75; }
.ssh-pane-switch { align-self: center; margin: 0 4px; padding: 2px; display: flex; border: 1px solid #2b3740; border-radius: 6px; background: #101418; }
.ssh-pane-switch button { padding: 3px 9px; border: 0; border-radius: 4px; background: transparent; color: #7f8d99; font-size: 12px; cursor: pointer; }
.ssh-pane-switch button.active { background: #2b3a42; color: #dce4e9; }
.ssh-search-bar { position: absolute; z-index: 5; top: 47px; right: 12px; width: min(650px, calc(100% - 24px)); padding: 8px; display: flex; align-items: center; gap: 7px; border: 1px solid #4d606c; border-radius: 7px; background: #1a2228; box-shadow: 0 8px 24px #0008; }
.ssh-search-bar :deep(.n-button) { color: #e5edf2; background: #34434e; border-color: #5d707c; }
.ssh-search-bar :deep(.n-button:hover) { color: #101418; background: #9bc7c4; }
.ssh-search-bar > :first-child { min-width: 160px; flex: 1; }
.ssh-search-count { min-width: 54px; color: #dce5ea; font: 12px/1.4 monospace; text-align: center; white-space: nowrap; }
.ssh-search-index { width: 72px; flex: 0 0 72px; }
.ssh-config-actions { display: flex; flex-wrap: wrap; gap: 6px; margin-top: -7px; }
.ssh-config-actions :deep(.n-button) { color: #d5dfe5; background: #26323a; border-color: #41515d; }
.ssh-config-actions :deep(.n-button:hover) { color: #101418; background: #9bc7c4; }
.ssh-config-actions input { display: none; }
.ssh-renderer, .ssh-status-text { align-self: center; padding: 2px 6px; border: 1px solid #3c4851; border-radius: 4px; color: #8c9aa4; font: 10px/1.4 monospace; white-space: nowrap; }
.ssh-renderer.webgl { border-color: #3b7256; color: #76cf96; background: #193124; }
.ssh-status-text { max-width: 220px; overflow: hidden; text-overflow: ellipsis; }
.ssh-status-text.connected { border-color: #3b7256; color: #76cf96; background: #193124; }
.ssh-status-text.connecting, .ssh-status-text.authenticating { border-color: #816b35; color: #e4c36e; background: #382e18; }
.ssh-status-text.error { border-color: #814751; color: #f08a95; background: #381d22; }
.ssh-status-text.closed { border-color: #56616a; color: #a8b2ba; background: #252c31; }
.ssh-terminal-pane { min-width: 0; min-height: 0; display: grid; grid-template-rows: minmax(0, 1fr) auto; overflow: hidden; }
.ssh-terminal { box-sizing: border-box; min-width: 0; min-height: 0; padding: 8px 8px 12px; overflow: hidden; background: #101418; }
.ssh-terminal :deep(.xterm), .ssh-terminal :deep(.xterm-viewport) { height: 100%; }
.ssh-terminal :deep(.ssh-search-overlay) { position: absolute; z-index: 30; inset: 0; overflow: hidden; pointer-events: none; }
.ssh-terminal :deep(.ssh-search-match) { position: absolute; box-sizing: border-box; border: 1px solid #e1ca4d; background: rgb(167 145 25 / 62%); }
.ssh-terminal :deep(.ssh-search-match.active) { border: 2px solid #ffe39a; background: rgb(224 92 17 / 78%); }
.ssh-welcome { display: grid; place-content: center; justify-items: center; color: #788690; text-align: center; }
.ssh-welcome-mark { color: #79a8a5; font: 700 52px/1 monospace; }
.ssh-welcome h1 { margin: 18px 0 7px; color: #b9c3ca; font-size: 20px; }
.ssh-welcome p { margin: 0; font-size: 13px; }
.ssh-welcome-action { margin-top: 20px; }
.ssh-form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.ssh-form-grid--connection { grid-template-columns: 140px 1fr; }
.ssh-dialog-actions { display: flex; align-items: center; gap: 10px; }
.ssh-dialog-spacer { flex: 1; }
.ssh-dialog-actions--end { justify-content: flex-end; }
.ssh-reset-key { margin-left: 8px; }
.ssh-forget-credential { margin-left: 8px; }
.ssh-connect-target { margin: 0 0 16px; color: #637382; font-family: monospace; }
.ssh-field-hint { margin: 7px 0 0; color: #7a8792; font-size: 12px; }
.ssh-persist-credential { margin-left: 18px; }
.ssh-private-key { margin: 12px 0; font-family: monospace; }
.ssh-command-panel { padding: 7px 9px 9px; display: grid; gap: 7px; border-top: 1px solid #34414b; background: #171e23; }
.ssh-command-toolbar { min-width: 0; display: flex; align-items: center; gap: 7px; }
.ssh-command-toolbar :deep(.n-button) { color: #dce6eb; border-color: #50616c; background: #2c3941; }
.ssh-quick-snippets { min-width: 0; flex: 1; display: flex; gap: 6px; overflow-x: auto; }
.ssh-quick-snippets > span { align-self: center; color: #71808b; font-size: 12px; }
.ssh-command-editor { display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: stretch; gap: 8px; }
.ssh-command-editor :deep(textarea) { font-family: "SFMono-Regular", Consolas, monospace; }
.ssh-snippet-editor { display: grid; grid-template-columns: 1fr auto; align-items: center; gap: 10px; }
.ssh-snippet-editor > :nth-child(2) { grid-column: 1 / -1; }
.ssh-snippet-editor-actions { display: flex; justify-content: flex-end; gap: 8px; }
.ssh-snippet-list { margin-top: 16px; display: grid; gap: 7px; max-height: 280px; overflow: auto; }
.ssh-snippet-row { padding: 8px 10px; display: flex; align-items: center; gap: 8px; border: 1px solid #e3e7ea; border-radius: 6px; }
.ssh-snippet-row > button:first-child { min-width: 0; flex: 1; display: grid; gap: 4px; border: 0; background: transparent; text-align: left; cursor: pointer; }
.ssh-snippet-row code { overflow: hidden; color: #687783; text-overflow: ellipsis; white-space: nowrap; }
.ssh-forward-form { margin-top: 14px; display: grid; grid-template-columns: 1fr 130px; gap: 10px; }
.ssh-forward-form > :last-child { grid-column: 2; }
.ssh-forward-list { margin-top: 16px; display: grid; gap: 7px; }
.ssh-forward-row { padding: 9px 10px; display: flex; align-items: center; gap: 10px; border: 1px solid #e3e7ea; border-radius: 6px; }
.ssh-forward-row > div { min-width: 0; flex: 1; }
.ssh-forward-row strong, .ssh-forward-row small { display: block; }
.ssh-forward-row small { margin-top: 3px; color: #687783; }
.ssh-recording-options { display: grid; gap: 12px; }
.ssh-recording-options p { margin: 4px 0 0; color: #687783; font-size: 12px; }
.ssh-settings-recording { margin-top: 18px; }
.ssh-settings-switches { display: grid; gap: 10px; }
.ssh-clipboard-permission { padding-top: 2px; display: flex; align-items: center; justify-content: space-between; gap: 12px; color: #687783; font-size: 12px; }
.ssh-fingerprint { display: grid; grid-template-columns: 70px minmax(0, 1fr); gap: 10px; margin: 18px 0 0; }
.ssh-fingerprint dt { color: #637382; }
.ssh-fingerprint dd { margin: 0; overflow-wrap: anywhere; font-family: monospace; }
:global(.ssh-dialog) { --ssh-dialog-width: min(560px, calc(100vw - 32px)); }
@media (max-width: 720px) {
  .ssh-app { width: 100vw; grid-template-columns: minmax(0, 1fr); overflow: hidden; }
  .ssh-sidebar { width: 100%; padding: 10px; border-right: 0; }
  .ssh-app:not(.ssh-app--terminal-open) .ssh-workspace { display: none; }
  .ssh-app--terminal-open .ssh-sidebar { display: none; }
  .ssh-workspace { width: 100%; }
  .ssh-tabs { grid-template-columns: minmax(74px, 1fr) auto; }
  .ssh-tab-actions { max-width: calc(100vw - 74px); }
  .ssh-mobile-hosts { display: inline-flex; flex: 0 0 auto; margin: 4px; }
  .ssh-status-text { display: none; }
  .ssh-form-grid, .ssh-form-grid--connection { grid-template-columns: 1fr; gap: 0; }
  .ssh-persist-credential { display: flex; margin: 10px 0 0; }
  .ssh-forward-form { grid-template-columns: 1fr; }
  .ssh-forward-form > :last-child { grid-column: 1; }
  .ssh-search-bar { flex-wrap: wrap; }
  .ssh-search-bar > :first-child { flex-basis: 100%; }
  .ssh-command-toolbar { flex-wrap: wrap; }
  .ssh-quick-snippets { order: 3; flex-basis: 100%; }
  .ssh-command-editor { grid-template-columns: minmax(0, 1fr) 64px; }
  :global(.ssh-dialog) {
    --ssh-dialog-width: 100vw;
    max-width: 100vw;
    max-height: 100dvh;
    margin: 0;
    border-radius: 0;
  }
}
</style>
