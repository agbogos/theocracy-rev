// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
#include "cdaudio.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include <dirent.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace {

// Extensions we are willing to hand to libav. A whitelist rather than "try
// everything": the rip directory in practice also holds the disc images and
// macOS's .TOC.plist, and `theocracy-d1.iso` would otherwise parse as track 1
// and shadow the real data track.
bool audio_ext(const std::string& name) {
    static const char* kExts[] = {".aiff", ".aif", ".aifc", ".flac", ".wav",
                                  ".ogg",  ".mp3", ".m4a",  ".ape",  ".wv"};
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    for (const char* e : kExts)
        if (ext == e) return true;
    return false;
}

// The first digit run in the basename is the ABSOLUTE track number. macOS names
// audio-CD tracks by absolute number ("2 Audio Track.aiff" is disc track 2, not
// the 2nd audio track), and every common ripper form ("track02.flac", "02.wav")
// agrees. Getting this wrong would shift the whole soundtrack by one and the
// only symptom would be the wrong music in the wrong screen.
int track_number(const std::string& name) {
    size_t i = 0;
    while (i < name.size() && !std::isdigit((unsigned char)name[i])) i++;
    if (i >= name.size()) return -1;
    int n = 0;
    while (i < name.size() && std::isdigit((unsigned char)name[i])) {
        n = n * 10 + (name[i] - '0');
        if (n > 99) return -1;
        i++;
    }
    return n;
}

double probe_duration(const std::string& path) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return 0.0;
    double secs = 0.0;
    if (avformat_find_stream_info(fmt, nullptr) >= 0 && fmt->duration > 0)
        secs = (double)fmt->duration / (double)AV_TIME_BASE;
    avformat_close_input(&fmt);
    return secs;
}

// Keep roughly this much decoded audio ahead of the device. Large enough that a
// long host stall (a save, a level load) does not drop the music, small enough
// that a track change is not audibly late — the guest can switch mood between a
// Stop and the next Play, and anything already buffered would play over it.
constexpr size_t kRingTarget = (size_t)CDPlayer::kRate * CDPlayer::kChannels;  // 1s

}  // namespace

CDPlayer::~CDPlayer() { close(); }

bool CDPlayer::open(const std::string& path) {
    close();
    quit_.store(false);
    eof_.store(false);
    paused_.store(false);
    consumed_.store(0);
    active_.store(true);
    th_ = std::thread(&CDPlayer::decode_loop, this, path);
    return true;
}

void CDPlayer::close() {
    quit_.store(true);
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    std::lock_guard<std::mutex> lk(mu_);
    ring_.clear();
    active_.store(false);
    eof_.store(false);
    consumed_.store(0);
}

void CDPlayer::decode_loop(std::string path) {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    int stream_i = -1;

    auto cleanup = [&] {
        if (swr) swr_free(&swr);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (dec) avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
    };

    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(fmt, nullptr) < 0) {
        std::fprintf(stderr, "  [cd] cannot open '%s' for decode\n", path.c_str());
        cleanup();
        eof_.store(true);
        return;
    }
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { stream_i = (int)i; break; }
    if (stream_i < 0) { cleanup(); eof_.store(true); return; }

    AVCodecParameters* par = fmt->streams[stream_i]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) { cleanup(); eof_.store(true); return; }
    dec = avcodec_alloc_context3(codec);
    if (!dec || avcodec_parameters_to_context(dec, par) < 0 ||
        avcodec_open2(dec, codec, nullptr) < 0) {
        cleanup(); eof_.store(true); return;
    }

    AVChannelLayout out_ch;
    av_channel_layout_default(&out_ch, kChannels);
    if (swr_alloc_set_opts2(&swr, &out_ch, AV_SAMPLE_FMT_S16, kRate,
                            &dec->ch_layout, dec->sample_fmt, dec->sample_rate,
                            0, nullptr) < 0 || swr_init(swr) < 0) {
        cleanup(); eof_.store(true); return;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    std::vector<int16_t> conv;

    auto emit = [&](AVFrame* f) {
        // Worst case output count for this input frame, plus whatever swr holds.
        int64_t delay = swr_get_delay(swr, dec->sample_rate ? dec->sample_rate : kRate);
        int max_out = (int)av_rescale_rnd(delay + (f ? f->nb_samples : 0),
                                          kRate, dec->sample_rate ? dec->sample_rate : kRate,
                                          AV_ROUND_UP);
        if (max_out <= 0) return;
        conv.resize((size_t)max_out * kChannels);
        uint8_t* outp[1] = {reinterpret_cast<uint8_t*>(conv.data())};
        int got = swr_convert(swr, outp, max_out,
                              f ? (const uint8_t**)f->extended_data : nullptr,
                              f ? f->nb_samples : 0);
        if (got <= 0) return;
        std::unique_lock<std::mutex> lk(mu_);
        ring_.insert(ring_.end(), conv.begin(), conv.begin() + (size_t)got * kChannels);
    };

    bool draining = false;
    while (!quit_.load(std::memory_order_relaxed)) {
        {   // Backpressure: stop decoding once far enough ahead.
            std::unique_lock<std::mutex> lk(mu_);
            if (ring_.size() >= kRingTarget) {
                cv_.wait_for(lk, std::chrono::milliseconds(50));
                continue;
            }
        }
        if (draining) break;
        int rc = av_read_frame(fmt, pkt);
        if (rc < 0) {
            avcodec_send_packet(dec, nullptr);           // flush the decoder
            while (avcodec_receive_frame(dec, frame) >= 0) emit(frame);
            emit(nullptr);                               // flush swr's tail
            draining = true;
            continue;
        }
        if (pkt->stream_index == stream_i && avcodec_send_packet(dec, pkt) >= 0)
            while (avcodec_receive_frame(dec, frame) >= 0) emit(frame);
        av_packet_unref(pkt);
    }

    eof_.store(true);
    cleanup();
}

size_t CDPlayer::mix(int16_t* out, size_t nsamp, int volume) {
    if (!active_.load(std::memory_order_relaxed)) return 0;
    if (paused_.load(std::memory_order_relaxed)) return 0;
    if (volume <= 0) {
        // Muted, but time must still pass or the transport would never finish
        // the track and the guest would sit on it forever.
        std::lock_guard<std::mutex> lk(mu_);
        size_t n = std::min(nsamp, ring_.size());
        ring_.erase(ring_.begin(), ring_.begin() + (long)n);
        consumed_.fetch_add(n, std::memory_order_relaxed);
        cv_.notify_one();
        return n;
    }
    size_t mixed = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        size_t n = std::min(nsamp, ring_.size());
        for (size_t i = 0; i < n; ++i) {
            int32_t v = out[i] + (int32_t)ring_[i] * volume / 255;
            out[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        ring_.erase(ring_.begin(), ring_.begin() + (long)n);
        mixed = n;
    }
    consumed_.fetch_add(mixed, std::memory_order_relaxed);
    cv_.notify_one();
    return mixed;
}

double CDPlayer::played_seconds() const {
    return (double)consumed_.load(std::memory_order_relaxed) /
           ((double)kRate * kChannels);
}

bool CDPlayer::finished() const {
    if (!active_.load(std::memory_order_relaxed)) return false;
    if (!eof_.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lk(mu_);
    return ring_.empty();
}

int VirtualCD::scan(const std::string& dir) {
    tracks_.clear();
    durations_.clear();
    dir_ = dir;

    DIR* d = ::opendir(dir.c_str());
    if (!d) return 0;
    while (struct dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;
        if (!audio_ext(name)) continue;
        int n = track_number(name);
        if (n < 1) continue;
        // Track 1 is the data track on a mixed-mode disc; an audio file naming
        // itself track 1 is a single-session audio rip or a misnamed file.
        // Take it, but say so — silently accepting it would make first_track()
        // a lie.
        if (n == 1)
            std::fprintf(stderr, "  [cd] '%s' claims track 1, which is the data "
                                 "track on this disc — check the numbering\n",
                        name.c_str());
        tracks_[(uint8_t)n] = dir + "/" + name;
    }
    ::closedir(d);

    for (auto& [n, path] : tracks_) {
        durations_[n] = probe_duration(path);
        if (durations_[n] <= 0.0)
            std::fprintf(stderr, "  [cd] track %u: no duration from '%s' — it "
                                 "will report as ending immediately\n",
                        n, path.c_str());
    }
    return (int)tracks_.size();
}

uint8_t VirtualCD::last_track() const {
    return tracks_.empty() ? 1 : tracks_.rbegin()->first;
}

double VirtualCD::duration(uint8_t t) const {
    auto it = durations_.find(t);
    return it == durations_.end() ? 0.0 : it->second;
}

const std::string* VirtualCD::path(uint8_t t) const {
    auto it = tracks_.find(t);
    return it == tracks_.end() ? nullptr : &it->second;
}

bool VirtualCD::play(uint8_t start, uint8_t end) {
    if (!has_track(start)) {
        // A real drive fails the ioctl for a track that is not there. Reporting
        // success and silence instead is exactly the failure mode this whole
        // subsystem already had. Warn once per track: the guest retries, and a
        // line per retry would bury everything else.
        if (missing_warned_.insert(start).second)
            std::fprintf(stderr, "  [cd] play(%u..%u): track %u not in the rip\n",
                        start, end, start);
        return false;
    }
    cur_ = start;
    end_ = std::max(start, end);
    state_ = T::Playing;
    t0_ = clock::now();
    paused_at_ = 0.0;
    if (audio_out_)
        if (const std::string* p = path(start)) player_.open(*p);
    return true;
}

void VirtualCD::stop() {
    state_ = T::Stopped;
    cur_ = 0;
    paused_at_ = 0.0;
    player_.close();
}

void VirtualCD::pause() {
    if (state_ != T::Playing) return;
    paused_at_ = track_position();
    state_ = T::Paused;
    player_.set_paused(true);
}

void VirtualCD::resume() {
    if (state_ != T::Paused) return;
    t0_ = clock::now() - std::chrono::duration_cast<clock::duration>(
                             std::chrono::duration<double>(paused_at_));
    state_ = T::Playing;
    player_.set_paused(false);
}

void VirtualCD::mix(int16_t* out, size_t nsamp) {
    int vol = (int)volume_.load(std::memory_order_relaxed) *
              host_vol_.load(std::memory_order_relaxed) / 100;
    player_.mix(out, nsamp, vol);
}

// How far into the current track we are. When audio is actually playing the
// player's consumed-sample count is authoritative: a wall clock would drift
// against the device and, worse, would declare the track over while a second of
// it was still buffered — an audible cut at the end of every track.
double VirtualCD::track_position() const {
    if (player_.active()) return player_.played_seconds();
    return std::chrono::duration<double>(clock::now() - t0_).count();
}

void VirtualCD::advance() {
    if (state_ != T::Playing) return;
    for (;;) {
        // With audio running, "the track ended" means the decoder hit EOF *and*
        // the ring drained — the sample-accurate end. The duration check is the
        // fallback for when there is no audio device (the TOC-only path) and the
        // backstop for a file whose header duration lies.
        bool done = player_.active()
                        ? player_.finished()
                        : (duration(cur_) <= 0.0 ||
                           track_position() >= duration(cur_));
        if (!done) return;
        if (cur_ >= end_) { state_ = T::Completed; cur_ = 0; player_.close(); return; }
        // CDROMPLAYTRKIND plays a *range*; roll into the next track.
        uint8_t next = cur_ + 1;
        while (next <= end_ && !has_track(next)) next++;
        if (next > end_) { state_ = T::Completed; cur_ = 0; player_.close(); return; }
        cur_ = next;
        t0_ = clock::now();
        if (!audio_out_) continue;
        if (const std::string* p = path(next)) player_.open(*p);
        else return;
    }
}

VirtualCD::Status VirtualCD::status() {
    advance();
    Status s;
    switch (state_) {
        case T::Playing: s.audio_status = AS_PLAY;      s.track = cur_; break;
        case T::Paused:  s.audio_status = AS_PAUSED;    s.track = cur_; break;
        case T::Completed: s.audio_status = AS_COMPLETED; break;
        case T::Stopped:   s.audio_status = AS_NO_STATUS; break;
    }
    if (s.track) {
        double off = (state_ == T::Paused) ? paused_at_ : track_position();
        if (off < 0) off = 0;
        s.rel_frame = (uint32_t)(off * 75.0);
        // Absolute address is only cosmetic here (the guest reads neither), but
        // a plausible one costs nothing: 2s lead-in + every earlier track.
        double abs = 2.0;
        for (auto& [n, dur] : durations_)
            if (n < s.track) abs += dur;
        s.abs_frame = (uint32_t)((abs + off) * 75.0);
    }
    return s;
}

bool VirtualCD::idle() {
    advance();
    return state_ == T::Completed || state_ == T::Stopped;
}
