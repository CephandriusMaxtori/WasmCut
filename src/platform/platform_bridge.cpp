#include "platform_bridge.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/val.h>
#endif

namespace wasmcut::platform {

void request_import() {
#ifdef __EMSCRIPTEN__
  const emscripten::val bridge = emscripten::val::global("wasmcutBridge");
  if (!bridge.isUndefined()) {
    bridge.call<void>("requestImport");
  }
#endif
}

void request_probe() {
#ifdef __EMSCRIPTEN__
  const emscripten::val bridge = emscripten::val::global("wasmcutBridge");
  if (!bridge.isUndefined()) {
    bridge.call<void>("requestProbe");
  }
#endif
}

void play_video() {
#ifdef __EMSCRIPTEN__
  const emscripten::val bridge = emscripten::val::global("wasmcutBridge");
  if (!bridge.isUndefined()) {
    bridge.call<void>("playVideo");
  }
#endif
}

void pause_video() {
#ifdef __EMSCRIPTEN__
  const emscripten::val bridge = emscripten::val::global("wasmcutBridge");
  if (!bridge.isUndefined()) {
    bridge.call<void>("pauseVideo");
  }
#endif
}

void seek_video(double seconds) {
#ifdef __EMSCRIPTEN__
  const emscripten::val bridge = emscripten::val::global("wasmcutBridge");
  if (!bridge.isUndefined()) {
    bridge.call<void>("seekVideo", seconds);
  }
#endif
}

bool upload_video_frame() {
#ifdef __EMSCRIPTEN__
  const emscripten::val bridge = emscripten::val::global("wasmcutBridge");
  if (!bridge.isUndefined()) {
    return bridge.call<bool>("uploadVideoFrame");
  }
#endif
  return false;
}

}
