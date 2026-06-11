import { defineAsyncComponent } from "vue";
import { createRouter, createWebHistory } from "vue-router";
import CertificateTool from "@/components/tools/CertificateTool.vue";
import JsonFormatterTool from "@/components/tools/JsonFormatterTool.vue";
import RuntimeSettings from "@/components/settings/RuntimeSettings.vue";
import HomeView from "@/views/HomeView.vue";
import ToolView from "@/views/ToolView.vue";
import { findTool } from "@/tools";

const SchedulerTool = defineAsyncComponent(() => import("@/components/tools/SchedulerTool.vue"));
const FileShareTool = defineAsyncComponent(() => import("@/components/tools/FileShareTool.vue"));

const router = createRouter({
  history: createWebHistory("/web/"),
  routes: [
    {
      path: "/",
      name: "home",
      component: HomeView,
    },
    {
      path: "/tools/json-formatter",
      name: "json-formatter",
      component: ToolView,
      props: {
        tool: findTool("jsonFormatter"),
        component: JsonFormatterTool,
      },
    },
    {
      path: "/tools/certificate",
      name: "certificate",
      component: ToolView,
      props: {
        tool: findTool("certificate"),
        component: CertificateTool,
      },
    },
    {
      path: "/tools/scheduler",
      name: "scheduler",
      component: ToolView,
      props: {
        tool: findTool("scheduler"),
        component: SchedulerTool,
      },
    },
    {
      path: "/tools/file-share",
      name: "file-share",
      component: ToolView,
      props: {
        tool: findTool("fileShare"),
        component: FileShareTool,
      },
    },
    {
      path: "/settings/runtime",
      name: "runtime-settings",
      component: ToolView,
      props: {
        tool: findTool("runtime"),
        component: RuntimeSettings,
      },
    },
    {
      path: "/:pathMatch(.*)*",
      redirect: "/",
    },
  ],
});

export default router;
