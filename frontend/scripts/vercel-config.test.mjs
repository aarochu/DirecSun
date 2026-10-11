import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { describe, it } from 'node:test';
import { fileURLToPath } from 'node:url';

const frontend = join(dirname(fileURLToPath(import.meta.url)), '..');
const rootConfig = JSON.parse(readFileSync(join(frontend, '..', 'vercel.json'), 'utf8'));
const frontendConfig = JSON.parse(readFileSync(join(frontend, 'vercel.json'), 'utf8'));

describe('vercel.json', () => {
  it('keeps the repository-root copy and the frontend copy in sync', () => {
    assert.equal(rootConfig.framework, null);
    assert.equal(frontendConfig.framework, null);
    assert.deepEqual(rootConfig.git, frontendConfig.git);
    assert.deepEqual(rootConfig.git.deploymentEnabled, { '**': false, main: true });
    assert.deepEqual(rootConfig.redirects, frontendConfig.redirects);
    assert.deepEqual(rootConfig.rewrites, frontendConfig.rewrites);
    assert.equal(rootConfig.outputDirectory, 'frontend/dist');
    assert.equal(frontendConfig.outputDirectory, 'dist');
    for (const config of [rootConfig, frontendConfig]) {
      assert.match(config.installCommand, /ground-station/);
      assert.match(config.buildCommand, /build:deploy/);
    }
  });

  it('never answers a missing file with the app page', () => {
    const patterns = rootConfig.rewrites.map((rewrite) => new RegExp(`^${rewrite.source.replace(/\//g, '\\/')}$`));
    const rewritten = (path) => patterns.some((pattern) => pattern.test(path));
    assert.equal(rewritten('/ground-station'), true);
    assert.equal(rewritten('/ground-station/overview'), true);
    assert.equal(rewritten('/ground-station/some/client/route'), true);
    assert.equal(rewritten('/ground-station/assets/missing.js'), false);
    assert.equal(rewritten('/ground-station/anything.css'), false);
  });

  it('redirects the old draft URLs before any rewrite', () => {
    assert.deepEqual(
      rootConfig.redirects.map((redirect) => [redirect.source, redirect.destination]),
      [
        ['/draft/ground-station', '/ground-station/'],
        ['/draft/:path*', '/ground-station/overview'],
      ],
    );
  });
});
