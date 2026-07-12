import { defineAsyncComponent } from "vue";
import { createRouter, createWebHistory } from "vue-router";
import CertificateTool from "@/components/tools/CertificateTool.vue";
import JsonFormatterTool from "@/components/tools/JsonFormatterTool.vue";
import RuntimeSettings from "@/components/settings/RuntimeSettings.vue";
import HomeView from "@/views/HomeView.vue";
import ToolView from "@/views/ToolView.vue";
import { findTool } from "@/tools";

const SchedulerTool = defineAsyncComponent(() => import("@/components/tools/SchedulerTool.vue"));
const TimeManagerTool = defineAsyncComponent(() => import("@/components/tools/TimeManagerTool.vue"));
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
      path: "/tools/time-manager",
      name: "time-manager",
      component: ToolView,
      meta: { hideChrome: true },
      props: {
        tool: findTool("timeManager"),
        component: TimeManagerTool,
        hideHeader: true,
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

router.onError((error) => {
  const message = error instanceof Error ? error.message : String(error);
  if (!/Failed to fetch dynamically imported module|Importing a module script failed|error loading dynamically imported module/i.test(message)) {
    return;
  }

  const reloadKey = "space-station:chunk-reload";
  if (sessionStorage.getItem(reloadKey) === "1") {
    return;
  }
  sessionStorage.setItem(reloadKey, "1");
  window.location.reload();
});

router.afterEach(() => {
  sessionStorage.removeItem("space-station:chunk-reload");
});

export default router;
