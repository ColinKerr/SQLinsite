import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The C++ server serves index.html at "/" and every other asset under
// "/static/*"; CMake embeds the whole build directory (web_build/) into the
// binary at build time. Output is flat with stable, unhashed filenames and
// base "/static/". web_build/ is gitignored — build output is never committed.
export default defineConfig({
  plugins: [react()],
  base: "/static/",
  worker: {
    rollupOptions: {
      output: {
        entryFileNames: "[name].js",
        chunkFileNames: "[name].js",
        assetFileNames: "[name][extname]",
      },
    },
  },
  build: {
    outDir: "../web_build",
    emptyOutDir: true,
    assetsDir: ".",
    rollupOptions: {
      output: {
        entryFileNames: "sqlinsite.js",
        chunkFileNames: "[name].js",
        assetFileNames: (info) =>
          info.name && info.name.endsWith(".css") ? "sqlinsite.css" : "[name][extname]",
      },
    },
  },
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: ["./src/test/setup.ts"],
  },
});
