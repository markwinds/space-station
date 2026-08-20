import { CloseOutline, ContractOutline, ReorderTwoOutline } from "@vicons/ionicons5";
import { NIcon } from "naive-ui";
import { h, type Component } from "vue";

function renderIcon(component: Component, transform = "") {
  return () => h(
    NIcon,
    transform ? { style: { transform } } : null,
    { default: () => h(component) },
  );
}

const columnsIcon = renderIcon(ReorderTwoOutline, "rotate(90deg)");
const rowsIcon = renderIcon(ReorderTwoOutline);
const closePaneIcon = renderIcon(CloseOutline);
const closeAllIcon = renderIcon(ContractOutline);

export function terminalSplitMenuOptions(canSplit: boolean, isSplit: boolean) {
  return [
    { label: "当前窗格左右分屏", key: "columns", icon: columnsIcon, disabled: !canSplit },
    { label: "当前窗格上下分屏", key: "rows", icon: rowsIcon, disabled: !canSplit },
    ...(isSplit
      ? [
          { type: "divider" as const, key: "split-divider" },
          { label: "关闭当前窗格", key: "close-pane", icon: closePaneIcon },
          { label: "退出全部分屏", key: "close-all", icon: closeAllIcon },
        ]
      : []),
  ];
}
