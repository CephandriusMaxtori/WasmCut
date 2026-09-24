#include "model/track.hpp"

namespace wasmcut::model {

const char* track_type_name(TrackType type) noexcept {
  switch (type) {
    case TrackType::Video:
      return "Video";
    case TrackType::Audio:
      return "Audio";
  }
  return "Unknown";
}

}
