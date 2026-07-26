import { onBeforeUnmount, onMounted, ref } from "vue";

const mobileTerminalBreakpoint = 760;

/**
 * Keeps a full-screen terminal inside the browser's actually visible area.
 * Mobile Safari can leave the layout viewport at its pre-keyboard height, while
 * VisualViewport reports the smaller region above the software keyboard.
 */
export function useMobileVisualViewport() {
  const mobileViewportStyle = ref<Record<string, string>>({});
  let updateFrame = 0;

  function updateViewport() {
    updateFrame = 0;
    const viewport = window.visualViewport;
    if (!viewport || window.innerWidth > mobileTerminalBreakpoint) {
      mobileViewportStyle.value = {};
      return;
    }
    mobileViewportStyle.value = {
      "--terminal-visual-height": `${Math.max(1, Math.round(viewport.height))}px`,
      "--terminal-visual-width": `${Math.max(1, Math.round(viewport.width))}px`,
      "--terminal-visual-top": `${Math.max(0, Math.round(viewport.offsetTop))}px`,
      "--terminal-visual-left": `${Math.max(0, Math.round(viewport.offsetLeft))}px`,
    };
  }

  function scheduleViewportUpdate() {
    window.cancelAnimationFrame(updateFrame);
    updateFrame = window.requestAnimationFrame(updateViewport);
  }

  onMounted(() => {
    updateViewport();
    window.addEventListener("resize", scheduleViewportUpdate, { passive: true });
    window.visualViewport?.addEventListener("resize", scheduleViewportUpdate, { passive: true });
    window.visualViewport?.addEventListener("scroll", scheduleViewportUpdate, { passive: true });
  });

  onBeforeUnmount(() => {
    window.cancelAnimationFrame(updateFrame);
    window.removeEventListener("resize", scheduleViewportUpdate);
    window.visualViewport?.removeEventListener("resize", scheduleViewportUpdate);
    window.visualViewport?.removeEventListener("scroll", scheduleViewportUpdate);
  });

  return { mobileViewportStyle };
}
