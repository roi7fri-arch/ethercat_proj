import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import legacy from "@vitejs/plugin-legacy";

export default defineConfig({
  plugins: [
    react(),
    // Emit a nomodule (SystemJS) fallback bundle + polyfills so the GUI runs on
    // the old browser on the air-gapped Ubuntu 18.04 box, which ignores
    // <script type="module"> entirely (blank page, no console error).
    legacy({
      targets: ["firefox 52", "chrome 55", "safari 10"],
    }),
  ],
  server: {
    host: true,
    port: 5173,
    proxy: {
      "/api": "http://localhost:8000",
    },
  },
});
