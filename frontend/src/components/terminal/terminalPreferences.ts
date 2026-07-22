export interface TerminalPreferences {
  scrollbackLines: number;
  recordingMaxMiB: number;
  fontSize: number;
  lineHeight: number;
  letterSpacing: number;
  showCommandComposer: boolean;
  copyOnSelect: boolean;
  pasteOnRightClick: boolean;
}

export const TERMINAL_PREFERENCES_KEY = "space-station:terminal-preferences";

export const defaultTerminalPreferences: TerminalPreferences = {
  scrollbackLines: 50000,
  recordingMaxMiB: 50,
  fontSize: 14,
  lineHeight: 1.2,
  letterSpacing: 0,
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
  return {
    scrollbackLines: clampTerminalInteger(value.scrollbackLines, 1000, 500000, defaults.scrollbackLines),
    recordingMaxMiB: clampTerminalInteger(value.recordingMaxMiB, 1, 500, defaults.recordingMaxMiB),
    fontSize: clampTerminalInteger(value.fontSize, 10, 28, defaults.fontSize),
    lineHeight: clampTerminalDecimal(value.lineHeight, 1, 2, defaults.lineHeight),
    letterSpacing: clampTerminalDecimal(value.letterSpacing, 0, 4, defaults.letterSpacing),
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
