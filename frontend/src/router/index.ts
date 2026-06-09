import { createRouter, createWebHistory } from "vue-router";
import CertificateTool from "@/components/tools/CertificateTool.vue";
import JsonFormatterTool from "@/components/tools/JsonFormatterTool.vue";
import RuntimeSettings from "@/components/settings/RuntimeSettings.vue";
import HomeView from "@/views/HomeView.vue";
import ToolView from "@/views/ToolView.vue";
import { findTool } from "@/tools";

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
