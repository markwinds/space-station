import { computed, ref } from "vue";

export type TerminalClipboardReadState = PermissionState | "checking" | "per-use" | "unsupported" | "insecure";

export function describeTerminalClipboardError(error: unknown) {
  if (error instanceof DOMException) return `剪贴板读取失败（${error.name}: ${error.message}）`;
  if (error instanceof Error) return `剪贴板读取失败（${error.message}）`;
  return "浏览器不允许读取剪贴板";
}

export function useTerminalClipboardPermission() {
  const state = ref<TerminalClipboardReadState>("checking");
  let permissionStatus: PermissionStatus | undefined;

  const label = computed(() => ({
    checking: "检测中",
    granted: "已允许自动读取",
    prompt: "等待授权",
    denied: "已拒绝",
    "per-use": "浏览器按次确认",
    unsupported: "浏览器不支持",
    insecure: "当前页面不安全",
  })[state.value]);

  async function refresh() {
    if (permissionStatus) permissionStatus.onchange = null;
    permissionStatus = undefined;
    if (!window.isSecureContext) {
      state.value = "insecure";
      return state.value;
    }
    if (!navigator.clipboard?.readText) {
      state.value = "unsupported";
      return state.value;
    }
    if (!navigator.permissions?.query) {
      state.value = "per-use";
      return state.value;
    }
    state.value = "checking";
    try {
      const status = await navigator.permissions.query({ name: "clipboard-read" as PermissionName });
      permissionStatus = status;
      state.value = status.state;
      status.onchange = () => { state.value = status.state; };
    } catch {
      state.value = "per-use";
    }
    return state.value;
  }

  async function request() {
    if (!window.isSecureContext) throw new Error("当前页面不是浏览器认可的安全上下文，请使用受信任的 HTTPS 地址");
    if (!navigator.clipboard?.readText) throw new Error("当前浏览器不支持读取剪贴板");
    await navigator.clipboard.readText();
    return refresh();
  }

  function canReadAutomatically() {
    return state.value === "granted";
  }

  function markReadFailed() {
    state.value = "prompt";
    void refresh();
  }

  function dispose() {
    if (permissionStatus) permissionStatus.onchange = null;
    permissionStatus = undefined;
  }

  return {
    clipboardPermissionState: state,
    clipboardPermissionLabel: label,
    refreshClipboardPermission: refresh,
    requestClipboardPermission: request,
    canReadClipboardAutomatically: canReadAutomatically,
    markClipboardReadFailed: markReadFailed,
    disposeClipboardPermission: dispose,
  };
}
