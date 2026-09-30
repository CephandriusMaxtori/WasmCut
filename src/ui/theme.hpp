#pragma once

#include "imgui.h"

#include <string>

namespace wasmcut::ui {

// Editor palette. Single source of truth for every colour used by the panels
// and the hand-drawn timeline widget.
struct Palette {
  ImU32 background;
  ImU32 surface;
  ImU32 surface_raised;
  ImU32 border;
  ImU32 border_strong;
  ImU32 text;
  ImU32 text_muted;
  ImU32 accent;
  ImU32 accent_hover;
  ImU32 accent_active;
  ImU32 video_clip;
  ImU32 video_clip_selected;
  ImU32 audio_clip;
  ImU32 audio_clip_selected;
  ImU32 playhead;
  ImU32 snap_guide;
  ImU32 ruler;
  ImU32 ruler_tick;
  ImU32 grid_line;
  ImU32 warning;
  ImU32 danger;
  ImU32 success;
};

[[nodiscard]] const Palette& colors() noexcept;

// Applies the dark editor style. Safe to call more than once.
void apply_theme();

// Loads TTF faces from the WASM filesystem and rebuilds the ImGui font atlas so
// it always has Space Grotesk available. Called automatically once per frame
// until it succeeds, because the host may publish the font files at any point
// during startup.
void request_fonts();
void pump_font_load();
[[nodiscard]] bool fonts_ready() noexcept;

// Loads the given faces immediately. Returns true when the atlas was rebuilt.
bool load_fonts(const std::string& regular_path, const std::string& medium_path, const std::string& bold_path);

}
