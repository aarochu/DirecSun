# DirecSun frontend

The landing page: one fixed three.js view of the CAD model, driven by scroll through an anime.js timeline (vanilla TypeScript, Vite). The previous React app, which also holds the ground station, lives in [`draft/`](draft/README.md).

## Commands

```bash
npm ci
npm run dev          # http://localhost:5173
npm run build        # type-check + production build to dist/
npm run preview      # serve dist/ at http://localhost:4173
npm run typecheck    # type-check only
```

For the ground station in development, also run `npm run dev` in `draft/` (port 5174); the landing page's ground station links point there.

## Run the demo offline

Install and build both apps once while the laptop has internet access:

```bash
npm ci
npm ci --prefix draft
npm run build:demo
```

At the venue, connect the laptop to the box's Wi-Fi and serve the already-built files (no internet required):

```bash
npm run demo:serve
```

Open `http://127.0.0.1:4173`. The landing page and `/draft/ground-station` are served from the same local origin, and the ground station may connect to the box over `ws://direcsun.local/ws`. Keep this terminal open during the demo. Set `DIRECSUN_DEMO_PORT` if port 4173 is unavailable.

## Layout

- `src/main.ts`: markup, the scroll timeline, and boot.
- `src/scene.ts`: the CAD model view the timeline animates.
- `src/flow.ts`: the interactive electronics flowchart in the System section (wiring from `docs/technical-design.md` Sections 2 and 5).
- `src/stars.ts`: the hero's star field, ported from the previous site's `orbit-space` background.
- `src/content.ts`: all page copy.
- `src/data/`: hardware parts, CAD part mapping and asset paths (copied from the previous app).
- `public/`: the CAD GLB and part renders, shared with `draft/`.

## Deploying (Vercel)

`vercel.json` at the repository root (and a copy here, for a project whose Root Directory is `frontend`) installs both apps and builds the landing page to `dist/` and the previous app to `dist/draft/`. No framework preset is used.
