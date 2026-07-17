<template>
  <div class="transfer-tool">
    <n-tabs v-model:value="activeTab" type="segment" animated>
      <n-tab-pane name="client" tab="Client">
        <n-card title="创建传输任务" embedded>
          <n-alert type="info" :show-icon="false">
            文件路径属于运行 Space Station 后端的这台机器，不是当前浏览器设备。
          </n-alert>
          <n-form label-placement="top" class="transfer-form">
            <div class="transfer-grid compact">
              <n-form-item label="对端地址">
                <n-input v-model:value="client.host" placeholder="127.0.0.1" />
              </n-form-item>
              <n-form-item label="端口">
                <n-input-number v-model:value="client.port" :min="1" :max="65535" />
              </n-form-item>
              <n-form-item label="块大小（KiB）">
                <n-input-number v-model:value="chunkSizeKiB" :min="64" :max="16384" :step="64" />
              </n-form-item>
              <n-form-item label="压缩方式">
                <n-select v-model:value="client.compressionMode" :options="compressionOptions" />
              </n-form-item>
              <n-form-item label="启用 TLS 1.3">
                <n-switch v-model:value="client.tlsEnabled" />
              </n-form-item>
            </div>

            <div v-if="client.tlsEnabled" class="transfer-grid">
              <n-form-item label="客户端证书">
                <n-input v-model:value="client.certificatePath" placeholder="data/tls/client.crt" />
              </n-form-item>
              <n-form-item label="客户端私钥">
                <n-input v-model:value="client.privateKeyPath" placeholder="data/tls/client.key" />
              </n-form-item>
              <n-form-item label="服务端 CA">
                <n-input v-model:value="client.serverCaPath" placeholder="data/tls/root.crt" />
              </n-form-item>
              <n-form-item label="服务端名称">
                <n-input v-model:value="client.serverName" placeholder="证书 SAN 中的域名或 IP" />
              </n-form-item>
            </div>

            <n-form-item label="待传输文件路径（每行一个）">
              <n-input
                v-model:value="clientFilesText"
                type="textarea"
                :autosize="{ minRows: 4, maxRows: 10 }"
                placeholder="/data/releases/app.tar.zst"
              />
            </n-form-item>
            <n-space>
              <n-button type="primary" :loading="clientStarting" :disabled="!canStartClient" @click="startClient">
                开始传输
              </n-button>
              <n-button tertiary @click="refresh()">刷新进度</n-button>
            </n-space>
          </n-form>
        </n-card>

        <div class="transfer-jobs">
          <n-card v-for="job in state.jobs" :key="job.id" embedded>
            <template #header>
              <div class="job-header">
                <span>任务 {{ job.id }}</span>
                <n-tag :type="jobType(job.status)" :bordered="false">{{ jobStatus(job.status) }}</n-tag>
              </div>
            </template>
            <n-alert v-if="job.error" type="error" :show-icon="false">{{ job.error }}</n-alert>
            <div v-for="file in job.files" :key="file.fileId" class="transfer-file">
              <div class="transfer-file-title">
                <span>{{ file.path }}</span>
                <small>{{ stageText(file.stage) }}</small>
              </div>
              <n-progress
                type="line"
                :percentage="filePercentage(file)"
                :status="file.stage === 'failed' ? 'error' : file.stage === 'completed' ? 'success' : 'default'"
              />
              <div class="transfer-stats">
                <span>大小 {{ formatBytes(file.fileSize) }}</span>
                <span>复用 {{ formatBytes(file.matchedBytes) }}</span>
                <span>缺失 {{ formatBytes(file.uploadedBytes) }}</span>
                <span>实际发送 {{ formatBytes(file.wireBytes) }}</span>
                <span v-if="file.compressionMode !== 'none'">
                  {{ compressionModeText(file.compressionMode) }}，{{ compressionEffect(file) }}
                  <template v-if="file.compressionMode === 'chunk'">（{{ file.compressedChunks }} 块）</template>
                </span>
              </div>
              <n-alert v-if="file.error" type="error" :show-icon="false">{{ file.error }}</n-alert>
            </div>
          </n-card>
          <n-empty v-if="!state.jobs.length" description="暂无传输任务" />
        </div>
      </n-tab-pane>

      <n-tab-pane name="server" tab="Server">
        <n-card title="服务端配置" embedded>
          <n-form label-placement="top" class="transfer-form">
            <div class="server-status">
              <n-tag :type="state.serverRunning ? 'success' : 'default'" :bordered="false">
                {{ state.serverRunning ? "运行中" : "已停止" }}
              </n-tag>
              <span>修改配置前需要先停止服务端。</span>
            </div>
            <div class="transfer-grid compact">
              <n-form-item label="监听地址">
                <n-input v-model:value="server.listenAddress" placeholder="0.0.0.0" />
              </n-form-item>
              <n-form-item label="TLS 端口">
                <n-input-number v-model:value="server.tlsPort" :min="1" :max="65535" />
              </n-form-item>
              <n-form-item label="启用 TLS 1.3/mTLS">
                <n-switch v-model:value="server.tlsEnabled" />
              </n-form-item>
              <n-form-item label="覆盖同名目标文件">
                <n-switch v-model:value="server.overwrite" />
              </n-form-item>
            </div>
            <div class="transfer-grid">
              <n-form-item label="服务端证书">
                <n-input v-model:value="server.certificatePath" />
              </n-form-item>
              <n-form-item label="服务端私钥">
                <n-input v-model:value="server.privateKeyPath" />
              </n-form-item>
              <n-form-item label="客户端 CA">
                <n-input v-model:value="server.clientCaPath" />
              </n-form-item>
              <n-form-item label="上传文件目录">
                <n-input v-model:value="server.destinationRoot" />
              </n-form-item>
            </div>
            <n-form-item label="可读取文件片段目录（每行一个）">
              <n-input v-model:value="basisRootsText" type="textarea" :autosize="{ minRows: 3, maxRows: 8 }" />
            </n-form-item>
            <div class="transfer-grid compact">
              <n-form-item label="同名文件停止扩展扫描阈值">
                <n-input-number
                  v-model:value="sameNameThresholdPercent"
                  :min="0"
                  :max="100"
                  :step="5"
                  :format="(value) => `${value}%`"
                  :parse="(value) => Number(value.replace('%', ''))"
                />
              </n-form-item>
              <n-form-item label="启用普通端口">
                <n-switch v-model:value="server.plainEnabled" />
              </n-form-item>
              <n-form-item label="普通端口">
                <n-input-number v-model:value="server.plainPort" :min="1" :max="65535" />
              </n-form-item>
            </div>
            <n-alert v-if="server.plainEnabled" type="warning" :show-icon="false">
              普通端口没有 TLS 和客户端证书保护，只应在可信网络中临时使用。
            </n-alert>
            <n-space>
              <n-button
                type="primary"
                :loading="serverBusy"
                :disabled="state.serverRunning"
                @click="saveAndStartServer"
              >
                保存并启动
              </n-button>
              <n-button :loading="serverBusy" :disabled="!state.serverRunning" tertiary @click="stopServer">
                停止
              </n-button>
              <n-button :disabled="state.serverRunning" tertiary @click="saveServer">仅保存配置</n-button>
            </n-space>
          </n-form>
        </n-card>
      </n-tab-pane>
    </n-tabs>

    <n-alert v-if="errorMessage" class="transfer-error" type="error" :show-icon="false">
      {{ errorMessage }}
    </n-alert>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import {
  NAlert,
  NButton,
  NCard,
  NEmpty,
  NForm,
  NFormItem,
  NInput,
  NInputNumber,
  NProgress,
  NSelect,
  NSpace,
  NSwitch,
  NTabPane,
  NTabs,
  NTag,
  useMessage,
} from "naive-ui";
import {
  fetchTransferState,
  saveTransferServerConfig,
  startTransferClientJob,
  startTransferServer,
  stopTransferServer,
  type TransferClientRequest,
  type TransferFileProgress,
  type TransferServerConfig,
  type TransferState,
} from "@/api";

const message = useMessage();
const activeTab = ref("client");
const errorMessage = ref("");
const serverBusy = ref(false);
const clientStarting = ref(false);
const clientFilesText = ref("");
const basisRootsText = ref("");
const chunkSizeKiB = ref(1024);
const sameNameThresholdPercent = ref(50);
const compressionOptions = [
  { label: "连续流压缩（压缩率更高）", value: "stream" },
  { label: "逐块压缩（独立压缩块）", value: "chunk" },
];
let pollTimer: number | undefined;

const state = reactive<TransferState>({
  server: defaultServer(),
  serverRunning: false,
  jobs: [],
});
const server = reactive<TransferServerConfig>(defaultServer());
const client = reactive<Omit<TransferClientRequest, "files" | "chunkSize">>({
  host: "127.0.0.1",
  port: 9443,
  tlsEnabled: true,
  certificatePath: "",
  privateKeyPath: "",
  serverCaPath: "",
  serverName: "",
  compressionMode: "stream",
});

const clientFiles = computed(() => splitLines(clientFilesText.value));
const canStartClient = computed(() => Boolean(client.host.trim() && client.port && clientFiles.value.length));

onMounted(async () => {
  await refresh(true);
  pollTimer = window.setInterval(() => void refresh(false), 1000);
});

onBeforeUnmount(() => {
  if (pollTimer !== undefined) window.clearInterval(pollTimer);
});

function defaultServer(): TransferServerConfig {
  return {
    configVersion: 1,
    listenAddress: "0.0.0.0",
    tlsEnabled: true,
    tlsPort: 9443,
    plainEnabled: false,
    plainPort: 9080,
    certificatePath: "",
    privateKeyPath: "",
    clientCaPath: "",
    destinationRoot: "data/transfer/incoming",
    basisRoots: [],
    sameNameMatchThreshold: 0.5,
    overwrite: true,
  };
}

function splitLines(value: string) {
  return value.split(/\r?\n/).map((item) => item.trim()).filter(Boolean);
}

async function refresh(updateDraft = false) {
  try {
    const next = await fetchTransferState();
    Object.assign(state, next);
    if (updateDraft) {
      Object.assign(server, next.server);
      basisRootsText.value = next.server.basisRoots.join("\n");
      sameNameThresholdPercent.value = Math.round(next.server.sameNameMatchThreshold * 100);
    }
  } catch (error) {
    if (updateDraft) errorMessage.value = error instanceof Error ? error.message : "读取传输状态失败。";
  }
}

async function saveServer(): Promise<boolean> {
  serverBusy.value = true;
  errorMessage.value = "";
  try {
    server.basisRoots = splitLines(basisRootsText.value);
    server.sameNameMatchThreshold = sameNameThresholdPercent.value / 100;
    const saved = await saveTransferServerConfig({ ...server });
    Object.assign(server, saved);
    message.success("传输服务端配置已保存。");
    await refresh();
    return true;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "保存服务端配置失败。";
    return false;
  } finally {
    serverBusy.value = false;
  }
}

async function saveAndStartServer() {
  try {
    if (!(await saveServer())) return;
    serverBusy.value = true;
    await startTransferServer();
    message.success("差分传输服务端已启动。");
    await refresh();
  } catch {
    // saveServer/start API has already exposed the error.
  } finally {
    serverBusy.value = false;
  }
}

async function stopServer() {
  serverBusy.value = true;
  errorMessage.value = "";
  try {
    await stopTransferServer();
    message.success("差分传输服务端已停止。");
    await refresh();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "停止服务端失败。";
  } finally {
    serverBusy.value = false;
  }
}

async function startClient() {
  if (!canStartClient.value) return;
  clientStarting.value = true;
  errorMessage.value = "";
  try {
    await startTransferClientJob({
      ...client,
      chunkSize: chunkSizeKiB.value * 1024,
      files: clientFiles.value,
    });
    message.success("传输任务已创建。");
    await refresh();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : "创建传输任务失败。";
  } finally {
    clientStarting.value = false;
  }
}

function filePercentage(file: TransferFileProgress) {
  if (file.stage === "completed") return 100;
  if (!file.fileSize) return 0;
  return Math.min(99, Math.round(((file.matchedBytes + file.uploadedBytes) / file.fileSize) * 100));
}

function formatBytes(value: number) {
  if (value < 1024) return `${value} B`;
  if (value < 1024 ** 2) return `${(value / 1024).toFixed(1)} KiB`;
  if (value < 1024 ** 3) return `${(value / 1024 ** 2).toFixed(1)} MiB`;
  return `${(value / 1024 ** 3).toFixed(2)} GiB`;
}

function compressionModeText(mode: TransferFileProgress["compressionMode"]) {
  return mode === "stream" ? "连续流压缩" : mode === "chunk" ? "逐块压缩" : "未压缩";
}

function compressionEffect(file: TransferFileProgress) {
  const difference = file.uploadedBytes - file.wireBytes;
  return difference >= 0 ? `节省 ${formatBytes(difference)}` : `增加 ${formatBytes(-difference)}`;
}

function stageText(stage: TransferFileProgress["stage"]) {
  return ({
    hashing: "计算签名",
    scanning: "服务端扫描",
    uploading: "上传缺失块",
    verifying: "最终校验",
    completed: "完成",
    failed: "失败",
  } as const)[stage];
}

function jobStatus(status: string) {
  return ({ queued: "排队中", running: "运行中", completed: "完成", failed: "失败" } as Record<string, string>)[status] ?? status;
}

function jobType(status: string): "default" | "info" | "success" | "error" {
  if (status === "completed") return "success";
  if (status === "failed") return "error";
  if (status === "running") return "info";
  return "default";
}
</script>

<style scoped>
.transfer-tool,
.transfer-form,
.transfer-jobs {
  display: grid;
  gap: 16px;
}

.transfer-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0 16px;
}

.transfer-grid.compact {
  grid-template-columns: repeat(4, minmax(0, 1fr));
}

.server-status,
.job-header,
.transfer-file-title,
.transfer-stats {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.server-status {
  justify-content: flex-start;
}

.server-status span,
.transfer-stats,
.transfer-file-title small {
  color: #667786;
  font-size: 0.82rem;
}

.transfer-file {
  display: grid;
  gap: 8px;
  padding: 12px 0;
  border-bottom: 1px solid rgba(48, 67, 86, 0.12);
}

.transfer-file:last-child {
  border-bottom: 0;
}

.transfer-file-title span {
  min-width: 0;
  overflow-wrap: anywhere;
}

.transfer-stats {
  justify-content: flex-start;
  flex-wrap: wrap;
}

.transfer-error {
  margin-top: 16px;
}

@media (max-width: 900px) {
  .transfer-grid,
  .transfer-grid.compact {
    grid-template-columns: 1fr;
  }

  .transfer-file-title {
    align-items: flex-start;
    flex-direction: column;
  }
}
</style>
