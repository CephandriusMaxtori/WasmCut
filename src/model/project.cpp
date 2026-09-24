#include "model/project.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace wasmcut::model {

Project::Project()
    : id("project-1"),
      name("Untitled Project") {
  ensure_default_tracks();
}

bool Project::is_valid() const noexcept {
  if (id.empty() || name.empty() || !std::isfinite(frame_rate) || frame_rate <= 0.0 || sample_rate <= 0) {
    return false;
  }
  for (const MediaAsset& asset : media_assets) {
    if (!asset.is_valid()) {
      return false;
    }
  }
  for (const Track& track : tracks) {
    if (!track.is_valid()) {
      return false;
    }
    for (const Clip& clip : track.clips) {
      if (!clip.is_valid() || find_media(clip.media_id) == nullptr) {
        return false;
      }
    }
  }
  return true;
}

MediaAsset* Project::find_media(const std::string& media_id) noexcept {
  const auto iterator = std::find_if(media_assets.begin(), media_assets.end(), [&media_id](const MediaAsset& asset) {
    return asset.id == media_id;
  });
  return iterator == media_assets.end() ? nullptr : &*iterator;
}

const MediaAsset* Project::find_media(const std::string& media_id) const noexcept {
  const auto iterator = std::find_if(media_assets.begin(), media_assets.end(), [&media_id](const MediaAsset& asset) {
    return asset.id == media_id;
  });
  return iterator == media_assets.end() ? nullptr : &*iterator;
}

MediaAsset* Project::add_media(MediaAsset asset) {
  if (!asset.is_valid() || find_media(asset.id) != nullptr) {
    return nullptr;
  }
  media_assets.push_back(std::move(asset));
  return &media_assets.back();
}

MediaAsset* Project::upsert_media(MediaAsset asset) {
  if (!asset.is_valid()) {
    return nullptr;
  }
  MediaAsset* existing = find_media(asset.id);
  if (existing != nullptr) {
    *existing = std::move(asset);
    return existing;
  }
  return add_media(std::move(asset));
}

bool Project::remove_media(const std::string& media_id) {
  const bool referenced = std::any_of(tracks.begin(), tracks.end(), [&media_id](const Track& track) {
    return std::any_of(track.clips.begin(), track.clips.end(), [&media_id](const Clip& clip) {
      return clip.media_id == media_id;
    });
  });
  if (referenced) {
    return false;
  }
  const auto iterator = std::find_if(media_assets.begin(), media_assets.end(), [&media_id](const MediaAsset& asset) {
    return asset.id == media_id;
  });
  if (iterator == media_assets.end()) {
    return false;
  }
  media_assets.erase(iterator);
  return true;
}

Track* Project::find_track(const std::string& track_id) noexcept {
  const auto iterator = std::find_if(tracks.begin(), tracks.end(), [&track_id](const Track& track) {
    return track.id == track_id;
  });
  return iterator == tracks.end() ? nullptr : &*iterator;
}

const Track* Project::find_track(const std::string& track_id) const noexcept {
  const auto iterator = std::find_if(tracks.begin(), tracks.end(), [&track_id](const Track& track) {
    return track.id == track_id;
  });
  return iterator == tracks.end() ? nullptr : &*iterator;
}

Track* Project::add_track(Track track) {
  if (!track.is_valid() || find_track(track.id) != nullptr) {
    return nullptr;
  }
  tracks.push_back(std::move(track));
  recalculate_duration();
  return &tracks.back();
}

bool Project::remove_track(const std::string& track_id) {
  const auto iterator = std::find_if(tracks.begin(), tracks.end(), [&track_id](const Track& track) {
    return track.id == track_id;
  });
  if (iterator == tracks.end()) {
    return false;
  }
  tracks.erase(iterator);
  recalculate_duration();
  return true;
}

Clip* Project::find_clip(const std::string& clip_id) noexcept {
  for (Track& track : tracks) {
    if (Clip* clip = track.find_clip(clip_id); clip != nullptr) {
      return clip;
    }
  }
  return nullptr;
}

const Clip* Project::find_clip(const std::string& clip_id) const noexcept {
  for (const Track& track : tracks) {
    if (const Clip* clip = track.find_clip(clip_id); clip != nullptr) {
      return clip;
    }
  }
  return nullptr;
}

Clip* Project::create_clip(Clip clip, const std::string& track_id) {
  Track* track = find_track(track_id);
  if (track == nullptr) {
    return nullptr;
  }
  Clip* created = track->add_clip(std::move(clip));
  if (created != nullptr) {
    recalculate_duration();
  }
  return created;
}

bool Project::remove_clip(const std::string& clip_id) {
  for (Track& track : tracks) {
    if (track.remove_clip(clip_id)) {
      recalculate_duration();
      return true;
    }
  }
  return false;
}

TimeUs Project::duration() const noexcept {
  TimeUs result = 0;
  for (const Track& track : tracks) {
    result = std::max(result, track.duration());
  }
  return result;
}

void Project::recalculate_duration() noexcept {
  duration_us = duration();
}

void Project::ensure_default_tracks() {
  if (find_track("track-video") == nullptr) {
    Track video;
    video.id = "track-video";
    video.name = "Video 1";
    video.type = TrackType::Video;
    add_track(std::move(video));
  }
  if (find_track("track-audio") == nullptr) {
    Track audio;
    audio.id = "track-audio";
    audio.name = "Audio 1";
    audio.type = TrackType::Audio;
    add_track(std::move(audio));
  }
}

bool Project::set_frame_rate(double new_frame_rate) noexcept {
  if (!std::isfinite(new_frame_rate) || new_frame_rate <= 0.0) {
    return false;
  }
  frame_rate = new_frame_rate;
  return true;
}

bool Project::set_sample_rate(int new_sample_rate) noexcept {
  if (new_sample_rate <= 0) {
    return false;
  }
  sample_rate = new_sample_rate;
  return true;
}

}
