import { createReadStream, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, normalize, resolve, sep } from 'node:path';

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.webp': 'image/webp',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.glb': 'model/gltf-binary',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain; charset=utf-8',
};

const statOf = (path) => {
  try {
    return statSync(path);
  } catch {
    return null;
  }
};

const APP = 'ground-station';
const isApp = (pathname) => pathname === `/${APP}` || pathname.startsWith(`/${APP}/`);

export function redirectFor(pathname) {
  if (pathname !== '/draft' && !pathname.startsWith('/draft/')) return null;
  return /^\/draft\/ground-station\/?$/.test(pathname) ? `/${APP}/` : `/${APP}/overview`;
}

export function locate(root, pathname) {
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch {
    return { status: 400 };
  }
  if (decoded.includes('\0')) return { status: 400 };
  const base = resolve(root);
  const target = resolve(base, `.${normalize(`/${decoded}`)}`);
  if (target !== base && !target.startsWith(base + sep)) return { status: 403 };

  const stat = statOf(target);
  if (stat?.isFile()) return { status: 200, file: target };
  if (stat?.isDirectory()) {
    const index = join(target, 'index.html');
    if (statOf(index)?.isFile()) return { status: 200, file: index };
  }
  if (isApp(decoded) && extname(decoded) === '') {
    const fallback = join(base, APP, 'index.html');
    if (statOf(fallback)?.isFile()) return { status: 200, file: fallback };
  }
  return { status: 404 };
}

export function createDemoServer(root) {
  return createServer((request, response) => {
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      response.writeHead(405, { Allow: 'GET, HEAD' }).end();
      return;
    }
    const { pathname } = new URL(request.url ?? '/', 'http://localhost');
    const moved = redirectFor(pathname);
    if (moved) {
      response.writeHead(302, { Location: moved }).end();
      return;
    }
    const found = locate(root, pathname);
    if (found.status !== 200) {
      response.writeHead(found.status, { 'Content-Type': 'text/plain; charset=utf-8' });
      response.end(found.status === 404 ? 'Not found\n' : 'Bad request\n');
      return;
    }
    response.writeHead(200, {
      'Content-Type': TYPES[extname(found.file).toLowerCase()] ?? 'application/octet-stream',
      'Content-Length': statOf(found.file).size,
      'Cache-Control': 'no-cache',
      'X-Content-Type-Options': 'nosniff',
    });
    if (request.method === 'HEAD') {
      response.end();
      return;
    }
    createReadStream(found.file).pipe(response);
  });
}
