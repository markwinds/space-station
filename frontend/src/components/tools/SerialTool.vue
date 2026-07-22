<template>
  <div class="serial-app" :class="{ 'serial-app--session-open': activeView && !mobileSetup }">
    <aside class="serial-sidebar">
      <div class="serial-brand">
        <router-link to="/" aria-label="返回首页">SS</router-link>
        <div><strong>串口终端</strong><small>浏览器 / 服务器</small></div>
      </div>

      <div class="serial-source-switch">
        <button type="button" :class="{ active: source === 'browser' }" @click="source = 'browser'">浏览器串口</button>
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
      </template>
      <template v-else>
        <div class="serial-field-heading">
          <span>后端主机设备</span>
          <n-button size="tiny" secondary :loading="loadingServerPorts" @click="refreshServerPorts">刷新</n-button>
        </div>
        <n-select v-model:value="serverPortId" filterable :options="serverPortOptions" placeholder="选择服务器串口" />
        <p class="serial-hint">设备位于运行 Space Station 后端的主机，不一定是当前浏览器所在设备。</p>
      </template>

      <div class="serial-divider" />
      <div class="serial-grid">
        <label><span>波特率</span><n-select v-model:value="settings.baudRate" filterable tag :options="baudOptions" /></label>
        <label><span>数据位</span><n-select v-model:value="settings.dataBits" :options="dataBitsOptions" /></label>
        <label><span>停止位</span><n-select v-model:value="settings.stopBits" :options="stopBitsOptions" /></label>
        <label><span>校验位</span><n-select v-model:value="settings.parity" :options="parityOptions" /></label>
      </div>
      <label class="serial-flow"><span>流控制</span><n-select v-model:value="settings.flowControl" :options="flowOptions" /></label>
      <n-button class="serial-open" type="primary" :disabled="!selectedPortAvailable" @click="openSelectedPort">
        打开串口
      </n-button>

      <div class="serial-rules">
        <strong>占用规则</strong>
        <p>本程序内重复打开会共享同一连接；其他程序已占用时会显示系统返回的占用错误。最后一个视图关闭后才释放设备。</p>
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
          <n-button size="tiny" secondary @click="duplicateActiveView">新视图</n-button>
          <n-button v-if="activeSession.status === 'error' || activeSession.status === 'closed'" size="tiny" secondary @click="reconnectActive">重连</n-button>
          <span class="serial-location">{{ activeSession.location === 'browser' ? '浏览器' : '服务器' }}</span>
          <span class="serial-status-label" :class="activeSession.status">{{ statusText(activeSession) }}</span>
        </div>
      </header>

      <section v-if="views.length === 0" class="serial-empty">
        <div>›_</div>
        <h1>打开一个串口开始通信</h1>
        <p>接收到的数据会保留在会话缓冲区，新建共享视图也能看到已有内容。</p>
        <n-button class="serial-empty-mobile" type="primary" @click="mobileSetup = true">选择串口</n-button>
      </section>

      <template v-for="view in views" :key="view.id">
        <section v-show="view.id === activeViewId" class="serial-terminal-section">
          <web-terminal
            class="serial-terminal"
            :scrollback="10000"
            :font-size="terminalFontSize"
            background="#0d1318"
            foreground="#d7e0e7"
            cursor="#8ec5c3"
            selection-background="#315063"
            @ready="handleTerminalReady(view, $event)"
            @data="handleTerminalData(view, $event)"
          />
          <div v-if="sessionFor(view)?.error" class="serial-error">{{ sessionFor(view)?.error }}</div>
          <div class="serial-composer">
            <n-select v-model:value="view.sendMode" class="serial-send-mode" :options="sendModeOptions" size="small" />
            <n-input
              v-model:value="view.command"
              class="serial-command"
              :placeholder="view.sendMode === 'hex' ? '例如：48 65 6C 6C 6F' : '输入要发送的内容'"
              @keyup.ctrl.enter="sendFromComposer(view)"
              @keyup.meta.enter="sendFromComposer(view)"
            />
            <n-select v-if="view.sendMode === 'text'" v-model:value="view.lineEnding" class="serial-line-ending" :options="lineEndingOptions" size="small" />
            <n-button type="primary" :disabled="sessionFor(view)?.status !== 'open'" @click="sendFromComposer(view)">发送</n-button>
          </div>
        </section>
      </template>
    </main>
  </div>
</template>

<script setup lang="ts">
import { NAlert, NButton, NInput, NSelect, useMessage } from "naive-ui";
import { computed, markRaw, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { fetchBackendSerialPorts, type BackendSerialPort } from "@/api";
import WebTerminal from "../terminal/WebTerminal.vue";
import type { WebTerminalHandle, WebTerminalReadyEvent } from "../terminal/WebTerminal.types";

type Location = "browser" | "server";
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
  chunks: Uint8Array[];
  bufferedBytes: number;
  socket?: WebSocket;
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
}

const message = useMessage();
const terminalFontSize = window.innerWidth <= 760 ? 13 : 14;
const source = ref<Location>("server");
const browserSupported = "serial" in navigator;
const browserPorts = ref<BrowserPortItem[]>([]);
const browserPortId = ref<string | null>(null);
const serverPorts = ref<BackendSerialPort[]>([]);
const serverPortId = ref<string | null>(null);
const loadingServerPorts = ref(false);
const sessions = reactive(new Map<string, SerialSession>());
const views = reactive<SerialView[]>([]);
const activeViewId = ref("");
const mobileSetup = ref(false);
const browserIds = new WeakMap<SerialPort, string>();
let browserSequence = 0;

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
const selectedPortAvailable = computed(() => source.value === "browser" ? Boolean(browserPortId.value) : Boolean(serverPortId.value));
const activeView = computed(() => views.find((view) => view.id === activeViewId.value));
const activeSession = computed(() => activeView.value ? sessions.get(activeView.value.sessionKey) : undefined);

function browserPortLabel(port: SerialPort, index: number) {
  const info = port.getInfo();
  const ids = info.usbVendorId === undefined ? "已授权设备" : `USB ${info.usbVendorId.toString(16).padStart(4, "0")}:${(info.usbProductId ?? 0).toString(16).padStart(4, "0")}`;
  return `${ids} · 串口 ${index + 1}`;
}

function registerBrowserPorts(ports: SerialPort[]) {
  browserPorts.value = ports.map((port, index) => {
    let id = browserIds.get(port);
    if (!id) { id = `browser-${++browserSequence}`; browserIds.set(port, id); }
    return { id, label: browserPortLabel(port, index), port };
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

function settingsMatch(left: SerialSettings, right: SerialSettings) {
  return left.baudRate === right.baudRate && left.dataBits === right.dataBits && left.stopBits === right.stopBits && left.parity === right.parity && left.flowControl === right.flowControl;
}

async function openSelectedPort() {
  const portId = source.value === "browser" ? browserPortId.value : serverPortId.value;
  if (!portId) return;
  const key = `${source.value}:${portId}`;
  const existing = sessions.get(key);
  if (existing) {
    if (!settingsMatch(existing.settings, settings)) {
      message.warning(`该串口已按 ${existing.settings.baudRate}-${existing.settings.dataBits}-${existing.settings.parity} 打开，请关闭全部视图后再修改参数。`);
      return;
    }
    await addView(existing);
    return;
  }

  const browserItem = browserPorts.value.find((item) => item.id === portId);
  const serverItem = serverPorts.value.find((item) => item.id === portId);
  const session = reactive<SerialSession>({
    key,
    location: source.value,
    portId,
    name: browserItem?.label ?? serverItem?.name ?? portId,
    settings: { ...settings },
    status: "connecting",
    error: "",
    closing: false,
    chunks: [],
    bufferedBytes: 0,
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
  const readTask = (async () => {
    if (!session.browserPort?.readable) return;
    const reader = session.browserPort.readable.getReader();
    session.reader = markRaw(reader);
    try {
      while (!session.closing) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value?.byteLength) receiveData(session, value);
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
    session.status = "closed";
    broadcastNotice(session, "\r\n\x1b[33m[串口读取已结束]\x1b[0m\r\n");
  }
}

function openServerSession(session: SerialSession) {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const socket = markRaw(new WebSocket(`${protocol}//${window.location.host}/api/tools/serial/session`));
  socket.binaryType = "arraybuffer";
  session.socket = socket;
  socket.onopen = () => socket.send(JSON.stringify({ type: "open", port: session.portId, options: {
    baudRate: session.settings.baudRate,
    dataBits: session.settings.dataBits,
    stopBits: session.settings.stopBits,
    parity: session.settings.parity,
    flowControl: session.settings.flowControl,
  } }));
  socket.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) { receiveData(session, new Uint8Array(event.data)); return; }
    if (event.data instanceof Blob) { void event.data.arrayBuffer().then((data) => receiveData(session, new Uint8Array(data))); return; }
    try {
      const payload = JSON.parse(String(event.data));
      if (payload.type === "opened") {
        session.status = "open";
        broadcastNotice(session, `\r\n\x1b[36m[已连接服务器串口 ${session.name} · ${payload.viewers ?? 1} 个订阅]\x1b[0m\r\n`);
      } else if (payload.type === "state" && payload.status) {
        session.status = payload.status === "open" ? "open" : "closed";
      } else if (payload.type === "error") failSession(session, payload.message || "服务器串口连接失败");
    } catch { /* Ignore non-protocol text. */ }
  };
  socket.onerror = () => failSession(session, "串口 WebSocket 连接失败。");
  socket.onclose = () => {
    if (!session.closing && session.status !== "error") {
      session.status = "closed";
      broadcastNotice(session, "\r\n\x1b[33m[服务器串口连接已关闭]\x1b[0m\r\n");
    }
  };
}

function failSession(session: SerialSession, reason: string) {
  session.status = "error";
  session.error = reason;
  broadcastNotice(session, `\r\n\x1b[31m[连接失败：${reason}]\x1b[0m\r\n`);
}

function receiveData(session: SerialSession, input: Uint8Array) {
  const data = input.slice();
  session.chunks.push(data);
  session.bufferedBytes += data.byteLength;
  while (session.bufferedBytes > 512 * 1024 && session.chunks.length > 1) {
    const removed = session.chunks.shift();
    session.bufferedBytes -= removed?.byteLength ?? 0;
  }
  views.filter((view) => view.sessionKey === session.key).forEach((view) => view.terminalView?.write(data));
}

function broadcastNotice(session: SerialSession, text: string) {
  views.filter((view) => view.sessionKey === session.key).forEach((view) => view.terminalView?.write(text));
}

async function writeSession(session: SerialSession, data: Uint8Array) {
  if (session.status !== "open") throw new Error("串口尚未连接。");
  session.writeChain = session.writeChain.then(async () => {
    if (session.location === "server") {
      if (session.socket?.readyState !== WebSocket.OPEN) throw new Error("服务器串口连接已断开。");
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
  const sameSessionCount = views.filter((view) => view.sessionKey === session.key).length;
  const view = reactive<SerialView>({
    id: crypto.randomUUID(),
    sessionKey: session.key,
    title: sameSessionCount ? `${session.name} (${sameSessionCount + 1})` : session.name,
    command: "",
    sendMode: "text",
    lineEnding: "none",
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
  session.chunks.forEach((chunk) => event.terminal.write(chunk));
}

function handleTerminalData(view: SerialView, data: string) {
  const session = sessions.get(view.sessionKey);
  if (!session) return;
  void writeSession(session, new TextEncoder().encode(data)).catch((error) => message.error(errorMessage(error)));
}

function activateView(viewId: string) {
  activeViewId.value = viewId;
  mobileSetup.value = false;
  nextTick(() => views.find((view) => view.id === viewId)?.terminalView?.fit());
}

function duplicateActiveView() {
  if (activeSession.value) void addView(activeSession.value);
}

async function closeView(viewId: string) {
  const index = views.findIndex((view) => view.id === viewId);
  if (index < 0) return;
  const [view] = views.splice(index, 1);
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
  if (session.location === "server") {
    const socket = session.socket;
    if (socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "close" }));
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
  await session.reader?.cancel().catch(() => undefined);
  await session.readTask?.catch(() => undefined);
  await session.browserPort?.close().catch(() => undefined);
}

async function reconnectActive() {
  const session = activeSession.value;
  if (!session) return;
  await closeSession(session);
  await startSession(session);
}

function sessionFor(view: SerialView) { return sessions.get(view.sessionKey); }
function statusText(session: SerialSession) {
  return session.status === "connecting" ? "连接中" : session.status === "open" ? `已连接 · ${views.filter((view) => view.sessionKey === session.key).length} 个视图` : session.status === "error" ? "连接失败" : "已断开";
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

watch(settings, (value) => localStorage.setItem("space-station:serial-settings", JSON.stringify(value)), { deep: true });
watch(source, (value) => { if (value === "server") void refreshServerPorts(); else void refreshBrowserPorts(); });

onMounted(() => {
  void refreshServerPorts();
  void refreshBrowserPorts();
});

onBeforeUnmount(() => {
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
.serial-source-switch { display: grid; grid-template-columns: 1fr 1fr; padding: 3px; border-radius: 8px; background: #0e151a; }
.serial-source-switch button { min-height: 34px; border: 0; border-radius: 6px; background: transparent; color: #9caab4; cursor: pointer; }
.serial-source-switch button.active { background: #283842; color: #e9f0f4; font-weight: 700; }
.serial-field-heading { display: flex; align-items: center; justify-content: space-between; min-height: 24px; color: #c9d3da; font-size: 13px; font-weight: 700; }
.serial-hint { margin: -4px 0 0; font-size: 12px; line-height: 1.55; }
.serial-divider { height: 1px; background: #2a3841; }
.serial-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.serial-grid label, .serial-flow { min-width: 0; display: grid; gap: 5px; }
.serial-grid label > span, .serial-flow > span { color: #aebbc4; font-size: 12px; }
.serial-open { width: 100%; }
.serial-rules { margin-top: auto; padding: 12px; border: 1px solid #2c3d47; border-radius: 8px; background: #11191f; }
.serial-rules strong { font-size: 13px; }
.serial-rules p { margin: 6px 0 0; color: #8fa0ad; font-size: 12px; line-height: 1.55; }
.serial-workspace { min-width: 0; min-height: 0; display: flex; flex-direction: column; overflow: hidden; }
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
.serial-tab-actions { flex: 0 0 auto; padding: 0 12px; display: flex; align-items: center; gap: 8px; }
.serial-location, .serial-status-label { padding: 3px 8px; border-radius: 999px; background: #263640; color: #b9c8d1; font-size: 11px; white-space: nowrap; }
.serial-status-label.open { background: rgba(39, 135, 83, .26); color: #78d7a1; }
.serial-status-label.error { background: rgba(169, 62, 62, .28); color: #f0a0a0; }
.serial-mobile-menu { display: none; align-self: center; margin-left: 8px; }
.serial-empty { flex: 1; display: grid; place-content: center; justify-items: center; padding: 30px; text-align: center; }
.serial-empty > div { color: #85b8b5; font: 700 54px/1 monospace; }
.serial-empty h1 { margin: 20px 0 8px; font-size: 24px; }
.serial-empty p { margin: 0; color: #83939e; }
.serial-empty-mobile { display: none; margin-top: 18px; }
.serial-terminal-section { min-height: 0; flex: 1; display: flex; flex-direction: column; }
.serial-terminal { min-height: 0; flex: 1; padding: 8px 5px 3px 10px; overflow: hidden; background: #0d1318; }
.serial-terminal :deep(.xterm) { height: 100%; }
.serial-error { padding: 8px 12px; border-top: 1px solid #653b3b; background: #3a2222; color: #f3b3b3; font-size: 13px; }
.serial-composer { padding: 10px 12px; display: flex; align-items: center; gap: 8px; border-top: 1px solid #2c3942; background: #151d23; }
.serial-send-mode { width: 84px; flex: 0 0 auto; }
.serial-command { min-width: 100px; flex: 1; }
.serial-line-ending { width: 100px; flex: 0 0 auto; }

@media (max-width: 760px) {
  .serial-app { grid-template-columns: 1fr; }
  .serial-sidebar { width: 100%; border-right: 0; padding: max(14px, env(safe-area-inset-top)) 14px max(14px, env(safe-area-inset-bottom)); }
  .serial-app--session-open .serial-sidebar { display: none; }
  .serial-workspace { display: none; }
  .serial-app--session-open .serial-workspace { display: flex; }
  .serial-mobile-menu { display: inline-flex; }
  .serial-tabs { padding-top: env(safe-area-inset-top); min-height: calc(46px + env(safe-area-inset-top)); }
  .serial-tab-actions .serial-location, .serial-tab-actions button { display: none; }
  .serial-tab-actions { padding: 0 7px; }
  .serial-status-label { max-width: 92px; overflow: hidden; text-overflow: ellipsis; }
  .serial-tab { min-width: 112px; max-width: 165px; }
  .serial-empty-mobile { display: inline-flex; }
  .serial-terminal { padding-left: 5px; }
  .serial-composer { padding-bottom: max(10px, env(safe-area-inset-bottom)); flex-wrap: wrap; }
  .serial-command { order: -1; flex-basis: 100%; }
  .serial-send-mode { flex: 1; }
  .serial-line-ending { flex: 1; }
}
</style>
