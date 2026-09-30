#include "ui/theme.hpp"

#include <fstream>
#include <vector>

namespace wasmcut::ui {
namespace {

// Glyph coverage: ASCII plus the typographic characters the editor uses.
const ImWchar* glyph_ranges() {
  static const ImWchar ranges[] = {
      0x0020, 0x00ff,  // Basic Latin + Latin-1 Supplement
      0x0100, 0x017f,  // Latin Extended-A
      0x2010, 0x2027,  // Dashes and quotes
      0x2030, 0x205e,  // Per-mille, primes, ellipsis, references
      0x20a0, 0x20bf,  // Currency symbols
      0x2190, 0x21ff,  // Arrows
      0x2202, 0x22ff,  // Mathematical operators
      0x25a0, 0x25ff,  // Geometric shapes (bullet, play, stop markers)
      0xfb00, 0xfb06,  // Latin ligatures
      0,
  };
  return ranges;
}

const Palette& palette() noexcept {
  static const Palette value = {
      IM_COL32(14, 16, 20, 255),      // background
      IM_COL32(22, 25, 31, 255),      // surface
      IM_COL32(30, 34, 42, 255),      // surface_raised
      IM_COL32(44, 49, 59, 255),      // border
      IM_COL32(64, 71, 84, 255),      // border_strong
      IM_COL32(228, 233, 240, 255),   // text
      IM_COL32(146, 155, 170, 255),   // text_muted
      IM_COL32(232, 108, 76, 255),    // accent
      IM_COL32(244, 130, 99, 255),    // accent_hover
      IM_COL32(255, 158, 128, 255),   // accent_active
      IM_COL32(58, 104, 168, 255),    // video_clip
      IM_COL32(88, 148, 216, 255),    // video_clip_selected
      IM_COL32(62, 140, 116, 255),    // audio_clip
      IM_COL32(90, 178, 148, 255),    // audio_clip_selected
      IM_COL32(240, 96, 96, 255),     // playhead
      IM_COL32(244, 196, 92, 255),    // snap_guide
      IM_COL32(38, 43, 52, 255),      // ruler
      IM_COL32(96, 106, 122, 255),    // ruler_tick
      IM_COL32(46, 51, 61, 255),      // grid_line
      IM_COL32(232, 176, 76, 255),    // warning
      IM_COL32(226, 88, 88, 255),     // danger
      IM_COL32(104, 196, 132, 255),   // success
  };
  return value;
}

std::vector<unsigned char>* read_file(const std::string& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    return nullptr;
  }
  const auto size = static_cast<std::size_t>(stream.tellg());
  if (size == 0) {
    return nullptr;
  }
  stream.seekg(0, std::ios::beg);
  std::vector<unsigned char> buffer(size);
  if (!stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size))) {
    return nullptr;
  }
  return new std::vector<unsigned char>(std::move(buffer));
}

constexpr const char* font_regular_path = "/fonts/SpaceGrotesk-Regular.ttf";
constexpr const char* font_medium_path = "/fonts/SpaceGrotesk-Medium.ttf";
constexpr const char* font_bold_path = "/fonts/SpaceGrotesk-Bold.ttf";

bool g_fonts_requested = false;
bool g_fonts_ready = false;
int g_font_attempts = 0;

// stb_truetype borrows the face data instead of copying it, so the buffers must
// outlive the atlas. Keeping them for the lifetime of the process costs about
// 260 kB, which is far cheaper than re-reading the files on demand.
std::vector<std::vector<unsigned char>*>& font_buffers() {
  static std::vector<std::vector<unsigned char>*> buffers;
  return buffers;
}

}

const Palette& colors() noexcept {
  return palette();
}

void apply_theme() {
  ImGuiStyle& style = ImGui::GetStyle();
  const Palette& c = palette();

  style.WindowPadding = ImVec2(10.0f, 10.0f);
  style.FramePadding = ImVec2(8.0f, 5.0f);
  style.CellPadding = ImVec2(6.0f, 4.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  style.IndentSpacing = 20.0f;
  style.ScrollbarSize = 11.0f;
  style.GrabMinSize = 10.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.PopupRounding = 4.0f;

  ImVec4* colors_ref = style.Colors;
  colors_ref[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(c.surface);
  colors_ref[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(c.surface);
  colors_ref[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(c.border);
  colors_ref[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors_ref[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(c.border_strong);
  colors_ref[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(c.accent_active);
  colors_ref[ImGuiCol_TitleBg] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_TitleBgCollapsed] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_MenuBarBg] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors_ref[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(c.border_strong);
  colors_ref[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(c.text_muted);
  colors_ref[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(c.text_muted);
  colors_ref[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(c.accent);
  colors_ref[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(c.accent);
  colors_ref[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(c.accent_active);
  colors_ref[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(c.border_strong);
  colors_ref[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(c.accent);
  colors_ref[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(IM_COL32(255, 255, 255, 18));
  colors_ref[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(255, 255, 255, 32));
  colors_ref[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(232, 108, 76, 90));
  colors_ref[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(c.border);
  colors_ref[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(c.border_strong);
  colors_ref[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(c.accent);
  colors_ref[ImGuiCol_ResizeGrip] = ImGui::ColorConvertU32ToFloat4(IM_COL32(232, 108, 76, 60));
  colors_ref[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_TabHovered] = ImGui::ColorConvertU32ToFloat4(c.border_strong);
  colors_ref[ImGuiCol_TabActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(232, 108, 76, 130));
  colors_ref[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(c.text);
  colors_ref[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(IM_COL32(146, 155, 170, 110));
  colors_ref[ImGuiCol_PlotLines] = ImGui::ColorConvertU32ToFloat4(c.accent);
  colors_ref[ImGuiCol_PlotLinesHovered] = ImGui::ColorConvertU32ToFloat4(c.accent_hover);
  colors_ref[ImGuiCol_PlotHistogram] = ImGui::ColorConvertU32ToFloat4(c.accent);
  colors_ref[ImGuiCol_PlotHistogramHovered] = ImGui::ColorConvertU32ToFloat4(c.accent_hover);
  colors_ref[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(c.surface_raised);
  colors_ref[ImGuiCol_TableBorderStrong] = ImGui::ColorConvertU32ToFloat4(c.border);
  colors_ref[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(c.border);
  colors_ref[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors_ref[ImGuiCol_TableRowBgAlt] = ImGui::ColorConvertU32ToFloat4(IM_COL32(255, 255, 255, 12));
  colors_ref[ImGuiCol_TextSelectedBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(232, 108, 76, 80));
  colors_ref[ImGuiCol_DragDropTarget] = ImGui::ColorConvertU32ToFloat4(IM_COL32(244, 196, 92, 120));
}

bool load_fonts(const std::string& regular_path, const std::string& medium_path, const std::string& bold_path) {
  std::vector<unsigned char>* regular = read_file(regular_path);
  if (regular == nullptr) {
    return false;
  }
  std::vector<unsigned char>* medium = read_file(medium_path);
  if (medium == nullptr) {
    medium = read_file(bold_path);
  }
  std::vector<unsigned char>* bold = read_file(bold_path);
  if (bold == nullptr || bold == medium) {
    bold = nullptr;
  }

  ImGuiIO& io = ImGui::GetIO();
  if (io.Fonts == nullptr) {
    return false;
  }
  io.Fonts->Clear();

  font_buffers().push_back(regular);
  const ImWchar* ranges = glyph_ranges();

  ImFontConfig regular_config;
  regular_config.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(regular->data(), static_cast<int>(regular->size()), 17.0f, &regular_config, ranges);

  if (medium != nullptr) {
    font_buffers().push_back(medium);
    ImFontConfig medium_config;
    medium_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(medium->data(), static_cast<int>(medium->size()), 17.0f, &medium_config, ranges);
  }
  if (bold != nullptr) {
    font_buffers().push_back(bold);
    ImFontConfig bold_config;
    bold_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(bold->data(), static_cast<int>(bold->size()), 17.0f, &bold_config, ranges);
  }

  if (!io.Fonts->Build() || io.Fonts->Fonts.Size == 0) {
    return false;
  }
  io.FontDefault = io.Fonts->Fonts.Data[0];
  // The GL backend uploads atlas textures from RenderDrawData, so nothing else
  // is needed here.
  return true;
}

void request_fonts() {
  g_fonts_requested = true;
}

void pump_font_load() {
  if (!g_fonts_requested || g_fonts_ready) {
    return;
  }
  if (g_font_attempts >= 600) {
    g_fonts_requested = false;
    return;
  }
  ++g_font_attempts;
  if (load_fonts(font_regular_path, font_medium_path, font_bold_path)) {
    g_fonts_ready = true;
    g_fonts_requested = false;
  }
}

bool fonts_ready() noexcept {
  return g_fonts_ready;
}

}
