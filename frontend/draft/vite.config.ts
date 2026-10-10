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

// The previous landing page and the ground station. public/ is shared with the
// new landing page in ../. `--mode deploy` builds it to be served under /draft/
// from ../dist/draft (vercel.json); the root site already serves public/ there.
export default defineConfig(({ mode }) => ({
  plugins: [react(), routeEntryPages(['ground-station'])],
  base: mode === 'deploy' ? '/draft/' : '/',
  publicDir: mode === 'deploy' ? false : '../public',
  server: { port: 5174, strictPort: true },
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
