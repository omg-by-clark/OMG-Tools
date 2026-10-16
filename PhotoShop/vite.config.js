import { defineConfig } from "vite";

export default defineConfig({
  clearScreen: false,
  server: { port: 1421, strictPort: true, watch: { ignored: ["**/src-tauri/target/**"] } },
  build: { target: "esnext" }
});
