# DirecSun frontend

The landing page: one fixed three.js view of the CAD model, driven by scroll through an anime.js timeline (vanilla TypeScript, Vite). The ground station (the operator client) is a separate React app in [`ground-station/`](ground-station/README.md); hardware data and styles both apps use live in `shared/`.

## Commands

```bash
npm ci
npm run dev          # http://localhost:5173
npm run build        # type-check + production build to dist/
npm run preview      # serve dist/ at http://localhost:4173
npm run typecheck    # type-check only
npm test             # unit tests: the demo server, vercel.json, and the flowchart data
npm run test:e2e     # builds, serves, and runs Playwright in the installed Google Chrome
```

For the ground station in development, also run `npm run dev` in `ground-station/` (port 5174); the landing page's ground station links point there.

## Run the demo offline

The venue may have no internet, and the box's own Wi-Fi has none. Build once while online, then serve both apps from the laptop:

```bash
npm run demo          # installs if needed, builds both apps, serves http://localhost:8080
npm run demo:serve    # serves the last build without rebuilding (works offline)
npm run demo:serve -- --host 0.0.0.0 --port 8080   # also reachable from other devices
npm run demo:check    # builds both and probes the served pages (what CI runs)
```

The landing page is at `/` and the ground station at `/ground-station/`, served the same way `vercel.json` serves them (`/ground-station/*` routes go to the app; a missing file is a 404, not a page; the old `/draft` URLs redirect). The site makes no request outside the laptop, so it works with no internet.

The timed pitch, preflight checklist, safety cues, and fallback paths are in [`DEMO.md`](DEMO.md).

Open the ground station over plain `http`, as above, so the browser allows `ws://`. Join the box's Wi-Fi and enter `ws://192.168.4.1/ws` in Device link (or `ws://direcsun.local/ws` on a shared network). The hosted copy on Vercel is `https`, and a browser blocks `ws://` from an `https` page, so use this local copy with the real box.

## Layout

- `src/main.ts`: markup, the scroll timeline, and boot.
- `src/scene.ts`: the CAD model view the timeline animates.
- `src/flow.ts`: the interactive electronics flowchart in the System section (wiring from `docs/technical-design.md` Sections 2 and 5).
- `src/stars.ts`: the hero's star field, ported from the previous site's `orbit-space` background.
- `src/content.ts`: all page copy.
- `shared/`: hardware parts, CAD part mapping, asset paths, tokens and base styles, used by this app and by `ground-station/` as `@shared/...`.
- `public/`: the CAD GLB and part renders, shared with `ground-station/`.

## Deploying (Vercel)

`vercel.json` at the repository root (and a copy here, for a project whose Root Directory is `frontend`) installs both apps and builds the landing page to `dist/` and the ground station to `dist/ground-station/`. No framework preset is used.

Automatic Vercel deployment is enabled only for `main`. Feature and pull-request branches rely on GitHub CI instead of creating rate-limited preview deployments; merging to `main` still produces the production deployment.
