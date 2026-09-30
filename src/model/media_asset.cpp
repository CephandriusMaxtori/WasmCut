#include "model/media_asset.hpp"

namespace wasmcut::model {

const char* media_type_name(MediaType type) noexcept {
  switch (type) {
    case MediaType::Video:
      return "video";
    case MediaType::Audio:
      return "audio";
    case MediaType::Image:
      return "image";
    case MediaType::Unknown:
      break;
  }
  return "unknown";
}

MediaType parse_media_type(std::string_view value) noexcept {
  if (value == "video") {
    return MediaType::Video;
  }
  if (value == "audio") {
    return MediaType::Audio;
  }
  if (value == "image") {
    return MediaType::Image;
  }
  return MediaType::Unknown;
}

}
