import createWasmcutModule from "./generated/wasmcut.js";
import wasmUrl from "./generated/wasmcut.wasm?url";
import { createBridge, type WasmcutModule } from "./bridge";
import "./styles.css";

async function start() {
  const canvas = document.querySelector<HTMLCanvasElement>("#wasmcut-canvas");
  if (canvas === null) {
    throw new Error("The Wasmcut canvas is missing");
  }

  let setViewportSize: ((width: number, height: number) => unknown) | null = null;
  const resizeCanvas = () => {
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.floor(canvas.clientWidth * ratio));
    canvas.height = Math.max(1, Math.floor(canvas.clientHeight * ratio));
    setViewportSize?.(canvas.width, canvas.height);
  };

  resizeCanvas();
  window.addEventListener("resize", resizeCanvas);

  const bridge = createBridge();
  const module = await createWasmcutModule({
    canvas,
    locateFile: (path: string) => (path.endsWith(".wasm") ? wasmUrl : path),
    print: (...values: unknown[]) => console.log(...values),
    printErr: (...values: unknown[]) => console.error(...values)
  });

  const wasmModule = module as unknown as WasmcutModule;
  bridge.attach(wasmModule);
  setViewportSize = wasmModule.cwrap("wasmcut_set_viewport_size", null, ["number", "number"]) as (
    width: number,
    height: number
  ) => unknown;
  setViewportSize(canvas.width, canvas.height);
  const status = document.querySelector<HTMLDivElement>("#bridge-status");
  if (status !== null) {
    status.textContent = "C++ module ready";
  }
}

void start().catch((error: unknown) => {
  const status = document.querySelector<HTMLDivElement>("#bridge-status");
  if (status !== null) {
    status.textContent = error instanceof Error ? error.message : String(error);
  }
  console.error(error);
});
