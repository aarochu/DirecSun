import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, describe, it } from 'node:test';
import { createDemoServer, locate, redirectFor } from './demo-server.mjs';

let root;
let outside;
let server;
let base;

before(async () => {
  const parent = mkdtempSync(join(tmpdir(), 'direcsun-demo-'));
  root = join(parent, 'dist');
  outside = join(parent, 'secret.txt');
  mkdirSync(join(root, 'ground-station', 'assets'), { recursive: true });
  mkdirSync(join(root, 'ground-station', 'overview'), { recursive: true });
  mkdirSync(join(root, 'assets'), { recursive: true });
  writeFileSync(outside, 'secret');
  writeFileSync(join(root, 'index.html'), '<title>landing</title>');
  writeFileSync(join(root, 'ground-station', 'index.html'), '<title>app</title>');
  writeFileSync(join(root, 'ground-station', 'overview', 'index.html'), '<title>overview</title>');
  writeFileSync(join(root, 'ground-station', 'assets', 'app.js'), 'export {}');
  writeFileSync(join(root, 'assets', 'model.glb'), 'glTF');
  server = createDemoServer(root);
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  base = `http://127.0.0.1:${server.address().port}`;
});

after(() => {
  server.close();
  rmSync(join(root, '..'), { recursive: true, force: true });
});

const get = async (path, init) => {
  const response = await fetch(base + path, init);
  return { status: response.status, type: response.headers.get('content-type') ?? '', body: await response.text() };
};

describe('demo server', () => {
  it('serves the landing page and its assets with the right types', async () => {
    assert.match((await get('/')).body, /landing/);
    const glb = await get('/assets/model.glb');
    assert.equal(glb.status, 200);
    assert.equal(glb.type, 'model/gltf-binary');
  });

  it('serves the ground station at /ground-station and rewrites its routes to the app page', async () => {
    assert.match((await get('/ground-station/')).body, /app/);
    assert.match((await get('/ground-station')).body, /app/);
    assert.match((await get('/ground-station/overview')).body, /overview/);
    assert.match((await get('/ground-station/some/client/route')).body, /app/);
  });

  it('redirects the old /draft URLs to their new homes', async () => {
    assert.equal(redirectFor('/draft/ground-station'), '/ground-station/');
    assert.equal(redirectFor('/draft/ground-station/'), '/ground-station/');
    assert.equal(redirectFor('/draft'), '/ground-station/overview');
    assert.equal(redirectFor('/draft/anything/else'), '/ground-station/overview');
    assert.equal(redirectFor('/ground-station/'), null);
    assert.equal(redirectFor('/drafty'), null);
    const response = await fetch(`${base}/draft/ground-station`, { redirect: 'manual' });
    assert.equal(response.status, 302);
    assert.equal(response.headers.get('location'), '/ground-station/');
  });

  it('answers a missing file with 404, never the app page', async () => {
    const script = await get('/ground-station/assets/missing.js');
    assert.equal(script.status, 404);
    assert.doesNotMatch(script.body, /app/);
    assert.equal((await get('/nope')).status, 404);
    assert.equal((await get('/ground-station/assets/app.js')).type, 'text/javascript; charset=utf-8');
  });

  it('refuses to leave the served folder', async () => {
    for (const path of ['/../secret.txt', '/%2e%2e/secret.txt', '/ground-station/../../secret.txt', '/..%2fsecret.txt']) {
      const result = await get(path);
      assert.notEqual(result.body, 'secret', path);
      assert.ok([403, 404, 200].includes(result.status), path);
    }
    assert.equal(locate(root, '/../secret.txt').file === outside, false);
    assert.equal(locate(root, '/%00').status, 400);
    assert.equal(locate(root, '/%E0%A4%A').status, 400);
  });

  it('answers HEAD without a body and rejects other methods', async () => {
    const head = await fetch(`${base}/`, { method: 'HEAD' });
    assert.equal(head.status, 200);
    assert.equal(await head.text(), '');
    assert.equal((await fetch(`${base}/`, { method: 'POST' })).status, 405);
  });
});
