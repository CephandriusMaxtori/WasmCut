#include "model/project.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

}

int main() {
  using namespace wasmcut::model;

  require(seconds_to_time(1.5) == 1'500'000, "time conversion failed");
  require(Project().is_valid(), "default project is invalid");

  Project project;
  require(project.tracks.size() == 2, "default tracks were not created");
  require(project.find_track("track-video") != nullptr, "video track is missing");
  require(project.find_track("track-audio") != nullptr, "audio track is missing");

  MediaAsset asset;
  asset.id = "media-1";
  asset.name = "Sample";
  asset.file_name = "sample.webm";
  asset.type = MediaType::Video;
  asset.duration = 4'000'000;
  asset.width = 1280;
  asset.height = 720;
  require(project.add_media(asset) != nullptr, "media asset was not added");
  require(project.add_media(asset) == nullptr, "duplicate media asset was accepted");
  require(project.find_media("media-1") != nullptr, "media asset lookup failed");

  Clip first;
  first.id = "clip-1";
  first.media_id = asset.id;
  first.name = "First";
  first.timeline_start = 2'000'000;
  first.set_source_range(0, 1'000'000);
  require(project.create_clip(first, "track-video") != nullptr, "first clip was not created");

  Clip second;
  second.id = "clip-2";
  second.media_id = asset.id;
  second.name = "Second";
  second.timeline_start = 0;
  second.set_source_range(1'000'000, 2'000'000);
  require(project.create_clip(second, "track-video") != nullptr, "second clip was not created");

  Track* video = project.find_track("track-video");
  require(video != nullptr, "video track lookup failed");
  require(video->clips.size() == 2, "clips were not stored");
  require(video->clips.front().id == "clip-2", "clips were not sorted");
  require(video->clip_at(500'000) != nullptr, "clip hit testing failed");
  require(video->clip_at(1'500'000) == nullptr, "unexpected clip hit");
  require(project.duration() == 3'000'000, "project duration is incorrect");
  require(project.is_valid(), "project became invalid");

  require(!project.remove_media(asset.id), "referenced media was removed");
  require(project.remove_clip("clip-1"), "clip removal failed");
  require(project.remove_clip("clip-2"), "second clip removal failed");
  require(project.remove_media(asset.id), "unreferenced media was not removed");
  require(!project.remove_clip("missing"), "missing clip removal reported success");

  std::cout << "model tests passed\n";
  return 0;
}
