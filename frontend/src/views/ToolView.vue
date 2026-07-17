<template>
  <section class="tool-view" :class="viewClass">
    <header v-if="!hideHeader" class="tool-header">
      <div class="tool-header-leading">
        <div>
          <p class="eyebrow">{{ tool?.category }}</p>
          <h2>{{ tool?.title }}</h2>
        </div>
      </div>
    </header>

    <component :is="component" />
  </section>
</template>

<script setup lang="ts">
import type { Component } from "vue";
import type { ToolDefinition } from "@/tools";

const props = defineProps<{
  tool?: ToolDefinition;
  component: Component;
  hideHeader?: boolean;
}>();
const viewClass = {
  "tool-view--fullscreen": props.hideHeader,
  "tool-view--habits": props.tool?.id === "habits",
};
</script>
