#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "model/project.hpp"
#include "platform/platform_bridge.hpp"

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#define WASMCUT_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define WASMCUT_KEEPALIVE
#endif

namespace {

struct AppState {
  bool has_media = false;
  std::string media_id;
  std::string file_name;
  double file_size = 0.0;
  double duration = 0.0;
  std::string clip_id;
  wasmcut::model::TimeUs playhead_us = 0;
  bool is_playing = false;
  std::string status = "No media loaded";
  float progress = 0.0f;
};

AppState state;
wasmcut::model::Project project;
int media_counter = 0;
SDL_Window* window = nullptr;
SDL_GLContext gl_context = nullptr;
bool running = true;
int viewport_width = 1280;
int viewport_height = 720;
GLuint preview_texture = 0;
bool preview_texture_initialized = false;

bool ensure_preview_texture() {
  if (preview_texture_initialized) {
    return preview_texture != 0;
  }
  glGenTextures(1, &preview_texture);
  if (preview_texture == 0) {
    return false;
  }
  glBindTexture(GL_TEXTURE_2D, preview_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  preview_texture_initialized = true;
  return true;
}

wasmcut::model::TimeUs timeline_time_for_source(
    const wasmcut::model::Clip& clip,
    wasmcut::model::TimeUs source_time) {
  if (source_time <= clip.source_in) {
    return clip.timeline_start;
  }
  if (source_time >= clip.source_out) {
    return clip.timeline_end();
  }
  const long double elapsed = static_cast<long double>(source_time - clip.source_in) / static_cast<long double>(clip.speed);
  const wasmcut::model::TimeUs elapsed_time = wasmcut::model::seconds_to_time(static_cast<double>(elapsed));
  return wasmcut::model::add_time_saturated(clip.timeline_start, elapsed_time);
}

struct EditorSnapshot {
  wasmcut::model::Project project;
  std::string media_id;
  std::string file_name;
  double file_size = 0.0;
  double duration = 0.0;
  std::string clip_id;
  wasmcut::model::TimeUs playhead_us = 0;
};

std::vector<EditorSnapshot> undo_history;
std::vector<EditorSnapshot> redo_history;

EditorSnapshot capture_snapshot() {
  return {
      project,
      state.media_id,
      state.file_name,
      state.file_size,
      state.duration,
      state.clip_id,
      state.playhead_us};
}

void restore_snapshot(const EditorSnapshot& snapshot) {
  project = snapshot.project;
  state.media_id = snapshot.media_id;
  state.file_name = snapshot.file_name;
  state.file_size = snapshot.file_size;
  state.duration = snapshot.duration;
  state.clip_id = snapshot.clip_id;
  state.playhead_us = snapshot.playhead_us;
  state.is_playing = false;
  wasmcut::platform::pause_video();
  state.progress = 0.0f;

  const wasmcut::model::MediaAsset* asset = project.find_media(state.media_id);
  state.has_media = asset != nullptr;
  if (asset != nullptr) {
    state.file_name = asset->file_name;
    state.file_size = static_cast<double>(asset->file_size);
    state.duration = wasmcut::model::time_to_seconds(asset->duration);
  } else {
    state.media_id.clear();
    state.file_name.clear();
    state.file_size = 0.0;
    state.duration = 0.0;
  }

  if (project.find_clip(state.clip_id) == nullptr) {
    state.clip_id.clear();
  }
}

void record_history() {
  undo_history.push_back(capture_snapshot());
  if (undo_history.size() > 100) {
    undo_history.erase(undo_history.begin());
  }
  redo_history.clear();
}

void undo_project() {
  if (undo_history.empty()) {
    state.status = "Nothing to undo";
    return;
  }
  redo_history.push_back(capture_snapshot());
  const EditorSnapshot snapshot = undo_history.back();
  undo_history.pop_back();
  restore_snapshot(snapshot);
  state.status = "Undo";
}

void redo_project() {
  if (redo_history.empty()) {
    state.status = "Nothing to redo";
    return;
  }
  undo_history.push_back(capture_snapshot());
  const EditorSnapshot snapshot = redo_history.back();
  redo_history.pop_back();
  restore_snapshot(snapshot);
  state.status = "Redo";
}

int clip_counter = 0;

void add_media_to_timeline() {
  if (!state.has_media || state.media_id.empty()) {
    state.status = "Import media before adding a clip";
    return;
  }

  wasmcut::model::MediaAsset* asset = project.find_media(state.media_id);
  if (asset == nullptr || asset->duration <= 0) {
    state.status = "Media duration is unavailable";
    return;
  }

  if (!state.clip_id.empty() && project.find_clip(state.clip_id) != nullptr) {
    state.status = "Clip is already on the timeline";
    return;
  }

  wasmcut::model::Clip clip;
  clip.id = "clip-" + std::to_string(++clip_counter);
  clip.media_id = asset->id;
  clip.name = asset->name;
  clip.timeline_start = 0;
  if (!clip.set_source_range(0, asset->duration)) {
    state.status = "Unable to create clip range";
    return;
  }

  const EditorSnapshot before = capture_snapshot();
  record_history();
  if (project.create_clip(clip, "track-video") == nullptr) {
    restore_snapshot(before);
    undo_history.pop_back();
    state.status = "Unable to add clip to Video 1";
    return;
  }

  state.clip_id = clip.id;
  state.playhead_us = 0;
  state.is_playing = false;
  wasmcut::platform::pause_video();
  wasmcut::platform::seek_video(0.0);
  state.status = "Clip added to Video 1";
}

wasmcut::model::Track* find_track_for_clip(const std::string& clip_id) {
  for (wasmcut::model::Track& track : project.tracks) {
    if (track.find_clip(clip_id) != nullptr) {
      return &track;
    }
  }
  return nullptr;
}

void split_selected_clip() {
  wasmcut::model::Clip* selected = project.find_clip(state.clip_id);
  wasmcut::model::Track* track = find_track_for_clip(state.clip_id);
  if (selected == nullptr || track == nullptr) {
    state.status = "Select a clip to split";
    return;
  }
  if (!selected->contains_time(state.playhead_us) || state.playhead_us <= selected->timeline_start ||
      state.playhead_us >= selected->timeline_end()) {
    state.status = "Place the playhead inside the selected clip";
    return;
  }

  const wasmcut::model::Clip original = *selected;
  const wasmcut::model::TimeUs source_split = selected->source_time_at(state.playhead_us);
  if (source_split <= original.source_in || source_split >= original.source_out) {
    state.status = "Unable to split at the playhead";
    return;
  }

  const std::string left_id = original.id + "-a";
  const std::string right_id = original.id + "-b";
  if (project.find_clip(left_id) != nullptr || project.find_clip(right_id) != nullptr) {
    state.status = "Split clip IDs already exist";
    return;
  }

  const EditorSnapshot before = capture_snapshot();
  record_history();
  project.remove_clip(original.id);

  wasmcut::model::Clip left = original;
  left.id = left_id;
  left.source_out = source_split;
  wasmcut::model::Clip right = original;
  right.id = right_id;
  right.timeline_start = state.playhead_us;
  right.source_in = source_split;

  if (project.create_clip(left, track->id) == nullptr || project.create_clip(right, track->id) == nullptr) {
    restore_snapshot(before);
    undo_history.pop_back();
    state.status = "Unable to split clip";
    return;
  }

  state.clip_id = right.id;
  state.is_playing = false;
  wasmcut::platform::pause_video();
  wasmcut::platform::seek_video(wasmcut::model::time_to_seconds(source_split));
  state.status = "Clip split at playhead";
}

void delete_selected_clip() {
  if (state.clip_id.empty() || project.find_clip(state.clip_id) == nullptr) {
    state.status = "Select a clip to delete";
    return;
  }
  record_history();
  project.remove_clip(state.clip_id);
  state.clip_id.clear();
  state.is_playing = false;
  wasmcut::platform::pause_video();
  state.status = "Clip deleted";
}

void trim_selected_left() {
  wasmcut::model::Clip* clip = project.find_clip(state.clip_id);
  if (clip == nullptr || !clip->contains_time(state.playhead_us) || state.playhead_us <= clip->timeline_start ||
      state.playhead_us >= clip->timeline_end()) {
    state.status = "Place the playhead inside the selected clip";
    return;
  }
  const wasmcut::model::TimeUs source_split = clip->source_time_at(state.playhead_us);
  if (source_split <= clip->source_in || source_split >= clip->source_out) {
    state.status = "Unable to trim clip";
    return;
  }
  record_history();
  const wasmcut::model::TimeUs source_out = clip->source_out;
  clip->set_source_range(source_split, source_out);
  clip->set_timeline_start(state.playhead_us);
  project.recalculate_duration();
  state.is_playing = false;
  wasmcut::platform::pause_video();
  wasmcut::platform::seek_video(wasmcut::model::time_to_seconds(source_split));
  state.status = "Trimmed clip left edge";
}

void trim_selected_right() {
  wasmcut::model::Clip* clip = project.find_clip(state.clip_id);
  if (clip == nullptr || !clip->contains_time(state.playhead_us) || state.playhead_us <= clip->timeline_start ||
      state.playhead_us >= clip->timeline_end()) {
    state.status = "Place the playhead inside the selected clip";
    return;
  }
  const wasmcut::model::TimeUs source_split = clip->source_time_at(state.playhead_us);
  if (source_split <= clip->source_in || source_split >= clip->source_out) {
    state.status = "Unable to trim clip";
    return;
  }
  record_history();
  const wasmcut::model::TimeUs source_in = clip->source_in;
  clip->set_source_range(source_in, source_split);
  project.recalculate_duration();
  state.is_playing = false;
  wasmcut::platform::pause_video();
  wasmcut::platform::seek_video(wasmcut::model::time_to_seconds(clip->source_in));
  state.status = "Trimmed clip right edge";
}

void pause_preview() {
  wasmcut::platform::pause_video();
  state.is_playing = false;
}

void seek_selected_preview() {
  const wasmcut::model::Clip* clip = project.find_clip(state.clip_id);
  if (clip == nullptr) {
    return;
  }
  wasmcut::model::TimeUs source_time = clip->source_time_at(state.playhead_us);
  if (source_time == 0 && state.playhead_us < clip->timeline_start) {
    source_time = clip->source_in;
  }
  wasmcut::platform::seek_video(wasmcut::model::time_to_seconds(source_time));
}

void handle_shortcuts() {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) {
    return;
  }
  const bool command = io.KeyCtrl || io.KeySuper;
  if (command && ImGui::IsKeyPressed(ImGuiKey_Z)) {
    if (io.KeyShift) {
      redo_project();
    } else {
      undo_project();
    }
  } else if (command && ImGui::IsKeyPressed(ImGuiKey_Y)) {
    redo_project();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_S)) {
    split_selected_clip();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
    delete_selected_clip();
  }
}

void draw_media_bin() {
  const float height = ImGui::GetIO().DisplaySize.y;
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(300.0f, height), ImGuiCond_Always);
  ImGui::Begin("Media Bin");

  if (ImGui::Button("Import media")) {
    wasmcut::platform::request_import();
  }

  ImGui::Separator();

  if (!state.has_media) {
    ImGui::TextWrapped("No media loaded.");
  } else {
    ImGui::TextWrapped("File: %s", state.file_name.c_str());
    ImGui::Text("Size: %.2f MB", state.file_size / (1024.0 * 1024.0));
    ImGui::Text("Duration: %.2f s", state.duration);
  }

  if (state.has_media && state.clip_id.empty() && ImGui::Button("Add to timeline")) {
    add_media_to_timeline();
  } else if (!state.clip_id.empty()) {
    ImGui::Text("Timeline clip: %s", state.clip_id.c_str());
  }

  ImGui::Text("Assets: %zu", project.media_assets.size());
  ImGui::End();
}

void draw_ffmpeg_panel() {
  const float width = ImGui::GetIO().DisplaySize.x;
  const float height = ImGui::GetIO().DisplaySize.y;
  ImGui::SetNextWindowPos(ImVec2(width - 360.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(360.0f, height < 260.0f ? height : 260.0f), ImGuiCond_Always);
  ImGui::Begin("FFmpeg.wasm");

  ImGui::TextWrapped("Status: %s", state.status.c_str());

  if (state.has_media && ImGui::Button("Run FFmpeg probe")) {
    wasmcut::platform::request_probe();
  }

  ImGui::ProgressBar(state.progress, ImVec2(-1.0f, 0.0f));

  ImGui::End();
}

void draw_preview() {
  const float width = ImGui::GetIO().DisplaySize.x;
  const float height = ImGui::GetIO().DisplaySize.y;
  ImGui::SetNextWindowPos(ImVec2(300.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width > 660.0f ? width - 660.0f : 360.0f, height < 440.0f ? height : 440.0f), ImGuiCond_Always);
  ImGui::Begin("Preview");

  if (state.clip_id.empty()) {
    ImGui::TextWrapped("No clip selected.");
  } else if (const wasmcut::model::Clip* clip = project.find_clip(state.clip_id); clip != nullptr) {
    ImGui::TextWrapped("Clip: %s", clip->name.c_str());
    ImGui::Text("Duration: %.2f s", wasmcut::model::time_to_seconds(clip->timeline_duration()));

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float image_height = available.y > 80.0f ? available.y - 70.0f : 100.0f;
    bool frame_uploaded = false;
    if (ensure_preview_texture()) {
      glBindTexture(GL_TEXTURE_2D, preview_texture);
      frame_uploaded = wasmcut::platform::upload_video_frame();
      glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (frame_uploaded) {
      ImGui::Image(
          ImTextureRef(preview_texture),
          ImVec2(available.x, image_height),
          ImVec2(0.0f, 1.0f),
          ImVec2(1.0f, 0.0f));
    } else {
      ImGui::TextWrapped("Video frame unavailable until the media is ready.");
      ImGui::Dummy(ImVec2(available.x, image_height));
    }

    if (ImGui::Button(state.is_playing ? "Pause" : "Play")) {
      if (state.is_playing) {
        pause_preview();
      } else {
        if (state.playhead_us >= clip->timeline_end()) {
          state.playhead_us = clip->timeline_start;
          seek_selected_preview();
        }
        wasmcut::platform::play_video();
        state.is_playing = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Go to start")) {
      state.playhead_us = clip->timeline_start;
      seek_selected_preview();
    }

    float preview_seconds = wasmcut::model::time_to_seconds(state.playhead_us);
    const float start_seconds = wasmcut::model::time_to_seconds(clip->timeline_start);
    float end_seconds = wasmcut::model::time_to_seconds(clip->timeline_end());
    if (end_seconds <= start_seconds) {
      end_seconds = start_seconds + 0.01f;
    }
    if (ImGui::SliderFloat("Position", &preview_seconds, start_seconds, end_seconds)) {
      state.playhead_us = wasmcut::model::seconds_to_time(preview_seconds);
      seek_selected_preview();
    }
    if (ImGui::Button("Export clip (remux)")) {
      pause_preview();
      wasmcut::platform::request_export(
          wasmcut::model::time_to_seconds(clip->source_in),
          wasmcut::model::time_to_seconds(clip->source_out));
    }
  } else {
    ImGui::TextWrapped("The selected clip is no longer available.");
  }

  ImGui::End();
}

void draw_timeline() {
  const float width = ImGui::GetIO().DisplaySize.x;
  const float height = ImGui::GetIO().DisplaySize.y;
  ImGui::SetNextWindowPos(ImVec2(300.0f, height > 440.0f ? 440.0f : height), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width > 300.0f ? width - 300.0f : 300.0f, height > 440.0f ? height - 440.0f : 180.0f), ImGuiCond_Always);
  ImGui::Begin("Timeline");

  ImGui::Text("Playhead: %.2f s", wasmcut::model::time_to_seconds(state.playhead_us));
  ImGui::TextWrapped("Click a clip to select it.");

  if (ImGui::Button("Undo")) {
    undo_project();
  }
  ImGui::SameLine();
  if (ImGui::Button("Redo")) {
    redo_project();
  }
  if (!state.clip_id.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Split at playhead")) {
      split_selected_clip();
    }
    ImGui::SameLine();
    if (ImGui::Button("Trim left")) {
      trim_selected_left();
    }
    ImGui::SameLine();
    if (ImGui::Button("Trim right")) {
      trim_selected_right();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
      delete_selected_clip();
    }
  }

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float surface_width = available.x > 100.0f ? available.x : 100.0f;
  const float surface_height = available.y > 80.0f ? available.y - 8.0f : 120.0f;
  ImGui::InvisibleButton("timeline_surface", ImVec2(surface_width, surface_height));
  const ImVec2 surface_min = ImGui::GetItemRectMin();
  const ImVec2 surface_max = ImGui::GetItemRectMax();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImU32 surface_color = IM_COL32(20, 23, 30, 255);
  const ImU32 ruler_color = IM_COL32(45, 51, 63, 255);
  const ImU32 track_color = IM_COL32(27, 31, 39, 255);
  const ImU32 clip_color = IM_COL32(46, 92, 150, 255);
  const ImU32 selected_clip_color = IM_COL32(64, 132, 196, 255);
  const ImU32 text_color = IM_COL32(220, 226, 235, 255);
  const float label_width = 110.0f;
  const float pixels_per_second = 80.0f;
  const float ruler_height = 24.0f;
  const float lane_height = 42.0f;

  draw_list->AddRectFilled(surface_min, surface_max, surface_color);
  draw_list->AddRectFilled(
      ImVec2(surface_min.x, surface_min.y),
      ImVec2(surface_max.x, surface_min.y + ruler_height),
      ruler_color);

  const double project_duration_seconds = wasmcut::model::time_to_seconds(project.duration());
  int max_seconds = project_duration_seconds >= 595.0 ? 600 : static_cast<int>(project_duration_seconds) + 5;
  if (max_seconds < 5) {
    max_seconds = 5;
  }

  for (int second = 0; second <= max_seconds; ++second) {
    const float x = surface_min.x + label_width + static_cast<float>(second) * pixels_per_second;
    if (x > surface_max.x) {
      break;
    }
    draw_list->AddLine(
        ImVec2(x, surface_min.y + ruler_height),
        ImVec2(x, surface_max.y),
        IM_COL32(55, 61, 73, 255));
    const std::string label = std::to_string(second) + "s";
    draw_list->AddText(ImVec2(x + 3.0f, surface_min.y + 4.0f), text_color, label.c_str());
  }

  for (std::size_t track_index = 0; track_index < project.tracks.size(); ++track_index) {
    const wasmcut::model::Track& track = project.tracks[track_index];
    const float lane_top = surface_min.y + ruler_height + static_cast<float>(track_index) * lane_height;
    const float lane_bottom = lane_top + lane_height;
    draw_list->AddRectFilled(
        ImVec2(surface_min.x, lane_top),
        ImVec2(surface_max.x, lane_bottom),
        track_color);
    draw_list->AddText(ImVec2(surface_min.x + 6.0f, lane_top + 13.0f), text_color, track.name.c_str());

    for (const wasmcut::model::Clip& clip : track.clips) {
      const double start_seconds = wasmcut::model::time_to_seconds(clip.timeline_start);
      const double duration_seconds = wasmcut::model::time_to_seconds(clip.timeline_duration());
      const float clip_x = surface_min.x + label_width + static_cast<float>(start_seconds) * pixels_per_second;
      const float clip_width = duration_seconds * pixels_per_second;
      if (clip_x > surface_max.x) {
        continue;
      }
      float visible_width = clip_width < 8.0f ? 8.0f : clip_width;
      if (clip_x + visible_width > surface_max.x) {
        visible_width = surface_max.x - clip_x;
      }
      const ImU32 color = state.clip_id == clip.id ? selected_clip_color : clip_color;
      draw_list->AddRectFilled(
          ImVec2(clip_x, lane_top + 4.0f),
          ImVec2(clip_x + visible_width, lane_bottom - 4.0f),
          color);
      if (visible_width > 40.0f) {
        draw_list->AddText(ImVec2(clip_x + 5.0f, lane_top + 13.0f), text_color, clip.name.c_str());
      }
    }
  }

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(surface_min, surface_max, true)) {
    const float mouse_x = ImGui::GetIO().MousePos.x;
    const float mouse_y = ImGui::GetIO().MousePos.y;
    if (mouse_x >= surface_min.x + label_width) {
      const double clicked_seconds = static_cast<double>(mouse_x - surface_min.x - label_width) / pixels_per_second;
      state.playhead_us = wasmcut::model::seconds_to_time(clicked_seconds);
      state.clip_id.clear();
      for (std::size_t track_index = 0; track_index < project.tracks.size(); ++track_index) {
        const wasmcut::model::Track& track = project.tracks[track_index];
        const float lane_top = surface_min.y + ruler_height + static_cast<float>(track_index) * lane_height;
        if (mouse_y >= lane_top && mouse_y <= lane_top + lane_height) {
          if (const wasmcut::model::Clip* clip = track.clip_at(state.playhead_us); clip != nullptr) {
            state.clip_id = clip->id;
          }
          break;
        }
      }
    }
  }

  const float playhead_x = surface_min.x + label_width + static_cast<float>(wasmcut::model::time_to_seconds(state.playhead_us)) * pixels_per_second;
  if (playhead_x >= surface_min.x + label_width && playhead_x <= surface_max.x) {
    draw_list->AddLine(
        ImVec2(playhead_x, surface_min.y),
        ImVec2(playhead_x, surface_max.y),
        IM_COL32(235, 93, 93, 255),
        2.0f);
  }

  ImGui::End();
}

void draw_ui() {
  draw_media_bin();
  draw_preview();
  draw_ffmpeg_panel();
  draw_timeline();
}

void main_loop() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type == SDL_QUIT) {
      running = false;
    }
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();

  int drawable_width = viewport_width;
  int drawable_height = viewport_height;
  SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
  if (drawable_width <= 0 || drawable_height <= 0) {
    drawable_width = viewport_width;
    drawable_height = viewport_height;
  }
  ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(drawable_width), static_cast<float>(drawable_height));
  ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

  ImGui::NewFrame();

  draw_ui();
  handle_shortcuts();

  ImGui::Render();

  glViewport(0, 0, drawable_width, drawable_height);
  glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(window);
}

}

extern "C" {

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasmcut_set_status(const char* value) {
  state.status = value != nullptr ? value : "";
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasmcut_set_media_info(const char* name, double size, double duration) {
  state.file_name = name != nullptr ? name : "";
  state.file_size = size;
  state.duration = duration;
  ++media_counter;
  state.media_id = "media-" + std::to_string(media_counter);
  state.clip_id.clear();
  state.playhead_us = 0;
  state.is_playing = false;
  state.progress = 0.0f;
  wasmcut::platform::pause_video();
  wasmcut::model::MediaAsset asset;
  asset.id = state.media_id;
  asset.name = state.file_name;
  asset.file_name = state.file_name;
  asset.file_size = size > 0.0 ? static_cast<std::uint64_t>(size) : 0;
  asset.duration = wasmcut::model::seconds_to_time(duration);
  record_history();
  project.upsert_media(asset);
  state.has_media = true;
  state.status = "Media loaded";
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasmcut_set_playback_time(double source_seconds) {
  const wasmcut::model::TimeUs source_time = wasmcut::model::seconds_to_time(source_seconds);
  const wasmcut::model::Clip* clip = project.find_clip(state.clip_id);
  state.playhead_us = clip == nullptr ? source_time : timeline_time_for_source(*clip, source_time);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasmcut_set_playback_state(int playing) {
  state.is_playing = playing != 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasmcut_set_progress(double progress) {
  if (progress < 0.0) {
    progress = 0.0;
  }
  if (progress > 1.0) {
    progress = 1.0;
  }
  state.progress = static_cast<float>(progress);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasmcut_set_viewport_size(int width, int height) {
  if (width > 0) {
    viewport_width = width;
  }
  if (height > 0) {
    viewport_height = height;
  }
}

}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
    return 1;
  }

#ifdef __EMSCRIPTEN__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  const char* glsl_version = "#version 300 es";
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  const char* glsl_version = "#version 330";
#endif

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  window = SDL_CreateWindow(
      "Wasmcut",
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      1280,
      720,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if (window == nullptr) {
    std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  gl_context = SDL_GL_CreateContext(window);
  if (gl_context == nullptr) {
    std::fprintf(stderr, "OpenGL context creation failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GL_MakeCurrent(window, gl_context);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();

  if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context)) {
    std::fprintf(stderr, "ImGui SDL2 initialization failed\n");
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
    std::fprintf(stderr, "ImGui OpenGL initialization failed\n");
    ImGui_ImplSDL2_Shutdown();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop(main_loop, 0, 1);
#else
  while (running) {
    main_loop();
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();
#endif

  return 0;
}
