<template>
  <div class="formatter-grid">
    <n-card class="tool-panel" title="输入" embedded>
      <n-space vertical :size="16">
        <n-form label-placement="top">
          <div class="control-row">
            <n-form-item label="缩进空格">
              <n-input-number :value="indentSize" :min="2" :max="8" @update:value="setIndentSize" />
            </n-form-item>
            <n-form-item label="示例">
              <n-button tertiary @click="loadExample">载入 JSON</n-button>
            </n-form-item>
          </div>
          <n-form-item label="原始 JSON">
            <n-input
              class="json-textarea"
              type="textarea"
              :autosize="{ minRows: 16 }"
              :value="sourceInput"
              placeholder='粘贴 JSON，例如 {"name":"space-station","enabled":true}'
              @update:value="sourceInput = $event"
            />
          </n-form-item>
        </n-form>
        <div class="action-row">
          <n-button type="primary" @click="formatNow">格式化</n-button>
          <n-button tertiary @click="minifyNow">压缩</n-button>
          <n-button tertiary :disabled="!formattedOutput" @click="copyOutput">复制</n-button>
          <n-button quaternary @click="clearAll">清空</n-button>
        </div>
        <n-alert v-if="diagnostic" :type="diagnostic.type" :show-icon="false">
          {{ diagnostic.message }}
        </n-alert>
      </n-space>
    </n-card>

    <n-card class="tool-panel result-panel" title="结果" embedded>
      <div class="result-meta">
        <span>{{ stateLabel }}</span>
        <span v-if="outputSize">{{ outputSize }}</span>
      </div>
      <pre class="json-output">{{ formattedOutput || "格式化结果会显示在这里。" }}</pre>
    </n-card>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from "vue";
import {
  NAlert,
  NButton,
  NCard,
  NForm,
  NFormItem,
  NInput,
  NInputNumber,
  NSpace,
} from "naive-ui";
import { writeClipboard } from "@/utils/clipboard";

interface Diagnostic {
  type: "success" | "warning" | "error";
  message: string;
}

const indentSize = ref(2);
const sourceInput = ref("");
const formattedOutput = ref("");
const mode = ref<"pretty" | "minified">("pretty");
const copied = ref(false);

const parseResult = computed(() => parseJson(sourceInput.value, indentSize.value));

const diagnostic = computed<Diagnostic | null>(() => {
  if (copied.value) {
    return { type: "success", message: "结果已复制到剪贴板。" };
  }
  if (!sourceInput.value.trim()) {
    return null;
  }
  if (parseResult.value.ok) {
    return { type: "success", message: "JSON 语法正确。" };
  }
  return { type: "error", message: parseResult.value.message };
});

const stateLabel = computed(() => {
  if (!sourceInput.value.trim()) {
    return "等待输入";
  }
  return parseResult.value.ok ? (mode.value === "pretty" ? "已格式化" : "已压缩") : "格式错误";
});

const outputSize = computed(() => {
  if (!formattedOutput.value) {
    return "";
  }
  return `${formattedOutput.value.length.toLocaleString("zh-CN")} 字符`;
});

watch(sourceInput, () => {
  copied.value = false;
  if (sourceInput.value.trim() && parseResult.value.ok && formattedOutput.value) {
    applyCurrentMode();
  }
});

function setIndentSize(value: number | null) {
  if (!value) {
    return;
  }
  indentSize.value = value;
  if (mode.value === "pretty" && parseResult.value.ok) {
    formattedOutput.value = parseResult.value.formatted;
  }
}

function formatNow() {
  copied.value = false;
  mode.value = "pretty";
  applyCurrentMode();
}

function minifyNow() {
  copied.value = false;
  mode.value = "minified";
  applyCurrentMode();
}

function applyCurrentMode() {
  if (!sourceInput.value.trim() || !parseResult.value.ok) {
    formattedOutput.value = "";
    return;
  }
  formattedOutput.value = mode.value === "pretty" ? parseResult.value.formatted : parseResult.value.minified;
}

async function copyOutput() {
  if (!formattedOutput.value) {
    return;
  }
  copied.value = await writeClipboard(formattedOutput.value);
  window.setTimeout(() => {
    copied.value = false;
  }, 1500);
}

function clearAll() {
  sourceInput.value = "";
  formattedOutput.value = "";
  copied.value = false;
}

function loadExample() {
  sourceInput.value = JSON.stringify({
    service: "space-station",
    enabled: true,
    features: ["json-format"],
    runtime: {
      frontend: "vite + vue",
      backend: "drogon",
    },
  });
  formatNow();
}

function parseJson(input: string, spaces: number):
  | { ok: true; formatted: string; minified: string }
  | { ok: false; message: string } {
  const trimmed = input.trim();
  if (!trimmed) {
    return { ok: false, message: "请输入 JSON 内容。" };
  }

  try {
    const parsed = JSON.parse(trimmed);
    return {
      ok: true,
      formatted: JSON.stringify(parsed, null, spaces),
      minified: JSON.stringify(parsed),
    };
  } catch (error) {
    const message = error instanceof Error ? error.message : "JSON 解析失败。";
    const match = message.match(/position\s+(\d+)/i);
    if (!match) {
      return { ok: false, message };
    }
    const position = Number(match[1]);
    const { line, column } = getLineColumn(trimmed, position);
    return { ok: false, message: `${message} 第 ${line} 行，第 ${column} 列。` };
  }
}

function getLineColumn(text: string, index: number) {
  const before = text.slice(0, index);
  const lines = before.split("\n");
  return {
    line: lines.length,
    column: lines[lines.length - 1].length + 1,
  };
}
</script>
