#pragma once

#include "model/clip.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace wasmcut::model {

enum class TrackType {
  Video,
  Audio
};

[[nodiscard]] const char* track_type_name(TrackType type) noexcept;

struct Track {
  std::string id;
  std::string name;
  TrackType type = TrackType::Video;
  bool enabled = true;
  bool locked = false;
  bool muted = false;
  bool solo = false;
  std::vector<Clip> clips;

  [[nodiscard]] bool is_valid() const noexcept {
    return !id.empty();
  }

  [[nodiscard]] Clip* find_clip(const std::string& clip_id) noexcept {
    const auto iterator = std::find_if(clips.begin(), clips.end(), [&clip_id](const Clip& clip) {
      return clip.id == clip_id;
    });
    return iterator == clips.end() ? nullptr : &*iterator;
  }

  [[nodiscard]] const Clip* find_clip(const std::string& clip_id) const noexcept {
    const auto iterator = std::find_if(clips.begin(), clips.end(), [&clip_id](const Clip& clip) {
      return clip.id == clip_id;
    });
    return iterator == clips.end() ? nullptr : &*iterator;
  }

  Clip* add_clip(Clip clip) {
    if (!clip.is_valid() || find_clip(clip.id) != nullptr) {
      return nullptr;
    }
    const auto position = std::upper_bound(
        clips.begin(),
        clips.end(),
        clip.timeline_start,
        [](TimeUs value, const Clip& existing) {
          return value < existing.timeline_start;
        });
    const auto inserted = clips.insert(position, std::move(clip));
    return &*inserted;
  }

  bool remove_clip(const std::string& clip_id) {
    const auto iterator = std::find_if(clips.begin(), clips.end(), [&clip_id](const Clip& clip) {
      return clip.id == clip_id;
    });
    if (iterator == clips.end()) {
      return false;
    }
    clips.erase(iterator);
    return true;
  }

  [[nodiscard]] Clip* clip_at(TimeUs timeline_time) noexcept {
    const auto iterator = std::find_if(clips.begin(), clips.end(), [timeline_time](const Clip& clip) {
      return clip.contains_time(timeline_time);
    });
    return iterator == clips.end() ? nullptr : &*iterator;
  }

  [[nodiscard]] const Clip* clip_at(TimeUs timeline_time) const noexcept {
    const auto iterator = std::find_if(clips.begin(), clips.end(), [timeline_time](const Clip& clip) {
      return clip.contains_time(timeline_time);
    });
    return iterator == clips.end() ? nullptr : &*iterator;
  }

  [[nodiscard]] TimeUs duration() const noexcept {
    TimeUs result = 0;
    for (const Clip& clip : clips) {
      result = std::max(result, clip.timeline_end());
    }
    return result;
  }
};

}
