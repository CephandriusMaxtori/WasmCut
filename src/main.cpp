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

#include <cstdio>
#include <string>

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

  ImGui::TextWrapped("Preview surface");
  ImGui::TextWrapped("The single-clip preview will be connected after the bridge spike.");

  ImGui::End();
}

void draw_timeline() {
  const float width = ImGui::GetIO().DisplaySize.x;
  const float height = ImGui::GetIO().DisplaySize.y;
  ImGui::SetNextWindowPos(ImVec2(300.0f, height > 440.0f ? 440.0f : height), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width > 300.0f ? width - 300.0f : 300.0f, height > 440.0f ? height - 440.0f : 180.0f), ImGuiCond_Always);
  ImGui::Begin("Timeline");

  ImGui::TextWrapped("Tracks: %zu", project.tracks.size());
  for (const wasmcut::model::Track& track : project.tracks) {
    ImGui::Text("%s (%s, %zu clips)", track.name.c_str(), wasmcut::model::track_type_name(track.type), track.clips.size());
  }
  ImGui::TextWrapped("Timeline model and clip interactions are next.");

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
  if (state.media_id.empty()) {
    ++media_counter;
    state.media_id = "media-" + std::to_string(media_counter);
  }
  wasmcut::model::MediaAsset asset;
  asset.id = state.media_id;
  asset.name = state.file_name;
  asset.file_name = state.file_name;
  asset.file_size = size > 0.0 ? static_cast<std::uint64_t>(size) : 0;
  asset.duration = wasmcut::model::seconds_to_time(duration);
  project.upsert_media(asset);
  state.has_media = true;
  state.status = "Media loaded";
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
