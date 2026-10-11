import { defineConfig } from 'vite';

// The landing page. public/ (the CAD GLB and part renders) and shared/ (hardware
// data, tokens) are shared with the ground station app in ground-station/, which
// Vercel builds into dist/ground-station (see vercel.json).
export default defineConfig({
  resolve: { alias: { '@shared': new URL('./shared', import.meta.url).pathname } },
  server: { port: 5173, strictPort: true },
  preview: { port: 4173, strictPort: true },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    chunkSizeWarningLimit: 700,
  },
});
