import { cp, mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const sourceDirectory = dirname(fileURLToPath(import.meta.resolve("@ffmpeg/core")));
const targetDirectory = resolve("web/public/ffmpeg");

await mkdir(targetDirectory, { recursive: true });
await cp(resolve(sourceDirectory, "ffmpeg-core.js"), resolve(targetDirectory, "ffmpeg-core.js"));
await cp(resolve(sourceDirectory, "ffmpeg-core.wasm"), resolve(targetDirectory, "ffmpeg-core.wasm"));

console.log("FFmpeg core copied to web/public/ffmpeg");
