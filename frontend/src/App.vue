<template>
  <n-config-provider>
    <n-message-provider>
      <div class="app-shell">
        <header class="app-topbar">
          <router-link class="brand-block" to="/">
            <div class="brand-mark">SS</div>
            <div>
              <h1>Space Station</h1>
              <p>轻量工具舱</p>
            </div>
          </router-link>

          <div class="topbar-actions">
            <div class="backend-status" :class="{ online: health?.ok }">
              <span class="status-dot"></span>
              <span>{{ healthLabel }}</span>
            </div>
            <router-link class="settings-link" to="/settings/runtime">运行配置</router-link>
          </div>
        </header>

        <main class="workspace">
          <router-view />
        </main>
      </div>
    </n-message-provider>
  </n-config-provider>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { RouterLink, RouterView } from "vue-router";
import { NConfigProvider, NMessageProvider } from "naive-ui";
import { fetchHealth, type HealthResponse } from "@/api";

const health = ref<HealthResponse | null>(null);
const healthLabel = computed(() => (health.value?.ok ? "后端已连接" : "前端独立运行"));

onMounted(async () => {
  try {
    health.value = await fetchHealth();
  } catch {
    health.value = null;
  }
});
</script>
