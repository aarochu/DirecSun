import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { networkInterfaces } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createDemoServer } from './demo-server.mjs';

const frontend = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const appDir = join(frontend, 'ground-station');
const dist = join(frontend, 'dist');

const args = process.argv.slice(2);
const flag = (name) => args.includes(name);
const option = (name, fallback) => {
  const index = args.indexOf(name);
  return index === -1 ? fallback : args[index + 1];
};

const run = (command, commandArgs, cwd) => {
  const result = spawnSync(command, commandArgs, { cwd, stdio: 'inherit', shell: process.platform === 'win32' });
  if (result.status !== 0) {
    console.error(`demo: "${command} ${commandArgs.join(' ')}" failed in ${cwd}`);
    process.exit(result.status ?? 1);
  }
};

function build() {
  for (const dir of [frontend, appDir]) {
    if (!existsSync(join(dir, 'node_modules'))) run('npm', ['ci'], dir);
  }
  run('npm', ['run', 'build'], frontend);
  run('npm', ['run', 'build:deploy'], appDir);
}

function listen(server, port, host) {
  return new Promise((resolveListen, rejectListen) => {
    server.once('error', rejectListen);
    server.listen(port, host, () => resolveListen(server.address().port));
  });
}

async function check() {
  const server = createDemoServer(dist);
  const port = await listen(server, 0, '127.0.0.1');
  const expectations = [
    ['/', 200, 'text/html'],
    ['/ground-station/', 200, 'text/html'],
    ['/ground-station', 200, 'text/html'],
    ['/ground-station/overview', 200, 'text/html'],
    ['/assets/hardware-stack.glb', 200, 'model/gltf-binary'],
    ['/ground-station/missing-file.js', 404, 'text/plain'],
    ['/missing', 404, 'text/plain'],
  ];
  const failures = [];
  for (const [path, status, type] of expectations) {
    const response = await fetch(`http://127.0.0.1:${port}${path}`);
    await response.arrayBuffer();
    const actual = response.headers.get('content-type') ?? '';
    const ok = response.status === status && actual.startsWith(type);
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${path} -> ${response.status} ${actual}`);
    if (!ok) failures.push(path);
  }
  server.close();
  if (failures.length) process.exit(1);
}

async function serve() {
  const port = Number(option('--port', '8080'));
  const host = option('--host', '127.0.0.1');
  const server = createDemoServer(dist);
  const actual = await listen(server, port, host).catch((error) => {
    console.error(`demo: cannot listen on ${host}:${port} (${error.code ?? error.message}). Try --port <other>.`);
    process.exit(1);
  });
  const addresses =
    host === '0.0.0.0' || host === '::'
      ? Object.values(networkInterfaces())
          .flat()
          .filter((entry) => entry && entry.family === 'IPv4' && !entry.internal)
          .map((entry) => entry.address)
      : [host === '127.0.0.1' ? 'localhost' : host];
  console.log('\nDirecSun demo, served from frontend/dist');
  for (const address of addresses) {
    console.log(`  Landing page    http://${address}:${actual}/`);
    console.log(`  Ground station  http://${address}:${actual}/ground-station/`);
  }
  console.log('\nOpen the ground station over http (not https), then enter the box URL in Device link:');
  console.log('  ws://192.168.4.1/ws (the box\'s own Wi-Fi) or ws://direcsun.local/ws (same network)');
  console.log('Ctrl+C stops the server.\n');
}

if (!flag('--no-build')) build();
if (!existsSync(join(dist, 'index.html')) || !existsSync(join(dist, 'ground-station', 'index.html'))) {
  console.error('demo: frontend/dist is missing or incomplete. Run without --no-build first, while online.');
  process.exit(1);
}
await (flag('--check') ? check() : serve());
