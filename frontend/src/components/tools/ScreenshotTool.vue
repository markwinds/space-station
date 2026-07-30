<template>
  <div class="shot-app" :class="{ 'capture-ui-hidden': captureUiHidden }">
    <header class="shot-toolbar">
      <div class="shot-brand">
        <router-link class="shot-home" to="/" aria-label="返回首页">SS</router-link>
        <div>
          <strong>浏览器截屏</strong>
          <small>{{ documentStatus }}</small>
        </div>
      </div>

      <div class="shot-actions shot-source-actions">
        <button class="primary-action" type="button" :disabled="capturing" @click="captureScreen">
          <n-icon><CameraOutline /></n-icon>
          {{ capturing ? "正在截取…" : "截取浏览器" }}
        </button>
        <button type="button" :disabled="capturing" @click="fileInput?.click()">
          <n-icon><CloudUploadOutline /></n-icon>
          导入图片
        </button>
        <label class="capture-option" title="取帧前隐藏本工具界面">
          <input v-model="hideUiDuringCapture" type="checkbox" :disabled="capturing" @change="saveCapturePreferences" />
          <n-icon><EyeOffOutline /></n-icon>
          <span>隐藏界面</span>
        </label>
        <label class="capture-delay" title="选择捕获目标后的等待时间">
          <n-icon><TimerOutline /></n-icon>
          <select v-model.number="captureDelaySeconds" :disabled="capturing" aria-label="截图倒计时" @change="saveCapturePreferences">
            <option v-for="option in captureDelayOptions" :key="option.value" :value="option.value">
              {{ option.label }}
            </option>
          </select>
        </label>
        <input ref="fileInput" class="hidden-input" type="file" accept="image/*" @change="loadFile" />
      </div>

      <div v-if="hasImage" class="shot-toolset" aria-label="编辑工具">
        <button
          v-for="item in editorTools"
          :key="item.id"
          type="button"
          :class="{ active: activeTool === item.id }"
          :title="item.hint"
          @click="activateTool(item.id)"
        >
          <n-icon><component :is="item.icon" /></n-icon>
          <span>{{ item.label }}</span>
          <kbd>{{ item.key }}</kbd>
        </button>
      </div>

      <div v-if="hasImage" class="shot-actions shot-history">
        <button type="button" :disabled="!canUndo" title="撤销 (⌘/Ctrl+Z)" @click="undo">
          <n-icon><ArrowUndoOutline /></n-icon>
        </button>
        <button type="button" :disabled="!canRedo" title="重做 (⌘/Ctrl+Shift+Z)" @click="redo">
          <n-icon><ArrowRedoOutline /></n-icon>
        </button>
        <button type="button" :disabled="!selectedAnnotation" title="删除所选 (Delete)" @click="deleteSelected">
          <n-icon><TrashOutline /></n-icon>
        </button>
      </div>

      <div v-if="hasImage" class="shot-actions shot-export-actions">
        <button type="button" @click="copyImage">
          <n-icon><CopyOutline /></n-icon>
          复制
        </button>
        <button class="primary-action" type="button" @click="downloadImage">
          <n-icon><DownloadOutline /></n-icon>
          下载 PNG
        </button>
      </div>
    </header>

    <div v-if="captureCountdown !== null" class="capture-countdown" role="status">
      <strong>{{ captureCountdown }}</strong>
      <span>秒后截取，请切换到目标窗口</span>
    </div>

    <main class="shot-body">
      <section v-if="!hasImage" class="shot-empty">
        <div class="empty-illustration" aria-hidden="true">
          <n-icon size="52"><ScanOutline /></n-icon>
        </div>
        <h2>截取浏览器内容后直接标注</h2>
        <p>系统会让你选择一个标签页、窗口或屏幕。截图只在当前浏览器中处理，不会上传到服务器。</p>
        <div class="empty-actions">
          <button class="primary-action large" type="button" :disabled="capturing" @click="captureScreen">
            <n-icon><CameraOutline /></n-icon>
            {{ capturing ? "等待浏览器授权…" : "开始截屏" }}
          </button>
          <button class="large" type="button" @click="fileInput?.click()">打开已有图片</button>
        </div>
        <small>提示：截网页时直接选择目标标签页；截整个屏幕时可配置倒计时，并按需隐藏当前界面。</small>
      </section>

      <template v-else>
        <section ref="viewport" class="shot-viewport">
          <div class="canvas-space">
            <canvas
              ref="canvas"
              class="shot-canvas"
              :class="`tool-${activeTool}`"
              :style="canvasStyle"
              :width="imageWidth"
              :height="imageHeight"
              aria-label="截图编辑画布"
              @pointerdown="onPointerDown"
              @pointermove="onPointerMove"
              @pointerup="onPointerUp"
              @pointercancel="onPointerUp"
              @pointerleave="onPointerLeave"
              @dblclick="onDoubleClick"
            />
            <div
              v-if="activeTool === 'eyedropper' && sampledColor && colorTooltipVisible"
              class="color-tooltip"
              :style="colorTooltipStyle"
            >
              <canvas ref="colorMagnifier" class="color-magnifier" width="132" height="132" />
              <div class="color-tooltip-meta">
                <span class="color-tooltip-swatch" :style="{ backgroundColor: sampledColor.hex }" />
                <strong>{{ sampledColor.hex }}</strong>
                <small>{{ sampledPosition.x }}, {{ sampledPosition.y }}</small>
              </div>
            </div>
          </div>

          <div class="zoom-control" aria-label="缩放控制">
            <button type="button" title="缩小" @click="changeZoom(-0.1)">−</button>
            <button type="button" title="适合窗口" @click="fitToViewport">{{ Math.round(zoom * 100) }}%</button>
            <button type="button" title="放大" @click="changeZoom(0.1)">＋</button>
          </div>

          <div v-if="activeTool === 'crop' && cropDraft" class="crop-actions">
            <span>{{ cropSizeLabel }}</span>
            <button type="button" @click="cancelCrop">取消</button>
            <button class="primary-action" type="button" :disabled="!canApplyCrop" @click="applyCrop">应用裁剪</button>
          </div>
        </section>

        <aside class="shot-inspector">
          <div class="inspector-heading">
            <div>
              <small>PROPERTIES</small>
              <strong>{{ inspectorTitle }}</strong>
            </div>
            <span v-if="selectedAnnotation" class="type-chip">{{ annotationTypeLabel(selectedAnnotation.type) }}</span>
          </div>

          <template v-if="selectedAnnotation?.type === 'text'">
            <label class="field-label" for="shot-text-editor">文字内容</label>
            <textarea
              id="shot-text-editor"
              ref="textEditor"
              :value="selectedAnnotation.text"
              rows="5"
              placeholder="输入标注文字"
              @input="updateSelectedText"
              @change="commitHistory"
            />
            <div class="field-grid">
              <label>
                <span>字号</span>
                <input
                  :value="selectedAnnotation.fontSize"
                  type="number"
                  min="12"
                  max="160"
                  @change="updateTextFontSize"
                />
              </label>
              <label>
                <span>颜色</span>
                <input
                  :value="selectedAnnotation.color"
                  type="color"
                  @input="updateSelectedColor"
                  @change="commitHistory"
                />
              </label>
            </div>
          </template>

          <template v-else-if="selectedAnnotation?.type === 'rect' || selectedAnnotation?.type === 'arrow' || selectedAnnotation?.type === 'brush'">
            <div class="field-grid">
              <label>
                <span>线宽</span>
                <input
                  :value="selectedAnnotation.lineWidth"
                  type="number"
                  min="1"
                  max="40"
                  @change="updateStrokeLineWidth"
                />
              </label>
              <label>
                <span>颜色</span>
                <input
                  :value="selectedAnnotation.color"
                  type="color"
                  @input="updateSelectedColor"
                  @change="commitHistory"
                />
              </label>
            </div>
            <p class="inspector-help">拖动标注可以移动；矩形和箭头可通过控制点调整大小。</p>
          </template>

          <template v-else-if="selectedAnnotation?.type === 'mosaic'">
            <label class="field-label">
              马赛克颗粒
              <input
                :value="selectedAnnotation.blockSize"
                type="number"
                min="4"
                max="80"
                @change="updateMosaicBlockSize"
              />
            </label>
            <p class="inspector-help">数值越大，遮挡颗粒越粗。马赛克区域仍可移动和缩放。</p>
          </template>

          <template v-else-if="activeTool === 'eyedropper'">
            <div v-if="sampledColor" class="color-inspector">
              <div class="color-preview" :style="{ backgroundColor: sampledColor.hex }" />
              <div class="color-value-row">
                <span>HEX</span>
                <strong>{{ sampledColor.hex }}</strong>
              </div>
              <div class="color-value-row">
                <span>RGB</span>
                <strong>{{ sampledColor.rgb }}</strong>
              </div>
              <div class="color-value-row">
                <span>HSL</span>
                <strong>{{ sampledColor.hsl }}</strong>
              </div>
              <button type="button" @click="copySampledColor">复制 {{ sampledColor.hex }}</button>
            </div>
            <div v-else class="tool-tip-card">
              <n-icon size="24"><EyedropOutline /></n-icon>
              <strong>实时取色</strong>
              <p>将鼠标移到截图上查看颜色值，点击即可复制 HEX。</p>
            </div>
          </template>

          <template v-else-if="activeTool === 'crop'">
            <div class="tool-tip-card">
              <n-icon size="24"><CropOutline /></n-icon>
              <strong>框选保留区域</strong>
              <p>在图片上拖出裁剪区域，然后点击“应用裁剪”或按 Enter。</p>
            </div>
          </template>

          <template v-else>
            <div class="tool-tip-card">
              <n-icon size="24"><component :is="activeToolIcon" /></n-icon>
              <strong>{{ activeToolLabel }}</strong>
              <p>{{ activeToolHelp }}</p>
            </div>
          </template>

          <div class="inspector-footer">
            <div><span>画布</span><strong>{{ imageWidth }} × {{ imageHeight }}</strong></div>
            <div><span>标注</span><strong>{{ annotations.length }}</strong></div>
          </div>
        </aside>
      </template>
    </main>
  </div>
</template>

<script setup lang="ts">
import {
  ArrowForwardOutline,
  ArrowRedoOutline,
  ArrowUndoOutline,
  BrushOutline,
  CameraOutline,
  CloudUploadOutline,
  CopyOutline,
  CropOutline,
  DownloadOutline,
  EyeOffOutline,
  EyedropOutline,
  GridOutline,
  LocateOutline,
  ScanOutline,
  SquareOutline,
  TextOutline,
  TimerOutline,
  TrashOutline,
} from "@vicons/ionicons5";
import { NIcon, useMessage } from "naive-ui";
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from "vue";
import { writeClipboard } from "@/utils/clipboard";

type EditorTool = "select" | "eyedropper" | "crop" | "mosaic" | "rect" | "arrow" | "brush" | "text";
type AnnotationType = "rect" | "text" | "mosaic" | "arrow" | "brush";

interface Point {
  x: number;
  y: number;
}

interface Bounds {
  x: number;
  y: number;
  width: number;
  height: number;
}

interface SampledColor {
  hex: string;
  rgb: string;
  hsl: string;
  red: number;
  green: number;
  blue: number;
  alpha: number;
}

interface RectangleAnnotation extends Bounds {
  id: string;
  type: "rect";
  color: string;
  lineWidth: number;
}

interface TextAnnotation extends Bounds {
  id: string;
  type: "text";
  color: string;
  fontSize: number;
  text: string;
}

interface MosaicAnnotation extends Bounds {
  id: string;
  type: "mosaic";
  blockSize: number;
}

interface ArrowAnnotation extends Bounds {
  id: string;
  type: "arrow";
  color: string;
  lineWidth: number;
  start: Point;
  end: Point;
}

interface BrushAnnotation extends Bounds {
  id: string;
  type: "brush";
  color: string;
  lineWidth: number;
  points: Point[];
}

type Annotation = RectangleAnnotation | TextAnnotation | MosaicAnnotation | ArrowAnnotation | BrushAnnotation;

interface EditorSnapshot {
  imageUrl: string;
  imageWidth: number;
  imageHeight: number;
  annotations: Annotation[];
}

interface DragInteraction {
  kind: "draw-rect" | "draw-mosaic" | "draw-arrow" | "draw-brush" | "move" | "resize";
  start: Point;
  annotationId: string;
  original: Annotation;
  changed: boolean;
}

const editorTools = [
  { id: "select" as const, label: "选择", key: "V", hint: "选择、移动或调整标注", icon: LocateOutline },
  { id: "eyedropper" as const, label: "取色", key: "I", hint: "实时读取鼠标位置的颜色", icon: EyedropOutline },
  { id: "crop" as const, label: "裁剪", key: "C", hint: "框选要保留的区域", icon: CropOutline },
  { id: "mosaic" as const, label: "马赛克", key: "M", hint: "遮挡敏感内容", icon: GridOutline },
  { id: "rect" as const, label: "框选", key: "R", hint: "绘制矩形标注", icon: SquareOutline },
  { id: "arrow" as const, label: "箭头", key: "A", hint: "绘制指向箭头", icon: ArrowForwardOutline },
  { id: "brush" as const, label: "画笔", key: "B", hint: "自由绘制标记", icon: BrushOutline },
  { id: "text" as const, label: "文字", key: "T", hint: "添加并编辑文字", icon: TextOutline },
];
const captureDelayOptions = [
  { value: 0, label: "立即" },
  { value: 1, label: "1 秒" },
  { value: 3, label: "3 秒" },
  { value: 5, label: "5 秒" },
  { value: 10, label: "10 秒" },
];
const capturePreferencesKey = "space-station:screenshot-capture-preferences";

const message = useMessage();
const canvas = ref<HTMLCanvasElement | null>(null);
const viewport = ref<HTMLElement | null>(null);
const fileInput = ref<HTMLInputElement | null>(null);
const textEditor = ref<HTMLTextAreaElement | null>(null);
const colorMagnifier = ref<HTMLCanvasElement | null>(null);
const activeTool = ref<EditorTool>("select");
const capturing = ref(false);
const hideUiDuringCapture = ref(true);
const captureDelaySeconds = ref(3);
const captureUiHidden = ref(false);
const captureCountdown = ref<number | null>(null);
const imageUrl = ref("");
const imageWidth = ref(0);
const imageHeight = ref(0);
const annotations = ref<Annotation[]>([]);
const selectedId = ref<string | null>(null);
const cropDraft = ref<Bounds | null>(null);
const cropStart = ref<Point | null>(null);
const zoom = ref(1);
const history = ref<EditorSnapshot[]>([]);
const historyIndex = ref(-1);
const sampledColor = ref<SampledColor | null>(null);
const colorTooltipVisible = ref(false);
const colorTooltipPosition = ref({ x: 0, y: 0 });
const sampledPosition = ref({ x: 0, y: 0 });

let sourceImage: HTMLImageElement | null = null;
let interaction: DragInteraction | null = null;
let resizeObserver: ResizeObserver | null = null;
let nextAnnotationId = 1;
let textEditDirty = false;

const hasImage = computed(() => Boolean(imageUrl.value && imageWidth.value && imageHeight.value));
const selectedAnnotation = computed(() => annotations.value.find((item) => item.id === selectedId.value) ?? null);
const canUndo = computed(() => historyIndex.value > 0);
const canRedo = computed(() => historyIndex.value >= 0 && historyIndex.value < history.value.length - 1);
const canApplyCrop = computed(() => Boolean(cropDraft.value && cropDraft.value.width >= 2 && cropDraft.value.height >= 2));
const cropSizeLabel = computed(() => {
  const crop = cropDraft.value;
  return crop ? `${Math.round(crop.width)} × ${Math.round(crop.height)}` : "";
});
const canvasStyle = computed(() => ({
  width: `${Math.max(1, imageWidth.value * zoom.value)}px`,
  height: `${Math.max(1, imageHeight.value * zoom.value)}px`,
}));
const colorTooltipStyle = computed(() => ({
  left: `${colorTooltipPosition.value.x}px`,
  top: `${colorTooltipPosition.value.y}px`,
}));
const documentStatus = computed(() => hasImage.value
  ? `${imageWidth.value} × ${imageHeight.value} · ${annotations.value.length} 个标注`
  : "本地截图与标注");
const inspectorTitle = computed(() => {
  if (selectedAnnotation.value) return "编辑所选标注";
  return activeTool.value === "eyedropper" ? "实时取色" : "工具设置";
});
const activeToolDefinition = computed(() => editorTools.find((item) => item.id === activeTool.value) ?? editorTools[0]);
const activeToolIcon = computed(() => activeToolDefinition.value.icon);
const activeToolLabel = computed(() => activeToolDefinition.value.label);
const activeToolHelp = computed(() => {
  if (activeTool.value === "select") return "点击标注进行选择，拖动可移动；按 Delete 删除。";
  if (activeTool.value === "eyedropper") return "移动鼠标实时读取颜色，点击画布复制当前 HEX。";
  if (activeTool.value === "mosaic") return "拖动框选需要遮挡的敏感区域，可在属性栏调整颗粒大小。";
  if (activeTool.value === "rect") return "在图片上拖动，画出醒目的矩形标注。";
  if (activeTool.value === "arrow") return "从起点拖向目标位置，绘制一个指向箭头。";
  if (activeTool.value === "brush") return "按住并拖动，自由绘制重点或圈画内容。";
  return "点击图片放置文字，然后在这里修改内容、字号和颜色。";
});

onMounted(() => {
  loadCapturePreferences();
  window.addEventListener("keydown", onKeyDown);
  resizeObserver = new ResizeObserver(() => {
    if (hasImage.value && zoom.value === 1) fitToViewport();
  });
  if (viewport.value) resizeObserver.observe(viewport.value);
});

onBeforeUnmount(() => {
  window.removeEventListener("keydown", onKeyDown);
  resizeObserver?.disconnect();
});

async function captureScreen() {
  if (!navigator.mediaDevices?.getDisplayMedia) {
    message.error("当前浏览器不支持屏幕捕获，请改用“导入图片”。");
    return;
  }

  capturing.value = true;
  let stream: MediaStream | null = null;
  try {
    stream = await navigator.mediaDevices.getDisplayMedia({
      video: { frameRate: { ideal: 1, max: 5 } },
      audio: false,
    });
    const video = document.createElement("video");
    video.srcObject = stream;
    video.muted = true;
    video.playsInline = true;
    await video.play();
    await waitForVideoFrame(video);

    if (captureDelaySeconds.value > 0 || hideUiDuringCapture.value) await prepareCapture();

    const output = document.createElement("canvas");
    output.width = video.videoWidth;
    output.height = video.videoHeight;
    output.getContext("2d")?.drawImage(video, 0, 0);
    captureUiHidden.value = false;
    await replaceImage(output.toDataURL("image/png"));
    message.success("截图已载入，可以开始框选和标注。");
  } catch (error) {
    if ((error as DOMException)?.name !== "NotAllowedError") {
      message.error(error instanceof Error ? error.message : "截屏失败。");
    }
  } finally {
    captureCountdown.value = null;
    captureUiHidden.value = false;
    stream?.getTracks().forEach((track) => track.stop());
    capturing.value = false;
  }
}

async function prepareCapture() {
  for (let remaining = captureDelaySeconds.value; remaining >= 1; remaining -= 1) {
    captureCountdown.value = remaining;
    await delay(1000);
  }
  captureCountdown.value = null;
  if (hideUiDuringCapture.value) {
    captureUiHidden.value = true;
    await nextTick();
    await new Promise<void>((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => resolve())));
    await delay(160);
  }
}

function loadCapturePreferences() {
  try {
    const stored = JSON.parse(localStorage.getItem(capturePreferencesKey) ?? "null") as {
      delaySeconds?: unknown;
      hideUi?: unknown;
    } | null;
    if (typeof stored?.hideUi === "boolean") hideUiDuringCapture.value = stored.hideUi;
    if (typeof stored?.delaySeconds === "number" && captureDelayOptions.some((option) => option.value === stored.delaySeconds)) {
      captureDelaySeconds.value = stored.delaySeconds;
    }
  } catch {
    // Keep defaults when storage is unavailable or contains invalid data.
  }
}

function saveCapturePreferences() {
  try {
    localStorage.setItem(capturePreferencesKey, JSON.stringify({
      delaySeconds: captureDelaySeconds.value,
      hideUi: hideUiDuringCapture.value,
    }));
  } catch {
    // The capture flow still works when storage is unavailable.
  }
}

function delay(milliseconds: number) {
  return new Promise<void>((resolve) => window.setTimeout(resolve, milliseconds));
}

function waitForVideoFrame(video: HTMLVideoElement) {
  return new Promise<void>((resolve, reject) => {
    const finish = () => {
      if (!video.videoWidth || !video.videoHeight) {
        reject(new Error("没有从所选页面读取到画面。"));
        return;
      }
      requestAnimationFrame(() => requestAnimationFrame(() => resolve()));
    };
    if (video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA) finish();
    else {
      video.addEventListener("loadeddata", finish, { once: true });
      video.addEventListener("error", () => reject(new Error("无法读取所选页面的画面。")), { once: true });
    }
  });
}

async function loadFile(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  input.value = "";
  if (!file) return;
  if (!file.type.startsWith("image/")) {
    message.warning("请选择图片文件。");
    return;
  }
  try {
    await replaceImage(await readAsDataUrl(file));
    message.success(`已打开 ${file.name}`);
  } catch (error) {
    message.error(error instanceof Error ? error.message : "图片读取失败。");
  }
}

function readAsDataUrl(file: Blob) {
  return new Promise<string>((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result));
    reader.onerror = () => reject(new Error("图片读取失败。"));
    reader.readAsDataURL(file);
  });
}

async function replaceImage(url: string, reset = true) {
  sourceImage = await loadImage(url);
  imageUrl.value = url;
  imageWidth.value = sourceImage.naturalWidth;
  imageHeight.value = sourceImage.naturalHeight;
  if (reset) {
    annotations.value = [];
    selectedId.value = null;
    cropDraft.value = null;
    activeTool.value = "select";
    history.value = [];
    historyIndex.value = -1;
    commitHistory();
  }
  sampledColor.value = null;
  colorTooltipVisible.value = false;
  await nextTick();
  fitToViewport();
  renderEditor();
}

function loadImage(url: string) {
  return new Promise<HTMLImageElement>((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error("无法解析图片。"));
    image.src = url;
  });
}

function activateTool(tool: EditorTool) {
  flushTextHistory();
  activeTool.value = tool;
  if (tool !== "crop") cropDraft.value = null;
  if (tool !== "select") selectedId.value = null;
  if (tool !== "eyedropper") colorTooltipVisible.value = false;
  renderEditor();
}

function onPointerDown(event: PointerEvent) {
  if (!hasImage.value || !canvas.value) return;
  const point = canvasPoint(event);
  canvas.value.setPointerCapture(event.pointerId);

  if (activeTool.value === "eyedropper") {
    sampleCanvasColor(point, event);
    void copySampledColor();
    return;
  }

  if (activeTool.value === "crop") {
    selectedId.value = null;
    cropStart.value = point;
    cropDraft.value = { x: point.x, y: point.y, width: 0, height: 0 };
    renderEditor();
    return;
  }

  if (activeTool.value === "rect" || activeTool.value === "mosaic") {
    const annotation: RectangleAnnotation | MosaicAnnotation = activeTool.value === "rect"
      ? {
          id: createId(),
          type: "rect",
          x: point.x,
          y: point.y,
          width: 0,
          height: 0,
          color: "#ff3b30",
          lineWidth: Math.max(3, Math.round(imageWidth.value / 640)),
        }
      : {
          id: createId(),
          type: "mosaic",
          x: point.x,
          y: point.y,
          width: 0,
          height: 0,
          blockSize: Math.max(8, Math.round(imageWidth.value / 80)),
        };
    annotations.value.push(annotation);
    selectedId.value = annotation.id;
    interaction = {
      kind: annotation.type === "rect" ? "draw-rect" : "draw-mosaic",
      start: point,
      annotationId: annotation.id,
      original: cloneAnnotation(annotation),
      changed: false,
    };
    renderEditor();
    return;
  }

  if (activeTool.value === "arrow") {
    const annotation: ArrowAnnotation = {
      id: createId(),
      type: "arrow",
      x: point.x,
      y: point.y,
      width: 0,
      height: 0,
      color: "#ff3b30",
      lineWidth: Math.max(4, Math.round(imageWidth.value / 520)),
      start: { ...point },
      end: { ...point },
    };
    annotations.value.push(annotation);
    selectedId.value = annotation.id;
    interaction = {
      kind: "draw-arrow",
      start: point,
      annotationId: annotation.id,
      original: cloneAnnotation(annotation),
      changed: false,
    };
    renderEditor();
    return;
  }

  if (activeTool.value === "brush") {
    const annotation: BrushAnnotation = {
      id: createId(),
      type: "brush",
      x: point.x,
      y: point.y,
      width: 1,
      height: 1,
      color: "#ff3b30",
      lineWidth: Math.max(4, Math.round(imageWidth.value / 480)),
      points: [{ ...point }],
    };
    annotations.value.push(annotation);
    selectedId.value = annotation.id;
    interaction = {
      kind: "draw-brush",
      start: point,
      annotationId: annotation.id,
      original: cloneAnnotation(annotation),
      changed: false,
    };
    renderEditor();
    return;
  }

  if (activeTool.value === "text") {
    const fontSize = Math.max(24, Math.min(54, Math.round(imageWidth.value / 28)));
    const annotation: TextAnnotation = {
      id: createId(),
      type: "text",
      x: point.x,
      y: point.y,
      width: fontSize * 6,
      height: fontSize * 1.35,
      color: "#ff3b30",
      fontSize,
      text: "输入文字",
    };
    updateTextBounds(annotation);
    annotations.value.push(annotation);
    selectedId.value = annotation.id;
    activeTool.value = "select";
    commitHistory();
    renderEditor();
    nextTick(() => {
      textEditor.value?.focus();
      textEditor.value?.select();
    });
    return;
  }

  const selected = selectedAnnotation.value;
  if (selected && isOnResizeHandle(point, selected)) {
    interaction = {
      kind: "resize",
      start: point,
      annotationId: selected.id,
      original: cloneAnnotation(selected),
      changed: false,
    };
    return;
  }

  const hit = hitTest(point);
  selectedId.value = hit?.id ?? null;
  if (hit) {
    interaction = {
      kind: "move",
      start: point,
      annotationId: hit.id,
      original: cloneAnnotation(hit),
      changed: false,
    };
  }
  renderEditor();
}

function onPointerMove(event: PointerEvent) {
  if (!canvas.value || !hasImage.value) return;
  const point = canvasPoint(event);

  if (activeTool.value === "eyedropper") {
    sampleCanvasColor(point, event);
    return;
  }

  if (activeTool.value === "crop" && cropStart.value) {
    cropDraft.value = normalizeBounds(cropStart.value, point);
    renderEditor();
    return;
  }

  if (!interaction) return;
  const annotation = annotations.value.find((item) => item.id === interaction?.annotationId);
  if (!annotation) return;

  if (interaction.kind === "draw-rect" || interaction.kind === "draw-mosaic") {
    Object.assign(annotation, normalizeBounds(interaction.start, point));
  } else if (interaction.kind === "draw-arrow" && annotation.type === "arrow") {
    annotation.end = { ...point };
    updateArrowBounds(annotation);
  } else if (interaction.kind === "draw-brush" && annotation.type === "brush") {
    annotation.points.push({ ...point });
    updateBrushBounds(annotation);
  } else if (interaction.kind === "move") {
    moveAnnotation(annotation, interaction.original, point.x - interaction.start.x, point.y - interaction.start.y);
  } else {
    if (annotation.type === "arrow") {
      annotation.end = { ...point };
      updateArrowBounds(annotation);
    } else {
      annotation.width = clamp(interaction.original.width + point.x - interaction.start.x, 8, imageWidth.value - annotation.x);
      annotation.height = clamp(interaction.original.height + point.y - interaction.start.y, 8, imageHeight.value - annotation.y);
    }
    if (annotation.type === "text") {
      annotation.fontSize = clamp(Math.round(annotation.height / 1.35), 12, 160);
      updateTextBounds(annotation);
    }
  }
  interaction.changed = true;
  renderEditor();
}

function onPointerLeave() {
  colorTooltipVisible.value = false;
}

function sampleCanvasColor(point: Point, event: PointerEvent) {
  const context = canvas.value?.getContext("2d", { willReadFrequently: true });
  if (!context) return;
  const x = clamp(Math.floor(point.x), 0, imageWidth.value - 1);
  const y = clamp(Math.floor(point.y), 0, imageHeight.value - 1);
  const [red, green, blue, alphaByte] = context.getImageData(x, y, 1, 1).data;
  const alpha = alphaByte / 255;
  sampledColor.value = {
    hex: rgbToHex(red, green, blue),
    rgb: alphaByte === 255
      ? `rgb(${red}, ${green}, ${blue})`
      : `rgba(${red}, ${green}, ${blue}, ${alpha.toFixed(2)})`,
    hsl: rgbToHsl(red, green, blue),
    red,
    green,
    blue,
    alpha,
  };
  sampledPosition.value = { x, y };
  const tooltipWidth = 150;
  const tooltipHeight = 176;
  const preferredX = event.clientX + 18;
  const preferredY = event.clientY + 18;
  colorTooltipPosition.value = {
    x: preferredX + tooltipWidth <= window.innerWidth ? preferredX : Math.max(8, event.clientX - tooltipWidth - 18),
    y: preferredY + tooltipHeight <= window.innerHeight ? preferredY : Math.max(8, event.clientY - tooltipHeight - 18),
  };
  colorTooltipVisible.value = true;
  void nextTick(() => drawColorMagnifier(x, y));
}

function drawColorMagnifier(centerX: number, centerY: number) {
  const source = canvas.value;
  const target = colorMagnifier.value;
  if (!source || !target) return;
  const context = target.getContext("2d");
  if (!context) return;

  const radius = 5;
  const cells = radius * 2 + 1;
  const cellSize = target.width / cells;
  const sampleLeft = centerX - radius;
  const sampleTop = centerY - radius;
  const sourceX = Math.max(0, sampleLeft);
  const sourceY = Math.max(0, sampleTop);
  const sourceRight = Math.min(imageWidth.value, centerX + radius + 1);
  const sourceBottom = Math.min(imageHeight.value, centerY + radius + 1);
  const sourceWidth = Math.max(0, sourceRight - sourceX);
  const sourceHeight = Math.max(0, sourceBottom - sourceY);
  const destinationX = (sourceX - sampleLeft) * cellSize;
  const destinationY = (sourceY - sampleTop) * cellSize;

  context.clearRect(0, 0, target.width, target.height);
  context.fillStyle = "#d6dde1";
  context.fillRect(0, 0, target.width, target.height);
  context.imageSmoothingEnabled = false;
  if (sourceWidth && sourceHeight) {
    context.drawImage(
      source,
      sourceX,
      sourceY,
      sourceWidth,
      sourceHeight,
      destinationX,
      destinationY,
      sourceWidth * cellSize,
      sourceHeight * cellSize,
    );
  }

  context.save();
  context.strokeStyle = "rgba(0, 0, 0, 0.18)";
  context.lineWidth = 1;
  for (let index = 1; index < cells; index += 1) {
    const offset = Math.round(index * cellSize) + 0.5;
    context.beginPath();
    context.moveTo(offset, 0);
    context.lineTo(offset, target.height);
    context.moveTo(0, offset);
    context.lineTo(target.width, offset);
    context.stroke();
  }
  const centerOffset = radius * cellSize;
  context.strokeStyle = "#ffffff";
  context.lineWidth = 3;
  context.strokeRect(centerOffset + 1.5, centerOffset + 1.5, cellSize - 3, cellSize - 3);
  context.strokeStyle = "#111820";
  context.lineWidth = 1;
  context.strokeRect(centerOffset + 0.5, centerOffset + 0.5, cellSize - 1, cellSize - 1);
  context.restore();
}

function rgbToHex(red: number, green: number, blue: number) {
  return `#${[red, green, blue].map((value) => value.toString(16).padStart(2, "0")).join("")}`.toUpperCase();
}

function rgbToHsl(red: number, green: number, blue: number) {
  const r = red / 255;
  const g = green / 255;
  const b = blue / 255;
  const maximum = Math.max(r, g, b);
  const minimum = Math.min(r, g, b);
  const lightness = (maximum + minimum) / 2;
  const delta = maximum - minimum;
  let hue = 0;
  let saturation = 0;

  if (delta) {
    saturation = delta / (1 - Math.abs(2 * lightness - 1));
    if (maximum === r) hue = 60 * (((g - b) / delta) % 6);
    else if (maximum === g) hue = 60 * ((b - r) / delta + 2);
    else hue = 60 * ((r - g) / delta + 4);
  }
  if (hue < 0) hue += 360;
  return `hsl(${Math.round(hue)}, ${Math.round(saturation * 100)}%, ${Math.round(lightness * 100)}%)`;
}

async function copySampledColor() {
  if (!sampledColor.value) return;
  if (await writeClipboard(sampledColor.value.hex)) message.success(`已复制 ${sampledColor.value.hex}`);
  else message.error("浏览器不允许写入剪贴板。");
}

function onPointerUp(event: PointerEvent) {
  if (canvas.value?.hasPointerCapture(event.pointerId)) canvas.value.releasePointerCapture(event.pointerId);
  cropStart.value = null;
  if (!interaction) return;

  const annotation = annotations.value.find((item) => item.id === interaction?.annotationId);
  const tooSmall = annotation && (
    ((interaction.kind === "draw-rect" || interaction.kind === "draw-mosaic") && (annotation.width < 3 || annotation.height < 3))
    || (interaction.kind === "draw-arrow" && annotation.type === "arrow" && Math.hypot(annotation.end.x - annotation.start.x, annotation.end.y - annotation.start.y) < 3)
    || (interaction.kind === "draw-brush" && annotation.type === "brush" && annotation.points.length < 2)
  );
  if (tooSmall && annotation) {
    annotations.value = annotations.value.filter((item) => item.id !== annotation.id);
    selectedId.value = null;
  } else if (interaction.changed) {
    commitHistory();
  }
  interaction = null;
  renderEditor();
}

function onDoubleClick(event: MouseEvent) {
  const hit = hitTest(canvasPoint(event));
  if (hit?.type !== "text") return;
  selectedId.value = hit.id;
  activeTool.value = "select";
  renderEditor();
  nextTick(() => {
    textEditor.value?.focus();
    textEditor.value?.select();
  });
}

function canvasPoint(event: MouseEvent | PointerEvent): Point {
  const bounds = canvas.value!.getBoundingClientRect();
  return {
    x: clamp((event.clientX - bounds.left) * imageWidth.value / bounds.width, 0, imageWidth.value),
    y: clamp((event.clientY - bounds.top) * imageHeight.value / bounds.height, 0, imageHeight.value),
  };
}

function normalizeBounds(start: Point, end: Point): Bounds {
  return {
    x: Math.min(start.x, end.x),
    y: Math.min(start.y, end.y),
    width: Math.abs(end.x - start.x),
    height: Math.abs(end.y - start.y),
  };
}

function annotationBounds(annotation: Annotation): Bounds {
  return { x: annotation.x, y: annotation.y, width: annotation.width, height: annotation.height };
}

function cloneAnnotation<T extends Annotation>(annotation: T): T {
  if (annotation.type === "arrow") {
    return { ...annotation, start: { ...annotation.start }, end: { ...annotation.end } } as T;
  }
  if (annotation.type === "brush") {
    return { ...annotation, points: annotation.points.map((point) => ({ ...point })) } as T;
  }
  return { ...annotation };
}

function moveAnnotation(annotation: Annotation, original: Annotation, deltaX: number, deltaY: number) {
  const nextX = clamp(original.x + deltaX, 0, imageWidth.value - original.width);
  const nextY = clamp(original.y + deltaY, 0, imageHeight.value - original.height);
  const appliedX = nextX - original.x;
  const appliedY = nextY - original.y;
  annotation.x = nextX;
  annotation.y = nextY;

  if (annotation.type === "arrow" && original.type === "arrow") {
    annotation.start = { x: original.start.x + appliedX, y: original.start.y + appliedY };
    annotation.end = { x: original.end.x + appliedX, y: original.end.y + appliedY };
  } else if (annotation.type === "brush" && original.type === "brush") {
    annotation.points = original.points.map((point) => ({ x: point.x + appliedX, y: point.y + appliedY }));
  }
}

function updateArrowBounds(annotation: ArrowAnnotation) {
  annotation.x = Math.min(annotation.start.x, annotation.end.x);
  annotation.y = Math.min(annotation.start.y, annotation.end.y);
  annotation.width = Math.abs(annotation.end.x - annotation.start.x);
  annotation.height = Math.abs(annotation.end.y - annotation.start.y);
}

function updateBrushBounds(annotation: BrushAnnotation) {
  const xs = annotation.points.map((point) => point.x);
  const ys = annotation.points.map((point) => point.y);
  annotation.x = Math.min(...xs);
  annotation.y = Math.min(...ys);
  annotation.width = Math.max(1, Math.max(...xs) - annotation.x);
  annotation.height = Math.max(1, Math.max(...ys) - annotation.y);
}

function hitTest(point: Point) {
  const tolerance = 7 / zoom.value;
  return [...annotations.value].reverse().find((annotation) => (
    point.x >= annotation.x - tolerance
    && point.x <= annotation.x + annotation.width + tolerance
    && point.y >= annotation.y - tolerance
    && point.y <= annotation.y + annotation.height + tolerance
  )) ?? null;
}

function isOnResizeHandle(point: Point, annotation: Annotation) {
  if (annotation.type === "brush") return false;
  const size = 12 / zoom.value;
  const x = annotation.type === "arrow" ? annotation.end.x : annotation.x + annotation.width;
  const y = annotation.type === "arrow" ? annotation.end.y : annotation.y + annotation.height;
  return Math.abs(point.x - x) <= size && Math.abs(point.y - y) <= size;
}

function renderEditor() {
  const target = canvas.value;
  if (!target || !sourceImage) return;
  const context = target.getContext("2d");
  if (!context) return;
  renderContent(context, true);
}

function renderContent(context: CanvasRenderingContext2D, showControls: boolean) {
  context.save();
  context.clearRect(0, 0, imageWidth.value, imageHeight.value);
  if (sourceImage) context.drawImage(sourceImage, 0, 0, imageWidth.value, imageHeight.value);

  for (const annotation of annotations.value) {
    if (annotation.type === "rect") drawRectangle(context, annotation);
    else if (annotation.type === "text") drawText(context, annotation);
    else if (annotation.type === "mosaic") drawMosaic(context, annotation);
    else if (annotation.type === "arrow") drawArrow(context, annotation);
    else drawBrush(context, annotation);
  }

  if (showControls && selectedAnnotation.value) drawSelection(context, selectedAnnotation.value);
  if (showControls && cropDraft.value) drawCropOverlay(context, cropDraft.value);
  context.restore();
}

function drawRectangle(context: CanvasRenderingContext2D, annotation: RectangleAnnotation) {
  context.save();
  context.strokeStyle = annotation.color;
  context.lineWidth = annotation.lineWidth;
  context.lineJoin = "round";
  const offset = annotation.lineWidth / 2;
  context.strokeRect(
    annotation.x + offset,
    annotation.y + offset,
    Math.max(0, annotation.width - annotation.lineWidth),
    Math.max(0, annotation.height - annotation.lineWidth),
  );
  context.restore();
}

function drawMosaic(context: CanvasRenderingContext2D, annotation: MosaicAnnotation) {
  const x = Math.max(0, Math.floor(annotation.x));
  const y = Math.max(0, Math.floor(annotation.y));
  const width = Math.min(imageWidth.value - x, Math.ceil(annotation.width));
  const height = Math.min(imageHeight.value - y, Math.ceil(annotation.height));
  if (width < 1 || height < 1) return;

  const pixelWidth = Math.max(1, Math.ceil(width / annotation.blockSize));
  const pixelHeight = Math.max(1, Math.ceil(height / annotation.blockSize));
  const pixels = document.createElement("canvas");
  pixels.width = pixelWidth;
  pixels.height = pixelHeight;
  const pixelContext = pixels.getContext("2d");
  if (!pixelContext) return;
  pixelContext.imageSmoothingEnabled = false;
  pixelContext.drawImage(context.canvas, x, y, width, height, 0, 0, pixelWidth, pixelHeight);

  context.save();
  context.imageSmoothingEnabled = false;
  context.drawImage(pixels, 0, 0, pixelWidth, pixelHeight, x, y, width, height);
  context.restore();
}

function drawArrow(context: CanvasRenderingContext2D, annotation: ArrowAnnotation) {
  const angle = Math.atan2(annotation.end.y - annotation.start.y, annotation.end.x - annotation.start.x);
  const headLength = Math.max(annotation.lineWidth * 4, Math.min(32, Math.hypot(annotation.width, annotation.height) * 0.22));
  context.save();
  context.strokeStyle = annotation.color;
  context.fillStyle = annotation.color;
  context.lineWidth = annotation.lineWidth;
  context.lineCap = "round";
  context.lineJoin = "round";
  context.beginPath();
  context.moveTo(annotation.start.x, annotation.start.y);
  context.lineTo(annotation.end.x, annotation.end.y);
  context.stroke();
  context.beginPath();
  context.moveTo(annotation.end.x, annotation.end.y);
  context.lineTo(annotation.end.x - headLength * Math.cos(angle - Math.PI / 6), annotation.end.y - headLength * Math.sin(angle - Math.PI / 6));
  context.lineTo(annotation.end.x - headLength * Math.cos(angle + Math.PI / 6), annotation.end.y - headLength * Math.sin(angle + Math.PI / 6));
  context.closePath();
  context.fill();
  context.restore();
}

function drawBrush(context: CanvasRenderingContext2D, annotation: BrushAnnotation) {
  if (annotation.points.length < 2) return;
  context.save();
  context.strokeStyle = annotation.color;
  context.lineWidth = annotation.lineWidth;
  context.lineCap = "round";
  context.lineJoin = "round";
  context.beginPath();
  context.moveTo(annotation.points[0].x, annotation.points[0].y);
  annotation.points.slice(1).forEach((point) => context.lineTo(point.x, point.y));
  context.stroke();
  context.restore();
}

function drawText(context: CanvasRenderingContext2D, annotation: TextAnnotation) {
  context.save();
  context.font = `700 ${annotation.fontSize}px Inter, "PingFang SC", "Microsoft YaHei", sans-serif`;
  context.textBaseline = "top";
  context.fillStyle = annotation.color;
  context.strokeStyle = "rgba(255, 255, 255, 0.92)";
  context.lineWidth = Math.max(2, annotation.fontSize / 14);
  context.lineJoin = "round";
  annotation.text.split("\n").forEach((line, index) => {
    const y = annotation.y + index * annotation.fontSize * 1.3;
    context.strokeText(line || " ", annotation.x, y);
    context.fillText(line || " ", annotation.x, y);
  });
  context.restore();
}

function drawSelection(context: CanvasRenderingContext2D, annotation: Annotation) {
  const scale = 1 / zoom.value;
  context.save();
  context.strokeStyle = "#00a6ff";
  context.lineWidth = 2 * scale;
  context.setLineDash([7 * scale, 5 * scale]);
  context.strokeRect(annotation.x, annotation.y, annotation.width, annotation.height);
  context.setLineDash([]);
  context.fillStyle = "#ffffff";
  context.strokeStyle = "#0078b8";
  context.lineWidth = 2 * scale;
  if (annotation.type !== "brush") {
    const size = 10 * scale;
    const handleX = annotation.type === "arrow" ? annotation.end.x : annotation.x + annotation.width;
    const handleY = annotation.type === "arrow" ? annotation.end.y : annotation.y + annotation.height;
    context.fillRect(handleX - size / 2, handleY - size / 2, size, size);
    context.strokeRect(handleX - size / 2, handleY - size / 2, size, size);
  }
  context.restore();
}

function drawCropOverlay(context: CanvasRenderingContext2D, crop: Bounds) {
  context.save();
  context.fillStyle = "rgba(4, 12, 18, 0.58)";
  context.fillRect(0, 0, imageWidth.value, crop.y);
  context.fillRect(0, crop.y, crop.x, crop.height);
  context.fillRect(crop.x + crop.width, crop.y, imageWidth.value - crop.x - crop.width, crop.height);
  context.fillRect(0, crop.y + crop.height, imageWidth.value, imageHeight.value - crop.y - crop.height);
  context.strokeStyle = "#ffffff";
  context.lineWidth = 2 / zoom.value;
  context.setLineDash([8 / zoom.value, 5 / zoom.value]);
  context.strokeRect(crop.x, crop.y, crop.width, crop.height);
  context.restore();
}

function updateTextBounds(annotation: TextAnnotation) {
  const measureCanvas = document.createElement("canvas");
  const context = measureCanvas.getContext("2d");
  if (!context) return;
  context.font = `700 ${annotation.fontSize}px Inter, "PingFang SC", "Microsoft YaHei", sans-serif`;
  const lines = annotation.text.split("\n");
  annotation.width = Math.max(annotation.fontSize, ...lines.map((line) => context.measureText(line || " ").width));
  annotation.height = Math.max(annotation.fontSize * 1.3, lines.length * annotation.fontSize * 1.3);
}

function updateSelectedText(event: Event) {
  const annotation = selectedAnnotation.value;
  if (annotation?.type !== "text") return;
  annotation.text = (event.target as HTMLTextAreaElement).value;
  updateTextBounds(annotation);
  textEditDirty = true;
  renderEditor();
}

function updateTextFontSize(event: Event) {
  const annotation = selectedAnnotation.value;
  if (annotation?.type !== "text") return;
  annotation.fontSize = clamp(Number((event.target as HTMLInputElement).value) || 24, 12, 160);
  updateTextBounds(annotation);
  commitHistory();
  renderEditor();
}

function updateStrokeLineWidth(event: Event) {
  const annotation = selectedAnnotation.value;
  if (!annotation || !("lineWidth" in annotation)) return;
  annotation.lineWidth = clamp(Number((event.target as HTMLInputElement).value) || 3, 1, 40);
  commitHistory();
  renderEditor();
}

function updateMosaicBlockSize(event: Event) {
  const annotation = selectedAnnotation.value;
  if (annotation?.type !== "mosaic") return;
  annotation.blockSize = clamp(Number((event.target as HTMLInputElement).value) || 12, 4, 80);
  commitHistory();
  renderEditor();
}

function updateSelectedColor(event: Event) {
  const annotation = selectedAnnotation.value;
  if (!annotation || !("color" in annotation)) return;
  annotation.color = (event.target as HTMLInputElement).value;
  renderEditor();
}

function annotationTypeLabel(type: AnnotationType) {
  return ({ rect: "矩形", text: "文字", mosaic: "马赛克", arrow: "箭头", brush: "画笔" })[type];
}

function flushTextHistory() {
  if (!textEditDirty) return;
  textEditDirty = false;
  commitHistory();
}

async function applyCrop() {
  if (!sourceImage || !cropDraft.value || !canApplyCrop.value) return;
  const crop = {
    x: Math.round(cropDraft.value.x),
    y: Math.round(cropDraft.value.y),
    width: Math.round(cropDraft.value.width),
    height: Math.round(cropDraft.value.height),
  };
  const output = document.createElement("canvas");
  output.width = crop.width;
  output.height = crop.height;
  output.getContext("2d")?.drawImage(sourceImage, crop.x, crop.y, crop.width, crop.height, 0, 0, crop.width, crop.height);

  const adjusted = annotations.value
    .filter((annotation) => boundsIntersect(annotation, crop))
    .map((annotation) => translateAnnotation(annotation, -crop.x, -crop.y));

  sourceImage = await loadImage(output.toDataURL("image/png"));
  imageUrl.value = sourceImage.src;
  imageWidth.value = crop.width;
  imageHeight.value = crop.height;
  annotations.value = adjusted;
  selectedId.value = null;
  cropDraft.value = null;
  activeTool.value = "select";
  commitHistory();
  await nextTick();
  fitToViewport();
  renderEditor();
}

function boundsIntersect(left: Bounds, right: Bounds) {
  return left.x < right.x + right.width
    && left.x + left.width > right.x
    && left.y < right.y + right.height
    && left.y + left.height > right.y;
}

function translateAnnotation(annotation: Annotation, deltaX: number, deltaY: number) {
  const translated = cloneAnnotation(annotation);
  translated.x += deltaX;
  translated.y += deltaY;
  if (translated.type === "arrow") {
    translated.start.x += deltaX;
    translated.start.y += deltaY;
    translated.end.x += deltaX;
    translated.end.y += deltaY;
  } else if (translated.type === "brush") {
    translated.points.forEach((point) => {
      point.x += deltaX;
      point.y += deltaY;
    });
  }
  return translated;
}

function cancelCrop() {
  cropDraft.value = null;
  cropStart.value = null;
  renderEditor();
}

function deleteSelected() {
  if (!selectedId.value) return;
  annotations.value = annotations.value.filter((item) => item.id !== selectedId.value);
  selectedId.value = null;
  commitHistory();
  renderEditor();
}

function commitHistory() {
  textEditDirty = false;
  if (!imageUrl.value) return;
  const snapshot = currentSnapshot();
  const current = history.value[historyIndex.value];
  if (current && JSON.stringify(current) === JSON.stringify(snapshot)) return;
  history.value = history.value.slice(0, historyIndex.value + 1);
  history.value.push(snapshot);
  if (history.value.length > 30) history.value.shift();
  historyIndex.value = history.value.length - 1;
}

function currentSnapshot(): EditorSnapshot {
  return {
    imageUrl: imageUrl.value,
    imageWidth: imageWidth.value,
    imageHeight: imageHeight.value,
    annotations: annotations.value.map((item) => cloneAnnotation(item)),
  };
}

async function undo() {
  flushTextHistory();
  if (!canUndo.value) return;
  historyIndex.value -= 1;
  await restoreSnapshot(history.value[historyIndex.value]);
}

async function redo() {
  if (!canRedo.value) return;
  historyIndex.value += 1;
  await restoreSnapshot(history.value[historyIndex.value]);
}

async function restoreSnapshot(snapshot: EditorSnapshot) {
  sourceImage = await loadImage(snapshot.imageUrl);
  imageUrl.value = snapshot.imageUrl;
  imageWidth.value = snapshot.imageWidth;
  imageHeight.value = snapshot.imageHeight;
  annotations.value = snapshot.annotations.map((item) => cloneAnnotation(item));
  selectedId.value = null;
  cropDraft.value = null;
  activeTool.value = "select";
  await nextTick();
  renderEditor();
}

function exportBlob() {
  return new Promise<Blob>((resolve, reject) => {
    const output = document.createElement("canvas");
    output.width = imageWidth.value;
    output.height = imageHeight.value;
    const context = output.getContext("2d");
    if (!context) {
      reject(new Error("无法创建导出画布。"));
      return;
    }
    renderContent(context, false);
    output.toBlob((blob) => blob ? resolve(blob) : reject(new Error("PNG 生成失败。")), "image/png");
  });
}

async function copyImage() {
  try {
    if (!("ClipboardItem" in window) || !navigator.clipboard?.write) {
      throw new Error("当前浏览器不支持复制图片，请使用下载。");
    }
    await navigator.clipboard.write([new ClipboardItem({ "image/png": await exportBlob() })]);
    message.success("图片已复制到剪贴板。");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "复制图片失败。");
  }
}

async function downloadImage() {
  try {
    const blob = await exportBlob();
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `space-station-screenshot-${formatTimestamp(new Date())}.png`;
    link.click();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    message.success("PNG 已下载。");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "下载失败。");
  }
}

function formatTimestamp(date: Date) {
  const pad = (value: number) => String(value).padStart(2, "0");
  return `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}-${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`;
}

function fitToViewport() {
  if (!viewport.value || !imageWidth.value || !imageHeight.value) return;
  const availableWidth = Math.max(240, viewport.value.clientWidth - 72);
  const availableHeight = Math.max(180, viewport.value.clientHeight - 72);
  zoom.value = clamp(Math.min(availableWidth / imageWidth.value, availableHeight / imageHeight.value, 1), 0.08, 1);
  renderEditor();
}

function changeZoom(delta: number) {
  zoom.value = clamp(Math.round((zoom.value + delta) * 10) / 10, 0.1, 2);
  renderEditor();
}

function onKeyDown(event: KeyboardEvent) {
  const target = event.target as HTMLElement | null;
  const editingText = target?.tagName === "TEXTAREA" || target?.tagName === "INPUT";
  const command = event.metaKey || event.ctrlKey;

  if (command && event.key.toLowerCase() === "z") {
    event.preventDefault();
    void (event.shiftKey ? redo() : undo());
    return;
  }
  if (editingText) return;
  if ((event.key === "Delete" || event.key === "Backspace") && selectedAnnotation.value) {
    event.preventDefault();
    deleteSelected();
    return;
  }
  if (event.key === "Escape") {
    cancelCrop();
    selectedId.value = null;
    renderEditor();
    return;
  }
  if (event.key === "Enter" && activeTool.value === "crop" && canApplyCrop.value) {
    event.preventDefault();
    void applyCrop();
    return;
  }
  const keyMap: Record<string, EditorTool> = {
    v: "select",
    i: "eyedropper",
    c: "crop",
    m: "mosaic",
    r: "rect",
    a: "arrow",
    b: "brush",
    t: "text",
  };
  const tool = keyMap[event.key.toLowerCase()];
  if (tool && hasImage.value && !command && !event.altKey) activateTool(tool);
}

function createId() {
  return `annotation-${nextAnnotationId++}`;
}

function clamp(value: number, minimum: number, maximum: number) {
  return Math.min(Math.max(value, minimum), Math.max(minimum, maximum));
}
</script>

<style scoped>
.shot-app {
  height: 100dvh;
  display: grid;
  grid-template-rows: auto minmax(0, 1fr);
  overflow: hidden;
  background: #e9eef2;
  color: #172632;
}

.shot-app.capture-ui-hidden { opacity: 0; }

button {
  min-height: 34px;
  padding: 0 11px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  border: 1px solid rgba(30, 48, 62, 0.16);
  border-radius: 7px;
  background: #fff;
  color: #273b49;
  font-size: 12px;
  font-weight: 700;
  cursor: pointer;
  transition: 140ms ease;
}

button:hover:not(:disabled) { border-color: rgba(25, 64, 86, 0.36); background: #f5f9fb; }
button:focus-visible { outline: 2px solid #1884b5; outline-offset: 2px; }
button:disabled { cursor: not-allowed; opacity: 0.42; }
button.primary-action { border-color: #163a4e; background: #163a4e; color: #fff; }
button.primary-action:hover:not(:disabled) { border-color: #235c78; background: #235c78; }

.shot-toolbar {
  min-height: 64px;
  padding: 10px 14px;
  display: flex;
  align-items: center;
  gap: 12px;
  z-index: 3;
  border-bottom: 1px solid rgba(36, 55, 69, 0.14);
  background: rgba(249, 251, 252, 0.96);
  box-shadow: 0 3px 18px rgba(38, 54, 66, 0.07);
}

.shot-brand {
  min-width: 174px;
  display: flex;
  align-items: center;
  gap: 10px;
}

.shot-home {
  width: 38px;
  height: 38px;
  display: grid;
  place-items: center;
  flex: 0 0 auto;
  border-radius: 8px;
  background: #153243;
  color: #fff;
  font-size: 13px;
  font-weight: 800;
  text-decoration: none;
}

.shot-brand > div { min-width: 0; display: grid; gap: 1px; }
.shot-brand strong { font-size: 14px; }
.shot-brand small { overflow: hidden; color: #73818b; font-size: 10px; text-overflow: ellipsis; white-space: nowrap; }
.shot-actions { display: flex; align-items: center; gap: 6px; }
.shot-actions button { white-space: nowrap; }
.shot-source-actions { padding-right: 12px; border-right: 1px solid rgba(33, 52, 66, 0.12); }
.shot-history { margin-left: auto; }
.shot-history button { width: 34px; padding: 0; font-size: 16px; }
.shot-export-actions { padding-left: 6px; }
.hidden-input { display: none; }

.capture-option {
  min-height: 34px;
  padding: 0 8px;
  display: inline-flex;
  align-items: center;
  gap: 5px;
  color: #566a77;
  font-size: 11px;
  font-weight: 700;
  white-space: nowrap;
  cursor: pointer;
}

.capture-option input { accent-color: #1b6d93; }

.capture-delay {
  min-height: 34px;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  color: #566a77;
}

.capture-delay select {
  height: 30px;
  padding: 0 22px 0 7px;
  border: 1px solid rgba(30, 48, 62, 0.14);
  border-radius: 6px;
  background: #fff;
  color: #405663;
  font-size: 11px;
  font-weight: 700;
  outline: none;
  cursor: pointer;
}

.capture-delay select:focus { border-color: #287b9f; box-shadow: 0 0 0 2px rgba(40, 123, 159, 0.12); }
.capture-delay select:disabled { cursor: not-allowed; opacity: 0.5; }

.capture-countdown {
  position: fixed;
  inset: 0;
  z-index: 20;
  display: grid;
  place-content: center;
  justify-items: center;
  gap: 8px;
  background: rgba(12, 25, 34, 0.82);
  color: #fff;
  backdrop-filter: blur(8px);
}

.capture-countdown strong { font-size: 72px; line-height: 1; font-variant-numeric: tabular-nums; }
.capture-countdown span { color: rgba(255, 255, 255, 0.78); font-size: 15px; }

.shot-toolset {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 4px;
  border-radius: 9px;
  background: #e9eef1;
}

.shot-toolset button {
  min-height: 32px;
  border-color: transparent;
  background: transparent;
}

.shot-toolset button.active {
  border-color: rgba(25, 70, 94, 0.2);
  background: #fff;
  color: #0b668f;
  box-shadow: 0 2px 7px rgba(40, 61, 74, 0.1);
}

.shot-toolset kbd {
  min-width: 16px;
  padding: 1px 4px;
  border-radius: 4px;
  background: rgba(53, 73, 88, 0.08);
  color: #75838d;
  font: 9px/1.5 "SFMono-Regular", Consolas, monospace;
}

.shot-body { min-width: 0; min-height: 0; display: flex; }
.shot-empty {
  width: min(620px, calc(100% - 48px));
  margin: auto;
  padding: 56px;
  display: grid;
  justify-items: center;
  text-align: center;
  border: 1px dashed rgba(35, 62, 80, 0.22);
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.68);
  box-shadow: 0 20px 55px rgba(48, 67, 81, 0.08);
}

.empty-illustration {
  width: 104px;
  height: 82px;
  display: grid;
  place-items: center;
  margin-bottom: 18px;
  border-radius: 17px;
  background: linear-gradient(145deg, #d6e9f1, #eef6f8);
  color: #27647e;
}

.shot-empty h2 { margin: 0; font-size: 25px; }
.shot-empty p { max-width: 470px; margin: 12px 0 20px; color: #647681; line-height: 1.7; }
.shot-empty > small { margin-top: 20px; color: #87939b; line-height: 1.6; }
.empty-actions { display: flex; flex-wrap: wrap; justify-content: center; gap: 9px; }
button.large { min-height: 42px; padding: 0 18px; font-size: 14px; }

.shot-viewport {
  min-width: 0;
  min-height: 0;
  position: relative;
  flex: 1 1 auto;
  overflow: auto;
  background:
    linear-gradient(45deg, rgba(90, 109, 122, 0.06) 25%, transparent 25%),
    linear-gradient(-45deg, rgba(90, 109, 122, 0.06) 25%, transparent 25%),
    linear-gradient(45deg, transparent 75%, rgba(90, 109, 122, 0.06) 75%),
    linear-gradient(-45deg, transparent 75%, rgba(90, 109, 122, 0.06) 75%),
    #dce3e7;
  background-position: 0 0, 0 8px, 8px -8px, -8px 0;
  background-size: 16px 16px;
}

.canvas-space {
  min-width: 100%;
  min-height: 100%;
  padding: 36px;
  display: grid;
  place-items: center;
}

.shot-canvas {
  display: block;
  flex: 0 0 auto;
  background: #fff;
  box-shadow: 0 10px 35px rgba(17, 29, 37, 0.22), 0 0 0 1px rgba(31, 49, 60, 0.14);
  touch-action: none;
  user-select: none;
}

.shot-canvas.tool-select { cursor: default; }
.shot-canvas.tool-eyedropper { cursor: crosshair; }
.shot-canvas.tool-crop { cursor: crosshair; }
.shot-canvas.tool-mosaic { cursor: crosshair; }
.shot-canvas.tool-rect { cursor: crosshair; }
.shot-canvas.tool-arrow { cursor: crosshair; }
.shot-canvas.tool-brush { cursor: crosshair; }
.shot-canvas.tool-text { cursor: text; }

.color-tooltip {
  position: fixed;
  z-index: 12;
  width: 146px;
  padding: 6px;
  display: grid;
  gap: 6px;
  border: 1px solid rgba(16, 31, 41, 0.22);
  border-radius: 7px;
  background: rgba(255, 255, 255, 0.96);
  color: #20333f;
  box-shadow: 0 6px 18px rgba(19, 33, 42, 0.24);
  font: 700 11px/1 "SFMono-Regular", Consolas, monospace;
  pointer-events: none;
}

.color-magnifier {
  width: 132px;
  height: 132px;
  display: block;
  border: 1px solid rgba(17, 28, 35, 0.42);
  border-radius: 4px;
  background: #d6dde1;
}

.color-tooltip-meta {
  min-width: 0;
  display: grid;
  grid-template-columns: 22px minmax(0, 1fr) auto;
  align-items: center;
  gap: 6px;
}

.color-tooltip-meta strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.color-tooltip-meta small { color: #78868f; font: 500 8px/1 "SFMono-Regular", Consolas, monospace; }

.color-tooltip-swatch {
  width: 22px;
  height: 22px;
  flex: 0 0 auto;
  border: 1px solid rgba(20, 35, 45, 0.2);
  border-radius: 5px;
}

.zoom-control,
.crop-actions {
  position: fixed;
  bottom: 18px;
  z-index: 4;
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 5px;
  border: 1px solid rgba(30, 48, 61, 0.15);
  border-radius: 9px;
  background: rgba(255, 255, 255, 0.94);
  box-shadow: 0 8px 26px rgba(31, 47, 58, 0.15);
}

.zoom-control { left: 18px; }
.zoom-control button { min-width: 34px; padding: 0 8px; border-color: transparent; background: transparent; }
.crop-actions { left: 50%; transform: translateX(-50%); }
.crop-actions span { min-width: 82px; padding: 0 7px; color: #657681; font-size: 12px; font-variant-numeric: tabular-nums; }

.shot-inspector {
  width: 270px;
  padding: 18px;
  display: flex;
  flex-direction: column;
  flex: 0 0 auto;
  gap: 12px;
  border-left: 1px solid rgba(34, 53, 67, 0.14);
  background: #f8fafb;
  box-shadow: -6px 0 20px rgba(50, 66, 77, 0.04);
}

.inspector-heading { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding-bottom: 7px; }
.inspector-heading > div { display: grid; gap: 2px; }
.inspector-heading small { color: #8b969e; font-size: 9px; font-weight: 800; letter-spacing: 0.08em; }
.inspector-heading strong { font-size: 15px; }
.type-chip { padding: 3px 7px; border-radius: 999px; background: #e2edf2; color: #27617c; font-size: 10px; font-weight: 800; }
.field-label,
.field-grid label { display: grid; gap: 6px; color: #687781; font-size: 11px; font-weight: 700; }

.shot-inspector textarea,
.shot-inspector input[type="number"] {
  width: 100%;
  border: 1px solid rgba(32, 52, 66, 0.18);
  border-radius: 7px;
  background: #fff;
  color: #20333f;
  outline: none;
}

.shot-inspector textarea { min-height: 106px; padding: 10px; resize: vertical; line-height: 1.55; }
.shot-inspector input[type="number"] { height: 36px; padding: 0 8px; }
.shot-inspector textarea:focus,
.shot-inspector input:focus { border-color: #287b9f; box-shadow: 0 0 0 2px rgba(40, 123, 159, 0.12); }
.field-grid { display: grid; grid-template-columns: 1fr 82px; gap: 10px; }
.field-grid input[type="color"] { width: 100%; height: 36px; padding: 3px; border: 1px solid rgba(32, 52, 66, 0.18); border-radius: 7px; background: #fff; cursor: pointer; }
.inspector-help { margin: 0; color: #7b8992; font-size: 11px; line-height: 1.6; }

.color-inspector { display: grid; gap: 10px; }
.color-preview {
  height: 74px;
  border: 1px solid rgba(31, 50, 63, 0.15);
  border-radius: 9px;
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.22);
}
.color-value-row {
  min-width: 0;
  padding: 8px 0;
  display: grid;
  grid-template-columns: 38px minmax(0, 1fr);
  align-items: center;
  gap: 8px;
  border-bottom: 1px solid rgba(34, 53, 67, 0.08);
}
.color-value-row span { color: #85929a; font-size: 9px; font-weight: 800; }
.color-value-row strong { overflow: hidden; color: #314854; font: 600 11px/1.4 "SFMono-Regular", Consolas, monospace; text-overflow: ellipsis; white-space: nowrap; }
.color-inspector button { width: 100%; }

.tool-tip-card {
  padding: 18px 14px;
  display: grid;
  justify-items: start;
  gap: 8px;
  border: 1px solid rgba(38, 64, 80, 0.11);
  border-radius: 9px;
  background: #fff;
  color: #3f6478;
}

.tool-tip-card strong { color: #263c49; font-size: 13px; }
.tool-tip-card p { margin: 0; color: #72818b; font-size: 11px; line-height: 1.6; }
.inspector-footer {
  margin-top: auto;
  padding-top: 14px;
  display: grid;
  gap: 8px;
  border-top: 1px solid rgba(34, 53, 67, 0.1);
}

.inspector-footer div { display: flex; align-items: center; justify-content: space-between; gap: 10px; }
.inspector-footer span { color: #839099; font-size: 10px; }
.inspector-footer strong { color: #536570; font-size: 11px; font-variant-numeric: tabular-nums; }

@media (max-width: 1450px) {
  .shot-toolset button span,
  .shot-toolset kbd { display: none; }
  .shot-toolset button { width: 34px; padding: 0; }
}

@media (max-width: 1050px) {
  .shot-brand { min-width: auto; }
  .shot-brand > div { display: none; }
  .shot-toolset button span,
  .shot-toolset kbd,
  .shot-source-actions button:not(.primary-action) { display: none; }
  .capture-option span { display: none; }
}

@media (max-width: 760px) {
  .shot-toolbar { min-height: 58px; padding: 8px; gap: 6px; overflow-x: auto; }
  .shot-brand { position: sticky; left: 0; z-index: 2; background: #f9fbfc; }
  .shot-home { width: 34px; height: 34px; }
  .shot-source-actions { padding-right: 6px; }
  .shot-source-actions button { width: 34px; padding: 0; font-size: 0; }
  .shot-source-actions button :deep(.n-icon) { font-size: 16px; }
  .shot-history { margin-left: 0; }
  .shot-export-actions button { width: 34px; padding: 0; font-size: 0; }
  .shot-export-actions button :deep(.n-icon) { font-size: 16px; }
  .shot-body { position: relative; }
  .shot-inspector {
    width: 220px;
    max-height: 48vh;
    position: absolute;
    right: 10px;
    bottom: 10px;
    z-index: 5;
    overflow: auto;
    border: 1px solid rgba(34, 53, 67, 0.14);
    border-radius: 10px;
    box-shadow: 0 12px 34px rgba(35, 52, 64, 0.2);
  }
  .shot-inspector:has(.tool-tip-card) { display: none; }
  .zoom-control { position: absolute; }
  .crop-actions { position: absolute; }
  .shot-empty { padding: 32px 22px; }
}
</style>
