<template>
  <n-config-provider>
    <n-message-provider>
      <div class="app-shell">
        <header v-if="isHome" class="app-topbar">
          <router-link class="brand-block" to="/">
            <div class="brand-mark">SS</div>
            <div>
              <h1>Space Station</h1>
              <p>轻量工具舱</p>
            </div>
          </router-link>
          <nav class="topbar-actions" aria-label="全局操作">
            <router-link class="topbar-action" to="/settings/runtime" aria-label="运行配置" title="运行配置">
              <n-icon class="topbar-action-icon" size="18" aria-hidden="true">
                <SettingsOutline />
              </n-icon>
              <span class="topbar-action-label">运行配置</span>
            </router-link>
          </nav>
        </header>

        <main
          class="workspace"
          :class="{
            'workspace--without-topbar': !isHome,
            'workspace--fullscreen': isFullscreen,
          }"
        >
          <router-view />
        </main>
      </div>
    </n-message-provider>
  </n-config-provider>
</template>

<script setup lang="ts">
import { SettingsOutline } from "@vicons/ionicons5";
import { NConfigProvider, NIcon, NMessageProvider } from "naive-ui";
import { computed } from "vue";
import { useRoute } from "vue-router";
import { RouterLink, RouterView } from "vue-router";

const route = useRoute();
const isHome = computed(() => route.name === "home");
const isFullscreen = computed(() => route.meta.hideChrome === true);
</script>
