import { defineConfig } from "vite";
import { resolve } from "node:path";

export default defineConfig({
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    watch: {
      // 排除 tauri 编译生成的 target 目录，防止 EBUSY 文件锁死冲突
      ignored: ["**/src-tauri/target/**", "**/.edge-*/**"]
    }
  },
  build: {
    target: "esnext",
    rollupOptions: {
      input: {
        index: resolve("index.html")
      }
    }
  }
});
