#include "cdaudio.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include <dirent.h>

extern "C" {
#include <libavformat/avformat.h>
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

}  // namespace

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
    return true;
}

void VirtualCD::stop() {
    state_ = T::Stopped;
    cur_ = 0;
    paused_at_ = 0.0;
}

void VirtualCD::pause() {
    if (state_ != T::Playing) return;
    paused_at_ = std::chrono::duration<double>(clock::now() - t0_).count();
    state_ = T::Paused;
}

void VirtualCD::resume() {
    if (state_ != T::Paused) return;
    t0_ = clock::now() - std::chrono::duration_cast<clock::duration>(
                             std::chrono::duration<double>(paused_at_));
    state_ = T::Playing;
}

void VirtualCD::advance() {
    if (state_ != T::Playing) return;
    for (;;) {
        double dur = duration(cur_);
        double elapsed = std::chrono::duration<double>(clock::now() - t0_).count();
        // A track with no known duration would spin this loop forever; treat it
        // as finished so the guest moves on rather than the host hanging.
        if (dur <= 0.0) { state_ = T::Completed; cur_ = 0; return; }
        if (elapsed < dur) return;
        if (cur_ >= end_) { state_ = T::Completed; cur_ = 0; return; }
        // CDROMPLAYTRKIND plays a *range*; roll into the next track, carrying
        // the overshoot so a long stall does not slew the whole range.
        uint8_t next = cur_ + 1;
        while (next <= end_ && !has_track(next)) next++;
        if (next > end_) { state_ = T::Completed; cur_ = 0; return; }
        t0_ += std::chrono::duration_cast<clock::duration>(
                   std::chrono::duration<double>(dur));
        cur_ = next;
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
        double off = (state_ == T::Paused)
                         ? paused_at_
                         : std::chrono::duration<double>(clock::now() - t0_).count();
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
