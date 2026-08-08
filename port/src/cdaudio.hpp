// VirtualCD — a host stand-in for the CD-ROM drive's *audio transport*.
//
// Theocracy's music is Redbook CD audio: the game never reads a single byte of
// it. cCD_Linux (libmvos) tells the drive `CDROMPLAYTRKIND` and the drive's own
// DAC plays the track; nothing enters the process. So there is no file read to
// redirect the way `/mnt/cdrom` paths are — the host has to *be* the drive.
// See docs/subsystems/music-and-redbook.md.
//
// This class is the transport half: the TOC, and a wall-clock model of where
// the laser is. It deliberately does NOT decode or output audio yet — its whole
// job is to answer CDROMREADTOCHDR and CDROMSUBCHNL correctly so the guest's own
// cVCDThread / cVCD / cCD_Linux run unmodified and ask for the right track at
// the right moment. Track durations are real (probed from the ripped files), so
// "the track ended" happens when it actually would.
#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>

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
    void set_volume(uint8_t v) { volume_ = v; }
    uint8_t volume() const { return volume_; }

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
    void advance();   // roll the model forward to now

    std::string dir_;
    std::map<uint8_t, std::string> tracks_;     // absolute track number -> path
    std::map<uint8_t, double> durations_;       // seconds

    enum class T { Stopped, Playing, Paused, Completed };
    T state_ = T::Stopped;
    uint8_t cur_ = 0, end_ = 0;
    clock::time_point t0_{};        // when cur_ started playing
    double paused_at_ = 0.0;        // offset into cur_ when paused
    uint8_t volume_ = 0xff;
    std::set<uint8_t> missing_warned_;   // play() of an absent track: warn once
};
