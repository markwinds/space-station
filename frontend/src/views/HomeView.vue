<template>
  <section class="home-view">
    <div class="home-header">
      <div>
        <p class="eyebrow">Station Dashboard</p>
        <h2>选择一个工具开始</h2>
      </div>
    </div>

    <div class="tool-grid">
      <router-link
        v-for="tool in tools"
        :key="tool.id"
        :class="['tool-card', { disabled: tool.disabled }]"
        :to="tool.disabled ? '/' : tool.path"
        :aria-disabled="tool.disabled || undefined"
        @click="handleDisabledClick($event, tool.disabled)"
      >
        <n-icon size="24">
          <component :is="tool.icon" />
        </n-icon>
        <span>{{ tool.title }}</span>
        <small>{{ tool.description }}</small>
      </router-link>
    </div>
  </section>
</template>

<script setup lang="ts">
import { NIcon } from "naive-ui";
import { RouterLink } from "vue-router";
import { tools } from "@/tools";

function handleDisabledClick(event: MouseEvent, disabled?: boolean) {
  if (disabled) {
    event.preventDefault();
  }
}
</script>
