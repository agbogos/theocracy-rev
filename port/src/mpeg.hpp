// Minimal MPEG-1 decode for SMPEG HLE (intro/logo cutscenes).
// Decodes to RGB565 frames; host-owned, keyed by guest SMPEG* handle.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct MpegMovie {
    int width = 0, height = 0;
    double fps = 15.0;
    bool has_audio = false;
    int channels = 2;
    int samplerate = 22050;
    std::vector<std::vector<uint16_t>> frames;  // RGB565, width*height each
    size_t frame_i = 0;
    bool playing = false;
    uint32_t display = 0;  // guest cDisplay*
    int move_x = 0, move_y = 0;
    // Wall-clock deadline for the *next* frame (playvideoframe paces to fps).
    std::chrono::steady_clock::time_point next_frame_at{};
};

class MpegStore {
public:
    // Decode whole file to RGB565 frames. Returns false on failure.
    bool load(uint32_t handle, const std::string& host_path);
    void erase(uint32_t handle);
    MpegMovie* get(uint32_t handle);
    void clear();

private:
    std::unordered_map<uint32_t, MpegMovie> movies_;
};
