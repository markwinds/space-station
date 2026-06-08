<template>
  <n-card class="tool-panel" title="基础配置" embedded>
    <n-space vertical :size="16">
      <n-alert v-if="loadError" type="warning" :show-icon="false">
        {{ loadError }}
      </n-alert>

      <n-form v-if="form" label-placement="top">
        <div class="settings-grid">
          <n-form-item label="数据目录">
            <n-input v-model:value="form.dataPath" />
          </n-form-item>
          <n-form-item label="日志级别">
            <n-select v-model:value="form.logLevel" :options="logLevelOptions" />
          </n-form-item>
          <n-form-item label="监听端口">
            <n-input-number v-model:value="form.port" :min="1" :max="65535" />
          </n-form-item>
        </div>
      </n-form>

      <div class="action-row">
        <n-button type="primary" :loading="saving" :disabled="!form" @click="save">保存配置</n-button>
        <n-button tertiary :loading="loading" @click="load">重新读取</n-button>
      </div>

      <n-alert v-if="saved" type="success" :show-icon="false">
        配置已保存。端口变更需要重启后端后生效。
      </n-alert>

      <div v-if="config" class="config-paths">
        <div>
          <span>配置文件</span>
          <code>{{ config.configPath }}</code>
        </div>
        <div>
          <span>日志文件</span>
          <code>{{ config.logPath }}</code>
        </div>
      </div>
    </n-space>
  </n-card>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from "vue";
import {
  NAlert,
  NButton,
  NCard,
  NForm,
  NFormItem,
  NInput,
  NInputNumber,
  NSelect,
  NSpace,
} from "naive-ui";
import { fetchConfig, updateConfig, type AppConfig } from "@/api";

type ConfigForm = Pick<AppConfig, "dataPath" | "logLevel" | "port">;

const logLevelOptions = [
  { label: "Trace", value: "trace" },
  { label: "Debug", value: "debug" },
  { label: "Info", value: "info" },
  { label: "Warn", value: "warn" },
  { label: "Error", value: "error" },
];

const config = ref<AppConfig | null>(null);
const form = ref<ConfigForm | null>(null);
const loading = ref(false);
const saving = ref(false);
const saved = ref(false);
const loadError = ref("");

onMounted(() => {
  void load();
});

async function load() {
  loading.value = true;
  saved.value = false;
  loadError.value = "";
  try {
    const nextConfig = await fetchConfig();
    config.value = nextConfig;
    form.value = reactive({
      dataPath: nextConfig.dataPath,
      logLevel: nextConfig.logLevel,
      port: nextConfig.port,
    });
  } catch {
    loadError.value = "未连接到后端，暂时无法读取配置。";
  } finally {
    loading.value = false;
  }
}

async function save() {
  if (!form.value) {
    return;
  }

  saving.value = true;
  saved.value = false;
  loadError.value = "";
  try {
    const nextConfig = await updateConfig({
      dataPath: form.value.dataPath,
      logLevel: form.value.logLevel,
      port: form.value.port,
    });
    config.value = nextConfig;
    form.value = reactive({
      dataPath: nextConfig.dataPath,
      logLevel: nextConfig.logLevel,
      port: nextConfig.port,
    });
    saved.value = true;
  } catch {
    loadError.value = "保存失败，请确认后端正在运行。";
  } finally {
    saving.value = false;
  }
}
</script>
