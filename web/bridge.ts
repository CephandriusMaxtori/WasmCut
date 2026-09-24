import { FFmpeg } from "@ffmpeg/ffmpeg";

type CwrapFunction = (...args: unknown[]) => unknown;

type WasmcutModule = {
  cwrap: (name: string, returnType: string | null, argumentTypes: string[]) => CwrapFunction;
};

type WasmcutBridge = {
  requestImport: () => void;
  requestProbe: () => Promise<void>;
  playVideo: () => void;
  pauseVideo: () => void;
  seekVideo: (seconds: number) => void;
  uploadVideoFrame: () => boolean;
  attach: (module: WasmcutModule) => void;
};

declare global {
  interface Window {
    wasmcutBridge: WasmcutBridge;
  }
}

const fileInput = document.querySelector<HTMLInputElement>("#file-input");
const bridgeStatus = document.querySelector<HTMLDivElement>("#bridge-status");
const canvas = document.querySelector<HTMLCanvasElement>("#wasmcut-canvas");
const bridgeVideo = document.querySelector<HTMLVideoElement>("#bridge-video");
const bridgeThumbnail = document.querySelector<HTMLImageElement>("#bridge-thumbnail");

let activeFile: File | null = null;
let ffmpegLoad: Promise<FFmpeg> | null = null;
let thumbnailUrl: string | null = null;
let videoUrl: string | null = null;
let setStatus: ((value: string) => unknown) | null = null;
let setMediaInfo: ((name: string, size: number, duration: number) => unknown) | null = null;
let setProgress: ((value: number) => unknown) | null = null;
let setPlaybackTime: ((value: number) => unknown) | null = null;
let setPlaybackState: ((value: number) => unknown) | null = null;

function updateStatus(value: string) {
  if (bridgeStatus !== null) {
    bridgeStatus.textContent = value;
  }
  setStatus?.(value);
}

function assetUrl(path: string) {
  return new URL(path, new URL(import.meta.env.BASE_URL, window.location.href)).href;
}

function reportVideoTime() {
  if (bridgeVideo !== null) {
    setPlaybackTime?.(bridgeVideo.currentTime);
  }
}

function reportVideoState() {
  if (bridgeVideo !== null) {
    setPlaybackState?.(bridgeVideo.paused ? 0 : 1);
  }
}

function playVideo() {
  if (bridgeVideo === null) {
    updateStatus("Import media before playback");
    return;
  }
  void bridgeVideo.play().then(reportVideoState).catch((error: unknown) => {
    setPlaybackState?.(0);
    const message = error instanceof Error ? error.message : String(error);
    updateStatus(`Playback failed: ${message}`);
  });
}

function pauseVideo() {
  bridgeVideo?.pause();
  reportVideoState();
}

function seekVideo(seconds: number) {
  if (bridgeVideo === null) {
    return;
  }
  const maximum = Number.isFinite(bridgeVideo.duration) ? bridgeVideo.duration : seconds;
  const clamped = Math.max(0, Math.min(seconds, maximum));
  bridgeVideo.currentTime = clamped;
  setPlaybackTime?.(clamped);
}

function uploadVideoFrame() {
  const gl = canvas?.getContext("webgl2");
  if (gl === null || gl === undefined || bridgeVideo === null || bridgeVideo.readyState < 2 || bridgeVideo.videoWidth === 0) {
    return false;
  }
  const texture = gl.getParameter(gl.TEXTURE_BINDING_2D) as WebGLTexture | null;
  if (texture === null) {
    return false;
  }
  try {
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, bridgeVideo);
    return true;
  } catch {
    return false;
  }
}

function readDuration(file: File) {
  return new Promise<number>((resolve) => {
    const probe = document.createElement("video");
    const url = URL.createObjectURL(file);
    probe.preload = "metadata";
    probe.onloadedmetadata = () => {
      URL.revokeObjectURL(url);
      resolve(Number.isFinite(probe.duration) ? probe.duration : 0.0);
    };
    probe.onerror = () => {
      URL.revokeObjectURL(url);
      resolve(0.0);
    };
    probe.src = url;
  });
}

function fileExtension(file: File) {
  const extension = file.name.split(".").pop()?.replace(/[^a-z0-9]/gi, "") ?? "";
  return extension.length > 0 ? extension.toLowerCase() : "bin";
}

async function loadFFmpeg() {
  if (ffmpegLoad !== null) {
    return ffmpegLoad;
  }

  const instance = new FFmpeg();
  instance.on("log", ({ message }) => {
    updateStatus(`FFmpeg: ${message}`);
  });
  instance.on("progress", ({ progress }) => {
    setProgress?.(progress);
  });

  ffmpegLoad = instance
    .load({
      coreURL: assetUrl("ffmpeg/ffmpeg-core.js"),
      wasmURL: assetUrl("ffmpeg/ffmpeg-core.wasm")
    })
    .then(() => instance)
    .catch((error: unknown) => {
      ffmpegLoad = null;
      throw error;
    });

  return ffmpegLoad;
}

async function onFileSelected(file: File) {
  activeFile = file;
  const duration = await readDuration(file);
  if (bridgeVideo !== null) {
    if (videoUrl !== null) {
      URL.revokeObjectURL(videoUrl);
    }
    videoUrl = URL.createObjectURL(file);
    bridgeVideo.src = videoUrl;
    bridgeVideo.load();
    bridgeVideo.currentTime = 0;
  }
  setMediaInfo?.(file.name, file.size, duration);
  updateStatus("Media loaded. Ready for an FFmpeg probe.");
}

async function requestProbe() {
  if (activeFile === null) {
    updateStatus("Import a media file first.");
    return;
  }

  const inputName = `input-${Date.now()}.${fileExtension(activeFile)}`;
  const outputName = `probe-${Date.now()}.jpg`;

  try {
    updateStatus("Loading ffmpeg.wasm...");
    const instance = await loadFFmpeg();
    updateStatus("Copying media into the FFmpeg worker...");
    await instance.writeFile(inputName, new Uint8Array(await activeFile.arrayBuffer()));
    updateStatus("Running the FFmpeg probe...");
    await instance.exec([
      "-i",
      inputName,
      "-frames:v",
      "1",
      "-vf",
      "scale=320:-1",
      "-f",
      "image2",
      outputName
    ]);
    const output = await instance.readFile(outputName);
    if (typeof output === "string") {
      throw new Error("FFmpeg returned text instead of image data");
    }
    const blob = new Blob([new Uint8Array(output) as unknown as BlobPart], { type: "image/jpeg" });
    if (thumbnailUrl !== null) {
      URL.revokeObjectURL(thumbnailUrl);
    }
    thumbnailUrl = URL.createObjectURL(blob);
    if (bridgeThumbnail !== null) {
      bridgeThumbnail.src = thumbnailUrl;
      bridgeThumbnail.classList.add("visible");
    }
    setProgress?.(1.0);
    updateStatus(`FFmpeg probe complete: ${output.byteLength} bytes`);
    await instance.deleteFile(inputName);
    await instance.deleteFile(outputName);
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    setProgress?.(0.0);
    updateStatus(`FFmpeg probe failed: ${message}`);
  }
}

export function createBridge(): WasmcutBridge {
  const bridge: WasmcutBridge = {
    requestImport: () => fileInput?.click(),
    requestProbe,
    playVideo,
    pauseVideo,
    seekVideo,
    uploadVideoFrame,
    attach: (module: WasmcutModule) => {
      setStatus = module.cwrap("wasmcut_set_status", null, ["string"]) as (value: string) => unknown;
      setMediaInfo = module.cwrap("wasmcut_set_media_info", null, ["string", "number", "number"]) as (
        name: string,
        size: number,
        duration: number
      ) => unknown;
      setProgress = module.cwrap("wasmcut_set_progress", null, ["number"]) as (value: number) => unknown;
      setPlaybackTime = module.cwrap("wasmcut_set_playback_time", null, ["number"]) as (value: number) => unknown;
      setPlaybackState = module.cwrap("wasmcut_set_playback_state", null, ["number"]) as (value: number) => unknown;
    }
  };

  window.wasmcutBridge = bridge;
  fileInput?.addEventListener("change", () => {
    const file = fileInput.files?.[0];
    if (file !== undefined) {
      void onFileSelected(file);
    }
  });
  bridgeVideo?.addEventListener("loadedmetadata", reportVideoTime);
  bridgeVideo?.addEventListener("timeupdate", reportVideoTime);
  bridgeVideo?.addEventListener("seeked", reportVideoTime);
  bridgeVideo?.addEventListener("play", reportVideoState);
  bridgeVideo?.addEventListener("pause", reportVideoState);
  bridgeVideo?.addEventListener("ended", reportVideoState);

  return bridge;
}

export type { WasmcutBridge, WasmcutModule };
