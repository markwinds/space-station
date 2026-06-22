import { CodeSlashOutline, CubeOutline, FolderOpenOutline, KeyOutline, TimerOutline } from "@vicons/ionicons5";

export type ToolId = "jsonFormatter" | "certificate" | "scheduler" | "fileShare" | "runtime";

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
    id: "scheduler",
    title: "任务调度",
    description: "规划任务时间、依赖和执行顺序",
    category: "Planning",
    icon: TimerOutline,
    path: "/tools/scheduler",
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
