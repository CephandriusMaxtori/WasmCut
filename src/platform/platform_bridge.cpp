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

}
