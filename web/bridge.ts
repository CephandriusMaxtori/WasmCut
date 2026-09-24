import { FFmpeg } from "@ffmpeg/ffmpeg";

type CwrapFunction = (...args: unknown[]) => unknown;

type WasmcutModule = {
  cwrap: (name: string, returnType: string | null, argumentTypes: string[]) => CwrapFunction;
};

type WasmcutBridge = {
  requestImport: () => void;
  requestProbe: () => Promise<void>;
  requestExport: (sourceIn: number, sourceOut: number) => Promise<void>;
  addMediaToTimeline: (timelineSeconds?: number) => void;
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
const addTimelineButton = document.querySelector<HTMLButtonElement>("#add-timeline-button");
const timelineDropTarget = document.querySelector<HTMLDivElement>("#timeline-drop-target");
const bridgeStatus = document.querySelector<HTMLDivElement>("#bridge-status");
const canvas = document.querySelector<HTMLCanvasElement>("#wasmcut-canvas");
const bridgeVideo = document.querySelector<HTMLVideoElement>("#bridge-video");
const bridgeThumbnail = document.querySelector<HTMLImageElement>("#bridge-thumbnail");

let activeFile: File | null = null;
let ffmpegLoad: Promise<FFmpeg> | null = null;
let thumbnailUrl: string | null = null;
let videoUrl: string | null = null;
let mediaJobInProgress = false;
let setAddMediaToTimeline: ((timelineSeconds?: number) => unknown) | null = null;
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

async function onFileSelected(file: File, addToTimeline = false, timelineSeconds = -1) {
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
  addTimelineButton?.classList.add("available");
  if (addToTimeline) {
    setAddMediaToTimeline?.(timelineSeconds);
  }
  updateStatus("Media loaded. Ready for an FFmpeg probe.");
}

async function requestProbe() {
  if (activeFile === null) {
    updateStatus("Import a media file first.");
    return;
  }
  if (mediaJobInProgress) {
    updateStatus("Another media job is already running.");
    return;
  }

  mediaJobInProgress = true;
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
  } finally {
    mediaJobInProgress = false;
  }
}

async function requestExport(sourceIn: number, sourceOut: number) {
  if (activeFile === null) {
    updateStatus("Import a media file before exporting.");
    return;
  }
  if (mediaJobInProgress) {
    updateStatus("Another media job is already running.");
    return;
  }

  const start = Math.max(0, sourceIn);
  const end = Math.max(start, sourceOut);
  const duration = end - start;
  if (duration <= 0) {
    updateStatus("The clip range is empty.");
    return;
  }

  mediaJobInProgress = true;
  const sourceExtension = fileExtension(activeFile);
  const outputExtension = sourceExtension === "mp4" || sourceExtension === "mov" || sourceExtension === "webm"
    ? sourceExtension
    : "webm";
  const inputName = `export-input-${Date.now()}.${sourceExtension}`;
  const outputName = `wasmcut-export-${Date.now()}.${outputExtension}`;
  try {
    setProgress?.(0.0);
    updateStatus("Loading ffmpeg.wasm for export...");
    const instance = await loadFFmpeg();
    updateStatus("Preparing media for export...");
    await instance.writeFile(inputName, new Uint8Array(await activeFile.arrayBuffer()));
    updateStatus(`Cutting ${outputExtension.toUpperCase()} export...`);
    await instance.exec([
      "-ss",
      start.toFixed(6),
      "-i",
      inputName,
      "-t",
      duration.toFixed(6),
      "-map",
      "0:v:0",
      "-map",
      "0:a:0?",
      "-c:v",
      "copy",
      "-c:a",
      "copy",
      "-f",
      outputExtension === "mp4" ? "mp4" : outputExtension === "mov" ? "mov" : "webm",
      outputName
    ]);
    const output = await instance.readFile(outputName);
    if (typeof output === "string") {
      throw new Error("FFmpeg returned text instead of video data");
    }
    const mimeType = outputExtension === "mp4" ? "video/mp4" : outputExtension === "mov" ? "video/quicktime" : "video/webm";
    const blob = new Blob([new Uint8Array(output) as unknown as BlobPart], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = outputName;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    setProgress?.(1.0);
    updateStatus(`Export complete: ${output.byteLength} bytes`);
    await instance.deleteFile(inputName);
    await instance.deleteFile(outputName);
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    setProgress?.(0.0);
    updateStatus(`Export failed: ${message}`);
  } finally {
    mediaJobInProgress = false;
  }
}

function timelineSecondsAt(clientX: number, clientY: number) {
  if (canvas === null) {
    return -1;
  }
  const rect = canvas.getBoundingClientRect();
  const x = clientX - rect.left;
  const y = clientY - rect.top;
  const timelineTop = rect.height > 440 ? 440 : rect.height;
  if (x < 300 || y < timelineTop || y > rect.height) {
    return -1;
  }
  return Math.max(0, (x - 300 - 110) / 80);
}

function hasDraggedFiles(event: DragEvent) {
  return event.dataTransfer?.types.includes("Files") ?? false;
}

function setTimelineDrag(active: boolean) {
  timelineDropTarget?.classList.toggle("active", active);
}

function syncTimelineDropTarget() {
  if (canvas === null || timelineDropTarget === null) {
    return;
  }
  const rect = canvas.getBoundingClientRect();
  const timelineTop = rect.height > 440 ? 440 : rect.height;
  timelineDropTarget.style.left = `${rect.left + 300}px`;
  timelineDropTarget.style.top = `${rect.top + timelineTop}px`;
  timelineDropTarget.style.width = `${Math.max(0, rect.width - 300)}px`;
  timelineDropTarget.style.height = `${Math.max(0, rect.height - timelineTop)}px`;
}

function handleTimelineDragEnter(event: DragEvent) {
  if (!hasDraggedFiles(event) || timelineSecondsAt(event.clientX, event.clientY) < 0) {
    return;
  }
  event.preventDefault();
  if (event.dataTransfer !== null) {
    event.dataTransfer.dropEffect = "copy";
  }
  syncTimelineDropTarget();
  setTimelineDrag(true);
}

function handleTimelineDragOver(event: DragEvent) {
  if (!hasDraggedFiles(event) || timelineSecondsAt(event.clientX, event.clientY) < 0) {
    return;
  }
  event.preventDefault();
  if (event.dataTransfer !== null) {
    event.dataTransfer.dropEffect = "copy";
  }
  syncTimelineDropTarget();
  setTimelineDrag(true);
}

function handleTimelineDragLeave(event: DragEvent) {
  if (event.relatedTarget !== null && timelineSecondsAt(event.clientX, event.clientY) >= 0) {
    return;
  }
  setTimelineDrag(false);
}

async function handleTimelineDrop(event: DragEvent) {
  const timelineSeconds = timelineSecondsAt(event.clientX, event.clientY);
  if (!hasDraggedFiles(event) || timelineSeconds < 0) {
    return;
  }
  event.preventDefault();
  setTimelineDrag(false);
  const file = event.dataTransfer?.files[0];
  if (file !== undefined) {
    await onFileSelected(file, true, timelineSeconds);
  }
}

export function createBridge(): WasmcutBridge {
  const bridge: WasmcutBridge = {
    requestImport: () => fileInput?.click(),
    requestProbe,
    requestExport,
    addMediaToTimeline: (timelineSeconds = -1) => setAddMediaToTimeline?.(timelineSeconds),
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
      setAddMediaToTimeline = module.cwrap("wasmcut_add_media_to_timeline", null, ["number"]) as (
        timelineSeconds?: number
      ) => unknown;
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
  addTimelineButton?.addEventListener("click", () => {
    setAddMediaToTimeline?.(-1);
  });
  window.addEventListener("dragenter", handleTimelineDragEnter);
  window.addEventListener("dragover", handleTimelineDragOver);
  window.addEventListener("dragleave", handleTimelineDragLeave);
  window.addEventListener("drop", (event) => void handleTimelineDrop(event));
  window.addEventListener("resize", syncTimelineDropTarget);
  syncTimelineDropTarget();
  bridgeVideo?.addEventListener("loadedmetadata", reportVideoTime);
  bridgeVideo?.addEventListener("timeupdate", reportVideoTime);
  bridgeVideo?.addEventListener("seeked", reportVideoTime);
  bridgeVideo?.addEventListener("play", reportVideoState);
  bridgeVideo?.addEventListener("pause", reportVideoState);
  bridgeVideo?.addEventListener("ended", reportVideoState);

  return bridge;
}

export type { WasmcutBridge, WasmcutModule };
