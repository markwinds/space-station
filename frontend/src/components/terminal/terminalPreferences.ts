export interface TerminalPreferences {
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

export const TERMINAL_PREFERENCES_KEY = "space-station:terminal-preferences";
export const DEFAULT_TERMINAL_FONT_FAMILY = '"DejaVu Sans Mono", "Courier New", Courier, monospace';

export const defaultTerminalPreferences: TerminalPreferences = {
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
