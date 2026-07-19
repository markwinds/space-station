import {
  CalendarOutline,
  CheckmarkCircleOutline,
  CodeSlashOutline,
  CubeOutline,
  FolderOpenOutline,
  KeyOutline,
  SwapHorizontalOutline,
  TerminalOutline,
} from "@vicons/ionicons5";

export type ToolId = "jsonFormatter" | "certificate" | "timeManager" | "habits" | "fileShare" | "transfer" | "ssh" | "runtime";

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
    id: "timeManager",
    title: "时间管理器",
    description: "树状任务、过滤器和日历视图",
    category: "Planning",
    icon: CalendarOutline,
    path: "/tools/time-manager",
  },
  {
    id: "habits",
    title: "习惯养成",
    description: "培养行动、减少干扰并回顾长期变化",
    category: "Wellbeing",
    icon: CheckmarkCircleOutline,
    path: "/tools/habits",
  },
  {
    id: "fileShare",
    title: "共享文件",
    description: "映射本机目录并上传下载文件",
    category: "Storage",
    icon: FolderOpenOutline,
    path: "/tools/file-share",
  },
  {
    id: "transfer",
    title: "差分传输",
    description: "使用块匹配和 SHA-256 传输多个文件",
    category: "Transfer",
    icon: SwapHorizontalOutline,
    path: "/tools/transfer",
  },
  {
    id: "ssh",
    title: "SSH 终端",
    description: "管理主机并打开远程终端",
    category: "Remote",
    icon: TerminalOutline,
    path: "/tools/ssh",
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
