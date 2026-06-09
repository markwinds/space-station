import { CodeSlashOutline, CubeOutline, KeyOutline, TimerOutline, WifiOutline } from "@vicons/ionicons5";

export type ToolId = "jsonFormatter" | "certificate" | "network" | "scheduler" | "runtime";

export interface ToolDefinition {
  id: ToolId;
  title: string;
  description: string;
  category: string;
  icon: unknown;
  path: string;
  disabled?: boolean;
}

export const tools: ToolDefinition[] = [
  {
    id: "jsonFormatter",
    title: "JSON 格式化",
    description: "校验、格式化和压缩 JSON",
    category: "Formatter",
    icon: CodeSlashOutline,
    path: "/tools/json-formatter",
  },
  {
    id: "certificate",
    title: "证书助手",
    description: "生成、签发和解析 X.509 证书",
    category: "Security",
    icon: KeyOutline,
    path: "/tools/certificate",
  },
  {
    id: "network",
    title: "网络工具",
    description: "预留入口",
    category: "Reserved",
    icon: WifiOutline,
    path: "/tools/network",
    disabled: true,
  },
  {
    id: "scheduler",
    title: "任务调度",
    description: "预留入口",
    category: "Reserved",
    icon: TimerOutline,
    path: "/tools/scheduler",
    disabled: true,
  },
  {
    id: "runtime",
    title: "运行配置",
    description: "管理数据目录、日志级别和后端端口",
    category: "Settings",
    icon: CubeOutline,
    path: "/settings/runtime",
  },
];

export function findTool(id: ToolId) {
  return tools.find((tool) => tool.id === id);
}
