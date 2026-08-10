export interface TerminalPreferences {
  themeId: TerminalThemeId;
  scrollbackLines: number;
  recordingMaxMiB: number;
  fontFamily: string;
  fontSize: number;
  lineHeight: number;
  letterSpacing: number;
  showLineNumbers: boolean;
  showLineTimestamps: boolean;
  showCommandComposer: boolean;
  copyOnSelect: boolean;
  pasteOnRightClick: boolean;
}

export type TerminalThemeId = "classic" | "station" | "midnight" | "solarized" | "paper" | "amber";

export interface TerminalThemePalette {
  background: string;
  foreground: string;
  cursor: string;
  cursorAccent: string;
  selectionBackground: string;
  selectionForeground: string;
  black: string;
  red: string;
  green: string;
  yellow: string;
  blue: string;
  magenta: string;
  cyan: string;
  white: string;
  brightBlack: string;
  brightRed: string;
  brightGreen: string;
  brightYellow: string;
  brightBlue: string;
  brightMagenta: string;
  brightCyan: string;
  brightWhite: string;
}

export interface TerminalThemeDefinition {
  id: TerminalThemeId;
  label: string;
  description: string;
  palette: TerminalThemePalette;
}

export const TERMINAL_THEMES: TerminalThemeDefinition[] = [
  {
    id: "classic",
    label: "经典原始",
    description: "主题功能加入前的原始配色，当前默认",
    palette: {
      background: "#101418", foreground: "#d8dee9", cursor: "#8fbcbb", cursorAccent: "#000000",
      selectionBackground: "#d96820", selectionForeground: "#ffffff",
      black: "#2e3436", red: "#cc0000", green: "#4e9a06", yellow: "#c4a000", blue: "#3465a4", magenta: "#75507b", cyan: "#06989a", white: "#d3d7cf",
      brightBlack: "#555753", brightRed: "#ef2929", brightGreen: "#8ae234", brightYellow: "#fce94f", brightBlue: "#729fcf", brightMagenta: "#ad7fa8", brightCyan: "#34e2e2", brightWhite: "#eeeeec",
    },
  },
  {
    id: "station",
    label: "空间站",
    description: "沉稳青灰，优化 ANSI 配色",
    palette: {
      background: "#101418", foreground: "#d8dee9", cursor: "#8fbcbb", cursorAccent: "#101418",
      selectionBackground: "#d96820", selectionForeground: "#ffffff",
      black: "#1b2228", red: "#e06c75", green: "#7fbd8f", yellow: "#e5c07b", blue: "#75a7d8", magenta: "#c59bdc", cyan: "#76c7c0", white: "#d8dee9",
      brightBlack: "#5f6b74", brightRed: "#f08a95", brightGreen: "#9bd5a9", brightYellow: "#f2d294", brightBlue: "#96bfe6", brightMagenta: "#d8b4ea", brightCyan: "#9bddda", brightWhite: "#f4f8fa",
    },
  },
  {
    id: "midnight",
    label: "午夜蓝",
    description: "高对比冷色，适合长时间工作",
    palette: {
      background: "#0b1020", foreground: "#c8d3f5", cursor: "#82aaff", cursorAccent: "#0b1020",
      selectionBackground: "#2d3f76", selectionForeground: "#ffffff",
      black: "#1b1d2b", red: "#ff757f", green: "#c3e88d", yellow: "#ffc777", blue: "#82aaff", magenta: "#c099ff", cyan: "#86e1fc", white: "#c8d3f5",
      brightBlack: "#636da6", brightRed: "#ff98a4", brightGreen: "#d5f0a7", brightYellow: "#ffd99a", brightBlue: "#a5c4ff", brightMagenta: "#d0b3ff", brightCyan: "#a8ebff", brightWhite: "#ffffff",
    },
  },
  {
    id: "solarized",
    label: "日光暗色",
    description: "低疲劳经典配色",
    palette: {
      background: "#002b36", foreground: "#93a1a1", cursor: "#eee8d5", cursorAccent: "#002b36",
      selectionBackground: "#07505e", selectionForeground: "#fdf6e3",
      black: "#073642", red: "#dc322f", green: "#859900", yellow: "#b58900", blue: "#268bd2", magenta: "#d33682", cyan: "#2aa198", white: "#eee8d5",
      brightBlack: "#586e75", brightRed: "#cb4b16", brightGreen: "#93a1a1", brightYellow: "#839496", brightBlue: "#657b83", brightMagenta: "#6c71c4", brightCyan: "#2aa198", brightWhite: "#fdf6e3",
    },
  },
  {
    id: "paper",
    label: "纸张亮色",
    description: "明亮环境下更清晰",
    palette: {
      background: "#f7f7f3", foreground: "#263238", cursor: "#00695c", cursorAccent: "#f7f7f3",
      selectionBackground: "#b2dfdb", selectionForeground: "#1c292e",
      black: "#263238", red: "#c62828", green: "#2e7d32", yellow: "#9a6700", blue: "#1565c0", magenta: "#8e24aa", cyan: "#00796b", white: "#cfd8dc",
      brightBlack: "#607d8b", brightRed: "#e53935", brightGreen: "#43a047", brightYellow: "#c58b00", brightBlue: "#1e88e5", brightMagenta: "#ab47bc", brightCyan: "#009688", brightWhite: "#ffffff",
    },
  },
  {
    id: "amber",
    label: "琥珀复古",
    description: "暖色终端，专注且醒目",
    palette: {
      background: "#17130a", foreground: "#f6d58a", cursor: "#ffb454", cursorAccent: "#17130a",
      selectionBackground: "#5a451c", selectionForeground: "#fff4d6",
      black: "#241d0f", red: "#ef6f6c", green: "#a8c96f", yellow: "#e7b65a", blue: "#77a8c9", magenta: "#c58ab3", cyan: "#72b7a8", white: "#ead7ad",
      brightBlack: "#74664b", brightRed: "#ff8d86", brightGreen: "#c0dd86", brightYellow: "#ffd078", brightBlue: "#91c0dd", brightMagenta: "#dda4ca", brightCyan: "#8fd0c1", brightWhite: "#fff4d6",
    },
  },
];

export function terminalThemeDefinition(themeId: unknown) {
  return TERMINAL_THEMES.find((theme) => theme.id === themeId) ?? TERMINAL_THEMES[0];
}

export function terminalThemePalette(themeId: unknown) {
  return terminalThemeDefinition(themeId).palette;
}

export const TERMINAL_PREFERENCES_KEY = "space-station:terminal-preferences";
export const DEFAULT_TERMINAL_FONT_FAMILY = '"DejaVu Sans Mono", "Courier New", Courier, monospace';

export const defaultTerminalPreferences: TerminalPreferences = {
  themeId: "classic",
  scrollbackLines: 50000,
  recordingMaxMiB: 50,
  fontFamily: DEFAULT_TERMINAL_FONT_FAMILY,
  fontSize: 13,
  lineHeight: 1,
  letterSpacing: 0,
  showLineNumbers: false,
  showLineTimestamps: false,
  showCommandComposer: true,
  copyOnSelect: false,
  pasteOnRightClick: false,
};

export function clampTerminalInteger(value: unknown, min: number, max: number, fallback: number) {
  const number = Math.trunc(Number(value));
  return Number.isFinite(number) ? Math.max(min, Math.min(max, number)) : fallback;
}

export function clampTerminalDecimal(value: unknown, min: number, max: number, fallback: number) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.max(min, Math.min(max, number)) : fallback;
}

export function normalizeTerminalPreferences(value: Partial<TerminalPreferences> = {}): TerminalPreferences {
  const defaults = defaultTerminalPreferences;
  const hasLegacyAppearance = typeof value.fontFamily !== "string" || !value.fontFamily.trim();
  const legacyFontSize = Number(value.fontSize);
  const legacyLineHeight = Number(value.lineHeight);
  return {
    themeId: terminalThemeDefinition(value.themeId).id,
    scrollbackLines: clampTerminalInteger(value.scrollbackLines, 1000, 500000, defaults.scrollbackLines),
    recordingMaxMiB: clampTerminalInteger(value.recordingMaxMiB, 1, 500, defaults.recordingMaxMiB),
    fontFamily: typeof value.fontFamily === "string" && value.fontFamily.trim()
      ? value.fontFamily.trim().slice(0, 300)
      : defaults.fontFamily,
    // Migrate untouched values from the former 14px/1.2 defaults while
    // preserving any clearly customized legacy appearance.
    fontSize: clampTerminalInteger(hasLegacyAppearance && legacyFontSize === 14 ? defaults.fontSize : value.fontSize, 10, 28, defaults.fontSize),
    lineHeight: clampTerminalDecimal(hasLegacyAppearance && legacyLineHeight === 1.2 ? defaults.lineHeight : value.lineHeight, 1, 2, defaults.lineHeight),
    letterSpacing: clampTerminalDecimal(value.letterSpacing, 0, 4, defaults.letterSpacing),
    showLineNumbers: value.showLineNumbers === true,
    showLineTimestamps: value.showLineTimestamps === true,
    showCommandComposer: value.showCommandComposer !== false,
    copyOnSelect: value.copyOnSelect === true,
    pasteOnRightClick: value.pasteOnRightClick === true,
  };
}

export function loadTerminalPreferences(): TerminalPreferences {
  try {
    const current = localStorage.getItem(TERMINAL_PREFERENCES_KEY);
    // Migrate the original SSH-only key once so existing user settings are retained.
    const legacy = localStorage.getItem("ssh-terminal-settings");
    return normalizeTerminalPreferences(JSON.parse(current || legacy || "{}") as Partial<TerminalPreferences>);
  } catch {
    return { ...defaultTerminalPreferences };
  }
}

export function saveTerminalPreferences(value: TerminalPreferences) {
  const normalized = normalizeTerminalPreferences(value);
  localStorage.setItem(TERMINAL_PREFERENCES_KEY, JSON.stringify(normalized));
  // Keep the old key synchronized for exported configurations from older builds.
  localStorage.setItem("ssh-terminal-settings", JSON.stringify(normalized));
  return normalized;
}
