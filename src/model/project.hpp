#pragma once

#include "model/media_asset.hpp"
#include "model/track.hpp"

#include <string>
#include <vector>

namespace wasmcut::model {

struct Project {
  int schema_version = 1;
  std::string id;
  std::string name;
  double frame_rate = 30.0;
  int sample_rate = 48000;
  TimeUs duration_us = 0;
  std::vector<MediaAsset> media_assets;
  std::vector<Track> tracks;

  Project();

  [[nodiscard]] bool is_valid() const noexcept;

  [[nodiscard]] MediaAsset* find_media(const std::string& media_id) noexcept;
  [[nodiscard]] const MediaAsset* find_media(const std::string& media_id) const noexcept;
  MediaAsset* add_media(MediaAsset asset);
  MediaAsset* upsert_media(MediaAsset asset);
  bool remove_media(const std::string& media_id);

  [[nodiscard]] Track* find_track(const std::string& track_id) noexcept;
  [[nodiscard]] const Track* find_track(const std::string& track_id) const noexcept;
  Track* add_track(Track track);
  bool remove_track(const std::string& track_id);

  [[nodiscard]] Clip* find_clip(const std::string& clip_id) noexcept;
  [[nodiscard]] const Clip* find_clip(const std::string& clip_id) const noexcept;
  Clip* create_clip(Clip clip, const std::string& track_id);
  bool remove_clip(const std::string& clip_id);

  [[nodiscard]] TimeUs duration() const noexcept;
  void recalculate_duration() noexcept;
  void ensure_default_tracks();
  bool set_frame_rate(double new_frame_rate) noexcept;
  bool set_sample_rate(int new_sample_rate) noexcept;
};

}
