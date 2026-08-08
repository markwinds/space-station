import { defineAsyncComponent } from "vue";
import { createRouter, createWebHistory } from "vue-router";
import CertificateTool from "@/components/tools/CertificateTool.vue";
import JsonFormatterTool from "@/components/tools/JsonFormatterTool.vue";
import RuntimeSettings from "@/components/settings/RuntimeSettings.vue";
import HomeView from "@/views/HomeView.vue";
import ToolView from "@/views/ToolView.vue";
import { findTool, isLocalAccess, tools } from "@/tools";
import { recordRecentTool } from "@/toolPreferences";

const TimeManagerTool = defineAsyncComponent(() => import("@/components/tools/TimeManagerTool.vue"));
const HabitTool = defineAsyncComponent(() => import("@/components/tools/HabitTool.vue"));
const TransferTool = defineAsyncComponent(() => import("@/components/tools/TransferTool.vue"));
const SshTool = defineAsyncComponent(() => import("@/components/tools/SshTool.vue"));
const SerialTool = defineAsyncComponent(() => import("@/components/tools/SerialTool.vue"));
const AuthenticatorTool = defineAsyncComponent(() => import("@/components/tools/AuthenticatorTool.vue"));
const TerminalPluginTool = defineAsyncComponent(() => import("@/components/tools/TerminalPluginTool.vue"));
const TextCompareTool = defineAsyncComponent(() => import("@/components/tools/TextCompareTool.vue"));
const ScreenshotTool = defineAsyncComponent(() => import("@/components/tools/ScreenshotTool.vue"));
const FileManagerTool = defineAsyncComponent(() => import("@/components/tools/FileManagerTool.vue"));

const router = createRouter({
  history: createWebHistory("/web/"),
  routes: [
    {
      path: "/",
      name: "home",
      component: HomeView,
    },
    {
      path: "/tools/files",
      name: "file-manager",
      component: ToolView,
      meta: { hideChrome: true, localOnly: true },
      props: {
        tool: findTool("fileManager"),
        component: FileManagerTool,
        hideHeader: true,
      },
    },
    {
      path: "/tools/screenshot",
      name: "screenshot",
      component: ToolView,
      meta: { hideChrome: true },
      props: {
        tool: findTool("screenshot"),
        component: ScreenshotTool,
        hideHeader: true,
      },
    },
    {
      path: "/tools/file-compare",
      alias: "/tools/text-compare",
      name: "file-compare",
      component: ToolView,
      meta: { hideChrome: true },
      props: {
        tool: findTool("textCompare"),
        component: TextCompareTool,
        hideHeader: true,
      },
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
      path: "/tools/authenticator",
      name: "authenticator",
      component: ToolView,
      props: {
        tool: findTool("authenticator"),
        component: AuthenticatorTool,
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
      path: "/tools/habits",
      name: "habits",
      component: ToolView,
      props: {
        tool: findTool("habits"),
        component: HabitTool,
      },
    },
    {
      path: "/tools/transfer",
      name: "transfer",
      component: ToolView,
      props: {
        tool: findTool("transfer"),
        component: TransferTool,
      },
    },
    {
      path: "/tools/ssh",
      name: "ssh",
      component: ToolView,
      meta: { hideChrome: true },
      props: {
        tool: findTool("ssh"),
        component: SshTool,
        hideHeader: true,
      },
    },
    {
      path: "/tools/serial",
      name: "serial",
      component: ToolView,
      meta: { hideChrome: true },
      props: {
        tool: findTool("serial"),
        component: SerialTool,
        hideHeader: true,
      },
    },
    {
      path: "/tools/terminal-plugins",
      name: "terminal-plugins",
      component: ToolView,
      props: {
        tool: findTool("terminalPlugins"),
        component: TerminalPluginTool,
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

router.beforeEach((to) => {
  if (to.meta.localOnly === true && !isLocalAccess()) return { name: "home" };
  return true;
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

router.afterEach((to) => {
  sessionStorage.removeItem("space-station:chunk-reload");
  const tool = tools.find((item) => item.path === to.path && item.id !== "runtime");
  if (tool && !tool.disabled) recordRecentTool(tool.id);
});

export default router;
