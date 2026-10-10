import { defineConfig } from 'vite';

// The landing page. public/ (the CAD GLB and part renders) is shared with the
// previous app in draft/, which Vercel builds into dist/draft (see vercel.json).
export default defineConfig({
  server: { port: 5173, strictPort: true },
  preview: { port: 4173, strictPort: true },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    chunkSizeWarningLimit: 700,
  },
});
