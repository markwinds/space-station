<template>
  <n-config-provider>
    <n-message-provider>
      <div class="app-shell">
        <aside class="tool-rail" aria-label="工具入口">
          <div class="brand-block">
            <div class="brand-mark">SS</div>
            <div>
              <h1>Space Station</h1>
              <p>轻量工具舱</p>
            </div>
          </div>

          <nav class="tool-list">
            <button
              v-for="tool in tools"
              :key="tool.id"
              :class="['tool-entry', { active: activeToolId === tool.id, disabled: tool.disabled }]"
              type="button"
              :disabled="tool.disabled"
              @click="activeToolId = tool.id"
            >
              <n-icon size="20">
                <component :is="tool.icon" />
              </n-icon>
              <span>{{ tool.title }}</span>
            </button>
          </nav>

          <div class="backend-status" :class="{ online: health?.ok }">
            <span class="status-dot"></span>
            <span>{{ healthLabel }}</span>
          </div>
        </aside>

        <main class="workspace">
          <section v-if="activeToolId === HOME_TOOL_ID" class="home-view">
            <div class="home-header">
              <p class="eyebrow">Station Dashboard</p>
              <h2>选择一个工具开始</h2>
            </div>

            <div class="tool-grid">
              <button
                v-for="tool in launchTools"
                :key="tool.id"
                :class="['tool-card', { disabled: tool.disabled }]"
                type="button"
                :disabled="tool.disabled"
                @click="activeToolId = tool.id"
              >
                <n-icon size="24">
                  <component :is="tool.icon" />
                </n-icon>
                <span>{{ tool.title }}</span>
                <small>{{ tool.description }}</small>
              </button>
            </div>
          </section>

          <section v-else class="tool-view">
            <header class="tool-header">
              <div>
                <p class="eyebrow">{{ activeTool?.category }}</p>
                <h2>{{ activeTool?.title }}</h2>
              </div>
              <n-button tertiary @click="activeToolId = HOME_TOOL_ID">返回主页</n-button>
            </header>

            <json-formatter-tool v-if="activeToolId === 'jsonFormatter'" />
            <certificate-tool v-if="activeToolId === 'certificate'" />
            <runtime-settings v-if="activeToolId === 'runtime'" />
          </section>
        </main>
      </div>
    </n-message-provider>
  </n-config-provider>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { CodeSlashOutline, CubeOutline, HomeOutline, KeyOutline, TimerOutline, WifiOutline } from "@vicons/ionicons5";
import { NButton, NConfigProvider, NIcon, NMessageProvider } from "naive-ui";
import RuntimeSettings from "@/components/settings/RuntimeSettings.vue";
import CertificateTool from "@/components/tools/CertificateTool.vue";
import JsonFormatterTool from "@/components/tools/JsonFormatterTool.vue";
import { fetchHealth, type HealthResponse } from "@/api";

const HOME_TOOL_ID = "home";

type ToolId = typeof HOME_TOOL_ID | "jsonFormatter" | "certificate" | "network" | "scheduler" | "runtime";

interface ToolDefinition {
  id: ToolId;
  title: string;
  description: string;
  category: string;
  icon: unknown;
  disabled?: boolean;
}

const tools: ToolDefinition[] = [
  {
    id: HOME_TOOL_ID,
    title: "主页",
    description: "工具入口总览",
    category: "Home",
    icon: HomeOutline,
  },
  {
    id: "jsonFormatter",
    title: "JSON 格式化",
    description: "校验、格式化和压缩 JSON",
    category: "Formatter",
    icon: CodeSlashOutline,
  },
  {
    id: "certificate",
    title: "证书助手",
    description: "生成 CA、证书、密钥、CSR，并用 CA 签发证书",
    category: "Security",
    icon: KeyOutline,
  },
  {
    id: "network",
    title: "网络工具",
    description: "预留入口",
    category: "Reserved",
    icon: WifiOutline,
    disabled: true,
  },
  {
    id: "scheduler",
    title: "任务调度",
    description: "预留入口",
    category: "Reserved",
    icon: TimerOutline,
    disabled: true,
  },
  {
    id: "runtime",
    title: "运行配置",
    description: "管理数据目录、日志级别和后端端口",
    category: "Settings",
    icon: CubeOutline,
  },
];

const activeToolId = ref<ToolId>(HOME_TOOL_ID);
const health = ref<HealthResponse | null>(null);

const launchTools = computed(() => tools.filter((tool) => tool.id !== HOME_TOOL_ID));
const activeTool = computed(() => tools.find((tool) => tool.id === activeToolId.value));
const healthLabel = computed(() => (health.value?.ok ? "后端已连接" : "前端独立运行"));

onMounted(async () => {
  try {
    health.value = await fetchHealth();
  } catch {
    health.value = null;
  }
});
</script>
