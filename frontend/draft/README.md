# DirecSun frontend (previous version)

The previous landing page and the ground station, moved here when the scroll-driven landing page in `frontend/` replaced it. In production Vercel serves it under `/draft/`, so the ground station is at `/draft/ground-station`. It shares `../public` (the CAD GLB and part renders) with the landing page.

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
npm run build:deploy # the same, built for /draft/ into ../dist/draft (Vercel)
npm run preview      # serve dist/ at http://localhost:4173
npm run typecheck    # type-check only
npm test             # unit + component tests (Vitest, jsdom)
npm run test:e2e     # builds, serves, and runs Playwright in the installed Google Chrome
QA_SCREENSHOTS=1 npx playwright test e2e/screenshots.spec.ts   # visual QA captures → qa-screenshots/
```
