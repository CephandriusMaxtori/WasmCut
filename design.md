# WASM Video Editor Design Document

**Stack:** C++ · Dear ImGui · Emscripten (WASM) · GitHub Pages

---

## 1. Overview & Goals

Build a **client-side non-linear video editor** that runs entirely in the browser as a single-page WebAssembly application.

- **Language / UI:** Pure C++ with Dear ImGui (immediate-mode GUI).
- **Compilation target:** Emscripten → `.wasm` + JS glue.
- **Hosting:** Static site on GitHub Pages (no backend).
- **Privacy & offline:** All media stays in the browser; no upload required.

**Core goals (MVP → stretch):**

- Import local video/audio files.
- Multi-track timeline (video + audio).
- Basic editing: cut, trim, move, delete clips.
- Real-time preview (WebGL).
- Simple effects / transitions.
- Export (re-encode or remux via FFmpeg-libav or WebCodecs).
- Responsive desktop-first UI that still works on large tablets.

---

## 2. High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Browser (GitHub Pages static site)                         │
│  ┌─────────────┐  ┌──────────────────────────────────────┐ │
│  │ index.html  │  │  main.js (Emscripten glue)           │ │
│  │ + canvas    │──┤  + Module (WASM)                     │ │
│  └─────────────┘  │                                      │ │
│                   │  C++ Application                     │ │
│                   │  ┌─────────────────────────────────┐ │ │
│                   │  │ App / Main Loop (emscripten)    │ │ │
│                   │  │  - ImGui (SDL2/WebGL backend)   │ │ │
│                   │  │  - Timeline / Project model     │ │ │
│                   │  │  - Media engine                 │ │ │
│                   │  │  - Preview renderer (WebGL)     │ │ │
│                   │  │  - Export pipeline              │ │ │
│                   │  └─────────────────────────────────┘ │ │
│                   └──────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Key layers

| Layer          | Responsibility                                      | Tech                                              |
|----------------|-----------------------------------------------------|---------------------------------------------------|
| UI             | Immediate-mode panels, docking, timeline interaction| Dear ImGui + ImGuiFileDialog / custom widgets     |
| Project model  | Tracks, clips, keyframes, undo/redo                 | Pure C++                                          |
| Media I/O      | Demux / decode / encode                             | FFmpeg (libav*) compiled to WASM **or** WebCodecs |
| Preview        | Real-time composition & playback                    | WebGL textures + audio via Web Audio API          |
| Persistence    | Project save / load, media cache                    | IndexedDB (via Emscripten FS / IDBFS) + local files |
| Hosting        | Static deployment                                   | GitHub Actions → `gh-pages` branch                |

---

## 3. Technology Choices & Rationale

### Dear ImGui

- Excellent Emscripten / WebGL examples already exist (official `example_emscripten_*`).
- Immediate-mode fits video-editor workflows (timeline scrubbing, property panels).
- Docking branch for professional layout.

### Emscripten

- Mature SDL2 + OpenGL/WebGL backend for ImGui.
- pthreads / SharedArrayBuffer support (with COOP/COEP headers).
- Easy virtual filesystem (MEMFS + IDBFS).

### Media processing

- **Preferred for quality:** Compile a slimmed FFmpeg (libavformat/libavcodec/libswscale/libswresample) with Emscripten.
- **Fallback / progressive enhancement:** WebCodecs API for hardware-accelerated decode/encode where available, with FFmpeg as software fallback.
- ByteDance-style architecture (C++ engine + WebGL/WebAudio) is a proven pattern.

### Hosting on GitHub Pages

- Pure static assets (`.html`, `.js`, `.wasm`, fonts, icons).
- Service Worker optional for offline caching of the WASM binary.
- Cross-Origin Isolation headers required for SharedArrayBuffer → use a GitHub Pages custom domain or Cloudflare Pages / Netlify with proper headers if needed. (GitHub Pages itself has limited header control; many projects use a thin Cloudflare Worker or switch to Cloudflare Pages.)

---

## 4. Core Components

### 4.1 Application Shell

- `main.cpp` – Emscripten main loop (`emscripten_set_main_loop` or the ImGui stub macros).
- SDL2 window + WebGL context.
- ImGui context + docking + style (dark video-editor theme).

### 4.2 Project & Timeline Model

```cpp
struct Clip {
    std::string id;
    std::string media_id;
    double timeline_start;   // seconds
    double source_in, source_out;
    // transform, opacity, speed, effects…
};

struct Track {
    std::string id;
    enum Type { Video, Audio };
    std::vector<Clip> clips;
};

struct Project {
    std::vector<Track> tracks;
    double duration;
    // undo stack, markers, etc.
};
```

### 4.3 Media Engine

- Media library (bin) that holds decoded metadata + optional thumbnail cache.
- Decoder pipeline (FFmpeg or WebCodecs) → RGBA frames + PCM audio.
- Frame cache / ring buffer for scrubbing.
- Audio mixer → Web Audio API.

### 4.4 Preview Renderer

- Off-screen WebGL framebuffer or texture atlas.
- Compose active video clips (blending, transforms, simple effects).
- Present to ImGui image widget or full-screen canvas.
- Audio clock drives video (or vice-versa).

### 4.5 Export Pipeline

- Render pipeline in a Web Worker (or pthread) to keep UI responsive.
- Encode via FFmpeg (libx264 / libvpx / etc.) or WebCodecs + muxer.
- Progress callback to ImGui.
- Final blob → browser download.

### 4.6 File System & Persistence

- Emscripten MEMFS for temporary files.
- IDBFS or OPFS for project + media cache.
- Drag-and-drop / file input via JS interop (`EM_ASM` or `emscripten::val`).

---

## 5. UI Layout (ImGui)

Typical docking layout:

```
┌────────────┬──────────────────────────────┬────────────┐
│ Media Bin  │         Preview              │ Properties │
│            │                              │            │
│            ├──────────────────────────────┤            │
│            │         Timeline             │            │
│            │  (tracks + playhead)         │            │
└────────────┴──────────────────────────────┴────────────┘
```

- Custom timeline widget (drawn with ImDrawList) for clips, waveforms, keyframes.
- Keyboard shortcuts (J/K/L, space, etc.).
- Modal dialogs for export settings, project properties.

---

## 6. Build & Deployment Pipeline

### Local / CI build

```bash
# Activate Emscripten
source emsdk_env.sh

# CMake or Makefile
emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DUSE_PTHREADS=ON -DALLOW_MEMORY_GROWTH=ON ...
emmake make -C build -j

# Output
# build/index.html  (or custom shell)
# build/video_editor.js
# build/video_editor.wasm
# build/video_editor.data (optional assets)
```

### Recommended CMake flags / Emscripten settings

- `-s USE_SDL=2 -s USE_WEBGL2=1 -s FULL_ES3=1`
- `-s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=512MB` (tune)
- `-s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=4` (for multi-threaded FFmpeg)
- `-s FORCE_FILESYSTEM=1`
- `-O3` + LTO for production
- `--shell-file custom_shell.html` (full-screen canvas, loading spinner, progress)

### GitHub Actions → GitHub Pages

1. On push to `main`:
   - Install Emscripten.
   - Build Release.
   - Upload artifacts to `gh-pages` branch (or use `peaceiris/actions-gh-pages`).
2. Optional: Cloudflare Pages for better headers / caching.

### Cross-Origin Isolation

Needed for SharedArrayBuffer / pthreads:

- Serve with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`.
- If pure GitHub Pages is insufficient, put a thin reverse-proxy (Cloudflare Worker) in front.

---

## 7. Performance & Constraints

| Concern                    | Mitigation                                                      |
|----------------------------|-----------------------------------------------------------------|
| WASM download size         | Slim FFmpeg build, Brotli/gzip, cache aggressively, optional progressive loading |
| Memory                     | ALLOW_MEMORY_GROWTH, careful frame caching, stream large files  |
| Single-thread vs multi-thread | Prefer pthreads for encode; fall back gracefully             |
| Mobile / low-end           | Limit preview resolution, disable heavy effects                 |
| File size limits           | Browser RAM is the hard limit; warn users early                 |

---

## 8. Development Roadmap

### Phase 1 – Skeleton (1–2 weeks)

- Emscripten + ImGui + SDL2/WebGL running on GitHub Pages.
- Empty docking UI + basic main loop.

### Phase 2 – Media Foundation

- File open (drag-drop).
- FFmpeg demux/decode → first frame display in ImGui.
- Simple single-clip timeline + play/pause.

### Phase 3 – Editing Core

- Multi-track, cut/trim/move, undo/redo.
- Waveform / thumbnail generation.
- Basic export (remux or re-encode).

### Phase 4 – Polish & Effects

- Transitions, color correction, text overlays.
- Project save/load (JSON + media references).
- Keyboard shortcuts, better timeline UX.

### Phase 5 – Optimization

- WebCodecs path, SIMD, better caching, Service Worker offline support.

---

## 9. Risks & Open Questions

- **FFmpeg WASM size & build complexity** – keep a minimal configuration; consider pre-built libav if possible.
- **SharedArrayBuffer headers** on pure GitHub Pages – may need Cloudflare or similar.
- **Audio/video sync** under variable browser load.
- **Browser codec support** vs software decode performance.
- Licensing: FFmpeg GPL vs LGPL builds carefully.

---

## 10. References & Starting Points

- Official ImGui Emscripten examples (`example_emscripten_opengl3`, WebGPU variant).
- ByteDance W3C talk: “A Non-linear Video Editor built with WebAssembly”.
- Existing pure-C++ ImGui video editors (desktop) for UI inspiration.
- FFmpeg-to-WASM build guides (slim libav builds).
- imgui_bundle / Hello ImGui for higher-level helpers if desired.

---

This design keeps the entire editor in C++ with a modern immediate-mode UI, leverages the proven Emscripten + WebGL path, and deploys as a pure static site on GitHub Pages (with minor header work for full multi-threading).
