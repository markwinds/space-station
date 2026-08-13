import {
  CalendarOutline,
  CheckmarkCircleOutline,
  CodeSlashOutline,
  CubeOutline,
  GitCompareOutline,
  KeyOutline,
  KeypadOutline,
  SwapHorizontalOutline,
  TerminalOutline,
  HardwareChipOutline,
  ExtensionPuzzleOutline,
  CameraOutline,
  FolderOpenOutline,
  ScanOutline,
} from "@vicons/ionicons5";

export type ToolId = "jsonFormatter" | "textCompare" | "screenshot" | "fileManager" | "certificate" | "authenticator" | "timeManager" | "habits" | "transfer" | "networkScan" | "ssh" | "serial" | "terminalPlugins" | "runtime";

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
    id: "fileManager",
    title: "文件管理器",
    description: "浏览和管理运行 Space Station 的本机文件",
    category: "Local",
    icon: FolderOpenOutline,
    path: "/tools/files",
  },
  {
    id: "screenshot",
    title: "浏览器截屏",
    description: "捕获页面并裁剪、打码和添加常用标注",
    category: "Capture",
    icon: CameraOutline,
    path: "/tools/screenshot",
  },
  {
    id: "textCompare",
    title: "文件对比",
    description: "比较文本和二进制文件差异",
    category: "Compare",
    icon: GitCompareOutline,
    path: "/tools/file-compare",
  },
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
    id: "authenticator",
    title: "认证器",
    description: "生成和管理 TOTP 动态验证码",
    category: "Security",
    icon: KeypadOutline,
    path: "/tools/authenticator",
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
    id: "transfer",
    title: "差分传输",
    description: "使用块匹配和 SHA-256 传输多个文件",
    category: "Transfer",
    icon: SwapHorizontalOutline,
    path: "/tools/transfer",
  },
  {
    id: "networkScan",
    title: "网络扫描",
    description: "发现网段内可达主机、延时和常用服务",
    category: "Network",
    icon: ScanOutline,
    path: "/tools/network-scan",
  },
  {
    id: "ssh",
    title: "远程终端",
    description: "管理 SSH 与 Telnet 主机并打开远程终端",
    category: "Remote",
    icon: TerminalOutline,
    path: "/tools/ssh",
  },
  {
    id: "serial",
    title: "串口终端",
    description: "连接浏览器或服务器所在主机的串口",
    category: "Device",
    icon: HardwareChipOutline,
    path: "/tools/serial",
  },
  {
    id: "terminalPlugins",
    title: "终端插件",
    description: "管理串口与 SSH 的后端 JavaScript 自动化",
    category: "Automation",
    icon: ExtensionPuzzleOutline,
    path: "/tools/terminal-plugins",
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

export function isLocalAccess() {
  const hostname = window.location.hostname.toLowerCase();
  return hostname === "localhost" || hostname.endsWith(".localhost") || hostname === "::1" ||
    hostname === "[::1]" || /^127(?:\.\d{1,3}){3}$/.test(hostname);
}

export function isToolAvailable(tool: ToolDefinition) {
  return !tool.disabled && (tool.id !== "fileManager" || isLocalAccess());
}

export function findTool(id: ToolId) {
  return tools.find((tool) => tool.id === id);
}
