#pragma once

#include "model/time.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace wasmcut::model {

enum class MediaType {
  Unknown,
  Video,
  Audio,
  Image
};

[[nodiscard]] const char* media_type_name(MediaType type) noexcept;
[[nodiscard]] MediaType parse_media_type(std::string_view value) noexcept;

struct MediaAsset {
  std::string id;
  std::string name;
  std::string file_name;
  std::string mime_type;
  // Matches the file name of the host-side blob, which is how a saved project
  // finds its media again on the next visit.
  std::string storage_key;
  MediaType type = MediaType::Unknown;
  std::uint64_t file_size = 0;
  TimeUs duration = 0;
  int width = 0;
  int height = 0;
  double frame_rate = 0.0;

  [[nodiscard]] bool is_valid() const noexcept {
    return !id.empty() && duration >= 0 && width >= 0 && height >= 0;
  }

  [[nodiscard]] bool has_video() const noexcept {
    return type == MediaType::Video || type == MediaType::Image;
  }

  [[nodiscard]] bool has_audio() const noexcept {
    return type == MediaType::Video || type == MediaType::Audio;
  }

  [[nodiscard]] bool is_image() const noexcept {
    return type == MediaType::Image;
  }

  [[nodiscard]] std::string duration_text() const {
    return format_timecode(duration, 1000.0);
  }
};

}
