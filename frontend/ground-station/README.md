# DirecSun ground station

The operator client for the box, and the previous landing page. It is the page that arms, e-stops and turns the DirecSun trainer, so the safety rules in `changes/CONTEXT.md` apply to it: a 5 Hz heartbeat, acks matched by `seq`, an e-stop that is never queued, and no automatic reconnect.

## Routes

| Path | Page |
| --- | --- |
| `/` | The ground station: a local simulation by default, the device link when you connect a box |
| `/overview` | The previous landing page, kept for its rotatable hardware 3D views |

In production Vercel serves the app at `/ground-station/`, so the ground station is `/ground-station/` and the previous landing page is `/ground-station/overview`. The ground station's "Overview" link goes to the site root, the scroll-driven landing page in `..`. The old `/draft` URLs redirect here.

It shares `../public` (the CAD GLB and part renders) and `../shared` (the hardware data, `assets.ts`, `hardware.ts`, `hardwareModels.ts`, and the tokens and global styles) with the landing page. Import them with `@shared/...`.

## Requirements

- Node.js 22 and npm 10 or newer
- Google Chrome (the end-to-end tests run in the installed Chrome, so no Playwright browser download is needed)

## Setup

```bash
npm ci
```

Use `npm ci`, not `npm install`, so the versions in `package-lock.json` are installed exactly.

## Commands

```bash
npm run dev          # http://localhost:5174 (the landing page links here)
npm run build        # type-check + production build to dist/
npm run build:deploy # the same, built for /ground-station/ into ../dist/ground-station (Vercel)
npm run preview      # serve dist/ at http://localhost:4173
npm run typecheck    # type-check only
npm test             # unit + component tests (Vitest, jsdom)
npm run test:e2e     # builds, serves, and runs Playwright in the installed Google Chrome
QA_SCREENSHOTS=1 npx playwright test e2e/screenshots.spec.ts   # visual QA captures to qa-screenshots/
```
