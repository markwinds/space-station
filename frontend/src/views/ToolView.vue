<template>
  <section class="tool-view">
    <header class="tool-header">
      <div>
        <p class="eyebrow">{{ tool?.category }}</p>
        <h2>{{ tool?.title }}</h2>
      </div>
      <n-button tertiary @click="router.push('/')">返回主页</n-button>
    </header>

    <component :is="component" />
  </section>
</template>

<script setup lang="ts">
import type { Component } from "vue";
import { useRouter } from "vue-router";
import { NButton } from "naive-ui";
import type { ToolDefinition } from "@/tools";

defineProps<{
  tool?: ToolDefinition;
  component: Component;
}>();

const router = useRouter();
</script>
