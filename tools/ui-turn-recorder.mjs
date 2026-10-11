#!/usr/bin/env node
// Drive the main UI (frontend/ground-station) against a box through the UI's
// own controls, and record every WebSocket frame both ways (R4.1, FD
// section 17). Check the result with tools/check-ui-capture.py.
//
//   node tools/ui-turn-recorder.mjs --ui URL --box WS_URL --scenario NAME --out DIR
//        [--turns N] [--playwright PATH] [--chrome PATH] [--headed] [--real-box-go]
//
// --ui is the built ground station, served (in frontend/ground-station:
// npm run build, then npx vite preview). --box is the endpoint typed into the
// UI's DEVICE LINK field. DIR receives frames.jsonl (every frame, both ways,
// with the recorder's clock in ms), timeline.jsonl (the status and result
// lines as they change), meta.json (what the checker needs), and a screenshot
// of each page at the end. Screenshots are taken only once the scenario is over
// (or has failed): a full-page screenshot stalls the page's timers, and a
// mid-run one delayed a 200 ms ping to 304 ms.
//
// Scenarios:
//   turn   arm, then --turns turns (default 4) alternating right and left, then disarm
//   stop   arm, start a right turn, press Stop the turn mid-slew, then disarm
//   estop  arm, start a right turn, press the space bar mid-slew, then press
//          Arm again at once: the box must refuse it (ramp-down in progress)
//   watch  a second page connects and only watches while the operator arms and
//          turns once; the watch page then presses E-stop; the operator re-arms
//          after the ramp-down and closes its page, and the watch page's tlm
//          shows HEARTBEAT_LOST about 1 s after the operator's last ping
//   hidden arm, then put another tab in front of the ground station for 30 s and
//          bring it back: the box must still be READY (no HEARTBEAT_LOST). Needs
//          --headed (on Linux without a display, under xvfb-run): headless Chrome never hides a page (a background tab and a
//          minimized window both stay "visible", with exact timers), so the
//          scenario fails if the page was not hidden
//
// SAFETY. Against any box that is not on a loopback address this arms the real
// box and spins the wheel. The recorder refuses unless --real-box-go is given,
// and that flag is given only with Julian's explicit go for that run
// (changes/CONTEXT.md, safety rules).
//
// Playwright comes from frontend/ground-station/node_modules (no new
// dependency); --playwright or $DIRECSUN_PLAYWRIGHT names another
// playwright/index.mjs. The UI runs in the installed Google Chrome, on the GPU
// (ANGLE on Vulkan), as an operator's browser does. With headless Chrome's
// default software WebGL (SwiftShader) the ground station's 3D view takes the
// page's whole main thread once the heading moves, and the page stops
// answering (measured 2026-10-11: 15.6 s of main-thread work in 15.7 s, of
// which 0.08 s was script; on the GPU, 1.1 s in 4 s).

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SCENARIOS = new Set(["turn", "stop", "estop", "watch", "hidden"]);
const GPU_ARGS = ["--use-angle=vulkan", "--enable-features=Vulkan", "--ignore-gpu-blocklist", "--enable-gpu"];

function parseArgs(argv) {
  const args = { turns: 4, headed: false, realBoxGo: false };
  for (let i = 0; i < argv.length; i++) {
    const key = argv[i];
    const value = () => {
      if (i + 1 >= argv.length) throw new Error(`${key} needs a value`);
      return argv[++i];
    };
    if (key === "--ui") args.ui = value();
    else if (key === "--box") args.box = value();
    else if (key === "--scenario") args.scenario = value();
    else if (key === "--out") args.out = value();
    else if (key === "--turns") args.turns = Number(value());
    else if (key === "--playwright") args.playwright = value();
    else if (key === "--chrome") args.chrome = value();
    else if (key === "--headed") args.headed = true;
    else if (key === "--real-box-go") args.realBoxGo = true;
    else throw new Error(`unknown argument ${key}`);
  }
  if (!args.ui || !args.box || !args.out || !SCENARIOS.has(args.scenario)) {
    throw new Error("usage: ui-turn-recorder.mjs --ui URL --box WS_URL --scenario turn|stop|estop|watch|hidden --out DIR");
  }
  if (!Number.isInteger(args.turns) || args.turns < 1) throw new Error("--turns must be a positive integer");
  return args;
}

// Google Chrome where it is installed (macOS, Windows, most Linux desktops).
// Otherwise the Chromium that Playwright ships or $PLAYWRIGHT_BROWSERS_PATH holds.
// Headed Chrome on a Linux box without a display needs xvfb-run: the hidden
// scenario cannot run headless, because headless Chrome never hides a page.
async function launchBrowser(chromium, args) {
  const options = { headless: !args.headed, args: GPU_ARGS };
  if (args.chrome) return chromium.launch({ ...options, executablePath: args.chrome });
  try {
    return await chromium.launch({ ...options, channel: "chrome" });
  } catch (error) {
    if (process.platform !== "linux") throw error;
    console.log("ui-turn-recorder: Google Chrome not found, using Playwright's Chromium");
    return chromium.launch(options);
  }
}

// A browser that will not close must not keep a failed run alive.
const closeBrowser = (browser) =>
  Promise.race([browser.close().catch(() => {}), new Promise((resolve) => setTimeout(resolve, 10000))]);

// Exact hosts only: a name that merely starts with 127. (127.box.example) is not loopback.
const isLoopback = (url) => ["127.0.0.1", "localhost", "[::1]", "::1"].includes(new URL(url).hostname);
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (!isLoopback(args.box) && !args.realBoxGo) {
    console.error(
      `ui-turn-recorder: ${args.box} is not a loopback address, so this would arm the real box and spin the wheel ` +
        `(scenario ${args.scenario}). It runs only with --real-box-go, given with Julian's explicit go for this run.`,
    );
    process.exit(2);
  }
  if (!isLoopback(args.box)) {
    console.log(`ui-turn-recorder: arming the real box at ${args.box} for scenario ${args.scenario} (--real-box-go given)`);
  }

  const playwrightPath =
    args.playwright ??
    process.env.DIRECSUN_PLAYWRIGHT ??
    path.join(REPO, "frontend", "ground-station", "node_modules", "playwright", "index.mjs");
  const { chromium } = await import(pathToFileURL(playwrightPath).href);

  fs.mkdirSync(args.out, { recursive: true });
  const framesOut = fs.createWriteStream(path.join(args.out, "frames.jsonl"));
  const timelineOut = fs.createWriteStream(path.join(args.out, "timeline.jsonl"));
  const t0 = performance.now();
  const now = () => Math.round((performance.now() - t0) * 1000) / 1000;
  const meta = {
    scenario: args.scenario,
    ui: args.ui,
    box: args.box,
    expected_refusals: args.scenario === "estop" ? ["ramp-down in progress"] : [],
    slews_interrupted: args.scenario === "stop" || args.scenario === "estop",
    results: [],
  };
  const failures = [];
  const lastPing = {};
  let recording = true;  // off before the end screenshots, which stall the pages' timers
  const watchTlm = [];
  const latestTlm = {};

  const browser = await launchBrowser(chromium, args);
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });

  async function openPage(name) {
    const page = await context.newPage();
    page.on("websocket", (ws) => {
      if (ws.url() !== args.box) return;
      const record = (dir) => (f) => {
        if (!recording) return;
        let frame;
        try {
          frame = JSON.parse(String(f.payload));
        } catch {
          frame = { unparsed: String(f.payload) };
        }
        const t = now();
        if (dir === "tx" && frame.type === "ping") lastPing[name] = t;
        if (dir === "rx" && frame.type === "tlm") latestTlm[name] = frame;
        if (name === "watch" && dir === "rx" && frame.type === "tlm") watchTlm.push({ t, frame });
        framesOut.write(`${JSON.stringify({ t, page: name, dir, frame })}\n`);
      };
      ws.on("framesent", record("tx"));
      ws.on("framereceived", record("rx"));
    });
    // The status and result lines, as the operator sees them change.
    let stopped = false;
    let last = "";
    (async () => {
      while (!stopped && !page.isClosed()) {
        try {
          const seen = await page.evaluate(() => ({
            status: document.querySelector(".controls__status")?.textContent ?? null,
            result: document.querySelector(".controls__result")?.textContent ?? null,
          }));
          const key = JSON.stringify(seen);
          if (key !== last) {
            last = key;
            timelineOut.write(`${JSON.stringify({ t: now(), page: name, ...seen })}\n`);
          }
        } catch {
          // The page is navigating or closing.
        }
        await sleep(50);
      }
    })();
    page.stopTimeline = () => {
      stopped = true;
    };
    await page.goto(args.ui);
    await page.getByRole("textbox", { name: "DEVICE LINK / WEBSOCKET" }).fill(args.box);
    await page.getByRole("button", { name: "Connect" }).click();
    await page.getByText(/DEVICE CONNECTED/).waitFor({ timeout: 10000 });
    return page;
  }

  const status = (page) => page.locator(".controls__status").textContent();
  const result = async (page) => ((await page.locator(".controls__result").count()) ? page.locator(".controls__result").textContent() : "");
  const waitStatus = (page, text, timeout) =>
    page.waitForFunction((want) => document.querySelector(".controls__status")?.textContent === want, text, { timeout });
  const waitResult = (page, pattern, timeout) =>
    page.waitForFunction(
      (source) => new RegExp(source).test(document.querySelector(".controls__result")?.textContent ?? ""),
      pattern.source,
      { timeout },
    );

  async function arm(page) {
    await page.getByRole("button", { name: "Arm the box" }).click();
    await page.getByRole("group", { name: "Confirm arming" }).getByRole("checkbox").check();
    await page.getByRole("button", { name: "Arm now" }).click();
  }
  async function wheelStopped(name) {
    // As an operator would: arm only once any ramp-down has ended, which the
    // box shows as SAFE at throttle 0 (it refuses arm during the ramp).
    const deadline = Date.now() + 8000;
    while (Date.now() < deadline) {
      const t = latestTlm[name];
      if (t && t.mode === "SAFE" && t.throttle === 0) return;
      await sleep(50);
    }
    throw new Error(`${name}: the box did not show SAFE at throttle 0 within 8 s`);
  }
  async function armToReady(page, name = "operator") {
    await wheelStopped(name);
    await arm(page);
    await waitStatus(page, "READY / DEVICE", 20000);
  }
  async function turn(page, direction) {
    const label = direction > 0 ? "Rotate right ninety degrees and return" : "Rotate left ninety degrees and return";
    await page.getByRole("button", { name: label }).click();
    await waitResult(page, /^TURN (COMPLETE|STOPPED)/, 30000);
    const text = await result(page);
    meta.results.push({ t: now(), turn: direction > 0 ? "right" : "left", result: text });
    return text;
  }
  async function midSlew(page) {
    // The SLEW is under way once the status shows it: the box's own tlm says SLEW.
    await page.waitForFunction(() => /^SLEW \/ DEVICE/.test(document.querySelector(".controls__status")?.textContent ?? ""), null, {
      timeout: 5000,
    });
    await sleep(500);
  }
  async function disarm(page) {
    await page.getByRole("button", { name: "Disarm the box" }).click();
    await waitStatus(page, "SAFE / DEVICE", 3000);
  }

  let watch = null;
  try {
    if (args.scenario === "watch") watch = await openPage("watch");
    const operator = await openPage("operator");
    await armToReady(operator);

    if (args.scenario === "turn") {
      for (let i = 0; i < args.turns; i++) {
        const text = await turn(operator, i % 2 === 0 ? 1 : -1);
        console.log(`ui-turn-recorder: turn ${i + 1} ${i % 2 === 0 ? "right" : "left"}: ${text}`);
        if (!text.startsWith("TURN COMPLETE")) failures.push(`turn ${i + 1}: ${text}`);
      }
      await disarm(operator);
    } else if (args.scenario === "stop") {
      await operator.getByRole("button", { name: "Rotate right ninety degrees and return" }).click();
      await midSlew(operator);
      await operator.getByRole("button", { name: "Stop the turn" }).click();
      await waitResult(operator, /^TURN STOPPED/, 5000);
      await waitStatus(operator, "READY / DEVICE", 5000);
      const text = await result(operator);
      meta.results.push({ t: now(), turn: "stop", result: text });
      console.log(`ui-turn-recorder: stop: ${text}`);
      await sleep(1500);  // the box comes to rest on the rate loop
      await disarm(operator);
    } else if (args.scenario === "estop") {
      await operator.getByRole("button", { name: "Rotate right ninety degrees and return" }).click();
      await midSlew(operator);
      await operator.evaluate(() => (document.activeElement instanceof HTMLElement ? document.activeElement.blur() : null));
      await operator.keyboard.press("Space");
      await waitStatus(operator, "SAFE / DEVICE", 3000);
      // Arm again at once, inside the 5 s ramp-down.
      await arm(operator);
      await waitResult(operator, /^ARM /, 3000);
      const text = await result(operator);
      meta.results.push({ t: now(), turn: "re-arm", result: text });
      console.log(`ui-turn-recorder: re-arm inside the ramp-down: ${text}`);
      if (text !== "ARM REFUSED: ramp-down in progress") failures.push(`re-arm: ${text}`);
    } else if (args.scenario === "hidden") {
      const cover = await context.newPage();
      await cover.goto("about:blank");
      await cover.bringToFront();
      await sleep(1000);
      const visibility = await operator.evaluate(() => document.visibilityState);
      meta.hidden = { visibility, from_ms: now() };
      await sleep(30000);
      meta.hidden.until_ms = now();
      await operator.bringToFront();
      await sleep(1000);
      const text = await status(operator);
      meta.results.push({ t: now(), turn: "hidden", result: text });
      console.log(`ui-turn-recorder: the ground station was ${visibility} for 30 s behind another tab; now ${text}`);
      if (visibility !== "hidden") failures.push(`the page stayed ${visibility} behind another tab: run with --headed`);
      if (text !== "READY / DEVICE") failures.push(`after 30 s behind another tab the status is ${text}`);
      await disarm(operator);
    } else if (args.scenario === "watch") {
      const text = await turn(operator, 1);
      console.log(`ui-turn-recorder: watched turn: ${text}`);
      if (!text.startsWith("TURN COMPLETE")) failures.push(`watched turn: ${text}`);
      await watch.getByRole("button", { name: "Emergency stop the box" }).click();
      await waitStatus(operator, "SAFE / DEVICE", 3000);
      await sleep(5500);  // the ramp-down
      await armToReady(operator);
      await sleep(1000);
      operator.stopTimeline();
      const closedAt = now();
      await operator.close();
      const operatorLastPing = lastPing.operator;
      const deadline = Date.now() + 4000;
      let lost = null;
      while (Date.now() < deadline && lost === null) {
        lost = watchTlm.find((r) => r.t > closedAt && r.frame.mode === "SAFE" && (r.frame.flags ?? []).includes("HEARTBEAT_LOST")) ?? null;
        await sleep(50);
      }
      if (lost === null) {
        failures.push("the watch page saw no SAFE tlm with HEARTBEAT_LOST within 4 s of the operator page closing");
      } else {
        meta.heartbeat_lost_after_ms = Math.round(lost.t - operatorLastPing);
        console.log(`ui-turn-recorder: HEARTBEAT_LOST in the watch page's tlm ${meta.heartbeat_lost_after_ms} ms after the operator's last ping`);
      }
    }
  } catch (error) {
    failures.push(String(error).split("\n").slice(0, 3).join(" | "));
  } finally {
    recording = false;
    for (const [i, page] of context.pages().entries()) {
      await page.screenshot({ path: path.join(args.out, `end-${i}.png`), fullPage: true }).catch(() => {});
    }
    for (const page of context.pages()) page.stopTimeline?.();
    await closeBrowser(browser);
    meta.failures = failures;
    fs.writeFileSync(path.join(args.out, "meta.json"), `${JSON.stringify(meta, null, 2)}\n`);
    await new Promise((resolve) => framesOut.end(resolve));
    await new Promise((resolve) => timelineOut.end(resolve));
  }
  for (const f of failures) console.error(`ui-turn-recorder: FAILED, ${f}`);
  console.log(`ui-turn-recorder: ${failures.length ? "FAILED" : "done"}, capture in ${args.out}`);
  process.exit(failures.length ? 1 : 0);
}

main().catch((error) => {
  console.error(`ui-turn-recorder: ${error.message}`);
  process.exit(64);
});
