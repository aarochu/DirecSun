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

export default defineConfig({
  plugins: [react(), routeEntryPages(['ground-station'])],
  build: {
    chunkSizeWarningLimit: 650,
  },
  test: {
    environment: 'jsdom',
    include: ['src/tests/**/*.test.{ts,tsx}'],
    setupFiles: ['src/tests/setup.ts'],
    restoreMocks: true,
  },
});
