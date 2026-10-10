# DirecSun frontend

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
npm run dev          # http://localhost:5173
npm run build        # type-check + production build to dist/
npm run preview      # serve dist/ at http://localhost:4173
npm run typecheck    # type-check only
npm test             # unit + component tests (Vitest, jsdom)
npm run test:e2e     # builds, serves, and runs Playwright in the installed Google Chrome
QA_SCREENSHOTS=1 npx playwright test e2e/screenshots.spec.ts   # visual QA captures → qa-screenshots/
```
