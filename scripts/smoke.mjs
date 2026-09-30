// Browser smoke test for Wasmcut.
//
// Serves dist/ over HTTP, drives headless Chrome through the DevTools protocol
// and asserts on real UI state, so the WASM/JS boundary is exercised the same way
// a user would. Run `npm run build` first.
//
//   node scripts/smoke.mjs [--headful] [--keep-open]

import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { createReadStream, existsSync, mkdtempSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { extname, join, normalize, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const projectRoot = resolve(fileURLToPath(new URL(".", import.meta.url)), "..");
const distDirectory = join(projectRoot, "dist");
const headful = process.argv.includes("--headful");
const screenshotIndex = process.argv.indexOf("--screenshot");
const screenshotPath = screenshotIndex >= 0 ? (process.argv[screenshotIndex + 1] ?? null) : null;

const chromeCandidates = [
  process.env.CHROME_PATH,
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
  "/usr/bin/google-chrome",
  "/usr/bin/chromium",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
].filter((value) => typeof value === "string");

const contentTypes = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".wasm": "application/wasm",
  ".ttf": "font/ttf",
  ".webm": "video/webm",
  ".mp4": "video/mp4",
  ".svg": "image/svg+xml",
  ".png": "image/png"
};

function startServer() {
  const server = createServer((request, response) => {
    const requested = decodeURIComponent((request.url ?? "/").split("?")[0]);
    const relative = normalize(requested === "/" ? "index.html" : requested.replace(/^\/+/, ""));
    const file = join(distDirectory, relative);
    if (!file.startsWith(distDirectory) || !existsSync(file) || !statSync(file).isFile()) {
      response.writeHead(404).end("not found");
      return;
    }
    response.writeHead(200, {
      "content-type": contentTypes[extname(file)] ?? "application/octet-stream",
      "cache-control": "no-store"
    });
    createReadStream(file).pipe(response);
  });
  return new Promise((resolveServer) => {
    server.listen(0, "127.0.0.1", () => resolveServer(server));
  });
}

function launchChrome(port, profileDirectory) {
  const executable = chromeCandidates.find((value) => existsSync(value));
  if (executable === undefined) {
    throw new Error("No Chrome installation found. Set CHROME_PATH to run the smoke test.");
  }
  const arguments_ = [
    `--remote-debugging-port=${port}`,
    `--user-data-dir=${profileDirectory}`,
    "--no-first-run",
    "--no-default-browser-check",
    "--disable-gpu",
    "--use-gl=swiftshader",
    "--enable-unsafe-swiftshader",
    "--autoplay-policy=no-user-gesture-required",
    "--window-size=1600,1000",
    "about:blank"
  ];
  if (!headful) {
    arguments_.unshift("--headless=new");
  }
  const child = spawn(executable, arguments_, { stdio: "ignore" });
  child.unref();
  return child;
}

async function fetchJson(url, attempts = 100) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    try {
      const response = await fetch(url);
      if (response.ok) {
        return await response.json();
      }
    } catch {
      // The debugger port is not listening yet.
    }
    await new Promise((resolveDelay) => setTimeout(resolveDelay, 200));
  }
  throw new Error(`Timed out waiting for ${url}`);
}

async function waitForPageTarget(port) {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    try {
      const targets = await fetchJson(`http://127.0.0.1:${port}/json/list`);
      const page = targets.find((entry) => entry.type === "page" && entry.webSocketDebuggerUrl !== undefined);
      if (page !== undefined) {
        return page;
      }
    } catch {
      // Chrome has not published its target list yet.
    }
    await new Promise((resolveDelay) => setTimeout(resolveDelay, 200));
  }
  throw new Error("Chrome never exposed a page target");
}

class DevTools {
  constructor(socket) {
    this.socket = socket;
    this.nextId = 0;
    this.pending = new Map();
    socket.addEventListener("message", (event) => {
      const message = JSON.parse(event.data);
      const resolver = this.pending.get(message.id);
      if (resolver !== undefined) {
        this.pending.delete(message.id);
        resolver(message);
      }
    });
  }

  static async connect(url) {
    const socket = new WebSocket(url);
    await new Promise((resolveOpen, rejectOpen) => {
      socket.addEventListener("open", resolveOpen, { once: true });
      socket.addEventListener("error", rejectOpen, { once: true });
    });
    const devtools = new DevTools(socket);
    await devtools.send("Runtime.enable");
    await devtools.send("Page.enable");
    return devtools;
  }

  send(method, params = {}) {
    return new Promise((resolveMessage) => {
      const id = this.nextId++;
      this.pending.set(id, resolveMessage);
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }

  async evaluate(expression) {
    const response = await this.send("Runtime.evaluate", {
      expression,
      awaitPromise: true,
      returnByValue: true
    });
    if (response.result?.exceptionDetails !== undefined) {
      throw new Error(response.result.exceptionDetails.exception?.description ?? "evaluation failed");
    }
    return response.result?.result?.value;
  }

  close() {
    this.socket.close();
  }
}

const checks = [];
function check(name, condition, detail = "") {
  checks.push({ name, ok: Boolean(condition), detail });
}

const harness = `
  const wait = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const status = () => document.querySelector("#bridge-status")?.textContent ?? "";
  async function waitForStatus(match, seconds = 90) {
    const deadline = Date.now() + seconds * 1000;
    while (Date.now() < deadline) {
      if (match(status())) {
        return status();
      }
      await wait(150);
    }
    return status();
  }
  async function makeTestVideo(seconds = 2) {
    const canvas = document.createElement("canvas");
    canvas.width = 320;
    canvas.height = 180;
    const context = canvas.getContext("2d");
    const recorder = new MediaRecorder(canvas.captureStream(30), { mimeType: "video/webm" });
    const chunks = [];
    recorder.ondataavailable = (event) => {
      if (event.data.size > 0) {
        chunks.push(event.data);
      }
    };
    const finished = new Promise((resolve) => {
      recorder.onstop = resolve;
    });
    recorder.start();
    const frames = Math.round(seconds * 30);
    for (let frame = 0; frame < frames; frame += 1) {
      context.fillStyle = "hsl(" + ((frame * 4) % 360) + ", 65%, 45%)";
      context.fillRect(0, 0, 320, 180);
      context.fillStyle = "#ffffff";
      context.font = "28px sans-serif";
      context.fillText("wasmcut " + frame, 16, 100);
      await wait(1000 / 30);
    }
    recorder.stop();
    await finished;
    return new Blob(chunks, { type: "video/webm" });
  }
  async function importFile(name, seconds = 2) {
    const file = new File([await makeTestVideo(seconds)], name, { type: "video/webm" });
    const transfer = new DataTransfer();
    transfer.items.add(file);
    const input = document.querySelector("#file-input");
    input.files = transfer.files;
    input.dispatchEvent(new Event("change", { bubbles: true }));
    await wait(800);
  }
  window.__wasmcutTest = { wait, status, waitForStatus, importFile, makeTestVideo };
`;

async function run() {
  if (!existsSync(distDirectory)) {
    throw new Error("dist/ is missing. Run `npm run build` before the smoke test.");
  }

  const server = await startServer();
  const { port } = server.address();
  const origin = `http://127.0.0.1:${port}`;

  const profileDirectory = mkdtempSync(join(tmpdir(), "wasmcut-smoke-"));
  const debugPort = 9222 + Math.floor(Math.random() * 400);
  const chrome = launchChrome(debugPort, profileDirectory);

  let devtools = null;
  try {
    const page = await waitForPageTarget(debugPort);
    devtools = await DevTools.connect(page.webSocketDebuggerUrl);

    const logs = [];
    devtools.socket.addEventListener("message", (event) => {
      const message = JSON.parse(event.data);
      if (message.method === "Runtime.consoleAPICalled") {
        logs.push(message.params.args.map((argument) => argument.value ?? argument.description).join(" "));
      }
    });
    await devtools.send("Runtime.enable");
    await devtools.send("Page.enable");
    await devtools.send(
      "Emulation.setDeviceMetricsOverride",
      { width: 1600, height: 1000, deviceScaleFactor: 1, mobile: false }
    );
    await devtools.send("Page.navigate", { url: origin });
    await devtools.evaluate(harness);

    const ready = await devtools.evaluate(
      'window.__wasmcutTest.waitForStatus((value) => value === "C++ module ready", 60)'
    );
    check("WASM module boots", ready === "C++ module ready", ready);

    const font = await devtools.evaluate('document.documentElement.dataset.font ?? ""');
    check("Space Grotesk installed", font === "space-grotesk", `data-font=${font}`);

    const cssFont = await devtools.evaluate(
      'document.fonts.check("17px \\"Space Grotesk\\"")'
    );
    check("CSS typeface ready", cssFont === true, `document.fonts.check=${cssFont}`);

    await devtools.evaluate('window.__wasmcutTest.importFile("smoke-a.webm")');
    const imported = await devtools.evaluate('window.__wasmcutTest.status()');
    check("Media import", imported.includes("Media loaded"), imported);

    const statusAfter = await devtools.evaluate("window.__wasmcutTest.status()");
    check("No runtime errors", logs.filter((line) => line.includes("ERROR")).length === 0, logs.join(" | "));

    if (screenshotPath !== null) {
      const capture = await devtools.send("Page.captureScreenshot", { format: "png" });
      writeFileSync(screenshotPath, Buffer.from(capture.result.data, "base64"));
      console.log(`screenshot: ${screenshotPath}`);
    }

    console.log(`status: ${statusAfter}`);
  } finally {
    devtools?.close();
    chrome.kill();
    server.close();
    // Chrome keeps writing to its profile for a moment after the kill signal.
    try {
      rmSync(profileDirectory, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
    } catch {
      // A leftover temp profile is harmless.
    }
  }

  let failed = 0;
  for (const { name, ok, detail } of checks) {
    if (!ok) {
      failed += 1;
    }
    console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || detail === "" ? "" : ` -> ${detail}`}`);
  }
  if (failed > 0) {
    process.exitCode = 1;
  }
}

await run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
