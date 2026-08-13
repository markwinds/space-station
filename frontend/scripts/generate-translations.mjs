import { readdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import ts from "typescript";
import { baseParse, NodeTypes } from "@vue/compiler-dom";
import { parse as parseSfc } from "@vue/compiler-sfc";

const frontendRoot = path.resolve(import.meta.dirname, "..");
const repositoryRoot = path.resolve(frontendRoot, "..");
const outputPath = path.join(frontendRoot, "src/i18n/english.generated.ts");
const hasHan = /\p{Script=Han}/u;
const phrases = new Set([
  "中文",
  "英文",
  "切换语言",
  "轻量工具舱",
  "拖放到此分屏",
]);

async function walk(directory, extensions) {
  const result = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    if (["node_modules", "dist", "out", ".git"].includes(entry.name)) continue;
    const fullPath = path.join(directory, entry.name);
    if (entry.isDirectory()) result.push(...await walk(fullPath, extensions));
    else if (extensions.some((extension) => entry.name.endsWith(extension))) result.push(fullPath);
  }
  return result;
}

function addPhrase(value) {
  const normalized = value.replace(/\s+/g, " ").trim();
  if (!normalized || !hasHan.test(normalized) || normalized.length > 500) return;
  phrases.add(normalized);
}

function collectTypeScript(source, filename) {
  const sourceFile = ts.createSourceFile(filename, source, ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
  const visit = (node) => {
    if (ts.isStringLiteralLike(node) || ts.isNoSubstitutionTemplateLiteral(node)) addPhrase(node.text);
    else if (ts.isTemplateHead(node) || ts.isTemplateMiddle(node) || ts.isTemplateTail(node)) addPhrase(node.text);
    ts.forEachChild(node, visit);
  };
  visit(sourceFile);
}

function collectTemplate(source, filename) {
  let root;
  try {
    root = baseParse(source, {
      comments: false,
      isVoidTag: (tag) => ["area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"].includes(tag),
    });
  } catch (error) {
    throw new Error(`Unable to parse template in ${filename}: ${error.message}`, { cause: error });
  }
  const visit = (node) => {
    if (node.type === NodeTypes.TEXT) addPhrase(node.content);
    if (node.type === NodeTypes.INTERPOLATION) collectTypeScript(node.content.content, filename);
    if (node.type === NodeTypes.ELEMENT) {
      for (const property of node.props) {
        if (property.type === NodeTypes.ATTRIBUTE && property.value) addPhrase(property.value.content);
        if (property.type === NodeTypes.DIRECTIVE && property.exp) collectTypeScript(property.exp.content, filename);
      }
      node.children.forEach(visit);
    } else if (node.type === NodeTypes.ROOT) {
      node.children.forEach(visit);
    } else if (node.type === NodeTypes.IF) {
      node.branches.forEach((branch) => branch.children.forEach(visit));
    } else if (node.type === NodeTypes.FOR) {
      node.children.forEach(visit);
    }
  };
  visit(root);
}

function collectCpp(source) {
  for (const match of source.matchAll(/"((?:\\.|[^"\\])*)"/gs)) {
    const raw = match[1];
    if (!hasHan.test(raw)) continue;
    addPhrase(raw.replace(/\\[rnt]/g, " ").replace(/\\x[0-9a-fA-F]{2}/g, ""));
  }
}

async function translate(source, attempt = 0) {
  const url = new URL("https://translate.googleapis.com/translate_a/single");
  Object.entries({ client: "gtx", sl: "zh-CN", tl: "en", dt: "t", q: source }).forEach(([key, value]) => url.searchParams.set(key, value));
  try {
    const response = await fetch(url, { signal: AbortSignal.timeout(20_000) });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    return data[0].map((part) => part[0]).join("").trim();
  } catch (error) {
    if (attempt >= 4) throw error;
    await new Promise((resolve) => setTimeout(resolve, 500 * (2 ** attempt)));
    return translate(source, attempt + 1);
  }
}

const vueFiles = await walk(path.join(frontendRoot, "src"), [".vue"]);
for (const filename of vueFiles) {
  const source = await readFile(filename, "utf8");
  const { descriptor } = parseSfc(source, { filename });
  if (descriptor.template) collectTemplate(descriptor.template.content, filename);
  if (descriptor.script) collectTypeScript(descriptor.script.content, filename);
  if (descriptor.scriptSetup) collectTypeScript(descriptor.scriptSetup.content, filename);
  for (const style of descriptor.styles) {
    for (const match of style.content.matchAll(/content\s*:\s*["']([^"']*\p{Script=Han}[^"']*)["']/gu)) addPhrase(match[1]);
  }
}

for (const filename of await walk(path.join(frontendRoot, "src"), [".ts"])) {
  collectTypeScript(await readFile(filename, "utf8"), filename);
}

for (const filename of await walk(path.join(repositoryRoot, "backend/src"), [".cpp", ".hpp"])) {
  collectCpp(await readFile(filename, "utf8"));
}

const sources = [...phrases].sort((left, right) => left.localeCompare(right, "zh-CN"));
const translations = new Map();
let cursor = 0;
const workers = Array.from({ length: 8 }, async () => {
  while (cursor < sources.length) {
    const index = cursor++;
    const source = sources[index];
    translations.set(source, await translate(source));
    if ((index + 1) % 100 === 0) process.stdout.write(`Translated ${index + 1}/${sources.length}\n`);
  }
});
await Promise.all(workers);

const overrides = {
  "中文": "中文",
  "英文": "English",
  "切换语言": "Switch language",
  "轻量工具舱": "Lightweight Utility Bay",
  "拖放到此分屏": "Drop into this pane",
  "文件管理器": "File Manager",
  "浏览器截屏": "Browser Screenshot",
  "文件对比": "File Comparison",
  "JSON 格式化": "JSON Formatter",
  "证书助手": "Certificate Assistant",
  "认证器": "Authenticator",
  "时间管理器": "Time Manager",
  "习惯养成": "Habits",
  "差分传输": "Differential Transfer",
  "SSH 终端": "SSH Terminal",
  "串口终端": "Serial Terminal",
  "终端插件": "Terminal Plugins",
  "运行配置": "Runtime Settings",
  "捕获页面并裁剪、打码和添加常用标注": "Capture pages, then crop, redact, and annotate them",
  "经典": "Classic",
  "简洁耐看的经典终端配色": "A clean, timeless terminal color scheme",
  "请先再打开一个终端标签，再使用分屏": "Open another terminal tab to use split view",
};
for (const [source, translation] of Object.entries(overrides)) translations.set(source, translation);

const entries = sources.map((source) => [source, translations.get(source)]);
const generated = `// Generated by scripts/generate-translations.mjs.\n// Keep product-language overrides in that script so this file stays reproducible.\nexport const englishMessages: Record<string, string> = ${JSON.stringify(Object.fromEntries(entries), null, 2)};\n`;
await writeFile(outputPath, generated, "utf8");
process.stdout.write(`Wrote ${entries.length} translations to ${path.relative(repositoryRoot, outputPath)}\n`);
