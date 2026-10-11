import { copyFileSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import react from '@vitejs/plugin-react';
import type { Plugin } from 'vite';
import { defineConfig } from 'vitest/config';

function routeEntryPages(routes: string[]): Plugin {
  let outDir = 'dist';
  return {
    name: 'direcsun-route-entry-pages',
    apply: 'build',
    configResolved(config) {
      outDir = resolve(config.root, config.build.outDir);
    },
    writeBundle() {
      for (const route of routes) {
        mkdirSync(join(outDir, route), { recursive: true });
        copyFileSync(join(outDir, 'index.html'), join(outDir, route, 'index.html'));
      }
    },
  };
}

// The ground station (the operator client, at the app root) and the previous
// landing page (at /overview). public/ and shared/ are shared with the landing
// page in ../. `--mode deploy` builds it to be served under /ground-station/ from
// ../dist/ground-station (vercel.json); the root site already serves public/ there.
export default defineConfig(({ mode }) => ({
  plugins: [react(), routeEntryPages(['overview'])],
  base: mode === 'deploy' ? '/ground-station/' : '/',
  publicDir: mode === 'deploy' ? false : '../public',
  // shared/ sits beside this app, so its bare "three" imports cannot find this
  // app's node_modules without an explicit alias (CI installs each app alone).
  resolve: { alias: {
    '@shared': new URL('../shared', import.meta.url).pathname,
    'three': new URL('./node_modules/three', import.meta.url).pathname,
  } },
  server: { port: 5174, strictPort: true, fs: { allow: ['..'] } },
  build: {
    chunkSizeWarningLimit: 650,
  },
  test: {
    environment: 'jsdom',
    include: ['src/tests/**/*.test.{ts,tsx}'],
    setupFiles: ['src/tests/setup.ts'],
    restoreMocks: true,
  },
}));
