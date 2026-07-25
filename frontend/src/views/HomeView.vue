<template>
  <section class="home-view">
    <div class="home-header">
      <div>
        <p class="eyebrow">Station Dashboard</p>
        <h2>选择一个工具开始</h2>
      </div>
    </div>

    <section v-if="favoriteTools.length || recentTools.length" class="quick-access" aria-labelledby="quick-access-title">
      <div class="home-section-heading">
        <div>
          <p class="eyebrow">Quick Access</p>
          <h3 id="quick-access-title">快捷访问</h3>
        </div>
      </div>

      <div class="quick-groups">
        <div v-if="favoriteTools.length" class="quick-group">
          <div class="quick-group-heading">
            <h4>喜爱</h4>
            <span>{{ favoriteTools.length }} 个</span>
          </div>
          <div class="favorite-grid">
            <article v-for="tool in favoriteTools" :key="tool.id" class="tool-card-shell favorite-card-shell">
              <router-link class="tool-card favorite-card" :to="tool.path">
                <n-icon size="22"><component :is="tool.icon" /></n-icon>
                <span>{{ tool.title }}</span>
                <small>{{ tool.description }}</small>
              </router-link>
              <button
                class="favorite-toggle active"
                type="button"
                :aria-label="`从喜爱中移除${tool.title}`"
                :aria-pressed="true"
                :title="`从喜爱中移除${tool.title}`"
                @click="toggleFavorite(tool.id)"
              >
                <n-icon size="18"><Star /></n-icon>
              </button>
            </article>
          </div>
        </div>

        <div v-if="recentTools.length" class="quick-group">
          <div class="quick-group-heading">
            <h4>最近使用</h4>
            <span>最近 {{ recentTools.length }} 个</span>
          </div>
          <div class="recent-strip">
            <router-link v-for="tool in recentTools" :key="tool.id" class="recent-tool" :to="tool.path">
              <n-icon size="20"><component :is="tool.icon" /></n-icon>
              <span>{{ tool.title }}</span>
              <small>{{ tool.category }}</small>
            </router-link>
          </div>
        </div>
      </div>
    </section>

    <section class="all-tools" aria-labelledby="all-tools-title">
      <div class="home-section-heading">
        <div>
          <p class="eyebrow">All Tools</p>
          <h3 id="all-tools-title">全部工具</h3>
        </div>
        <small>点击星标加入喜爱</small>
      </div>

      <div class="tool-grid">
        <article v-for="tool in dashboardTools" :key="tool.id" class="tool-card-shell">
          <router-link
            :class="['tool-card', { disabled: tool.disabled }]"
            :to="tool.disabled ? '/' : tool.path"
            :aria-disabled="tool.disabled || undefined"
            @click="handleDisabledClick($event, tool.disabled)"
          >
            <n-icon size="24"><component :is="tool.icon" /></n-icon>
            <span>{{ tool.title }}</span>
            <small>{{ tool.description }}</small>
          </router-link>
          <button
            class="favorite-toggle"
            :class="{ active: isFavorite(tool.id) }"
            type="button"
            :disabled="tool.disabled"
            :aria-label="isFavorite(tool.id) ? `从喜爱中移除${tool.title}` : `将${tool.title}加入喜爱`"
            :aria-pressed="isFavorite(tool.id)"
            :title="isFavorite(tool.id) ? `从喜爱中移除${tool.title}` : `将${tool.title}加入喜爱`"
            @click="toggleFavorite(tool.id)"
          >
            <n-icon size="18"><component :is="isFavorite(tool.id) ? Star : StarOutline" /></n-icon>
          </button>
        </article>
      </div>
    </section>
  </section>
</template>

<script setup lang="ts">
import { Star, StarOutline } from "@vicons/ionicons5";
import { NIcon } from "naive-ui";
import { computed, onBeforeUnmount, onMounted, ref } from "vue";
import { RouterLink } from "vue-router";
import { tools, type ToolDefinition, type ToolId } from "@/tools";
import {
  loadFavoriteToolIds,
  loadRecentTools,
  saveFavoriteToolIds,
  type RecentToolEntry,
} from "@/toolPreferences";

const dashboardTools = tools.filter((tool) => tool.id !== "runtime");
const favoriteIds = ref<ToolId[]>([]);
const recentEntries = ref<RecentToolEntry[]>([]);

const favoriteTools = computed(() => favoriteIds.value
  .map((id) => dashboardTools.find((tool) => tool.id === id))
  .filter((tool): tool is ToolDefinition => Boolean(tool && !tool.disabled)));

const recentTools = computed(() => recentEntries.value
  .map((entry) => dashboardTools.find((tool) => tool.id === entry.id))
  .filter((tool): tool is ToolDefinition => Boolean(tool && !tool.disabled)));

onMounted(() => {
  syncPreferences();
  window.addEventListener("storage", syncPreferences);
});

onBeforeUnmount(() => window.removeEventListener("storage", syncPreferences));

function syncPreferences() {
  favoriteIds.value = loadFavoriteToolIds().filter((id) => id !== "runtime");
  recentEntries.value = loadRecentTools().filter((entry) => entry.id !== "runtime");
}

function isFavorite(id: ToolId) {
  return favoriteIds.value.includes(id);
}

function toggleFavorite(id: ToolId) {
  favoriteIds.value = isFavorite(id)
    ? favoriteIds.value.filter((favoriteId) => favoriteId !== id)
    : [...favoriteIds.value, id];
  saveFavoriteToolIds(favoriteIds.value);
}

function handleDisabledClick(event: MouseEvent, disabled?: boolean) {
  if (disabled) event.preventDefault();
}
</script>

<style scoped>
.quick-access,
.all-tools { display: grid; gap: 14px; }

.home-section-heading,
.quick-group-heading { display: flex; align-items: flex-end; justify-content: space-between; gap: 14px; }
.home-section-heading h3 { margin: 0; color: #172632; font-size: 21px; }
.home-section-heading > small,
.quick-group-heading span { color: #71808d; font-size: 11px; }
.quick-groups { display: grid; gap: 18px; }
.quick-group { display: grid; gap: 9px; }
.quick-group-heading h4 { margin: 0; color: #344754; font-size: 13px; }

.tool-card-shell { position: relative; min-width: 0; }
.tool-card-shell .tool-card { height: 100%; padding-right: 52px; }
.favorite-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(235px, 1fr)); gap: 12px; }
.favorite-card-shell .favorite-card { min-height: 108px; padding-top: 15px; padding-bottom: 15px; gap: 7px; }
.favorite-card small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

.favorite-toggle {
  position: absolute;
  top: 12px;
  right: 12px;
  z-index: 2;
  width: 32px;
  height: 32px;
  padding: 0;
  display: grid;
  place-items: center;
  border: 1px solid transparent;
  border-radius: 8px;
  background: transparent;
  color: #83909a;
  cursor: pointer;
  transition: 150ms ease;
}
.favorite-toggle:hover { border-color: rgba(54, 77, 99, 0.14); background: #f4f7f8; color: #526573; }
.favorite-toggle.active { color: #c48a25; }
.favorite-toggle:focus-visible { outline: 2px solid #315d73; outline-offset: 2px; }

.recent-strip { display: grid; grid-template-columns: repeat(auto-fit, minmax(155px, 1fr)); gap: 9px; }
.recent-tool {
  min-width: 0;
  min-height: 56px;
  padding: 10px 12px;
  display: grid;
  grid-template-columns: 28px minmax(0, 1fr);
  grid-template-rows: auto auto;
  align-items: center;
  column-gap: 8px;
  border: 1px solid rgba(54, 77, 99, 0.11);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.66);
  color: #21313d;
  text-decoration: none;
  transition: 150ms ease;
}
.recent-tool:hover { transform: translateY(-1px); border-color: rgba(54, 77, 99, 0.22); background: rgba(255, 255, 255, 0.9); }
.recent-tool :deep(.n-icon) { grid-row: 1 / 3; color: #405a6c; }
.recent-tool span { overflow: hidden; font-size: 13px; font-weight: 700; text-overflow: ellipsis; white-space: nowrap; }
.recent-tool small { color: #7a8893; font-size: 9px; text-transform: uppercase; }

@media (max-width: 620px) {
  .home-section-heading { align-items: flex-start; }
  .home-section-heading > small { max-width: 120px; text-align: right; }
  .favorite-grid { grid-template-columns: 1fr; }
  .recent-strip {
    margin-right: -28px;
    padding-right: 28px;
    display: flex;
    overflow-x: auto;
    scroll-snap-type: x proximity;
  }
  .recent-tool { min-width: 170px; flex: 0 0 170px; scroll-snap-align: start; }
}
</style>
