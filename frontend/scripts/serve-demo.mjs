import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import { dirname, extname, join, normalize, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', 'dist');
const port = Number.parseInt(process.env.DIRECSUN_DEMO_PORT ?? '4173', 10);
const types = new Map([
  ['.css', 'text/css; charset=utf-8'],
  ['.glb', 'model/gltf-binary'],
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.png', 'image/png'],
  ['.svg', 'image/svg+xml'],
  ['.webp', 'image/webp'],
  ['.woff', 'font/woff'],
  ['.woff2', 'font/woff2'],
]);

function insideRoot(path) {
  return path === root || path.startsWith(`${root}${sep}`);
}

async function regularFile(path) {
  if (!insideRoot(path)) return false;
  try {
    return (await stat(path)).isFile();
  } catch {
    return false;
  }
}

async function resolveRequest(pathname) {
  const decoded = decodeURIComponent(pathname);
  const requested = resolve(root, `.${normalize(decoded)}`);
  if (await regularFile(requested)) return requested;
  if (extname(decoded)) return null;
  const entry = decoded === '/draft' || decoded.startsWith('/draft/')
    ? join(root, 'draft', 'index.html')
    : join(root, 'index.html');
  return (await regularFile(entry)) ? entry : null;
}

const server = createServer(async (request, response) => {
  let file;
  try {
    file = await resolveRequest(new URL(request.url ?? '/', 'http://localhost').pathname);
  } catch {
    response.writeHead(400).end('Bad request');
    return;
  }
  if (!file) {
    response.writeHead(404).end('Not found');
    return;
  }
  response.writeHead(200, {
    'Cache-Control': 'no-cache',
    'Content-Type': types.get(extname(file)) ?? 'application/octet-stream',
  });
  if (request.method === 'HEAD') response.end();
  else createReadStream(file).pipe(response);
});

if (process.argv.includes('--check')) {
  const checks = [
    ['/', join(root, 'index.html')],
    ['/draft/ground-station', join(root, 'draft', 'index.html')],
  ];
  for (const [url, expected] of checks) {
    const actual = await resolveRequest(url);
    if (actual !== expected) throw new Error(`${url} resolved to ${actual ?? 'nothing'}`);
  }
  if (await resolveRequest('/draft/assets/missing.js')) throw new Error('Missing assets must return 404');
  console.log('Offline demo routes are valid.');
} else {
  server.listen(port, '127.0.0.1', () => {
    console.log(`DirecSun demo: http://127.0.0.1:${port}`);
    console.log('Press Ctrl-C to stop.');
  });
}
