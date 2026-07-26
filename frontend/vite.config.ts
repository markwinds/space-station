import { fileURLToPath, URL } from "node:url";
import vue from "@vitejs/plugin-vue";
import { defineConfig } from "vite";
import { visualizer } from "rollup-plugin-visualizer";

export default defineConfig(({ mode }) => ({
  build: {
    // esbuild 0.25 can drop xterm 6's local DECRQM enum declaration when
    // minifying the combined chunk, causing Vim's mode query to crash the
    // terminal parser with `ReferenceError: i is not defined`.
    minify: "terser",
  },
  plugins: [
    vue(),
    mode === "analyze" &&
      visualizer({
        filename: "bundle-stats.html",
        gzipSize: true,
        brotliSize: true,
        template: "treemap",
      }),
  ].filter(Boolean),
  base: "/web/",
  resolve: {
    alias: {
      "@": fileURLToPath(new URL("./src", import.meta.url)),
    },
  },
  server: {
    host: "127.0.0.1",
    port: 5173,
    proxy: {
      "/api": {
        target: "http://127.0.0.1:34827",
        changeOrigin: true,
        ws: true,
      },
    },
  },
}));
