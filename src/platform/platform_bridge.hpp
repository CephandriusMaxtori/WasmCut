#pragma once

namespace wasmcut::platform {

void request_import();
void request_probe();
void play_video();
void pause_video();
void seek_video(double seconds);
void request_export(double source_in, double source_out);
bool upload_video_frame();

}
