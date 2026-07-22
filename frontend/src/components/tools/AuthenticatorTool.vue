<template>
  <section class="authenticator-tool">
    <header class="authenticator-toolbar">
      <div>
        <strong>{{ entries.length }} 个账号</strong>
        <span>密钥由后端加密保存</span>
      </div>
      <n-input v-model:value="keyword" clearable placeholder="搜索服务或账号" />
      <n-button type="primary" @click="openEditor()">添加账号</n-button>
    </header>

    <n-alert type="info" :show-icon="false">
      验证码根据服务器时间生成。密钥使用独立的 AES-256-GCM 密钥加密存储，不会通过列表接口返回前端。
    </n-alert>

    <div v-if="loading" class="authenticator-state"><n-spin size="large" /></div>
    <n-empty v-else-if="filteredEntries.length === 0" class="authenticator-state" :description="entries.length ? '没有匹配的账号' : '还没有认证器账号'">
      <template #extra><n-button v-if="entries.length === 0" type="primary" @click="openEditor()">添加第一个账号</n-button></template>
    </n-empty>
    <div v-else class="authenticator-grid">
      <article v-for="entry in filteredEntries" :key="entry.id" class="authenticator-card">
        <div class="authenticator-card-heading">
          <div class="authenticator-avatar">{{ avatarText(entry) }}</div>
          <div>
            <strong>{{ entry.issuer || entry.name }}</strong>
            <span>{{ entry.account || entry.name }}</span>
          </div>
          <n-button text size="small" @click="openEditor(entry)">编辑</n-button>
          <n-popconfirm @positive-click="removeEntry(entry)">
            <template #trigger><n-button text type="error" size="small">删除</n-button></template>
            确定删除“{{ entry.issuer || entry.name }}”吗？删除后无法恢复密钥。
          </n-popconfirm>
        </div>
        <button class="authenticator-code" type="button" title="点击复制" @click="copyCode(entry)">
          {{ formatCode(entry.code) }}
        </button>
        <div class="authenticator-progress-row">
          <n-progress
            type="line"
            :percentage="progressPercentage(entry)"
            :show-indicator="false"
            :height="5"
            :border-radius="3"
            :color="remaining(entry) <= 5 ? '#d97757' : '#79aaa6'"
            rail-color="#29353d"
          />
          <span>{{ remaining(entry) }}s</span>
        </div>
        <small>{{ entry.algorithm }} · {{ entry.digits }} 位 · {{ entry.period }} 秒</small>
      </article>
    </div>

    <n-modal v-model:show="showEditor" preset="card" :title="editingId ? '编辑认证器账号' : '添加认证器账号'" class="authenticator-dialog">
      <n-form label-placement="top">
        <n-form-item v-if="!editingId" label="otpauth URI（可选）">
          <n-input v-model:value="draft.uri" type="textarea" :autosize="{ minRows: 2, maxRows: 4 }" placeholder="粘贴 otpauth://totp/... 可自动填写下面内容" @blur="applyOtpAuthUri" />
        </n-form-item>
        <div class="authenticator-form-grid">
          <n-form-item label="显示名称"><n-input v-model:value="draft.name" placeholder="例如 GitHub" /></n-form-item>
          <n-form-item label="发行方"><n-input v-model:value="draft.issuer" placeholder="例如 GitHub" /></n-form-item>
          <n-form-item label="账号"><n-input v-model:value="draft.account" placeholder="name@example.com" /></n-form-item>
          <n-form-item :label="editingId ? 'Base32 密钥（留空表示不修改）' : 'Base32 密钥'">
            <n-input v-model:value="draft.secret" type="password" show-password-on="click" placeholder="JBSWY3DPEHPK3PXP" />
          </n-form-item>
          <n-form-item label="算法"><n-select v-model:value="draft.algorithm" :options="algorithmOptions" /></n-form-item>
          <n-form-item label="验证码位数"><n-select v-model:value="draft.digits" :options="digitOptions" /></n-form-item>
          <n-form-item label="刷新周期（秒）"><n-input-number v-model:value="draft.period" :min="15" :max="120" :step="15" /></n-form-item>
        </div>
      </n-form>
      <template #footer>
        <div class="authenticator-dialog-actions">
          <n-button @click="showEditor = false">取消</n-button>
          <n-button type="primary" :loading="saving" @click="saveEntry">保存</n-button>
        </div>
      </template>
    </n-modal>
  </section>
</template>

<script setup lang="ts">
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
  NProgress,
  NSelect,
  NSpin,
  useMessage,
} from "naive-ui";
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import {
  deleteAuthenticatorEntry,
  fetchAuthenticatorEntries,
  saveAuthenticatorEntry,
  type AuthenticatorEntry,
  type AuthenticatorEntryInput,
} from "@/api";
import { writeClipboard } from "@/utils/clipboard";

const message = useMessage();
const entries = ref<AuthenticatorEntry[]>([]);
const loading = ref(true);
const saving = ref(false);
const keyword = ref("");
const showEditor = ref(false);
const editingId = ref("");
const clockSeconds = ref(Math.floor(Date.now() / 1000));
let serverOffsetSeconds = 0;
let clockTimer = 0;
let lastBoundaryRefresh = 0;

const algorithmOptions = ["SHA1", "SHA256", "SHA512"].map((value) => ({ label: value, value }));
const digitOptions = [6, 8].map((value) => ({ label: `${value} 位`, value }));
const draft = reactive<AuthenticatorEntryInput & { uri: string }>({
  id: "",
  name: "",
  issuer: "",
  account: "",
  secret: "",
  algorithm: "SHA1",
  digits: 6,
  period: 30,
  uri: "",
});

const filteredEntries = computed(() => {
  const query = keyword.value.trim().toLowerCase();
  if (!query) return entries.value;
  return entries.value.filter((entry) => `${entry.name} ${entry.issuer} ${entry.account}`.toLowerCase().includes(query));
});

onMounted(() => {
  void refreshEntries();
  clockTimer = window.setInterval(() => {
    clockSeconds.value = Math.floor(Date.now() / 1000) - serverOffsetSeconds;
    if (entries.value.some((entry) => remaining(entry) === entry.period) && clockSeconds.value !== lastBoundaryRefresh) {
      lastBoundaryRefresh = clockSeconds.value;
      void refreshEntries(false);
    }
  }, 1000);
  document.addEventListener("visibilitychange", handleVisibilityChange);
});

onBeforeUnmount(() => {
  window.clearInterval(clockTimer);
  document.removeEventListener("visibilitychange", handleVisibilityChange);
});

async function refreshEntries(showLoading = true) {
  if (showLoading) loading.value = true;
  try {
    const response = await fetchAuthenticatorEntries();
    entries.value = response.entries;
    serverOffsetSeconds = Math.floor(Date.now() / 1000) - response.serverTime;
    clockSeconds.value = response.serverTime;
  } catch (error) {
    message.error(errorMessage(error, "认证器读取失败"));
  } finally {
    loading.value = false;
  }
}

function handleVisibilityChange() {
  if (!document.hidden) void refreshEntries(false);
}

function remaining(entry: AuthenticatorEntry) {
  const remainder = ((clockSeconds.value % entry.period) + entry.period) % entry.period;
  return entry.period - remainder;
}

function progressPercentage(entry: AuthenticatorEntry) {
  return Math.max(0, Math.min(100, (remaining(entry) / entry.period) * 100));
}

function avatarText(entry: AuthenticatorEntry) {
  return (entry.issuer || entry.name || "A").trim().slice(0, 1).toUpperCase();
}

function formatCode(code: string) {
  const midpoint = Math.ceil(code.length / 2);
  return `${code.slice(0, midpoint)} ${code.slice(midpoint)}`;
}

async function copyCode(entry: AuthenticatorEntry) {
  if (await writeClipboard(entry.code)) message.success("验证码已复制");
  else message.error("浏览器不允许写入剪贴板");
}

function resetDraft() {
  Object.assign(draft, {
    id: "",
    name: "",
    issuer: "",
    account: "",
    secret: "",
    algorithm: "SHA1",
    digits: 6,
    period: 30,
    uri: "",
  });
}

function openEditor(entry?: AuthenticatorEntry) {
  resetDraft();
  editingId.value = entry?.id ?? "";
  if (entry) {
    Object.assign(draft, {
      id: entry.id,
      name: entry.name,
      issuer: entry.issuer,
      account: entry.account,
      algorithm: entry.algorithm,
      digits: entry.digits,
      period: entry.period,
    });
  }
  showEditor.value = true;
}

function applyOtpAuthUri() {
  const value = draft.uri.trim();
  if (!value) return true;
  try {
    const uri = new URL(value);
    if (uri.protocol !== "otpauth:" || uri.hostname.toLowerCase() !== "totp") throw new Error("仅支持 otpauth://totp URI");
    const label = decodeURIComponent(uri.pathname.replace(/^\//, ""));
    const separator = label.indexOf(":");
    const labelIssuer = separator >= 0 ? label.slice(0, separator) : "";
    const account = separator >= 0 ? label.slice(separator + 1) : label;
    const algorithm = (uri.searchParams.get("algorithm") || "SHA1").toUpperCase();
    if (!["SHA1", "SHA256", "SHA512"].includes(algorithm)) throw new Error("URI 中的算法不受支持");
    const digits = Number(uri.searchParams.get("digits") || 6);
    const period = Number(uri.searchParams.get("period") || 30);
    Object.assign(draft, {
      name: account || label,
      issuer: uri.searchParams.get("issuer") || labelIssuer,
      account,
      secret: uri.searchParams.get("secret") || "",
      algorithm,
      digits: digits === 8 ? 8 : 6,
      period: Number.isFinite(period) ? Math.max(15, Math.min(120, period)) : 30,
    });
    return true;
  } catch (error) {
    message.error(error instanceof Error ? error.message : "otpauth URI 无效");
    return false;
  }
}

async function saveEntry() {
  if (draft.uri && !applyOtpAuthUri()) return;
  if (!draft.name.trim() || (!editingId.value && !draft.secret.trim())) {
    message.warning("请填写名称和 Base32 密钥");
    return;
  }
  saving.value = true;
  try {
    await saveAuthenticatorEntry({
      id: editingId.value || crypto.randomUUID(),
      name: draft.name.trim(),
      issuer: draft.issuer.trim(),
      account: draft.account.trim(),
      secret: draft.secret.trim(),
      algorithm: draft.algorithm,
      digits: draft.digits,
      period: draft.period,
    });
    showEditor.value = false;
    message.success(editingId.value ? "认证器账号已更新" : "认证器账号已添加");
    await refreshEntries(false);
  } catch (error) {
    message.error(errorMessage(error, "认证器保存失败"));
  } finally {
    saving.value = false;
  }
}

async function removeEntry(entry: AuthenticatorEntry) {
  try {
    await deleteAuthenticatorEntry(entry.id);
    entries.value = entries.value.filter((item) => item.id !== entry.id);
    message.success("认证器账号已删除");
  } catch (error) {
    message.error(errorMessage(error, "删除失败"));
  }
}

function errorMessage(error: unknown, fallback: string) {
  if (typeof error === "object" && error && "response" in error) {
    const response = (error as { response?: { data?: { message?: string } } }).response;
    if (response?.data?.message) return response.data.message;
  }
  return error instanceof Error ? error.message : fallback;
}
</script>

<style scoped>
.authenticator-tool { display: grid; gap: 16px; }
.authenticator-toolbar { display: grid; grid-template-columns: minmax(180px, 1fr) minmax(220px, 360px) auto; align-items: center; gap: 12px; }
.authenticator-toolbar > div { display: grid; gap: 3px; }
.authenticator-toolbar strong { font-size: 18px; }
.authenticator-toolbar span { color: #6f7e88; font-size: 13px; }
.authenticator-state { min-height: 260px; display: grid; place-items: center; }
.authenticator-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(290px, 1fr)); gap: 14px; }
.authenticator-card { padding: 16px; display: grid; gap: 13px; border: 1px solid #dce3e7; border-radius: 12px; background: #fff; box-shadow: 0 5px 18px rgb(28 43 52 / 5%); }
.authenticator-card-heading { min-width: 0; display: grid; grid-template-columns: 42px minmax(0, 1fr) auto auto; align-items: center; gap: 9px; }
.authenticator-card-heading > div:nth-child(2) { min-width: 0; display: grid; gap: 2px; }
.authenticator-card-heading strong, .authenticator-card-heading span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.authenticator-card-heading span, .authenticator-card > small { color: #71808a; }
.authenticator-avatar { width: 42px; height: 42px; display: grid; place-items: center; border-radius: 10px; color: #315e5b; background: #d9edeb; font-size: 18px; font-weight: 700; }
.authenticator-code { padding: 0; border: 0; color: #172229; background: transparent; font: 700 clamp(30px, 4vw, 42px)/1.1 "SFMono-Regular", Consolas, monospace; letter-spacing: .08em; text-align: left; cursor: pointer; }
.authenticator-code:hover { color: #287b75; }
.authenticator-progress-row { display: grid; grid-template-columns: minmax(0, 1fr) 34px; align-items: center; gap: 9px; }
.authenticator-progress-row span { color: #63737e; font: 12px/1 monospace; text-align: right; }
.authenticator-form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 0 14px; }
.authenticator-dialog-actions { display: flex; justify-content: flex-end; gap: 9px; }
@media (max-width: 700px) {
  .authenticator-toolbar { grid-template-columns: 1fr auto; }
  .authenticator-toolbar > div { grid-column: 1 / -1; }
  .authenticator-form-grid { grid-template-columns: 1fr; }
  .authenticator-grid { grid-template-columns: 1fr; }
  .authenticator-card-heading { grid-template-columns: 42px minmax(0, 1fr) auto; }
  .authenticator-card-heading > :last-child { grid-column: 3; }
}
</style>
