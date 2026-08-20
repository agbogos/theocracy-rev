// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
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
    // Decoded audio, resampled to the host device format: interleaved stereo
    // S16 @ 22050 Hz. `audio_pos` = samples already pushed to the mixer.
    std::vector<int16_t> audio;
    size_t audio_pos = 0;
    bool playing = false;
    uint32_t display = 0;  // guest cDisplay*
    int move_x = 0, move_y = 0;
    // Wall-clock deadline for the *next* frame (playvideoframe paces to fps).
    std::chrono::steady_clock::time_point next_frame_at{};

    // ---- aspect-fit presentation -------------------------------------------
    // The shipped movies come in two shapes — 480×360 (4:3: logos, briefings,
    // tutorials) and 608×300 (~2.03:1: the main cutscenes) — while the guest
    // opens a 640×480 mode for all of them. Frames are decoded and kept at native
    // size; this scales one on demand to fill the destination on its tighter axis
    // and centres it, leaving black bars on the other. 480×360 lands exactly on
    // 640×480 (both 4:3, no bars); 608×300 becomes 640×316 with 82px bars.
    //
    // Cached: the geometry and the bar-clearing are recomputed only when the
    // destination size changes, so per-frame cost is just the scale.
    int fit_w = 0, fit_h = 0;                                // destination extent
    int inner_x = 0, inner_y = 0, inner_w = 0, inner_h = 0;  // centred image rect
    std::vector<uint16_t> fit;                               // fit_w*fit_h RGB565
    // Precomputed horizontal sample table (rebuilt with the geometry): source
    // column pair + 0..256 blend weight for each destination column.
    std::vector<int32_t> map_x0, map_x1, map_wx;

    // Scale `fr` (native size) into the cached buffer for a dst_w × dst_h
    // destination, aspect-preserved and centred. Returns a dst_w*dst_h RGB565
    // image including the bars, or nullptr if the geometry is unusable.
    const uint16_t* fit_frame(const std::vector<uint16_t>& fr, int dst_w, int dst_h);
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
