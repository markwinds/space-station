import { computed, ref, watch } from "vue";
import { dateEnUS, dateZhCN, enUS, zhCN } from "naive-ui";
import { englishMessages as generatedEnglishMessages } from "./english.generated";
import { curatedEnglishMessages } from "./english.overrides";

export type AppLocale = "zh-CN" | "en-US";

const STORAGE_KEY = "space-station:locale";
const translatedAttributes = ["aria-label", "aria-description", "alt", "placeholder", "title"] as const;
const textState = new WeakMap<Text, { source: string; translated: string }>();
const attributeState = new WeakMap<Element, Map<string, { source: string; translated: string }>>();
const englishMessages = { ...generatedEnglishMessages, ...curatedEnglishMessages };

function initialLocale(): AppLocale {
  const stored = localStorage.getItem(STORAGE_KEY);
  if (stored === "zh-CN" || stored === "en-US") return stored;
  return navigator.language.toLowerCase().startsWith("zh") ? "zh-CN" : "en-US";
}

export const appLocale = ref<AppLocale>(initialLocale());
export const naiveLocale = computed(() => appLocale.value === "zh-CN" ? zhCN : enUS);
export const naiveDateLocale = computed(() => appLocale.value === "zh-CN" ? dateZhCN : dateEnUS);

const replacementMessages = Object.entries(englishMessages)
  .filter(([source, translated]) => (source.match(/\p{Script=Han}/gu)?.length ?? 0) >= 3 && source !== translated)
  .sort(([left], [right]) => right.length - left.length);

function preserveOuterWhitespace(source: string, translated: string) {
  const leading = source.match(/^\s*/)?.[0] ?? "";
  const trailing = source.match(/\s*$/)?.[0] ?? "";
  return `${leading}${translated}${trailing}`;
}

export function translateText(source: string): string {
  if (appLocale.value === "zh-CN" || !/\p{Script=Han}/u.test(source)) return source;
  const trimmed = source.trim();
  const exact = englishMessages[trimmed];
  if (exact) return preserveOuterWhitespace(source, exact);

  const addFavorite = trimmed.match(/^将(.+)加入喜爱$/u);
  if (addFavorite) return preserveOuterWhitespace(source, `Add ${englishMessages[addFavorite[1]] ?? addFavorite[1]} to favorites`);
  const removeFavorite = trimmed.match(/^从喜爱中移除(.+)$/u);
  if (removeFavorite) return preserveOuterWhitespace(source, `Remove ${englishMessages[removeFavorite[1]] ?? removeFavorite[1]} from favorites`);
  const recentCount = trimmed.match(/^最近\s*(\d+)\s*个$/u);
  if (recentCount) return preserveOuterWhitespace(source, `${recentCount[1]} recent`);
  const weeklyReplacements = trimmed.match(/^本周发生\s*·\s*(\d+)\s*次替代$/u);
  if (weeklyReplacements) return preserveOuterWhitespace(source, `Happening this week · ${weeklyReplacements[1]} substitutions`);
  if (/^周[一二三四五六日](、周[一二三四五六日])*$/u.test(trimmed)) {
    const weekdays: Record<string, string> = { 一: "Mon", 二: "Tue", 三: "Wed", 四: "Thu", 五: "Fri", 六: "Sat", 日: "Sun" };
    return preserveOuterWhitespace(source, trimmed.split("、").map((day) => weekdays[day.at(-1) ?? ""]).join(", "));
  }
  const pluginMeta = trimmed.match(/^(目标|网络)：(.*)$/u);
  if (pluginMeta) return preserveOuterWhitespace(source, `${pluginMeta[1] === "目标" ? "Targets" : "Network"}: ${englishMessages[pluginMeta[2]] ?? pluginMeta[2]}`);
  const transferAction = trimmed.match(/^(复制|移动)\s*([←→])$/u);
  if (transferAction) return preserveOuterWhitespace(source, `${transferAction[1] === "复制" ? "Copy" : "Move"} ${transferAction[2]}`);
  const paneLabel = trimmed.match(/^(左侧|右侧)\s*·\s*(.+)$/u);
  if (paneLabel) return preserveOuterWhitespace(source, `${paneLabel[1] === "左侧" ? "Left" : "Right"} · ${paneLabel[2]}`);
  const filterLabel = trimmed.match(/^筛选\s+(.+)$/u);
  if (filterLabel) return preserveOuterWhitespace(source, `Filter ${filterLabel[1]}`);
  const availableSize = trimmed.match(/^([\d.,]+\s*[A-Za-z]+)\s+可用$/u);
  if (availableSize) return preserveOuterWhitespace(source, `${availableSize[1]} available`);
  const simpleCount = trimmed.match(/^(不同|仅左|仅右|相同)\s+(\d+)$/u);
  if (simpleCount) {
    const labels: Record<string, string> = { 不同: "Different", 仅左: "Left only", 仅右: "Right only", 相同: "Same" };
    return preserveOuterWhitespace(source, `${labels[simpleCount[1]]} ${simpleCount[2]}`);
  }
  const totalLines = trimmed.match(/^共\s*(\d+)\s*行$/u);
  if (totalLines) return preserveOuterWhitespace(source, `${totalLines[1]} lines total`);
  const lineNumberRange = trimmed.match(/^请输入\s*1\s*到\s*(\d+)\s*之间的行号$/u);
  if (lineNumberRange) return preserveOuterWhitespace(source, `Enter a line number from 1 to ${lineNumberRange[1]}`);
  const selectLine = trimmed.match(/^选择第\s*(\d+)\s*行(?:；拖动或 Shift\+单击选择多行)?$/u);
  if (selectLine) {
    const hint = trimmed.includes("拖动") ? "; drag or Shift-click to select multiple lines" : "";
    return preserveOuterWhitespace(source, `Select line ${selectLine[1]}${hint}`);
  }
  const characterCount = trimmed.match(/^([\d,.]+)\s*字符$/u);
  if (characterCount) return preserveOuterWhitespace(source, `${characterCount[1]} characters`);
  const digitCount = trimmed.match(/^(6|8)\s*位$/u);
  if (digitCount) return preserveOuterWhitespace(source, `${digitCount[1]} digits`);
  const date = trimmed.match(/^(\d{1,2})月(\d{1,2})日星期([一二三四五六日])$/u);
  if (date) {
    const weekdays: Record<string, string> = { 一: "Monday", 二: "Tuesday", 三: "Wednesday", 四: "Thursday", 五: "Friday", 六: "Saturday", 日: "Sunday" };
    const months = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
    return preserveOuterWhitespace(source, `${weekdays[date[3]]}, ${months[Number(date[1]) - 1]} ${Number(date[2])}`);
  }

  let translated = source;
  translated = translated
    .replace(/每行一个域名/gu, "One domain per line")
    .replace(/每行一个\s*IP/gu, "One IP address per line")
    .replace(/(\d+)\s*项/gu, "$1 items")
    .replace(/(\d+)\s*次/gu, "$1 times")
    .replace(/(\d+)\s*天/gu, "$1 days");
  for (const [chinese, english] of replacementMessages) {
    if (translated.includes(chinese)) translated = translated.split(chinese).join(english);
  }
  return translated;
}

function translateTextNode(node: Text) {
  const state = textState.get(node);
  if (appLocale.value === "zh-CN") {
    if (state && node.data === state.translated) node.data = state.source;
    textState.delete(node);
    return;
  }
  if (state && node.data === state.translated) return;
  const source = node.data;
  const translated = translateText(source);
  if (source === translated) return;
  textState.set(node, { source, translated });
  node.data = translated;
}

function translateElementAttributes(element: Element) {
  let states = attributeState.get(element);
  for (const name of translatedAttributes) {
    if (!element.hasAttribute(name)) continue;
    const value = element.getAttribute(name) ?? "";
    const state = states?.get(name);
    if (appLocale.value === "zh-CN") {
      if (state && value === state.translated) element.setAttribute(name, state.source);
      states?.delete(name);
      continue;
    }
    if (state && value === state.translated) continue;
    const translated = translateText(value);
    if (translated === value) continue;
    if (!states) {
      states = new Map();
      attributeState.set(element, states);
    }
    states.set(name, { source: value, translated });
    element.setAttribute(name, translated);
  }
}

function translateTree(root: Node) {
  if (root.nodeType === Node.TEXT_NODE) {
    translateTextNode(root as Text);
    return;
  }
  if (root.nodeType !== Node.ELEMENT_NODE && root.nodeType !== Node.DOCUMENT_FRAGMENT_NODE) return;
  if (root.nodeType === Node.ELEMENT_NODE) translateElementAttributes(root as Element);
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
  let current: Node | null;
  while ((current = walker.nextNode())) {
    if (current.nodeType === Node.TEXT_NODE) translateTextNode(current as Text);
    else translateElementAttributes(current as Element);
  }
}

function updateDocumentMetadata() {
  const english = appLocale.value === "en-US";
  document.documentElement.lang = appLocale.value;
  document.querySelector('meta[name="description"]')?.setAttribute(
    "content",
    english ? "Space Station lightweight utility bay" : "Space Station 轻量工具舱",
  );
  document.documentElement.style.setProperty(
    "--i18n-drop-to-pane",
    english ? '"Drop into this pane"' : '"拖放到此分屏"',
  );
}

let observer: MutationObserver | undefined;

export function installI18n() {
  updateDocumentMetadata();
  translateTree(document.body);
  observer?.disconnect();
  observer = new MutationObserver((mutations) => {
    for (const mutation of mutations) {
      if (mutation.type === "childList") mutation.addedNodes.forEach(translateTree);
      else if (mutation.type === "characterData") translateTextNode(mutation.target as Text);
      else if (mutation.type === "attributes") translateElementAttributes(mutation.target as Element);
    }
  });
  observer.observe(document.body, {
    attributes: true,
    attributeFilter: [...translatedAttributes],
    childList: true,
    characterData: true,
    subtree: true,
  });
}

export function setAppLocale(locale: AppLocale) {
  appLocale.value = locale;
}

export function toggleAppLocale() {
  setAppLocale(appLocale.value === "zh-CN" ? "en-US" : "zh-CN");
}

watch(appLocale, (locale) => {
  localStorage.setItem(STORAGE_KEY, locale);
  updateDocumentMetadata();
  translateTree(document.body);
});
