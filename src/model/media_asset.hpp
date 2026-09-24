#pragma once

#include "model/time.hpp"

#include <cstdint>
#include <string>

namespace wasmcut::model {

enum class MediaType {
  Unknown,
  Video,
  Audio,
  Image
};

struct MediaAsset {
  std::string id;
  std::string name;
  std::string file_name;
  std::string mime_type;
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
};

}
