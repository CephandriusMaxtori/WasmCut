#include "model/track.hpp"

namespace wasmcut::model {

const char* track_type_name(TrackType type) noexcept {
  switch (type) {
    case TrackType::Video:
      return "video";
    case TrackType::Audio:
      return "audio";
  }
  return "unknown";
}

TrackType parse_track_type(std::string_view value) noexcept {
  if (value == "video") {
    return TrackType::Video;
  }
  if (value == "audio") {
    return TrackType::Audio;
  }
  return TrackType::Video;
}

}
