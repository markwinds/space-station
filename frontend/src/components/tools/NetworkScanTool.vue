<template>
  <div class="network-scan">
    <section class="scan-control-card">
      <div class="scan-control-heading">
        <div>
          <h3>扫描范围</h3>
          <p>从 Space Station 后端探测目标，因此结果反映服务器所在网络的可达性。</p>
        </div>
        <n-tag :type="scanning ? 'warning' : scanFinished ? 'success' : 'default'" round>
          {{ scanning ? '扫描中' : scanFinished ? '扫描完成' : '等待扫描' }}
        </n-tag>
      </div>

      <div class="scan-form">
        <n-form-item label="IPv4 网段（CIDR）">
          <n-input v-model:value="cidr" placeholder="192.168.1.0/24" :disabled="scanning" @keyup.enter="startScan" />
        </n-form-item>
        <n-form-item label="常用 TCP 端口">
          <n-select
            v-model:value="ports"
            multiple
            filterable
            tag
            :options="portOptions"
            :disabled="scanning"
            placeholder="用于补充判断主机可达性"
            @create="createPort"
          />
        </n-form-item>
        <n-form-item label="单项超时">
          <n-select v-model:value="timeoutMs" :options="timeoutOptions" :disabled="scanning" />
        </n-form-item>
        <n-form-item label="并发数">
          <n-input-number v-model:value="concurrency" :min="1" :max="128" :disabled="scanning" />
        </n-form-item>
        <n-checkbox v-model:checked="resolveNames" :disabled="scanning">反向解析主机名</n-checkbox>
        <div class="scan-actions">
          <n-button v-if="!scanning" type="primary" :disabled="!cidr.trim()" @click="startScan">
            <template #icon><n-icon><ScanOutline /></n-icon></template>
            开始扫描
          </n-button>
          <n-button v-else type="warning" secondary @click="cancelScan">
            <template #icon><n-icon><StopCircleOutline /></n-icon></template>
            停止扫描
          </n-button>
        </div>
      </div>

      <div v-if="scanning || scanned" class="scan-progress">
        <n-progress type="line" :percentage="progress" :indicator-placement="'inside'" processing />
        <span>{{ scanned }} / {{ total }} 个地址</span>
      </div>
      <n-alert v-if="error" type="error" closable @close="error = ''">{{ error }}</n-alert>
    </section>

    <section class="scan-summary" aria-label="扫描摘要">
      <article><span>已扫描</span><strong>{{ scanned }}</strong><small>{{ total ? `共 ${total} 个` : '尚未开始' }}</small></article>
      <article><span>可达主机</span><strong>{{ reachableResults.length }}</strong><small>{{ reachabilityRate }}</small></article>
      <article><span>平均延时</span><strong>{{ averageLatency }}</strong><small>可达主机</small></article>
      <article><span>开放端口</span><strong>{{ openPortCount }}</strong><small>{{ serviceCount }} 种服务</small></article>
    </section>

    <section class="scan-results-card">
      <div class="scan-results-heading">
        <div>
          <h3>扫描结果</h3>
          <p>ICMP 无响应但常用端口可连接的设备仍会标记为可达。</p>
        </div>
        <div class="scan-results-actions">
          <n-input v-model:value="keyword" clearable size="small" placeholder="筛选 IP、主机名或端口">
            <template #prefix><n-icon><SearchOutline /></n-icon></template>
          </n-input>
          <n-select v-model:value="statusFilter" size="small" :options="statusOptions" />
          <n-select v-model:value="sortMode" size="small" :options="sortOptions" />
          <n-button size="small" secondary :disabled="results.length === 0" @click="exportCsv">
            <template #icon><n-icon><DownloadOutline /></n-icon></template>
            导出 CSV
          </n-button>
        </div>
      </div>

      <div class="scan-table-wrap">
        <table class="scan-table">
          <thead><tr><th>状态</th><th>IP 地址</th><th>主机名</th><th>延时</th><th>开放端口 / 服务</th><th>操作</th></tr></thead>
          <tbody>
            <tr v-for="result in filteredResults" :key="result.ip" :class="{ unreachable: !result.reachable }">
              <td><span class="reachability" :class="{ online: result.reachable }"><i />{{ result.reachable ? '可达' : '无响应' }}</span></td>
              <td><code>{{ result.ip }}</code><small>{{ scopeLabel(result.scope) }}</small></td>
              <td>{{ result.hostname || '—' }}</td>
              <td>
                <template v-if="result.latencyMs !== null"><strong :class="latencyClass(result.latencyMs)">{{ formatLatency(result.latencyMs) }}</strong></template>
                <span v-else>—</span>
              </td>
              <td>
                <div v-if="result.openPorts.length" class="port-list">
                  <n-tag v-for="port in result.openPorts" :key="port.port" size="small" :bordered="false">{{ port.port }} · {{ port.service }}</n-tag>
                </div>
                <span v-else>—</span>
              </td>
              <td>
                <router-link v-if="terminalLink(result)" class="terminal-link" :to="terminalLink(result)!">打开终端</router-link>
                <span v-else>—</span>
              </td>
            </tr>
            <tr v-if="filteredResults.length === 0"><td colspan="6" class="empty-results">{{ results.length ? '没有符合筛选条件的结果' : '开始扫描后，结果会实时显示在这里' }}</td></tr>
          </tbody>
        </table>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { DownloadOutline, ScanOutline, SearchOutline, StopCircleOutline } from "@vicons/ionicons5";
import { NAlert, NButton, NCheckbox, NFormItem, NIcon, NInput, NInputNumber, NProgress, NSelect, NTag, useMessage } from "naive-ui";
import { computed, onBeforeUnmount, ref } from "vue";

interface ScanPort { port: number; service: string; latencyMs: number }
interface ScanResult {
  ip: string;
  hostname: string;
  scope: "private" | "public" | "loopback" | "link-local";
  reachable: boolean;
  latencyMs: number | null;
  openPorts: ScanPort[];
}

const message = useMessage();
const cidr = ref("192.168.1.0/24");
const ports = ref<Array<number | string>>([22, 23, 80, 443, 445, 3389]);
const timeoutMs = ref(800);
const concurrency = ref(32);
const resolveNames = ref(true);
const scanning = ref(false);
const scanFinished = ref(false);
const scanned = ref(0);
const total = ref(0);
const error = ref("");
const results = ref<ScanResult[]>([]);
const keyword = ref("");
const statusFilter = ref<"all" | "reachable" | "unreachable">("all");
const sortMode = ref<"ip" | "latency" | "status">("ip");
let socket: WebSocket | undefined;

const portOptions = [
  [21, "FTP"], [22, "SSH"], [23, "Telnet"], [53, "DNS"], [80, "HTTP"], [443, "HTTPS"],
  [445, "SMB"], [1433, "MSSQL"], [3306, "MySQL"], [3389, "RDP"], [5432, "PostgreSQL"],
  [5900, "VNC"], [6379, "Redis"], [8080, "HTTP"], [8443, "HTTPS"], [27017, "MongoDB"],
].map(([value, service]) => ({ label: `${value} · ${service}`, value }));
const timeoutOptions = [300, 500, 800, 1200, 2000, 3000].map((value) => ({ label: `${value} ms`, value }));
const statusOptions = [
  { label: "全部状态", value: "all" }, { label: "仅可达", value: "reachable" }, { label: "仅无响应", value: "unreachable" },
];
const sortOptions = [
  { label: "按 IP 排序", value: "ip" }, { label: "按状态排序", value: "status" }, { label: "按延时排序", value: "latency" },
];

const progress = computed(() => total.value ? Math.round(scanned.value / total.value * 100) : 0);
const reachableResults = computed(() => results.value.filter((item) => item.reachable));
const reachabilityRate = computed(() => scanned.value ? `${Math.round(reachableResults.value.length / scanned.value * 100)}% 可达` : "0% 可达");
const averageLatency = computed(() => {
  const values = reachableResults.value.map((item) => item.latencyMs).filter((value): value is number => value !== null);
  return values.length ? `${(values.reduce((sum, value) => sum + value, 0) / values.length).toFixed(1)} ms` : "—";
});
const openPortCount = computed(() => results.value.reduce((sum, item) => sum + item.openPorts.length, 0));
const serviceCount = computed(() => new Set(results.value.flatMap((item) => item.openPorts.map((port) => port.service))).size);
const filteredResults = computed(() => {
  const query = keyword.value.trim().toLowerCase();
  return results.value
    .filter((item) => statusFilter.value === "all" || (statusFilter.value === "reachable") === item.reachable)
    .filter((item) => !query || `${item.ip} ${item.hostname} ${item.openPorts.map((port) => `${port.port} ${port.service}`).join(" ")}`.toLowerCase().includes(query))
    .slice()
    .sort((left, right) => {
      if (sortMode.value === "status" && left.reachable !== right.reachable) return left.reachable ? -1 : 1;
      if (sortMode.value === "latency") return (left.latencyMs ?? Number.MAX_SAFE_INTEGER) - (right.latencyMs ?? Number.MAX_SAFE_INTEGER);
      return ipNumber(left.ip) - ipNumber(right.ip);
    });
});

function normalizedPorts() {
  return [...new Set(ports.value.map(Number).filter((port) => Number.isInteger(port) && port >= 1 && port <= 65535))].slice(0, 24);
}

function createPort(value: string) {
  const port = Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    message.warning("端口必须是 1 到 65535 之间的整数");
    return { label: value, value };
  }
  return { label: String(port), value: port };
}

function startScan() {
  if (scanning.value || !cidr.value.trim()) return;
  error.value = "";
  scanFinished.value = false;
  scanned.value = 0;
  total.value = 0;
  results.value = [];
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  socket = new WebSocket(`${protocol}//${window.location.host}/api/tools/network/scan`);
  socket.onmessage = (event) => handleMessage(JSON.parse(String(event.data)) as Record<string, unknown>);
  socket.onerror = () => { error.value = "无法连接网络扫描服务。"; scanning.value = false; };
  socket.onclose = () => { if (scanning.value) { scanning.value = false; error.value ||= "扫描连接已关闭。"; } };
  scanning.value = true;
}

function handleMessage(payload: Record<string, unknown>) {
  if (payload.type === "ready") {
    socket?.send(JSON.stringify({
      type: "scan", cidr: cidr.value.trim(), ports: normalizedPorts(), timeoutMs: timeoutMs.value,
      concurrency: concurrency.value, resolveNames: resolveNames.value,
    }));
  } else if (payload.type === "started") {
    total.value = Number(payload.total) || 0;
  } else if (payload.type === "result") {
    const result = payload as unknown as ScanResult & { scanned: number; total: number };
    const index = results.value.findIndex((item) => item.ip === result.ip);
    if (index >= 0) results.value[index] = result; else results.value.push(result);
    scanned.value = Math.max(scanned.value, Number(result.scanned) || results.value.length);
    total.value = Number(result.total) || total.value;
  } else if (payload.type === "complete" || payload.type === "cancelled") {
    scanned.value = Number(payload.scanned) || scanned.value;
    scanning.value = false;
    scanFinished.value = payload.type === "complete";
    socket?.close();
    socket = undefined;
  } else if (payload.type === "error") {
    error.value = String(payload.message || "扫描失败。");
    scanning.value = false;
    socket?.close();
    socket = undefined;
  }
}

function cancelScan() {
  socket?.send(JSON.stringify({ type: "cancel" }));
}

function terminalLink(result: ScanResult) {
  const protocol = result.openPorts.some((item) => item.port === 22) ? "ssh" : result.openPorts.some((item) => item.port === 23) ? "telnet" : "";
  return protocol ? { path: "/tools/ssh", query: { host: result.ip, protocol } } : null;
}

function scopeLabel(scope: ScanResult["scope"]) {
  return ({ private: "私有网络", public: "公网", loopback: "本机回环", "link-local": "链路本地" })[scope] || scope;
}
function latencyClass(value: number) { return value < 30 ? "latency-good" : value < 100 ? "latency-medium" : "latency-slow"; }
function formatLatency(value: number) { return `${value < 1 ? value.toFixed(1) : Math.round(value)} ms`; }
function ipNumber(value: string) { return value.split(".").reduce((sum, part) => sum * 256 + Number(part), 0); }

function exportCsv() {
  const escape = (value: unknown) => `"${String(value ?? "").replaceAll('"', '""')}"`;
  const rows = [["IP", "状态", "主机名", "延时(ms)", "范围", "开放端口"]];
  for (const item of filteredResults.value) rows.push([
    item.ip, item.reachable ? "可达" : "无响应", item.hostname, item.latencyMs === null ? "" : String(item.latencyMs),
    scopeLabel(item.scope), item.openPorts.map((port) => `${port.port}/${port.service}`).join(" "),
  ]);
  const url = URL.createObjectURL(new Blob(["\ufeff" + rows.map((row) => row.map(escape).join(",")).join("\n")], { type: "text/csv;charset=utf-8" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = `network-scan-${cidr.value.replaceAll(/[\/.]/g, "-")}.csv`;
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

onBeforeUnmount(() => {
  if (scanning.value) socket?.send(JSON.stringify({ type: "cancel" }));
  socket?.close();
});
</script>

<style scoped>
.network-scan { display: grid; gap: 18px; }
.scan-control-card, .scan-results-card { padding: 20px; border: 1px solid rgba(54, 77, 99, .12); border-radius: 12px; background: rgba(255, 255, 255, .76); box-shadow: 0 10px 28px rgba(33, 50, 63, .05); }
.scan-control-heading, .scan-results-heading { display: flex; align-items: flex-start; justify-content: space-between; gap: 18px; }
.scan-control-heading h3, .scan-results-heading h3 { margin: 0; color: #20323e; font-size: 17px; }
.scan-control-heading p, .scan-results-heading p { margin: 5px 0 0; color: #71808b; font-size: 12px; }
.scan-form { margin-top: 18px; display: grid; grid-template-columns: minmax(220px, 1.2fr) minmax(280px, 2fr) 130px 110px auto auto; align-items: end; gap: 12px; }
.scan-form :deep(.n-form-item) { margin-bottom: 0; }
.scan-form :deep(.n-input-number) { width: 100%; }
.scan-form > :deep(.n-checkbox) { margin-bottom: 8px; white-space: nowrap; }
.scan-actions { padding-bottom: 1px; }
.scan-progress { margin-top: 16px; display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: center; gap: 12px; color: #687984; font-size: 12px; }
.scan-control-card > :deep(.n-alert) { margin-top: 14px; }
.scan-summary { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
.scan-summary article { padding: 15px 17px; display: grid; grid-template-columns: 1fr auto; gap: 5px 10px; border: 1px solid rgba(54, 77, 99, .11); border-radius: 10px; background: rgba(255, 255, 255, .68); }
.scan-summary span { color: #687b88; font-size: 12px; }
.scan-summary strong { grid-row: 1 / 3; grid-column: 2; align-self: center; color: #234353; font-size: 24px; font-variant-numeric: tabular-nums; }
.scan-summary small { color: #8a98a2; font-size: 10px; }
.scan-results-actions { display: grid; grid-template-columns: minmax(210px, 1fr) 120px 130px auto; gap: 8px; }
.scan-table-wrap { margin-top: 16px; overflow: auto; border: 1px solid #dfe6e9; border-radius: 9px; }
.scan-table { width: 100%; min-width: 850px; border-collapse: collapse; color: #344752; font-size: 12px; }
.scan-table th { padding: 10px 12px; background: #f1f5f6; color: #6b7a84; font-size: 11px; font-weight: 700; text-align: left; white-space: nowrap; }
.scan-table td { padding: 11px 12px; border-top: 1px solid #e6ebed; }
.scan-table tbody tr:hover { background: #f7fafb; }
.scan-table tr.unreachable { color: #8b979e; }
.scan-table code { display: block; color: #253c49; font: 12px/1.4 "SFMono-Regular", Consolas, monospace; }
.scan-table td > small { display: block; margin-top: 2px; color: #89959d; font-size: 9px; }
.reachability { display: inline-flex; align-items: center; gap: 6px; white-space: nowrap; }
.reachability i { width: 7px; height: 7px; border-radius: 50%; background: #9aa4aa; }
.reachability.online { color: #23764c; font-weight: 700; }
.reachability.online i { background: #3eb879; box-shadow: 0 0 0 3px rgba(62, 184, 121, .13); }
.latency-good { color: #24794e; }.latency-medium { color: #9b6b19; }.latency-slow { color: #b54f4f; }
.port-list { display: flex; flex-wrap: wrap; gap: 5px; }
.terminal-link { color: #286b73; font-weight: 700; text-decoration: none; white-space: nowrap; }
.terminal-link:hover { text-decoration: underline; }
.empty-results { height: 150px; color: #8b989f; text-align: center; }
@media (max-width: 1080px) {
  .scan-form { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .scan-results-heading { display: grid; }
}
@media (max-width: 680px) {
  .scan-control-card, .scan-results-card { padding: 14px; }
  .scan-form, .scan-results-actions, .scan-summary { grid-template-columns: 1fr; }
  .scan-control-heading { align-items: center; }
}
</style>
