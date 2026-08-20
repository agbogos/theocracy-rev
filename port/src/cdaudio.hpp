// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
// VirtualCD — a host stand-in for the CD-ROM drive's *audio transport*.
//
// Theocracy's music is Redbook CD audio: the game never reads a single byte of
// it. cCD_Linux (libmvos) tells the drive `CDROMPLAYTRKIND` and the drive's own
// DAC plays the track; nothing enters the process. So there is no file read to
// redirect the way `/mnt/cdrom` paths are — the host has to *be* the drive.
// See docs/subsystems/music-and-redbook.md.
//
// Two halves. VirtualCD is the transport: the TOC and a model of where the
// laser is, answering CDROMREADTOCHDR and CDROMSUBCHNL so the guest's own
// cVCDThread / cVCD / cCD_Linux run unmodified and ask for the right track at
// the right moment. CDPlayer is the drive's DAC: it streams the ripped file into
// the host mixer.
//
// With no audio device the transport still runs on a wall clock and real track
// durations — that is the TOC-only path, and it is how the guest's track choices
// were verified before a note of audio was wired up.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

// Streaming decode of one CD track into the host mixer format.
//
// A host thread, not a green one: it touches no guest memory and never enters
// Unicorn, so the single-threaded-emulator rule does not apply (the watchdog
// thread is the same shape). Decoding on the emulation thread would stall the
// guest for milliseconds at a time, and decoding in the SDL callback would put
// file I/O in a realtime callback.
//
// The tracks are 44100 Hz stereo; the host device is 22050. swresample does the
// rate conversion, which is why nothing here has to care that the rip is AIFF-C
// `sowt`, FLAC, or anything else libav reads.
class CDPlayer {
public:
    ~CDPlayer();
    static constexpr int kRate = 22050;      // host mixer rate
    static constexpr int kChannels = 2;

    bool open(const std::string& path);      // start decoding from the top
    void close();
    void set_paused(bool p) { paused_.store(p, std::memory_order_relaxed); }
    bool active() const { return active_.load(std::memory_order_relaxed); }

    // Called from the SDL audio callback: sum this track into `out` (nsamp
    // interleaved int16 samples), scaled by volume 0..255, with clipping.
    // Returns samples actually mixed — short means the ring ran dry.
    size_t mix(int16_t* out, size_t nsamp, int volume);

    // Seconds of audio actually handed to the device. This is the transport
    // clock whenever a track is playing: using wall clock instead would let the
    // model and the audio drift apart, and the tail of a track would be cut.
    double played_seconds() const;
    // EOF reached AND the ring is drained — the track has finished being heard.
    bool finished() const;

private:
    void decode_loop(std::string path);

    std::thread th_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<int16_t> ring_;
    std::atomic<bool> quit_{false}, eof_{false}, active_{false}, paused_{false};
    std::atomic<uint64_t> consumed_{0};      // samples mixed out (all channels)
};

class VirtualCD {
public:
    // Linux CD-ROM audio status codes (cdrom.h) — the values cCD_Linux's jump
    // table at libmvos 0xbc1e0 switches on. Only PLAY and PAUSED make
    // GetActualTrack report a track; everything else reports 0, which is what
    // the guest reads as "the track ended, pick another".
    // Named AS_* rather than cdrom.h's CDROM_AUDIO_* / SDL's AUDIO_*: both of
    // those are macros, and a macro does not respect this class scope.
    enum : uint8_t {
        AS_INVALID   = 0x00,   // CDROM_AUDIO_INVALID
        AS_PLAY      = 0x11,   // CDROM_AUDIO_PLAY
        AS_PAUSED    = 0x12,   // CDROM_AUDIO_PAUSED
        AS_COMPLETED = 0x13,   // CDROM_AUDIO_COMPLETED
        AS_ERROR     = 0x14,   // CDROM_AUDIO_ERROR
        AS_NO_STATUS = 0x15,   // CDROM_AUDIO_NO_STATUS
    };

    // Find ripped tracks. Accepts macOS's audio-CD naming ("2 Audio Track.aiff")
    // and the usual ripper forms ("track02.flac", "02.wav") — the rule is the
    // first digit run in the basename, which is the ABSOLUTE track number on the
    // disc, not an index into the audio tracks. Returns tracks found.
    int scan(const std::string& dir);

    bool present() const { return !tracks_.empty(); }
    const std::string& dir() const { return dir_; }
    int count() const { return (int)tracks_.size(); }

    // CDROMREADTOCHDR. first is 1 because track 1 is the data track — the disc
    // is mixed-mode and the ripped files are only its audio half. last is the
    // highest track number found.
    uint8_t first_track() const { return 1; }
    uint8_t last_track() const;
    bool has_track(uint8_t t) const { return tracks_.count(t) != 0; }
    double duration(uint8_t t) const;                 // seconds, 0 if absent
    const std::string* path(uint8_t t) const;

    // Transport. play() mirrors CDROMPLAYTRKIND: play `start`..`end` inclusive,
    // returning immediately (the real ioctl is asynchronous, which is the whole
    // reason the guest polls).
    bool play(uint8_t start, uint8_t end);
    void stop();
    void pause();
    void resume();
    void set_volume(uint8_t v) { volume_.store(v, std::memory_order_relaxed); }
    uint8_t volume() const { return volume_.load(std::memory_order_relaxed); }

    // Called from the SDL audio callback only. Mixes the playing track into the
    // buffer the guest's own sample mixer has already filled.
    void mix(int16_t* out, size_t nsamp);
    // Host-side music trim, THEOC_MUSIC_VOL (0..100). The original mixed CD
    // audio through the sound card's CD line with its own level, which we have
    // no reference for — so this exists to balance music against SFX by ear.
    void set_host_volume(int pct) {
        host_vol_.store(pct < 0 ? 0 : (pct > 100 ? 100 : pct), std::memory_order_relaxed);
    }
    // Whether there is a host audio device to play into. With no device nothing
    // would ever drain the decoder's ring, so the track would never "finish" and
    // the guest would sit on it forever — so when this is false the transport
    // falls back to the wall-clock model and no decoder is started at all. That
    // is also the TOC-only path used to verify the guest's track choices with no
    // sound at all.
    void set_audio_enabled(bool on) { audio_out_ = on; }

    // CDROMSUBCHNL. Advances the transport model to now, so call it before
    // reading. `track` is 0 when the status is not PLAY/PAUSED.
    struct Status {
        uint8_t audio_status = AS_NO_STATUS;
        uint8_t track = 0;
        uint32_t abs_frame = 0;   // CD frames (75/s) since the start of the disc
        uint32_t rel_frame = 0;   // CD frames since the start of this track
    };
    Status status();

    // True when the transport has nothing playing and the guest should be asked
    // to choose the next track. Distinct from `status().track == 0` only in
    // intent: this is the auto-advance trigger.
    bool idle();

private:
    using clock = std::chrono::steady_clock;
    void advance();             // roll the model forward to now
    double track_position() const;   // seconds into cur_ (audio- or clock-based)

    std::string dir_;
    std::map<uint8_t, std::string> tracks_;     // absolute track number -> path
    std::map<uint8_t, double> durations_;       // seconds

    enum class T { Stopped, Playing, Paused, Completed };
    T state_ = T::Stopped;
    uint8_t cur_ = 0, end_ = 0;
    clock::time_point t0_{};        // when cur_ started playing
    double paused_at_ = 0.0;        // offset into cur_ when paused
    // Both are read on the SDL audio thread by mix() and written on the
    // emulation thread (CDROMVOLCTRL / startup), so they are atomic rather than
    // plain — a torn read would only be a wrong volume for one buffer, but it is
    // still a data race and this is the only place the two threads share state.
    std::atomic<uint8_t> volume_{0xff};  // as set by CDROMVOLCTRL
    std::atomic<int> host_vol_{100};     // THEOC_MUSIC_VOL, percent
    bool audio_out_ = false;             // is there a device to decode into?
    std::set<uint8_t> missing_warned_;   // play() of an absent track: warn once
    CDPlayer player_;
};
