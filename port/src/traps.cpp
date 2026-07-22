#include "traps.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include <stdexcept>

using namespace guestmap;

// ---- sscanf: a from-scratch scanf engine over guest memory ----------------
// str/fmt are guest pointers (args 0,1); each non-suppressed conversion stores
// through the next guest vararg pointer. Returns the assignment count (or EOF).
namespace {
bool sws(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }

uint32_t do_sscanf(Machine& m, uint32_t esp) {
    std::string in = m.cstr(TrapLayer::arg_at(m, esp, 0));
    std::string fmt = m.cstr(TrapLayer::arg_at(m, esp, 1));
    size_t ip = 0, fp = 0;
    int argidx = 2, assigned = 0;
    bool eof_before_assign = false;
    auto put = [&](uint32_t ptr, const void* p, uint32_t n) { m.write(ptr, p, n); };

    while (fp < fmt.size()) {
        char fc = fmt[fp];
        if (sws(fc)) { fp++; while (ip < in.size() && sws(in[ip])) ip++; continue; }
        if (fc != '%') {                       // literal
            if (ip < in.size() && in[ip] == fc) { ip++; fp++; continue; }
            break;
        }
        fp++;                                  // past '%'
        if (fp < fmt.size() && fmt[fp] == '%') {
            fp++;
            if (ip < in.size() && in[ip] == '%') { ip++; continue; }
            break;
        }
        bool suppress = false;
        if (fp < fmt.size() && fmt[fp] == '*') { suppress = true; fp++; }
        int width = 0;
        while (fp < fmt.size() && isdigit((unsigned char)fmt[fp]))
            width = width * 10 + (fmt[fp++] - '0');
        int len = 0;                           // 0=int 1=h(short) 2=hh(char) 3=l(long/double) 4=ll/q(8) 5=L
        if (fp < fmt.size()) {
            char lm = fmt[fp];
            if (lm=='h') { fp++; if (fp<fmt.size()&&fmt[fp]=='h') { fp++; len=2; } else len=1; }
            else if (lm=='l') { fp++; if (fp<fmt.size()&&fmt[fp]=='l') { fp++; len=4; } else len=3; }
            else if (lm=='q') { fp++; len=4; }
            else if (lm=='L') { fp++; len=5; }
        }
        if (fp >= fmt.size()) break;
        char conv = fmt[fp++];

        auto int_bytes = [&]() -> int {
            if (conv=='p') return 4;
            switch (len) { case 1: return 2; case 2: return 1; case 4: return 8; default: return 4; }
        };
        auto store_int = [&](unsigned long long v) {
            if (suppress) return;
            uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
            int nb = int_bytes();
            for (int i = 0; i < nb; ++i) { uint8_t b = (v >> (8*i)) & 0xff; put(ptr + i, &b, 1); }
            assigned++;
        };

        if (conv=='d'||conv=='i'||conv=='u'||conv=='x'||conv=='X'||conv=='o'||conv=='p') {
            while (ip < in.size() && sws(in[ip])) ip++;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            int base = (conv=='x'||conv=='X'||conv=='p') ? 16 : conv=='o' ? 8 : conv=='i' ? 0 : 10;
            std::string cand = in.substr(ip, width ? (size_t)width : std::string::npos);
            char* end = nullptr;
            unsigned long long v = std::strtoull(cand.c_str(), &end, base);
            size_t used = end - cand.c_str();
            if (used == 0) break;              // match failure
            ip += used;
            store_int(v);
        } else if (conv=='f'||conv=='e'||conv=='g'||conv=='E'||conv=='G'||conv=='a') {
            while (ip < in.size() && sws(in[ip])) ip++;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            std::string cand = in.substr(ip, width ? (size_t)width : std::string::npos);
            char* end = nullptr;
            double v = std::strtod(cand.c_str(), &end);
            size_t used = end - cand.c_str();
            if (used == 0) break;
            ip += used;
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                if (len==3||len==5) { double d=v; put(ptr,&d,8); }
                else { float f=(float)v; put(ptr,&f,4); }
                assigned++;
            }
        } else if (conv=='s') {
            while (ip < in.size() && sws(in[ip])) ip++;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            size_t start = ip;
            while (ip < in.size() && !sws(in[ip]) && (!width || (int)(ip-start) < width)) ip++;
            if (ip == start) break;
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                put(ptr, in.data()+start, (uint32_t)(ip-start));
                uint8_t z = 0; put(ptr + (ip-start), &z, 1);
                assigned++;
            }
        } else if (conv=='c') {
            int w = width ? width : 1;
            if (ip >= in.size()) { if (!assigned) eof_before_assign = true; break; }
            int n = 0; size_t start = ip;
            while (ip < in.size() && n < w) { ip++; n++; }
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                put(ptr, in.data()+start, (uint32_t)n);
                assigned++;
            }
        } else if (conv=='[') {
            bool neg = false;
            if (fp < fmt.size() && fmt[fp]=='^') { neg = true; fp++; }
            bool set[256] = {false};
            bool first = true;
            while (fp < fmt.size() && (fmt[fp] != ']' || first)) {
                if (fp+2 < fmt.size() && fmt[fp+1]=='-' && fmt[fp+2]!=']') {
                    for (int c=(unsigned char)fmt[fp]; c<=(unsigned char)fmt[fp+2]; ++c) set[c]=true;
                    fp += 3;
                } else { set[(unsigned char)fmt[fp]] = true; fp++; }
                first = false;
            }
            if (fp < fmt.size() && fmt[fp]==']') fp++;
            size_t start = ip;
            while (ip < in.size() && (set[(unsigned char)in[ip]] != neg) &&
                   (!width || (int)(ip-start) < width)) ip++;
            if (ip == start) break;
            if (!suppress) {
                uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                put(ptr, in.data()+start, (uint32_t)(ip-start));
                uint8_t z = 0; put(ptr + (ip-start), &z, 1);
                assigned++;
            }
        } else if (conv=='n') {
            if (!suppress) { uint32_t ptr = TrapLayer::arg_at(m, esp, argidx++);
                             uint32_t v = (uint32_t)ip; put(ptr, &v, 4); }
        } else break;                          // unknown conversion
    }
    return (assigned == 0 && eof_before_assign) ? (uint32_t)-1 : (uint32_t)assigned;
}
}  // namespace

using namespace guestmap;

TrapLayer::TrapLayer(std::vector<std::string> names)
    : names_(std::move(names)), hits_(names_.size(), 0), heap_next_(HEAP_BASE) {
    if (const char* e = std::getenv("THEOC_DATA")) data_root_ = e;
    else data_root_ = "data/game";
    register_builtins();
}

TrapLayer::~TrapLayer() {
    if (audio_dev_) {
        SDL_CloseAudioDevice(audio_dev_);
        audio_dev_ = 0;
    }
    for (auto& [_, f] : files_)
        if (f.fp && f.fp != stdin && f.fp != stdout && f.fp != stderr) std::fclose(f.fp);
    for (auto& [_, f] : fds_)
        if (f.host_fd >= 0) ::close(f.host_fd);
}

void TrapLayer::ensure_audio() {
    if (audio_dev_) return;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "  [audio] SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = 22050;
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &TrapLayer::audio_callback;
    want.userdata = this;
    audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!audio_dev_) {
        std::fprintf(stderr, "  [audio] OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(audio_dev_, 0);
    std::printf("  [audio] SDL device %u @ %d Hz %d ch\n",
                audio_dev_, have.freq, have.channels);
}

void TrapLayer::audio_callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<TrapLayer*>(userdata);
    auto* out = reinterpret_cast<int16_t*>(stream);
    int nsamp = len / 2;
    std::lock_guard<std::mutex> lock(self->audio_mu_);
    for (int i = 0; i < nsamp; ++i) {
        if (!self->audio_q_.empty()) {
            out[i] = self->audio_q_.front();
            self->audio_q_.pop_front();
        } else {
            out[i] = 0;
        }
    }
}

void TrapLayer::audio_push(const void* data, size_t nbytes) {
    if (!audio_dev_ || !data || !nbytes) return;
    // Assume little-endian 16-bit PCM (game SoundCard_Linux / SoftwareMix default).
    const auto* s = static_cast<const int16_t*>(data);
    size_t n = nbytes / 2;
    std::lock_guard<std::mutex> lock(audio_mu_);
    // Cap queue ~1s @ 22k stereo to avoid unbounded growth if callback lags.
    const size_t cap = 22050 * 2;
    for (size_t i = 0; i < n; ++i) {
        if (audio_q_.size() >= cap) audio_q_.pop_front();
        audio_q_.push_back(s[i]);
    }
}

std::string TrapLayer::resolve_path(const std::string& guest) const {
    if (guest.empty()) return guest;
    // Device nodes are never real files on the Mac host.
    if (guest.rfind("/dev/", 0) == 0) return guest;
    // Linux install default mountpoint — map to the CD tree we ship.
    // VM_GetCDRomName opens `$mount/cd.key` and checks for "Theocracy".
    const char* cd = std::getenv("THEOC_CD");
    std::string cd_root = cd ? cd : "data/cd";
    if (guest.rfind("/mnt/cdrom", 0) == 0) {
        std::string rest = guest.size() > 10 ? guest.substr(10) : "";
        if (rest.empty() || rest == "/") return cd_root;
        if (rest[0] == '/') return cd_root + rest;
        return cd_root + "/" + rest;
    }
    // Intro/logo MPEG cutscenes live on the CD, not in the installed data tree.
    // Game asks for "movie/ubi_logo.mpg", "logo.mpg", "intro.mpg", etc.
    if (guest.rfind("movie/", 0) == 0 || guest.rfind("movie\\", 0) == 0)
        return cd_root + "/" + guest;
    if (guest.size() > 4) {
        auto ends = [&](const char* ext) {
            auto n = std::strlen(ext);
            return guest.size() >= n &&
                   guest.compare(guest.size() - n, n, ext) == 0;
        };
        if (ends(".mpg") || ends(".MPG") || ends(".mpeg"))
            return cd_root + "/movie/" + guest;
    }
    if (guest[0] == '/') return guest;                    // other absolute paths
    // Guest uses "data/…"; install root is $THEOC_DATA (default data/game).
    return data_root_ + "/" + guest;
}

void TrapLayer::set_errno(Machine& m, int err) {
    m.w32(ERRNO_ADDR, (uint32_t)err);
}

uint32_t TrapLayer::bump_alloc(uint32_t size) {
    uint32_t aligned = (size + 15u) & ~15u;   // 16-byte aligned
    if (heap_next_ + aligned > HEAP_BASE + HEAP_SIZE) {
        std::fprintf(stderr, "[heap] OUT OF MEMORY requesting %u bytes\n", size);
        return 0;
    }
    uint32_t p = heap_next_;
    heap_next_ += aligned;
    alloc_sz_[p] = size;
    return p;
}

uint32_t TrapLayer::stub_alloc(Machine& m, uint32_t size) {
    if (!stub_next_) {
        try {
            m.map(STUB_CODE, STUB_CODE_SIZE, UC_PROT_READ | UC_PROT_EXEC | UC_PROT_WRITE);
        } catch (...) {
            std::fprintf(stderr, "[stub] map RX page failed\n");
            return 0;
        }
        // Keep writeable for install; Unicorn needs W to write then can leave WX.
        stub_next_ = STUB_CODE;
    }
    uint32_t aligned = (size + 15u) & ~15u;
    if (stub_next_ + aligned > STUB_CODE + STUB_CODE_SIZE) {
        std::fprintf(stderr, "[stub] OUT OF STUB SPACE\n");
        return 0;
    }
    uint32_t p = stub_next_;
    stub_next_ += aligned;
    return p;
}

// Best-effort printf: pulls 32-bit varargs off the stack starting at argidx.
// Enough for diagnostics during bring-up; not a conformant implementation.
std::string TrapLayer::format(Machine& m, const std::string& fmt, uint32_t esp,
                              int argidx) {
    std::string out;
    char tmp[64];
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') { out.push_back(fmt[i]); continue; }
        size_t j = i + 1;
        while (j < fmt.size() && strchr("-+ #0123456789.lhLzq", fmt[j])) ++j;
        if (j >= fmt.size()) break;
        char conv = fmt[j];
        switch (conv) {
            case '%': out.push_back('%'); break;
            case 'd': case 'i':
                std::snprintf(tmp, sizeof tmp, "%d", (int)arg(m, esp, argidx++));
                out += tmp; break;
            case 'u':
                std::snprintf(tmp, sizeof tmp, "%u", arg(m, esp, argidx++));
                out += tmp; break;
            case 'x': case 'X': case 'p':
                std::snprintf(tmp, sizeof tmp, conv == 'X' ? "%X" : "%x",
                              arg(m, esp, argidx++));
                out += tmp; break;
            case 'c': out.push_back((char)arg(m, esp, argidx++)); break;
            case 's': {
                uint32_t p = arg(m, esp, argidx++);
                out += p ? m.cstr(p) : "(null)";
                break;
            }
            case 'f': case 'g': case 'e':   // double = 8 bytes on the stack
                out += "<float>"; argidx += 2; break;
            default: out.push_back('%'); out.push_back(conv); break;
        }
        i = j;
    }
    return out;
}

void TrapLayer::register_builtins() {
    auto& t = table_;

    t["malloc"] = [this](Machine& m, uint32_t esp) {
        return bump_alloc(arg(m, esp, 0));
    };
    t["calloc"] = [this](Machine& m, uint32_t esp) {
        uint32_t n = arg(m, esp, 0) * arg(m, esp, 1);
        uint32_t p = bump_alloc(n);
        if (p && n) { std::vector<uint8_t> z(n, 0); m.write(p, z.data(), n); }
        return p;
    };
    t["realloc"] = [this](Machine& m, uint32_t esp) {
        uint32_t old = arg(m, esp, 0), nsz = arg(m, esp, 1);
        uint32_t p = bump_alloc(nsz);
        if (p && old) {
            auto it = alloc_sz_.find(old);
            uint32_t cpy = it != alloc_sz_.end() ? std::min(it->second, nsz) : 0;
            for (uint32_t k = 0; k < cpy; ++k) { uint8_t b; m.read(old + k, &b, 1); m.write(p + k, &b, 1); }
        }
        return p;
    };
    t["free"] = [](Machine&, uint32_t) -> uint32_t { return 0; };  // bump: no-op
    t["__builtin_vec_delete"] = [](Machine&, uint32_t) -> uint32_t { return 0; };

    t["memcpy"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> b(n);
        if (n) { m.read(s, b.data(), n); m.write(d, b.data(), n); }
        return d;
    };
    t["memmove"] = t["memcpy"];
    t["memset"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), c = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> b(n, (uint8_t)c);
        if (n) m.write(d, b.data(), n);
        return d;
    };
    t["strcpy"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1);
        std::string v = m.cstr(s);
        m.write(d, v.c_str(), (uint32_t)v.size() + 1);
        return d;
    };
    t["strncpy"] = [](Machine& m, uint32_t esp) {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1), n = arg(m, esp, 2);
        if (!d || !n) return d;
        std::string v = m.cstr(s, n);
        uint32_t copy = (uint32_t)std::min<size_t>(v.size(), n);
        if (copy) m.write(d, v.c_str(), copy);
        // C99: pad with zeros up to n if src shorter.
        if (copy < n) {
            std::vector<uint8_t> z(n - copy, 0);
            m.write(d + copy, z.data(), n - copy);
        }
        return d;
    };
    t["strlen"] = [](Machine& m, uint32_t esp) {
        return (uint32_t)m.cstr(arg(m, esp, 0)).size();
    };
    t["strcmp"] = [](Machine& m, uint32_t esp) {
        return (uint32_t)(int32_t)std::strcmp(m.cstr(arg(m, esp, 0)).c_str(),
                                              m.cstr(arg(m, esp, 1)).c_str());
    };
    // strtok(str, delim): tokenizes in place. str==NULL continues from the saved
    // position; writes '\0' over the terminating delimiter and returns a pointer
    // into the guest buffer. Static `saved` holds the guest resume address.
    t["strtok"] = [](Machine& m, uint32_t esp) -> uint32_t {
        static uint32_t saved = 0;
        uint32_t s = arg(m, esp, 0);
        std::string delim = m.cstr(arg(m, esp, 1));
        bool isdelim[256] = {false};
        for (unsigned char c : delim) isdelim[c] = true;
        uint32_t p = s ? s : saved;
        if (!p) return 0;
        uint8_t c;
        while (true) { m.read(p, &c, 1); if (!c || !isdelim[c]) break; p++; }  // skip leading
        if (!c) { saved = 0; return 0; }
        uint32_t tok = p;
        while (true) { m.read(p, &c, 1); if (!c || isdelim[c]) break; p++; }   // find end
        if (!c) { saved = 0; }
        else { uint8_t z = 0; m.write(p, &z, 1); saved = p + 1; }
        return tok;
    };

    t["puts"] = [](Machine& m, uint32_t esp) {
        std::printf("%s\n", m.cstr(arg(m, esp, 0)).c_str());
        return 1u;
    };
    t["printf"] = [this](Machine& m, uint32_t esp) {
        std::string s = format(m, m.cstr(arg(m, esp, 0)), esp, 1);
        std::fputs(s.c_str(), stdout);
        return (uint32_t)s.size();
    };
    t["fprintf"] = [this](Machine& m, uint32_t esp) {   // arg0 = FILE*, ignored
        std::string s = format(m, m.cstr(arg(m, esp, 1)), esp, 2);
        std::fputs(s.c_str(), stderr);
        return (uint32_t)s.size();
    };
    t["sprintf"] = [this](Machine& m, uint32_t esp) {
        uint32_t buf = arg(m, esp, 0);
        std::string s = format(m, m.cstr(arg(m, esp, 1)), esp, 2);
        m.write(buf, s.c_str(), (uint32_t)s.size() + 1);
        return (uint32_t)s.size();
    };
    t["sscanf"] = [](Machine& m, uint32_t esp) { return do_sscanf(m, esp); };

    t["__write"] = [](Machine& m, uint32_t esp) {
        uint32_t fd = arg(m, esp, 0), p = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> b(n);
        if (n) m.read(p, b.data(), n);
        std::fwrite(b.data(), 1, n, fd == 2 ? stderr : stdout);
        return n;
    };

    // ---- abort / exit -------------------------------------------------------
    // Fatal() ends in abort. Default (bring-up): log and return so the caller
    // can continue past non-critical Fatals. THEOC_LOUD_ABORT=1: dump a guest
    // backtrace and stop the current call() so a real fault surfaces here
    // instead of hiding as a silent OpenSubsystems restart / continued run.
    // exit/_exit always stop the current call() cleanly (no guest EH unwind).
    t["abort"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        static const bool loud = std::getenv("THEOC_LOUD_ABORT") != nullptr;
        if (!loud) {
            std::fprintf(stderr, "  [abort] ignored (bring-up; THEOC_LOUD_ABORT=1 to trap)\n");
            return 0;
        }
        const uint32_t mvos = mvos_base_ ? mvos_base_ : 0x10000000u;
        auto label = [mvos](uint32_t a) -> std::string {
            char b[48];
            if (a >= mvos && a < mvos + 0x200000)
                std::snprintf(b, sizeof b, "mvos+%#x", a - mvos);
            else if (a >= 0x08048000 && a < 0x08a00000)
                std::snprintf(b, sizeof b, "game %#010x", a);
            else
                std::snprintf(b, sizeof b, "%#010x", a);
            return b;
        };
        std::fprintf(stderr, "\n=== [abort] LOUD: guest abort()/Fatal — backtrace ===\n");
        try {
            std::fprintf(stderr, "  called from %s\n", label(m.r32(esp)).c_str());
        } catch (...) {}
        // Walk the g++ 2.95 EBP frame chain: [ebp]=saved ebp, [ebp+4]=ret addr.
        uint32_t ebp = m.reg(UC_X86_REG_EBP);
        for (int i = 0; i < 24 && ebp; ++i) {
            uint32_t ret = 0, next = 0;
            try { ret = m.r32(ebp + 4); next = m.r32(ebp); } catch (...) { break; }
            if (ret) std::fprintf(stderr, "  #%-2d %s\n", i, label(ret).c_str());
            if (next <= ebp) break;   // frame pointers must ascend, else bail
            ebp = next;
        }
        std::fprintf(stderr, "=== stopping call (unset THEOC_LOUD_ABORT to continue past) ===\n\n");
        m.request_stop();
        return 0;
    };
    auto stop = [](Machine& m, uint32_t) -> uint32_t {
        m.request_stop();
        return 0;
    };
    t["exit"]  = stop;
    t["_exit"] = stop;

    t["__errno_location"] = [](Machine& m, uint32_t) -> uint32_t {
        return ERRNO_ADDR;
    };

    t["getenv"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // Return 0 (NULL) — env not needed for asset load; avoids dangling host ptrs.
        (void)m; (void)esp;
        return 0;
    };

    t["strncmp"] = [](Machine& m, uint32_t esp) -> uint32_t {
        std::string a = m.cstr(arg(m, esp, 0)), b = m.cstr(arg(m, esp, 1));
        uint32_t n = arg(m, esp, 2);
        return (uint32_t)(int32_t)std::strncmp(a.c_str(), b.c_str(), n);
    };
    t["memcmp"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t a = arg(m, esp, 0), b = arg(m, esp, 1), n = arg(m, esp, 2);
        std::vector<uint8_t> A(n), B(n);
        if (n) { m.read(a, A.data(), n); m.read(b, B.data(), n); }
        return (uint32_t)(int32_t)std::memcmp(A.data(), B.data(), n);
    };
    t["__strtol_internal"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // glibc: strtol(nptr, endptr, base, group) — ignore group.
        std::string s = m.cstr(arg(m, esp, 0));
        uint32_t endp = arg(m, esp, 1);
        int base = (int)arg(m, esp, 2);
        char* end = nullptr;
        long v = std::strtol(s.c_str(), &end, base);
        if (endp && end) {
            uint32_t off = (uint32_t)(end - s.c_str());
            m.w32(endp, arg(m, esp, 0) + off);
        }
        return (uint32_t)v;
    };
    t["__strtod_internal"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // Returns double in st0 — use return_double. Simplified: return 0.0.
        std::string s = m.cstr(arg(m, esp, 0));
        uint32_t endp = arg(m, esp, 1);
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        if (endp && end) {
            uint32_t off = (uint32_t)(end - s.c_str());
            m.w32(endp, arg(m, esp, 0) + off);
        }
        m.return_double(v);
        return 0;
    };
    t["gettimeofday"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // struct timeval { time_t tv_sec; suseconds_t tv_usec; } — 32-bit each on i386.
        uint32_t tv = arg(m, esp, 0);
        struct timeval host{};
        gettimeofday(&host, nullptr);
        if (tv) {
            m.w32(tv, (uint32_t)host.tv_sec);
            m.w32(tv + 4, (uint32_t)host.tv_usec);
        }
        return 0;
    };
    t["usleep"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t us = arg(m, esp, 0);
        if (us > 100000) us = 100000;  // cap during bring-up
        ::usleep(us);
        return 0;
    };
    t["ioctl"] = [](Machine& m, uint32_t esp) -> uint32_t {
        // /dev/dsp probes etc. — succeed with zeros.
        (void)m; (void)esp;
        return 0;
    };
    t["strcat"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1);
        std::string ds = m.cstr(d), ss = m.cstr(s);
        std::string out = ds + ss;
        m.write(d, out.c_str(), (uint32_t)out.size() + 1);
        return d;
    };
    t["strncat"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t d = arg(m, esp, 0), s = arg(m, esp, 1), n = arg(m, esp, 2);
        std::string ds = m.cstr(d), ss = m.cstr(s);
        if (ss.size() > n) ss.resize(n);
        std::string out = ds + ss;
        m.write(d, out.c_str(), (uint32_t)out.size() + 1);
        return d;
    };
    // Minimal BSD sockets — enough for Start's localhost:5043 single-instance lock.
    t["socket"] = [](Machine&, uint32_t) -> uint32_t { return 32; };  // fake fd
    t["bind"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["listen"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["accept"] = [](Machine&, uint32_t) -> uint32_t { return 33; };
    t["connect"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["send"] = [](Machine& m, uint32_t esp) -> uint32_t { return arg(m, esp, 2); };
    t["recv"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["recvfrom"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["sendto"] = [](Machine& m, uint32_t esp) -> uint32_t { return arg(m, esp, 2); };
    t["htons"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t v = arg(m, esp, 0) & 0xffff;
        return ((v & 0xff) << 8) | (v >> 8);
    };
    t["ntohs"] = t["htons"];
    t["pipe"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t p = arg(m, esp, 0);
        if (p) { m.w32(p, 40); m.w32(p + 4, 41); }
        return 0;
    };
    t["fcntl"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["select"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["gethostbyname"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    for (const char* nm : {"sem_init", "sem_destroy", "sem_post", "sem_wait",
                           "sem_trywait", "sem_getvalue",
                           "sigemptyset", "sigaddset", "signal", "kill", "waitpid",
                           "fork", "execlp"})
        t[nm] = [](Machine&, uint32_t) -> uint32_t { return 0; };

    // setitimer(ITIMER_REAL) — guest cLinuxTimer. Host polls and calls
    // TimerSystem::Proc (normally SIGALRM → _TimerFunction).
    t["setitimer"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        // int setitimer(int which, const struct itimerval *new, struct itimerval *old);
        uint32_t which = arg(m, esp, 0);
        uint32_t newp  = arg(m, esp, 1);
        uint32_t oldp  = arg(m, esp, 2);
        (void)which;  // only ITIMER_REAL (0) used
        auto write_old = [&]() {
            if (!oldp) return;
            // itimerval: interval {sec,usec}, value {sec,usec} — i386 16 bytes
            auto us = timer_interval_.count();
            uint32_t isec = (uint32_t)(us / 1'000'000), iusec = (uint32_t)(us % 1'000'000);
            us = timer_value_.count();
            uint32_t vsec = (uint32_t)(us / 1'000'000), vusec = (uint32_t)(us % 1'000'000);
            m.w32(oldp + 0, isec); m.w32(oldp + 4, iusec);
            m.w32(oldp + 8, vsec); m.w32(oldp + 12, vusec);
        };
        write_old();
        if (!newp) return 0;
        uint32_t isec = m.r32(newp + 0), iusec = m.r32(newp + 4);
        uint32_t vsec = m.r32(newp + 8), vusec = m.r32(newp + 12);
        timer_interval_ = std::chrono::microseconds(
            (int64_t)isec * 1'000'000LL + (int64_t)iusec);
        timer_value_ = std::chrono::microseconds(
            (int64_t)vsec * 1'000'000LL + (int64_t)vusec);
        if (timer_value_.count() == 0 && timer_interval_.count() == 0) {
            timer_armed_ = false;
            static int n;
            if (n++ < 4) std::printf("  [timer] setitimer disarmed\n");
            return 0;
        }
        // One-shot value; reload from interval after each fire (POSIX).
        auto period = timer_value_.count() ? timer_value_ : timer_interval_;
        if (period.count() <= 0) period = std::chrono::milliseconds(20);
        if (timer_interval_.count() <= 0) timer_interval_ = period;
        timer_next_ = std::chrono::steady_clock::now() + period;
        timer_armed_ = true;
        if (!sigalrm_handler_ && mvos_base_)
            sigalrm_handler_ = mvos_base_ + 0x922e0;  // _TimerFunction__Fi
        static int n;
        if (n++ < 6)
            std::printf("  [timer] setitimer value=%lld us interval=%lld us handler=%#x\n",
                        (long long)timer_value_.count(),
                        (long long)timer_interval_.count(),
                        sigalrm_handler_);
        return 0;
    };

    t["sigaction"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        // int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
        uint32_t sig = arg(m, esp, 0);
        uint32_t act = arg(m, esp, 1);
        // Linux SIGALRM = 14. sa_handler is first word of struct sigaction.
        if (sig == 14 && act) {
            uint32_t h = m.r32(act);
            if (h && h != 0 && h != 1) {  // not SIG_DFL/SIG_IGN
                sigalrm_handler_ = h;
                static int n;
                if (n++ < 4)
                    std::printf("  [timer] sigaction SIGALRM handler=%#x\n", h);
            }
        }
        return 0;
    };

    // pthread_create: queue as a soft thread. cSoundCard_Linux::Launch relies on
    // this to start Main (mixer → write /dev/dsp). We don't spawn host threads
    // (Unicorn is single-threaded); HLE_SwapBuffers green-runs Entry each frame.
    t["pthread_create"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        // int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
        //                    void *(*start_routine)(void *), void *arg);
        uint32_t thread_out = arg(m, esp, 0);
        uint32_t entry = arg(m, esp, 2);
        uint32_t targ = arg(m, esp, 3);
        if (thread_out) m.w32(thread_out, 0x70000000u + (uint32_t)soft_threads_.size());
        if (entry && targ) {
            soft_threads_.push_back({entry, targ});
            patch_sound_main_oneshot(m);
            static int n;
            if (n++ < 6)
                std::printf("  [thread] soft-thread entry=%#x arg=%#x (n=%zu)\n",
                            entry, targ, soft_threads_.size());
        }
        return 0;  // success — Launch keeps cThread.running = 1
    };
    t["pthread_join"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["pthread_detach"] = [](Machine&, uint32_t) -> uint32_t { return 0; };

    // ---- SMPEG (Philos/Loki MPEG) — real decode via libav, or skip ----------
    // SMPEGstatus: ERROR=-1, STOPPED=0, PLAYING=1. THEOC_SKIP_MOVIES=1 forces
    // open-ok + immediate STOPPED (no decode). Guest handle: +0 magic, +4 status.
    t["SMPEG_new"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string file = m.cstr(arg(m, esp, 0));
        uint32_t info = arg(m, esp, 1);
        std::string host = resolve_path(file);
        bool skip = std::getenv("THEOC_SKIP_MOVIES") != nullptr;
        bool exists = false;
        {
            FILE* fp = std::fopen(host.c_str(), "rb");
            if (fp) { std::fclose(fp); exists = true; }
            else {
                fp = std::fopen(file.c_str(), "rb");
                if (fp) { std::fclose(fp); exists = true; host = file; }
            }
        }
        if (!exists && !skip) {
            smpeg_error_ = "no movie found " + file + " (host " + host + ")";
            std::printf("  [smpeg] SMPEG_new FAIL '%s' -> %s\n",
                        file.c_str(), host.c_str());
            return 0;
        }
        smpeg_error_.clear();
        uint32_t h = bump_alloc(32);
        if (!h) return 0;
        m.w32(h + 0, 1);   // magic
        m.w32(h + 4, 0);   // STOPPED until startplayvideo
        int w = 640, ht = 480;
        double fps = 15.0;
        if (!skip && exists) {
            if (mpeg_.load(h, host)) {
                if (auto* mov = mpeg_.get(h)) {
                    w = mov->width; ht = mov->height; fps = mov->fps;
                }
            } else {
                std::printf("  [smpeg] decode failed, will skip frames\n");
            }
        }
        if (info) {
            m.w32(info + 0, 1);
            m.w32(info + 4, 2);
            m.w32(info + 8, 22050);
            m.w32(info + 12, 1);
            m.w32(info + 16, (uint32_t)w);
            m.w32(info + 20, (uint32_t)ht);
            m.w32(info + 24, 0);
            m.write(info + 28, &fps, 8);
        }
        std::printf("  [smpeg] SMPEG_new OK '%s'%s\n",
                    file.c_str(), skip ? " [THEOC_SKIP_MOVIES]" : "");
        return h;
    };
    t["SMPEG_error"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t mpeg = arg(m, esp, 0);
        if (!mpeg || !smpeg_error_.empty()) {
            if (smpeg_error_.empty()) smpeg_error_ = "SMPEG null";
            uint32_t p = bump_alloc((uint32_t)smpeg_error_.size() + 1);
            if (p) m.write(p, smpeg_error_.c_str(),
                           (uint32_t)smpeg_error_.size() + 1);
            return p;
        }
        return 0;
    };
    t["SMPEG_status"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        if (!h) return (uint32_t)-1;
        auto* mov = mpeg_.get(h);
        if (!mov || !mov->playing) return 0;  // STOPPED
        if (mov->frame_i >= mov->frames.size()) {
            mov->playing = false;
            m.w32(h + 4, 0);
            return 0;
        }
        return 1;  // PLAYING
    };
    t["SMPEG_delete"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        if (h) mpeg_.erase(h);
        return 0;
    };
    t["SMPEG_enablevideo"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_enableaudio"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_setvolume"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_setdisplay"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0), disp = arg(m, esp, 1);
        if (auto* mov = mpeg_.get(h)) mov->display = disp;
        return 0;
    };
    t["SMPEG_move"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        int x = (int)arg(m, esp, 1), y = (int)arg(m, esp, 2);
        if (auto* mov = mpeg_.get(h)) { mov->move_x = x; mov->move_y = y; }
        return 0;
    };
    t["SMPEG_startplayvideo"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        auto* mov = mpeg_.get(h);
        if (!mov || mov->frames.empty()) return 0;
        mov->playing = true;
        mov->frame_i = 0;
        mov->audio_pos = 0;
        if (mov->has_audio) ensure_audio();   // open the SDL device for cutscene sound
        // First frame shows immediately; subsequent frames wait on next_frame_at.
        mov->next_frame_at = std::chrono::steady_clock::now();
        m.w32(h + 4, 1);
        return 1;
    };
    t["SMPEG_playvideoframe"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        auto* mov = mpeg_.get(h);
        if (!mov || !mov->playing || mov->frame_i >= mov->frames.size())
            return 0;

        // Pace to movie fps. External_PlayAnim's loop is free-running
        // (playvideoframe + status only) — without a host delay cutscenes run
        // as fast as Unicorn can blit (~10×). Hold until next_frame_at, then
        // schedule the following frame.
        {
            double fps = mov->fps > 1.0 ? mov->fps : 15.0;
            using clock = std::chrono::steady_clock;
            auto frame_dt = std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / fps));
            auto now = clock::now();
            if (mov->frame_i > 0 && now < mov->next_frame_at) {
                while (clock::now() < mov->next_frame_at) {
                    auto left = mov->next_frame_at - clock::now();
                    int ms = (int)std::chrono::duration_cast<
                        std::chrono::milliseconds>(left).count();
                    if (ms > 16) ms = 16;
                    if (ms < 1) ms = 1;
                    SDL_Delay((Uint32)ms);
                    video_.pump();  // keep window responsive during hold
                }
            }
            now = clock::now();
            if (mov->frame_i == 0)
                mov->next_frame_at = now + frame_dt;
            else {
                mov->next_frame_at += frame_dt;
                // If we fell more than 2 frames behind, resync (avoid spiral).
                if (now > mov->next_frame_at + 2 * frame_dt)
                    mov->next_frame_at = now + frame_dt;
            }
        }

        // Blit RGB565 frame into cDisplay::Address (or guest FB fallback).
        uint32_t dst = 0;
        uint16_t pitch = 0, dw = 0, dh = 0;
        if (mov->display) {
            dst = m.r32(mov->display + 0x00);
            m.read(mov->display + 0x04, &dw, 2);
            m.read(mov->display + 0x06, &dh, 2);
            m.read(mov->display + 0x08, &pitch, 2);
        }
        if (!dst) {
            dst = GUEST_FB_BASE;
            pitch = (uint16_t)(mov->width * 2);
            dw = (uint16_t)mov->width;
            dh = (uint16_t)mov->height;
        }
        const auto& fr = mov->frames[mov->frame_i++];
        int copy_w = mov->width;
        int copy_h = mov->height;
        if (dw && copy_w > dw) copy_w = dw;
        if (dh && copy_h > dh) copy_h = dh;
        if (pitch < copy_w * 2) pitch = (uint16_t)(copy_w * 2);
        for (int y = 0; y < copy_h; ++y) {
            m.write(dst + (uint32_t)y * pitch,
                    fr.data() + (size_t)y * (size_t)mov->width,
                    (uint32_t)copy_w * 2);
        }
        // Feed this frame's audio into the host mixer. Video is paced to fps
        // (the hold loop above sleeps in real time), so pushing one frame's
        // worth of samples per frame tracks the SDL callback's 22050 Hz drain.
        // Keep ~2 frames of lead so the callback never underruns mid-cutscene.
        if (mov->has_audio && !mov->audio.empty()) {
            double fps = mov->fps > 1.0 ? mov->fps : 15.0;
            size_t chans = (size_t)mov->channels;              // 2 (stereo)
            size_t per_frame = (size_t)(mov->samplerate / fps + 0.5) * chans;
            size_t frames_shown = mov->frame_i;                // already advanced
            size_t target = (frames_shown + 2) * per_frame;    // 2-frame lead
            if (target > mov->audio.size()) target = mov->audio.size();
            if (target > mov->audio_pos) {
                size_t n = target - mov->audio_pos;
                audio_push(mov->audio.data() + mov->audio_pos, n * sizeof(int16_t));
                if (mov->audio_pos == 0)
                    std::printf("  [audio] cutscene sound: %zu samp @ %d Hz\n",
                                mov->audio.size() / chans, mov->samplerate);
                mov->audio_pos = target;
            }
        }

        // Present so the user sees the cutscene (play loop may not call
        // SwapBuffers between frames).
        if (video_.is_open()) {
            uint32_t nbytes = video_.fb_bytes();
            if (nbytes > GUEST_FB_SIZE) nbytes = GUEST_FB_SIZE;
            m.read(GUEST_FB_BASE, video_.fb(), nbytes);
            video_.present();
        }
        if (mov->frame_i >= mov->frames.size()) {
            mov->playing = false;
            m.w32(h + 4, 0);
        }
        return 1;
    };
    t["SMPEG_startplayaudio"] = [](Machine&, uint32_t) -> uint32_t { return 0; };
    t["SMPEG_playAudio"] = [](Machine&, uint32_t) -> uint32_t { return 0; };

    // ---- cDisplay (Philos SMPEG framebuffer descriptor) --------------------
    // Defined in smpeg-philos/MPEGextra.{h,cpp}; provided by libsmpeg on Linux.
    // libmvos External_PlayAnim constructs one for the current LFB and passes
    // it to SMPEG_setdisplay. Layout (g++ 2.95, no vtable):
    //   +0x00 Address (void*)  +0x04 Width (u16)  +0x06 Height (u16)
    //   +0x08 Pitch (u16)      +0x0c RedMask (u32) +0x10 GreenMask
    //   +0x14 BlueMask         +0x18 BitsPerPixel (u8)
    // Without this, a zeroed object → pitch 0 → MemBlock.Alloc(0) Fatal, and
    // Start's recovery re-opens subsystems in a loop (looks like a freeze).
    t["__8cDisplayPvUsUsUsUcUiUlUl"] = [](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t self  = arg(m, esp, 0);
        uint32_t addr  = arg(m, esp, 1);
        uint16_t w     = (uint16_t)arg(m, esp, 2);
        uint16_t h     = (uint16_t)arg(m, esp, 3);
        uint16_t pitch = (uint16_t)arg(m, esp, 4);
        uint8_t  bpp   = (uint8_t)arg(m, esp, 5);
        uint32_t red   = arg(m, esp, 6);
        uint32_t green = arg(m, esp, 7);
        uint32_t blue  = arg(m, esp, 8);
        if (self) {
            m.w32(self + 0x00, addr);
            m.write(self + 0x04, &w, 2);
            m.write(self + 0x06, &h, 2);
            m.write(self + 0x08, &pitch, 2);
            m.w32(self + 0x0c, red);
            m.w32(self + 0x10, green);
            m.w32(self + 0x14, blue);
            m.write(self + 0x18, &bpp, 1);
        }
        static int n;
        if (n++ < 6)
            std::printf("  [HLE] cDisplay @%#x fb=%#x %ux%u pitch=%u bpp=%u\n",
                        self, addr, w, h, pitch, bpp);
        return self;  // g++ 2.95 ctor returns this
    };

    // ---- libdl: synthetic plugins (no real X11 .so load) --------------------
    t["dlopen"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        // Accept any libmvos_*_x.so / glide name.
        if (path.find("libmvos_") == std::string::npos && path.find("vvc") == std::string::npos) {
            last_dlerror_ = "synthetic dlopen: unknown " + path;
            static int n;
            if (n++ < 8) std::fprintf(stderr, "  [dlopen] reject '%s'\n", path.c_str());
            return 0;
        }
        uint32_t h = next_dl_handle_++;
        dl_handles_[h] = path;
        last_dlerror_.clear();
        std::printf("  [dlopen] '%s' -> handle %#x (synthetic)\n", path.c_str(), h);
        return h;
    };
    t["dlclose"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        dl_handles_.erase(arg(m, esp, 0));
        return 0;
    };
    t["dlerror"] = [this](Machine& m, uint32_t) -> uint32_t {
        if (last_dlerror_.empty()) return 0;
        // Return a stable guest string.
        uint32_t p = bump_alloc((uint32_t)last_dlerror_.size() + 1);
        if (p) m.write(p, last_dlerror_.c_str(), (uint32_t)last_dlerror_.size() + 1);
        return p;
    };
    t["dlsym"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t h = arg(m, esp, 0);
        std::string name = m.cstr(arg(m, esp, 1));
        if (!dl_handles_.count(h) && h != 0) {
            last_dlerror_ = "dlsym: bad handle";
            return 0;
        }
        // Map export name → plugin trap slot.
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i) {
            if (plugin_exports_[i] == name)
                return plugin_trap_base_ + i;
        }
        last_dlerror_ = "dlsym: " + name;
        static int n;
        if (n++ < 12)
            std::fprintf(stderr, "  [dlsym] miss '%s'\n", name.c_str());
        return 0;
    };

    // ---- FILE* stdio --------------------------------------------------------
    t["fopen"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        std::string mode = m.cstr(arg(m, esp, 1));
        if (path.rfind("/dev/", 0) == 0) {
            // Stub device: opaque FILE*, all I/O no-ops / zeros.
            uint32_t gp = bump_alloc(128);
            if (gp) { std::vector<uint8_t> z(128, 0); m.write(gp, z.data(), 128); }
            files_[gp] = HostFile{nullptr, -1, true, false, false};
            return gp;
        }
        std::string host = resolve_path(path);
        // Ensure parent dirs for writes (save/saveN.tsg, etc.).
        bool writing = mode.find('w') != std::string::npos ||
                       mode.find('a') != std::string::npos ||
                       mode.find('+') != std::string::npos;
        if (writing) {
            auto slash = host.find_last_of('/');
            if (slash != std::string::npos && slash > 0) {
                std::string dir = host.substr(0, slash), cur;
                for (size_t i = 0; i < dir.size(); ++i) {
                    cur.push_back(dir[i]);
                    if (dir[i] == '/' && cur.size() > 1)
                        ::mkdir(cur.c_str(), 0755);
                }
                if (!dir.empty() && dir.back() != '/')
                    ::mkdir(dir.c_str(), 0755);
            }
        }
        FILE* fp = std::fopen(host.c_str(), mode.c_str());
        if (!fp) {
            // Fallback: try path as-is relative to cwd.
            fp = std::fopen(path.c_str(), mode.c_str());
        }
        if (!fp) {
            set_errno(m, ENOENT);
            static int nmiss;
            if (nmiss++ < 20)
                std::fprintf(stderr, "  [fopen] miss '%s' (host '%s')\n",
                             path.c_str(), host.c_str());
            return 0;
        }
        uint32_t gp = bump_alloc(128);
        if (gp) { std::vector<uint8_t> z(128, 0); m.write(gp, z.data(), 128); }
        files_[gp] = HostFile{fp, -1, false, false, false};
        return gp;
    };

    t["fclose"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end()) return (uint32_t)-1;
        if (it->second.fp) std::fclose(it->second.fp);
        files_.erase(it);
        return 0;
    };

    t["fread"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t ptr = arg(m, esp, 0), size = arg(m, esp, 1),
                 nmemb = arg(m, esp, 2), gp = arg(m, esp, 3);
        auto it = files_.find(gp);
        if (it == files_.end()) return 0;
        if (it->second.stub) {
            // Zero-fill for device stubs.
            uint32_t n = size * nmemb;
            if (n) { std::vector<uint8_t> z(n, 0); m.write(ptr, z.data(), n); }
            return nmemb;
        }
        if (!it->second.fp) return 0;
        uint32_t n = size * nmemb;
        std::vector<uint8_t> buf(n);
        size_t got = std::fread(buf.data(), size, nmemb, it->second.fp);
        if (got) m.write(ptr, buf.data(), (uint32_t)(got * size));
        if (std::feof(it->second.fp)) it->second.eof = true;
        return (uint32_t)got;
    };

    t["fwrite"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t ptr = arg(m, esp, 0), size = arg(m, esp, 1),
                 nmemb = arg(m, esp, 2), gp = arg(m, esp, 3);
        auto it = files_.find(gp);
        if (it == files_.end()) return 0;
        if (it->second.stub || !it->second.fp) return nmemb;  // pretend OK
        uint32_t n = size * nmemb;
        std::vector<uint8_t> buf(n);
        if (n) m.read(ptr, buf.data(), n);
        return (uint32_t)std::fwrite(buf.data(), size, nmemb, it->second.fp);
    };

    t["fseek"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        int32_t off = (int32_t)arg(m, esp, 1);
        int whence = (int)arg(m, esp, 2);
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return 0;
        return (uint32_t)std::fseek(it->second.fp, off, whence);
    };

    t["ftell"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return 0;
        long p = std::ftell(it->second.fp);
        return p < 0 ? (uint32_t)-1 : (uint32_t)p;
    };

    t["feof"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end()) return 1;
        if (it->second.stub) return it->second.eof ? 1 : 0;
        if (!it->second.fp) return 1;
        return std::feof(it->second.fp) ? 1 : 0;
    };

    t["fflush"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        if (!gp) { std::fflush(nullptr); return 0; }
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return 0;
        return (uint32_t)std::fflush(it->second.fp);
    };

    t["_IO_getc"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        uint32_t gp = arg(m, esp, 0);
        auto it = files_.find(gp);
        if (it == files_.end() || it->second.stub || !it->second.fp) return (uint32_t)EOF;
        int c = std::fgetc(it->second.fp);
        return (uint32_t)c;
    };

    // ---- POSIX open/read/write/close ----------------------------------------
    t["open"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        int flags = (int)arg(m, esp, 1);
        if (path.rfind("/dev/", 0) == 0) {
            int gfd = next_fd_++;
            bool is_dsp = (path.find("dsp") != std::string::npos ||
                           path.find("audio") != std::string::npos);
            if (is_dsp) ensure_audio();
            fds_[gfd] = HostFile{nullptr, -1, true, is_dsp, false};
            if (is_dsp)
                std::printf("  [audio] open '%s' -> guest fd %d\n", path.c_str(), gfd);
            return (uint32_t)gfd;
        }
        std::string host = resolve_path(path);
        int hflags = O_RDONLY;
        if (flags & 1) hflags = O_WRONLY;       // O_WRONLY=1 on Linux i386
        if (flags & 2) hflags = O_RDWR;         // O_RDWR=2
        // O_CREAT etc. ignored for bring-up unless needed.
        int hfd = ::open(host.c_str(), hflags);
        if (hfd < 0) hfd = ::open(path.c_str(), hflags);
        if (hfd < 0) { set_errno(m, errno); return (uint32_t)-1; }
        int gfd = next_fd_++;
        fds_[gfd] = HostFile{nullptr, hfd, false, false, false};
        return (uint32_t)gfd;
    };

    t["close"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0);
        auto it = fds_.find(gfd);
        if (it == fds_.end()) return (uint32_t)-1;
        if (it->second.host_fd >= 0) ::close(it->second.host_fd);
        fds_.erase(it);
        return 0;
    };

    t["read"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0);
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        auto it = fds_.find(gfd);
        if (it == fds_.end()) return (uint32_t)-1;
        if (it->second.stub) {
            if (n) { std::vector<uint8_t> z(n, 0); m.write(buf, z.data(), n); }
            return n;                             // pretend device has data
        }
        if (it->second.host_fd < 0) return (uint32_t)-1;
        std::vector<uint8_t> b(n);
        ssize_t got = ::read(it->second.host_fd, b.data(), n);
        if (got < 0) { set_errno(m, errno); return (uint32_t)-1; }
        if (got) m.write(buf, b.data(), (uint32_t)got);
        return (uint32_t)got;
    };

    t["write"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        int gfd = (int)arg(m, esp, 0);
        uint32_t buf = arg(m, esp, 1), n = arg(m, esp, 2);
        if (gfd == 1 || gfd == 2) {
            std::vector<uint8_t> b(n);
            if (n) m.read(buf, b.data(), n);
            std::fwrite(b.data(), 1, n, gfd == 2 ? stderr : stdout);
            return n;
        }
        auto it = fds_.find(gfd);
        if (it == fds_.end()) return (uint32_t)-1;
        if (it->second.audio) {
            std::vector<uint8_t> b(n);
            if (n) m.read(buf, b.data(), n);
            audio_push(b.data(), n);
            static int nlog;
            if (nlog++ < 8)
                std::printf("  [audio] write %u bytes to dsp (q≈%zu samp)\n",
                            n, audio_q_.size());
            return n;
        }
        if (it->second.stub) return n;
        if (it->second.host_fd < 0) return (uint32_t)-1;
        std::vector<uint8_t> b(n);
        if (n) m.read(buf, b.data(), n);
        ssize_t w = ::write(it->second.host_fd, b.data(), n);
        if (w < 0) { set_errno(m, errno); return (uint32_t)-1; }
        return (uint32_t)w;
    };

    t["remove"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 0));
        std::string host = resolve_path(path);
        if (::unlink(host.c_str()) == 0) return 0;
        if (::unlink(path.c_str()) == 0) return 0;
        set_errno(m, errno);
        return (uint32_t)-1;
    };

    // Minimal stat for existence checks. __xstat(ver, path, statbuf).
    t["__xstat"] = [this](Machine& m, uint32_t esp) -> uint32_t {
        std::string path = m.cstr(arg(m, esp, 1));
        uint32_t sbuf = arg(m, esp, 2);
        if (path.rfind("/dev/", 0) == 0) {
            // Fake a char device stat (zeroed buffer is enough for most checks).
            if (sbuf) { std::vector<uint8_t> z(64, 0); m.write(sbuf, z.data(), 64); }
            return 0;
        }
        std::string host = resolve_path(path);
        struct stat st{};
        if (::stat(host.c_str(), &st) != 0 && ::stat(path.c_str(), &st) != 0) {
            set_errno(m, errno);
            return (uint32_t)-1;
        }
        // Linux i386 stat is not macOS stat — zero and fill a few portable fields
        // by approximate offsets is risky. For existence-only, zeroed success is OK
        // for many callers; fill st_mode/st_size at common Linux offsets if needed.
        // Linux i386 struct stat: st_mode @ 0x04? Actually varies. Write size at
        // several candidate offsets and mode as S_IFREG.
        std::vector<uint8_t> z(96, 0);
        uint32_t mode = (uint32_t)(S_IFREG | 0644);
        uint32_t sz = (uint32_t)st.st_size;
        // glibc2 i386 stat: st_mode at 4? Wait — use raw layout from linux:
        // Actually for i386 kernel stat: st_dev 0, st_ino 4, st_mode 8, ...
        // glibc uses xstat and may use different layout. Zero + return 0 is
        // enough when code only checks return value; if it reads st_size we may
        // need to refine. Put mode@16 size@20 as a common guess for old glibc.
        std::memcpy(z.data() + 16, &mode, 4);
        std::memcpy(z.data() + 20, &sz, 4);
        if (sbuf) m.write(sbuf, z.data(), (uint32_t)z.size());
        return 0;
    };

    // Harmless startup / teardown / pthread stubs (pthread_create is above —
    // soft-thread for the sound mixer).
    for (const char* nm : {"__libc_init_first", "pthread_key_delete",
                           "pthread_key_create", "pthread_getspecific",
                           "pthread_setspecific", "pthread_once",
                           "pthread_mutex_lock", "pthread_mutex_unlock",
                           "pthread_mutex_trylock", "pthread_cancel",
                           "monstartup", "_mcleanup", "atexit",
                           "__deregister_frame_info", "__register_frame_info",
                           "sigemptyset", "sigaddset", "signal",
                           "perror"})
        t[nm] = [](Machine&, uint32_t) -> uint32_t { return 0; };
}

bool TrapLayer::maybe_redirect_timer(Machine& m, uint32_t esp) {
    // Host-side SIGALRM without nested uc_emu_start (that crashes Unicorn).
    // Same pattern as sound: rewrite trap return into _TimerFunction(signo);
    // it returns into SwapBuffers__Fv after VVC present → AfterSwapBuffer.
    if (!timer_armed_ || !mvos_base_) return false;
    auto now = std::chrono::steady_clock::now();
    if (now < timer_next_) return false;

    // Advance schedule; collapse backlog so a slow frame doesn't storm.
    int skipped = 0;
    if (timer_interval_.count() <= 0) {
        timer_armed_ = false;
        return false;
    }
    while (timer_next_ <= now && skipped < 16) {
        timer_next_ += timer_interval_;
        skipped++;
    }
    if (timer_next_ < now)
        timer_next_ = now + timer_interval_;

    uint32_t fn = sigalrm_handler_;
    if (!fn) fn = mvos_base_ + 0x922e0;  // _TimerFunction__Fi
    uint32_t ret = m.r32(esp);
    // cdecl: [ret][signo]
    uint32_t sp = esp;
    sp -= 4; m.w32(sp, 14);
    sp -= 4; m.w32(sp, ret);
    m.redirect_guest(fn, sp);
    static int nlog;
    if (nlog++ < 8)
        std::printf("  [timer] redirect _TimerFunction (skipped schedule %d)\n", skipped);
    return true;
}

void TrapLayer::patch_sound_main_oneshot(Machine& m) {
    if (sound_main_patched_ || !mvos_base_) return;
    // Main__16cSoundCard_Linux @ 0x92b30 is a while(running) loop:
    //   92b3c: jmp 92b81   (test first)
    //   92b40: body (Do mix + write)
    //   92b81: test running; 92b8e: jne 92b40
    // For green-thread slices we need do { body } while(0):
    //   92b3c: jmp 92b40   (enter body once)
    //   92b8e: nop nop     (don't loop back)
    try {
        uint8_t enter_body[2] = {0xEB, 0x02};  // jmp +2 → 92b40
        m.write(mvos_base_ + 0x92b3c, enter_body, 2);
        uint8_t nops[2] = {0x90, 0x90};
        m.write(mvos_base_ + 0x92b8e, nops, 2);
        sound_main_patched_ = true;
        std::printf("  [audio] SoundCard Main patched to one-shot mix\n");
    } catch (...) {
        std::fprintf(stderr, "  [audio] failed to patch SoundCard Main\n");
    }
}

bool TrapLayer::maybe_redirect_sound(Machine& m, uint32_t esp) {
    // After SwapBuffers present: green-run Entry once per ~100ms (one OSS
    // fragment). Entry vcalls Main (one-shot after patch) → write /dev/dsp.
    if (redirecting_sound_ || soft_threads_.empty()) return false;
    auto now = std::chrono::steady_clock::now();
    if (next_sound_slice_.time_since_epoch().count() != 0 && now < next_sound_slice_)
        return false;

    SoftThread* pick = nullptr;
    for (auto& t : soft_threads_) {
        if (!t.entry || !t.arg) continue;
        uint8_t run = 0;
        try { m.read(t.arg + 0x10, &run, 1); } catch (...) { continue; }
        if (run) { pick = &t; break; }
    }
    if (!pick) return false;

    // Don't overfill the host queue (keeps latency low).
    {
        std::lock_guard<std::mutex> lock(audio_mu_);
        if (audio_q_.size() > 22050)  // > ~0.5s stereo
            return false;
    }

    uint32_t ret = m.r32(esp);
    uint32_t sp = esp;
    sp -= 4;
    m.w32(sp, pick->arg);
    sp -= 4;
    m.w32(sp, ret);
    next_sound_slice_ = now + std::chrono::milliseconds(90);
    static int nred;
    if (nred++ < 4)
        std::printf("  [audio] green-run Entry=%#x arg=%#x\n", pick->entry, pick->arg);
    m.redirect_guest(pick->entry, sp);
    return true;
}

uint32_t TrapLayer::dispatch(Machine& m, uint32_t slot, uint32_t esp) {
    if (slot >= names_.size()) return 0;
    hits_[slot]++;
    auto it = table_.find(names_[slot]);
    if (it != table_.end()) return it->second(m, esp);
    if (hits_[slot] == 1)   // first hit only, to keep the log readable
        std::fprintf(stderr, "  [trap] TODO %s\n", names_[slot].c_str());
    return 0;
}

void TrapLayer::report() const {
    uint32_t impl = 0, todo = 0; uint64_t impl_hits = 0, todo_hits = 0;
    std::vector<std::pair<std::string, uint64_t>> todos;
    for (uint32_t i = 0; i < names_.size(); ++i) {
        if (!hits_[i]) continue;
        bool have = table_.count(names_[i]) != 0;
        if (have) { impl++; impl_hits += hits_[i]; }
        else      { todo++; todo_hits += hits_[i]; todos.push_back({names_[i], hits_[i]}); }
    }
    std::printf("\n=== trap report ===\n");
    std::printf("  implemented imports hit: %u  (%llu calls)\n", impl,
                (unsigned long long)impl_hits);
    std::printf("  UNIMPLEMENTED hit:       %u  (%llu calls)\n", todo,
                (unsigned long long)todo_hits);
    std::sort(todos.begin(), todos.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (auto& [nm, n] : todos)
        std::printf("    %6llu  %s\n", (unsigned long long)n, nm.c_str());
}

uint32_t TrapLayer::make_device(Machine& m, const char* kind) {
    // Video device: 0x40-byte cVVC_Linux_X shell (vtable @ +0x28).
    // Mouse/pointer: full cMouse/cPointer layout so EVENT_Move/Buttons work.
    // Keyboard: large enough for key matrix + queue (PushKey layout).
    uint32_t obj_size = 0x40;
    if (std::strcmp(kind, "mouse") == 0 || std::strcmp(kind, "pointer") == 0)
        obj_size = 0x30;
    else if (std::strcmp(kind, "keyboard") == 0)
        obj_size = 0x90;
    else if (std::strcmp(kind, "video") == 0)
        obj_size = 0x40;

    uint32_t obj = bump_alloc(obj_size);
    if (!obj) return 0;
    {
        std::vector<uint8_t> z(obj_size, 0);
        m.write(obj, z.data(), obj_size);
    }

    if (std::strcmp(kind, "mouse") == 0 || std::strcmp(kind, "pointer") == 0) {
        // Match __6cMouse / __8cPointer: ring of 0x100 × 12-byte events.
        // +0x08 = read index, +0x0c = write index (empty when equal). See
        // EVENT_Move / GetNextEvent in libmvos.
        uint32_t buf = bump_alloc(0xc00);
        {
            std::vector<uint8_t> z(0xc00, 0);
            m.write(buf, z.data(), 0xc00);
        }
        m.w32(obj + 0x00, buf);
        m.w32(obj + 0x04, 0x100);       // capacity
        m.w32(obj + 0x08, 0);           // read idx
        m.w32(obj + 0x0c, 0);           // write idx
        // +0x14/+0x18 pos, +0x1c buttons — zeroed
        //
        // +0x20 = driver/backend object with a vtable. Real X plugin fills this;
        // GameSession_LoadSettings calls VMouse[+0x20]->vtbl[+0x14/+0x18]
        // (grab/ungrab style). GetNextEvent also does vtbl[+0xc](mouse).
        // Synthetic: a small object whose vtable is all Plugin_NoopOK.
        uint32_t noop = 0;
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i)
            if (plugin_exports_[i] == "Plugin_NoopOK") noop = plugin_trap_base_ + i;
        {
            // Driver vtable at +0x20. GetNextEvent calls [vt+0xc](mouse) as
            // cdecl (caller add $4). Just return 0 — then GetNextEvent drains
            // the ring itself. Plugin traps are also fine but guest stubs are
            // lighter and avoid trap re-entry during present.
            uint32_t stub0 = stub_alloc(m, 8);
            const uint8_t zero_stub[] = {0x31, 0xc0, 0xc3}; // xor eax,eax; ret
            if (stub0) m.write(stub0, zero_stub, sizeof zero_stub);
            uint32_t dvt = bump_alloc(16 * 4);
            for (uint32_t i = 0; i < 16; ++i) m.w32(dvt + 4 * i, stub0);
            m.w32(obj + 0x20, dvt);
            (void)noop;
        }
    } else if (std::strcmp(kind, "keyboard") == 0) {
        // cKeyboard layout (from __9cKeyboardPCl):
        //   +0x0c..+0x6f key-state matrix (PushKey writes here)
        //   +0x70..+0x7c event ring (buf/cap/rd/wr)
        //   +0x84 driver/function table pointer
        // cVOEditRow::Process probes shift via:
        //   call (*([VKeyboard+0x84]+0x10))(VKeyboard, keycode)  // 0x37/0x38
        // Our shell left +0x84 null → fault accessing 0x10 on save-name popup.
        uint32_t kbuf = bump_alloc(0x800);
        {
            std::vector<uint8_t> z(0x800, 0);
            m.write(kbuf, z.data(), 0x800);
        }
        m.w32(obj + 0x70, kbuf);
        m.w32(obj + 0x74, 0x100);
        m.w32(obj + 0x78, 0);
        m.w32(obj + 0x7c, 0);
        // Driver function table at +0x84. Conventions (from disasm):
        //   Process shift probe [vt+0x10](this, code) — cdecl, caller add $8
        //   PushKeyInput      [vt+0x0c](sret, this)  — callee ret $4, caller add $4
        uint32_t stub0 = stub_alloc(m, 8);
        const uint8_t zero_cdecl[] = {0x31, 0xc0, 0xc3}; // xor eax,eax; ret
        if (stub0) m.write(stub0, zero_cdecl, sizeof zero_cdecl);
        uint32_t stub_key = stub_alloc(m, 32);
        const uint8_t key_stub[] = {
            0x8B, 0x44, 0x24, 0x08,       // mov eax, [esp+8] ; code
            0x8B, 0x4C, 0x24, 0x04,       // mov ecx, [esp+4] ; this
            0x0F, 0xB6, 0x44, 0x01, 0x0C, // movzx eax, byte [ecx+eax+0x0c]
            0x85, 0xC0,                   // test eax, eax
            0x0F, 0x95, 0xC0,             // setne al
            0x0F, 0xB6, 0xC0,             // movzx eax, al
            0xC3,                         // ret (cdecl)
        };
        if (stub_key) m.write(stub_key, key_stub, sizeof key_stub);
        // PushKeyInput: zero 8-byte event at sret, ret $4 (stdcall half-clean).
        uint32_t stub_next = stub_alloc(m, 32);
        const uint8_t next_stub[] = {
            0x8B, 0x44, 0x24, 0x04,                   // mov eax, [esp+4] sret
            0xC7, 0x00, 0x00, 0x00, 0x00, 0x00,       // mov [eax], 0
            0xC7, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, // mov [eax+4], 0
            0xC2, 0x04, 0x00,                         // ret $4
        };
        if (stub_next) m.write(stub_next, next_stub, sizeof next_stub);
        uint32_t kvt = bump_alloc(16 * 4);
        for (uint32_t i = 0; i < 16; ++i) m.w32(kvt + 4 * i, stub0);
        m.w32(kvt + 0x0c, stub_next);
        m.w32(kvt + 0x10, stub_key);
        m.w32(obj + 0x84, kvt);
    } else if (std::strcmp(kind, "video") == 0) {
        uint32_t vt = bump_alloc(16 * 4);
        uint32_t noop = 0, setmode = 0;
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i) {
            if (plugin_exports_[i] == "Plugin_NoopOK") noop = plugin_trap_base_ + i;
            if (plugin_exports_[i] == "Plugin_SetVideoMode") setmode = plugin_trap_base_ + i;
        }
        for (uint32_t i = 0; i < 16; ++i) m.w32(vt + 4 * i, noop);
        if (setmode) m.w32(vt + 0xc, setmode);
        m.w32(obj + 0x28, vt);
        std::printf("  [plugin] Create*%sDevice -> obj %#x vt %#x\n", kind, obj, vt);
        return obj;
    }
    std::printf("  [plugin] Create*%sDevice -> obj %#x (input shell)\n", kind, obj);
    return obj;
}

// Host-side ring write matching EVENT_Move / EVENT_Buttons (libmvos cMouse).
// +0x08 = read idx, +0x0c = write idx (empty when equal). Slot size 12 bytes.
void TrapLayer::mouse_event_move(uint32_t dev, int32_t x, int32_t y) {
    if (!dev || !machine_) return;
    Machine& m = *machine_;
    m.w32(dev + 0x14, (uint32_t)x);
    m.w32(dev + 0x18, (uint32_t)y);
    uint32_t buf = m.r32(dev);
    uint32_t cap = m.r32(dev + 0x04);
    if (!buf || !cap) return;
    uint32_t wr = m.r32(dev + 0x0c);
    uint32_t off = wr * 12;
    m.w32(buf + off, 1);              // type = move
    m.w32(buf + off + 4, (uint32_t)x);
    m.w32(buf + off + 8, (uint32_t)y);
    wr++;
    if (wr >= cap) wr = 0;
    m.w32(dev + 0x0c, wr);
}

void TrapLayer::mouse_event_buttons(uint32_t dev, uint8_t buttons) {
    if (!dev || !machine_) return;
    Machine& m = *machine_;
    m.write(dev + 0x1c, &buttons, 1);
    uint32_t buf = m.r32(dev);
    uint32_t cap = m.r32(dev + 0x04);
    if (!buf || !cap) return;
    uint32_t wr = m.r32(dev + 0x0c);
    uint32_t off = wr * 12;
    m.w32(buf + off, 2);  // type = buttons
    m.w32(buf + off + 4, buttons);
    m.w32(buf + off + 8, 0);
    wr++;
    if (wr >= cap) wr = 0;
    m.w32(dev + 0x0c, wr);
}

void TrapLayer::draw_software_cursor() {
    if (!video_.is_open()) return;
    int W = video_.width(), H = video_.height();
    int x = mouse_x_, y = mouse_y_;
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint16_t* fb = video_.fb();
    // Hot-pink crosshair so it's obvious against any palette.
    auto plot = [&](int px, int py, uint16_t c) {
        if (px >= 0 && py >= 0 && px < W && py < H) fb[py * W + px] = c;
    };
    const uint16_t col = 0xF81F;  // magenta RGB565
    for (int d = -6; d <= 6; ++d) {
        plot(x + d, y, col);
        plot(x, y + d, col);
    }
    // Tip pixel white.
    plot(x, y, 0xFFFF);
}

uint32_t TrapLayer::pointer_sprite() const {
    if (!machine_) return 0;
    Machine& m = *machine_;
    uint32_t intu = intuition_obj();
    if (!intu) return 0;
    uint32_t scr = m.r32(intu + 0x24);
    if (!scr) return 0;
    return m.r32(scr + 0x24);
}

// Host stand-in for Process__9cSprClick (TimerProc → vt+0x10).
// Real path: setitimer(SIGALRM) → cTimerSystem_Linux::Proc → TimerProc__10cIntuition
// → sprite->Process(). We have no host signal timer, so advance from present.
// Layout: +0x58 cAnimBitmap*, +0x5c frame index; anim +0x38 = frame count.
// Throttle: every 3rd tick (matches mvos global counter @ aefda).
// Buttons held (Intuition+0xa8): frame++ toward last; else frame-- to 0 (idle).
void TrapLayer::tick_pointer_click_anim() {
    if (!machine_ || !mvos_base_) return;
    Machine& m = *machine_;
    uint32_t spr = pointer_sprite();
    if (!spr) return;

    // Only cSprClick-like sprites have a useful Process at vt+0x10.
    uint32_t vt = m.r32(spr + 0x08);
    if (!vt) return;
    uint32_t proc = m.r32(vt + 0x10);
    // Process__9cSprClick @ mvos 0x8ba70; Process__14cSprABitmapAdd @ 0x8c2f0;
    // weak Process__7cSprite @ 0x987e0 is a no-op we can skip.
    uint32_t click_proc = mvos_base_ + 0x8ba70;
    uint32_t abm_proc = mvos_base_ + 0x8c2f0;
    if (proc != click_proc && proc != abm_proc) return;

    // Shared throttle word in mvos .data (same as guest Process).
    uint32_t thr_addr = mvos_base_ + (proc == click_proc ? 0xaefda : 0xaefdc);
    uint16_t thr = 0;
    m.read(thr_addr, &thr, 2);
    thr = (uint16_t)(thr + 1);
    uint16_t limit = (proc == click_proc) ? 2 : 5;  // every 3rd / 6th call
    if (thr <= limit) {
        m.write(thr_addr, &thr, 2);
        return;
    }
    thr = 0;
    m.write(thr_addr, &thr, 2);

    uint32_t anim = m.r32(spr + 0x58);
    if (!anim) return;
    uint32_t nframes = m.r32(anim + 0x38);
    if (nframes < 1) return;

    if (proc == click_proc) {
        // Click hand: advance while any mouse button held, else retreat to 0.
        uint32_t intu = intuition_obj();
        uint16_t buttons = 0;
        if (intu) m.read(intu + 0xa8, &buttons, 2);
        uint32_t frame = m.r32(spr + 0x5c);
        if (buttons) {
            if (frame + 1 < nframes) frame++;
        } else {
            if (frame > 0) frame--;
        }
        m.w32(spr + 0x5c, frame);
    } else {
        // Idle multi-frame pointer: cycle +0x60.
        uint32_t frame = m.r32(spr + 0x60) + 1;
        if (frame >= nframes) frame = 0;
        m.w32(spr + 0x60, frame);
    }
}

// Intuition stores the pointer the game actually polls:
//   GetIPointerPos → this+0xa0 (tPoint {x,y})
//   GetIMouseButtons → *(uint16*)(this+0xa8)
// And ProcessInputs drains the 8-byte event ring at this+0x28 (fed by
// PushMouseInput in real SwapBuffers — which our HLE skips).
uint32_t TrapLayer::intuition_obj() const {
    if (!machine_) return 0;
    Machine& m = *machine_;
    uint32_t intu = m.r32(0x08598454);
    if (!intu && mvos_base_) intu = m.r32(mvos_base_ + 0xaefe4);
    return intu;
}

void TrapLayer::push_intuition_event(uint32_t type, uint32_t payload) {
    if (!machine_) return;
    Machine& m = *machine_;
    uint32_t intu = intuition_obj();
    if (!intu) return;
    // cIntuition ctor: ring object embedded at +0x28
    //   +0x28 buf*, +0x2c cap, +0x30 read, +0x34 write; slots are 8 bytes.
    uint32_t ring = intu + 0x28;
    uint32_t buf = m.r32(ring + 0x00);
    uint32_t cap = m.r32(ring + 0x04);
    if (!buf || !cap) return;
    int32_t wr = (int32_t)m.r32(ring + 0x0c);
    // write idx < 0 means "use read-idx path" in EVENT_*; normal is >= 0.
    if (wr < 0) wr = (int32_t)m.r32(ring + 0x08);
    uint32_t slot = (uint32_t)wr;
    if (slot >= cap) slot = 0;
    m.w32(buf + slot * 8, type);
    m.w32(buf + slot * 8 + 4, payload);
    slot++;
    if (slot >= cap) slot = 0;
    m.w32(ring + 0x0c, slot);
    // Overflow: write caught up with read → mark full as -1 (libmvos convention).
    if (slot == m.r32(ring + 0x08))
        m.w32(ring + 0x0c, 0xffffffffu);
}

void TrapLayer::push_intuition_move(int x, int y) {
    // type 1: payload = two packed uint16 {x,y} (see PushMouseInput).
    uint32_t payload = (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16);
    push_intuition_event(1, payload);
}

void TrapLayer::push_intuition_button_edges(uint8_t prev, uint8_t now) {
    // type 4: subcode 0=L↓ 1=L↑ 2=R↓ 3=R↑ 4=M↓ 5=M↑ (ProcessInputs jump table).
    auto edge = [&](uint8_t bit, uint32_t down_code, uint32_t up_code) {
        bool was = (prev & bit) != 0, is = (now & bit) != 0;
        if (!was && is) push_intuition_event(4, down_code);
        if (was && !is) push_intuition_event(4, up_code);
    };
    edge(1, 0, 1);
    edge(2, 2, 3);
    edge(4, 4, 5);
}

void TrapLayer::update_intuition_pointer(int x, int y, uint8_t buttons) {
    if (!machine_) return;
    Machine& m = *machine_;
    uint32_t intu = intuition_obj();
    if (!intu) return;
    m.w32(intu + 0xa0, (uint32_t)x);
    m.w32(intu + 0xa4, (uint32_t)y);
    m.w32(intu + 0x14, (uint32_t)x);   // SetPointerPos fields too
    m.w32(intu + 0x18, (uint32_t)y);
    uint16_t bw = buttons;
    m.write(intu + 0xa8, &bw, 2);
    // Keep active-screen pointer sprite at the mouse (MoveTo is gated on
    // Intuition_Mode which we leave 0 to avoid input-recording Fatals).
    uint32_t scr = m.r32(intu + 0x24);
    uint32_t spr = scr ? m.r32(scr + 0x24) : 0;
    if (spr) {
        uint8_t busy = 0;
        m.read(spr + 0x4c, &busy, 1);
        if (!busy) {
            m.w32(spr + 0x4d, (uint32_t)x);
            m.w32(spr + 0x51, (uint32_t)y);
        }
    }
}

// Map SDL scancodes → libmvos eKeyCode (KeyTableConvert in keyboard_x plugin).
// Dense enum 1..0x63; NOT IBM XT scancodes. 0 = unmapped.
static uint32_t sdl_scancode_to_ekey(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_1: return 0x02;
    case SDL_SCANCODE_2: return 0x03;
    case SDL_SCANCODE_3: return 0x04;
    case SDL_SCANCODE_4: return 0x05;
    case SDL_SCANCODE_5: return 0x06;
    case SDL_SCANCODE_6: return 0x07;
    case SDL_SCANCODE_7: return 0x08;
    case SDL_SCANCODE_8: return 0x09;
    case SDL_SCANCODE_9: return 0x0a;
    case SDL_SCANCODE_0: return 0x0b;
    case SDL_SCANCODE_A: return 0x0c;
    case SDL_SCANCODE_B: return 0x0d;
    case SDL_SCANCODE_C: return 0x0e;
    case SDL_SCANCODE_D: return 0x0f;
    case SDL_SCANCODE_E: return 0x10;
    case SDL_SCANCODE_F: return 0x11;
    case SDL_SCANCODE_G: return 0x12;
    case SDL_SCANCODE_H: return 0x13;
    case SDL_SCANCODE_I: return 0x14;
    case SDL_SCANCODE_J: return 0x15;
    case SDL_SCANCODE_K: return 0x16;
    case SDL_SCANCODE_L: return 0x17;
    case SDL_SCANCODE_M: return 0x18;
    case SDL_SCANCODE_N: return 0x19;
    case SDL_SCANCODE_O: return 0x1a;
    case SDL_SCANCODE_P: return 0x1b;
    case SDL_SCANCODE_Q: return 0x1c;
    case SDL_SCANCODE_R: return 0x1d;
    case SDL_SCANCODE_S: return 0x1e;
    case SDL_SCANCODE_T: return 0x1f;
    case SDL_SCANCODE_U: return 0x20;
    case SDL_SCANCODE_V: return 0x21;
    case SDL_SCANCODE_W: return 0x22;
    case SDL_SCANCODE_X: return 0x23;
    case SDL_SCANCODE_Y: return 0x24;
    case SDL_SCANCODE_Z: return 0x25;
    case SDL_SCANCODE_F1:  return 0x26;
    case SDL_SCANCODE_F2:  return 0x27;
    case SDL_SCANCODE_F3:  return 0x28;
    case SDL_SCANCODE_F4:  return 0x29;
    case SDL_SCANCODE_F5:  return 0x2a;
    case SDL_SCANCODE_F6:  return 0x2b;
    case SDL_SCANCODE_F7:  return 0x2c;
    case SDL_SCANCODE_F8:  return 0x2d;
    case SDL_SCANCODE_F9:  return 0x2e;
    case SDL_SCANCODE_F10: return 0x2f;
    case SDL_SCANCODE_F11: return 0x30;
    case SDL_SCANCODE_F12: return 0x31;
    case SDL_SCANCODE_UP:        return 0x32;
    case SDL_SCANCODE_RIGHT:     return 0x33;
    case SDL_SCANCODE_DOWN:      return 0x34;
    case SDL_SCANCODE_LEFT:      return 0x35;
    case SDL_SCANCODE_BACKSPACE: return 0x36;
    case SDL_SCANCODE_LSHIFT:    return 0x37;
    case SDL_SCANCODE_RSHIFT:    return 0x38;
    case SDL_SCANCODE_LCTRL:     return 0x39;
    case SDL_SCANCODE_RCTRL:     return 0x3a;
    case SDL_SCANCODE_LALT:      return 0x3b;
    case SDL_SCANCODE_RALT:      return 0x3c;
    case SDL_SCANCODE_CAPSLOCK:  return 0x3d;
    case SDL_SCANCODE_LGUI:      return 0x3e;
    case SDL_SCANCODE_RGUI:      return 0x40;
    case SDL_SCANCODE_APPLICATION: return 0x42;
    case SDL_SCANCODE_MINUS:     return 0x43;
    case SDL_SCANCODE_EQUALS:    return 0x44;
    case SDL_SCANCODE_TAB:       return 0x45;
    case SDL_SCANCODE_HOME:      return 0x46;
    case SDL_SCANCODE_END:       return 0x47;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:  return 0x48;
    case SDL_SCANCODE_SEMICOLON: return 0x49;
    case SDL_SCANCODE_APOSTROPHE: return 0x4a;
    case SDL_SCANCODE_GRAVE:     return 0x4b;
    case SDL_SCANCODE_BACKSLASH: return 0x4c;
    case SDL_SCANCODE_COMMA:     return 0x4d;
    case SDL_SCANCODE_PERIOD:    return 0x4e;
    case SDL_SCANCODE_SLASH:     return 0x4f;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x50;
    case SDL_SCANCODE_SPACE:     return 0x51;
    case SDL_SCANCODE_KP_7:      return 0x52;
    case SDL_SCANCODE_KP_8:      return 0x53;
    case SDL_SCANCODE_KP_9:      return 0x54;
    case SDL_SCANCODE_KP_MINUS:  return 0x55;
    case SDL_SCANCODE_KP_4:      return 0x56;
    case SDL_SCANCODE_KP_6:      return 0x57;
    case SDL_SCANCODE_KP_PLUS:   return 0x58;
    case SDL_SCANCODE_KP_1:      return 0x59;
    case SDL_SCANCODE_KP_2:      return 0x5a;
    case SDL_SCANCODE_KP_3:      return 0x5b;
    case SDL_SCANCODE_KP_0:      return 0x5c;
    case SDL_SCANCODE_KP_PERIOD: return 0x5d;
    case SDL_SCANCODE_PAGEUP:    return 0x5f;
    case SDL_SCANCODE_PAGEDOWN:  return 0x60;
    case SDL_SCANCODE_INSERT:    return 0x61;
    case SDL_SCANCODE_DELETE:    return 0x62;
    case SDL_SCANCODE_PAUSE:     return 0x63;
    default: return 0;
    }
}

void TrapLayer::on_sdl_event(const SDL_Event& e) {
    if (!machine_) return;
    Machine& m = *machine_;
    uint32_t vmouse = m.r32(0x08598c3c);
    uint32_t vptr   = m.r32(mvos_base_ + 0xaef9c);

    auto clamp = [&](int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };

    if (e.type == SDL_MOUSEMOTION) {
        int x = e.motion.x, y = e.motion.y;
        if (video_.is_open()) {
            x = clamp(x, 0, video_.width() - 1);
            y = clamp(y, 0, video_.height() - 1);
        }
        mouse_x_ = x; mouse_y_ = y;
        if (vmouse) mouse_event_move(vmouse, x, y);
        if (vptr)   mouse_event_move(vptr, x, y);
        update_intuition_pointer(x, y, mouse_buttons_);
        push_intuition_move(x, y);
    } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        uint8_t bit = 0;
        if (e.button.button == SDL_BUTTON_LEFT)   bit = 1;
        if (e.button.button == SDL_BUTTON_RIGHT)  bit = 2;
        if (e.button.button == SDL_BUTTON_MIDDLE) bit = 4;
        uint8_t prev = mouse_buttons_;
        if (e.type == SDL_MOUSEBUTTONDOWN) mouse_buttons_ = (uint8_t)(mouse_buttons_ | bit);
        else                              mouse_buttons_ = (uint8_t)(mouse_buttons_ & ~bit);
        // Also update position from the button event.
        if (video_.is_open()) {
            mouse_x_ = clamp(e.button.x, 0, video_.width() - 1);
            mouse_y_ = clamp(e.button.y, 0, video_.height() - 1);
        }
        if (vmouse) {
            mouse_event_move(vmouse, mouse_x_, mouse_y_);
            mouse_event_buttons(vmouse, mouse_buttons_);
        }
        if (vptr) {
            mouse_event_move(vptr, mouse_x_, mouse_y_);
            mouse_event_buttons(vptr, mouse_buttons_);
        }
        update_intuition_pointer(mouse_x_, mouse_y_, mouse_buttons_);
        push_intuition_move(mouse_x_, mouse_y_);
        push_intuition_button_edges(prev, mouse_buttons_);
        static int nlog;
        if (nlog++ < 16)
            std::printf("  [input] mouse btn mask=%u→%u at %d,%d (Intuition pipe)\n",
                        prev, mouse_buttons_, mouse_x_, mouse_y_);
    } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        // eKeyCode is *not* a PC scancode. Table is KeyTableConvert in
        // libmvos_keyboard_x (XKeysym → dense enum). ProcessInputs drains
        // Intuition ring types 8 (down) / 0x10 (up); cVOEditRow only reacts
        // to type 8. Shift qualifiers are eKey 0x37/0x38 (Intuition+0x73/74).
        uint32_t code = sdl_scancode_to_ekey(e.key.keysym.scancode);
        uint32_t vkey = m.r32(0x08598b58);
        uint32_t intu = intuition_obj();
        bool down = (e.type == SDL_KEYDOWN);
        if (code && code <= 0x63) {
            uint8_t b = down ? 1 : 0;
            // Direct matrix write: KeyMatrix / shift probes between ProcessInputs.
            if (intu) m.write(intu + 0x3c + code, &b, 1);
            if (vkey) {
                m.write(vkey + 0x0c + code, &b, 1);
                // PushKey ring: +0x70 buf, +0x74 cap, +0x78 read, +0x7c write.
                uint32_t kbuf = m.r32(vkey + 0x70);
                uint32_t cap  = m.r32(vkey + 0x74);
                int32_t wr    = (int32_t)m.r32(vkey + 0x7c);
                if (kbuf && cap) {
                    if (wr < 0) wr = (int32_t)m.r32(vkey + 0x78);
                    uint32_t slot = (uint32_t)wr;
                    if (slot >= cap) slot = 0;
                    m.w32(kbuf + slot * 8, code);
                    m.w32(kbuf + slot * 8 + 4, down ? 0x80u : 0u);
                    slot++;
                    if (slot >= cap) slot = 0;
                    m.w32(vkey + 0x7c, slot);
                    if (slot == m.r32(vkey + 0x78))
                        m.w32(vkey + 0x7c, 0xffffffffu);
                }
            }
            // ProcessInputs → ProcessTree (UI / edit rows / hotkeys).
            push_intuition_event(down ? 8u : 0x10u, code);
            // Mirror SetQualifierState for polls that skip ProcessInputs.
            if (intu) {
                uint8_t q = 0;
                auto held = [&](uint32_t c) {
                    uint8_t v = 0;
                    m.read(intu + 0x3c + c, &v, 1);
                    return v != 0;
                };
                if (held(0x37) || held(0x38)) q |= 1;   // shift
                if (held(0x3b) || held(0x3c)) q |= 2;   // alt
                if (held(0x39) || held(0x3a)) q |= 4;   // ctrl
                if (held(0x3f) || held(0x41)) q |= 8;   // meta
                m.write(intu + 0xb0, &q, 1);
            }
            static int klog;
            if (klog++ < 24)
                std::printf("  [input] key eKey=%#x %s sc=%d\n",
                            code, down ? "down" : "up",
                            (int)e.key.keysym.scancode);
        }
    } else if (e.type == SDL_WINDOWEVENT &&
               e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        mouse_buttons_ = 0;
        if (vmouse) mouse_event_buttons(vmouse, 0);
        if (vptr)   mouse_event_buttons(vptr, 0);
        update_intuition_pointer(mouse_x_, mouse_y_, 0);
        // ReleaseAll: clear key matrices so nothing sticks after alt-tab.
        uint32_t intu = intuition_obj();
        uint32_t vkey = m.r32(0x08598b58);
        std::vector<uint8_t> z(0x64, 0);
        if (intu) {
            m.write(intu + 0x3c, z.data(), 0x64);
            uint8_t q = 0;
            m.write(intu + 0xb0, &q, 1);
        }
        if (vkey) m.write(vkey + 0x0c, z.data(), 0x64);
    }
}

uint32_t TrapLayer::dispatch_plugin(Machine& m, uint32_t slot, uint32_t esp) {
    if (slot >= plugin_exports_.size()) return 0;
    const std::string& name = plugin_exports_[slot];
    if (name == "QueryDevice") return 1;
    if (name == "CreateVideoDevice")    return make_device(m, "video");
    if (name == "CreateKeyboardDevice") return make_device(m, "keyboard");
    if (name == "CreateMouseDevice")    return make_device(m, "mouse");
    if (name == "CreatePointerDevice")  return make_device(m, "pointer");
    if (name == "Plugin_NoopOK")        return 1;
    if (name == "Plugin_Return0")       return 0;
    // bool KeyMatrix-ish(cKeyboard* this, eKeyCode code) — matrix @ this+0x0c.
    if (name == "Plugin_KeyMatrix") {
        uint32_t self = arg(m, esp, 0);
        uint32_t code = arg(m, esp, 1);
        if (!self || code > 0x63) return 0;
        uint8_t b = 0;
        m.read(self + 0x0c + code, &b, 1);
        return b ? 1u : 0u;
    }
    if (name == "Plugin_SetVideoMode") {
        // cdecl: this, cVModeRequest*
        uint32_t self = arg(m, esp, 0), req = arg(m, esp, 1);
        int w = req ? (int)m.r32(req + 0) : 800;
        int h = req ? (int)m.r32(req + 4) : 600;
        int d = req ? (int)m.r32(req + 8) : 5;
        if (w <= 0) w = 800;
        if (h <= 0) h = 600;
        bool ok = video_.open(w, h, d);
        if (self) {
            m.w32(self + 0x20, (uint32_t)w);
            m.w32(self + 0x24, (uint32_t)h);
            m.w32(self + 0x1c, (uint32_t)d);
        }
        std::printf("  [plugin] SetVideoMode %dx%d depth %d -> %s\n",
                    w, h, d, ok ? "ok" : "FAIL");
        return ok ? 1 : 0;
    }
    if (name == "HLE_OpenDisplay") {
        // cdecl OpenDisplay(cVVC* this, cVModeRequest& req)
        // Build a real guest cGD_LFB16 over GUEST_FB_BASE so PaintTree/Paint
        // can blit; SDL presents that buffer on SwapBuffers.
        uint32_t self = arg(m, esp, 0), req = arg(m, esp, 1);
        int w = req ? (int)m.r32(req + 0) : 800;
        int h = req ? (int)m.r32(req + 4) : 600;
        int d = req ? (int)m.r32(req + 8) : 5;
        if (w <= 0) w = 800;
        if (h <= 0) h = 600;
        bool ok = video_.open(w, h, d);
        if (!ok) {
            std::printf("  [HLE] OpenDisplay %dx%d depth %d -> FAIL (SDL)\n", w, h, d);
            return 0;
        }
        uint32_t pitch = (uint32_t)w * 2;  // RGB565
        uint32_t nbytes = pitch * (uint32_t)h;
        if (nbytes > GUEST_FB_SIZE) nbytes = GUEST_FB_SIZE;
        // Clear guest FB (and mirror into SDL).
        std::vector<uint8_t> z(nbytes, 0);
        m.write(GUEST_FB_BASE, z.data(), nbytes);
        if (video_.fb_bytes() >= nbytes)
            std::memcpy(video_.fb(), z.data(), nbytes);

        // cDimension {w,h} on the guest stack/scratch area of the object.
        uint32_t dim = bump_alloc(8);
        m.w32(dim, (uint32_t)w);
        m.w32(dim + 4, (uint32_t)h);

        // Build cGD_LFB16 layout by hand (same fields as __9cGD_LFB16 ctor
        // @0x6bb30). Nested Machine::call from inside a trap is unreliable, so
        // we don't invoke the guest ctor.
        //   +0 w, +4 h, +8 fb*, +0xc depthCode(5), +0x10 pitch, +0x14 vtable
        uint32_t gd = bump_alloc(0x80);
        {
            std::vector<uint8_t> gz(0x80, 0);
            m.write(gd, gz.data(), 0x80);
        }
        uint32_t vt = mvos_base_ + 0xa2820;  // __vt_9cGD_LFB16 (relocated)
        m.w32(gd + 0x00, (uint32_t)w);
        m.w32(gd + 0x04, (uint32_t)h);
        m.w32(gd + 0x08, GUEST_FB_BASE);
        m.w32(gd + 0x0c, 5);                 // eBMType / depth code for LFB16
        m.w32(gd + 0x10, pitch);
        m.w32(gd + 0x14, vt);
        (void)dim;  // kept for future real-ctor path
        std::printf("  [HLE] cGD_LFB16 @%#x fb=%#x %dx%d pitch=%u vt=%#x\n",
                    gd, GUEST_FB_BASE, w, h, pitch, vt);

        if (self) {
            m.w32(self + 0x20, (uint32_t)w);
            m.w32(self + 0x24, (uint32_t)h);
            m.w32(self + 0x1c, (uint32_t)d);
            // Single-buffer LFB: all GD slots point at the same cGD_LFB16.
            //   +0x00/+0x04  front/back (PaintTree / various blits)
            //   +0x08/+0x10  SwapBuffers__4cVVC / Refresh__7cSprite GD
            //   +0x14        EndRefresh + BeforeSwapBuffer paint GD
            m.w32(self + 0x00, gd);
            m.w32(self + 0x04, gd);
            m.w32(self + 0x08, gd);
            m.w32(self + 0x10, gd);
            m.w32(self + 0x14, gd);
            uint8_t one = 1, zero = 0;
            m.write(self + 0x18, &zero, 1);
            m.write(self + 0x19, &one, 1);
        }
        gd_ = gd;
        std::printf("  [HLE] OpenDisplay %dx%d depth %d -> ok\n", w, h, d);
        return 1;
    }
    if (name == "HLE_SwapBuffers") {
        // Present only — guest SwapBuffers__Fv still runs and calls:
        //   MouseRefresh → MoveTo(sprite), BeforeSwapBuffer (paint pointer),
        //   SwapBuffers__4cVVC (us), AfterSwapBuffer (restore under-cursor).
        // So the real cSprite is already composited onto the LFB here.
        if (video_.is_open()) {
            // Keep VVC GD slots alive (Refresh__7cSprite reads +0x10).
            if (gd_ && mvos_base_) {
                uint32_t vvc = m.r32(mvos_base_ + 0xaefcc);
                if (!vvc) vvc = m.r32(0x08598cec);
                if (vvc) {
                    m.w32(vvc + 0x00, gd_);
                    m.w32(vvc + 0x04, gd_);
                    m.w32(vvc + 0x08, gd_);
                    m.w32(vvc + 0x10, gd_);
                    m.w32(vvc + 0x14, gd_);
                }
            }
            uint32_t nbytes = video_.fb_bytes();
            if (gd_) {
                uint32_t gw = m.r32(gd_ + 0x00);
                uint32_t gh = m.r32(gd_ + 0x04);
                uint32_t pitch = m.r32(gd_ + 0x10);
                if (gw && gh && pitch) {
                    uint32_t want = pitch * gh;
                    if (want > 0 && want <= GUEST_FB_SIZE) nbytes = want;
                    if ((int)gw != video_.width() || (int)gh != video_.height())
                        video_.open((int)gw, (int)gh, video_.depth_code());
                    nbytes = video_.fb_bytes();
                    if (pitch * gh < nbytes) nbytes = pitch * gh;
                }
            }
            if (nbytes > GUEST_FB_SIZE) nbytes = GUEST_FB_SIZE;
            if (nbytes > video_.fb_bytes()) nbytes = video_.fb_bytes();
            m.read(GUEST_FB_BASE, video_.fb(), nbytes);
            // Fallback crosshair only when no screen pointer sprite is set.
            uint32_t spr = pointer_sprite();
            if (!spr) draw_software_cursor();
            static int clog;
            if (clog++ < 3)
                std::printf("  [cursor] present spr=%#x gd=%#x timer=%s\n",
                            spr, gd_, timer_armed_ ? "on" : "off");
            video_.present();  // pumps SDL

            // Click-hand frame advance (also done by TimerProc when timer runs).
            tick_pointer_click_anim();

            // THEOC_AUTO_MENU=1: after the 800×600 menu has presented a few
            // frames, synthesize a left-click on the Single Player button
            // (menu.cfg: "single 20 250"). Used for automated G8 bring-up.
            if (std::getenv("THEOC_AUTO_MENU") && video_.width() == 800) {
                static int menu_frames = 0;
                menu_frames++;
                if (menu_frames == 45) {
                    const int ax = 80, ay = 260;
                    mouse_x_ = ax; mouse_y_ = ay;
                    update_intuition_pointer(ax, ay, 0);
                    push_intuition_move(ax, ay);
                    std::printf("  [input] AUTO_MENU aim %d,%d\n", ax, ay);
                } else if (menu_frames == 50) {
                    push_intuition_button_edges(0, 1);  // L down
                    update_intuition_pointer(mouse_x_, mouse_y_, 1);
                    mouse_buttons_ = 1;
                    std::printf("  [input] AUTO_MENU L-down\n");
                } else if (menu_frames == 55) {
                    push_intuition_button_edges(1, 0);  // L up
                    update_intuition_pointer(mouse_x_, mouse_y_, 0);
                    mouse_buttons_ = 0;
                    std::printf("  [input] AUTO_MENU L-up\n");
                }
            }

            // One guest redirect per present (no nested uc_emu_start). Prefer
            // sound when its ~90ms slice is due so the 33ms timer cannot starve
            // the mixer; otherwise fire SIGALRM → TimerSystem::Proc.
            if (maybe_redirect_sound(m, esp))
                return 0;
            if (maybe_redirect_timer(m, esp))
                return 0;
        }
        return 0;
    }
    return 0;
}

void TrapLayer::install_plugins_and_video(Machine& m, uint32_t mvos_base) {
    mvos_base_ = mvos_base;
    machine_ = &m;
    video_.set_event_hook([this](const SDL_Event& e) { on_sdl_event(e); });
    plugin_exports_ = {
        "QueryDevice",
        "CreateVideoDevice",
        "CreateKeyboardDevice",
        "CreateMouseDevice",
        "CreatePointerDevice",
        "Plugin_NoopOK",
        "Plugin_Return0",
        "Plugin_KeyMatrix",
        "Plugin_SetVideoMode",
        "HLE_OpenDisplay",
        "HLE_SwapBuffers",
    };
    plugin_trap_base_ = PLUGIN_TRAP_BASE;
    m.add_code_traps(PLUGIN_TRAP_BASE, (uint32_t)plugin_exports_.size(),
                     [this](Machine& mm, uint32_t slot, uint32_t esp) {
                         return dispatch_plugin(mm, slot, esp);
                     });

    try {
        m.map(GUEST_FB_BASE, GUEST_FB_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    } catch (...) {}

    auto patch_jmp = [&](uint32_t file_va, const char* export_name, const char* tag) {
        uint32_t hle = 0;
        for (uint32_t i = 0; i < plugin_exports_.size(); ++i)
            if (plugin_exports_[i] == export_name) hle = plugin_trap_base_ + i;
        if (!hle) return;
        uint32_t at = mvos_base + file_va;
        uint8_t stub[7] = {
            0xB8,
            (uint8_t)hle, (uint8_t)(hle >> 8), (uint8_t)(hle >> 16), (uint8_t)(hle >> 24),
            0xFF, 0xE0
        };
        m.write(at, stub, sizeof stub);
        std::printf("  [HLE] patched %s @%#x -> trap %#x\n", tag, at, hle);
    };
    // OpenDisplay: skip plugin SetVideoMode / cGD_X; install cGD_LFB16 ourselves.
    patch_jmp(0x85ce0, "HLE_OpenDisplay", "OpenDisplay");
    // Present only (SwapBuffers__4cVVC). Leave SwapBuffers__Fv intact so it:
    //   MouseRefresh → MoveTo(sprite), BeforeSwapBuffer (paint cSprite on LFB),
    //   VVC present (us), AfterSwapBuffer (restore under-cursor for next frame).
    patch_jmp(0x85e20, "HLE_SwapBuffers", "SwapBuffers__4cVVC");
    // SDL already injects Intuition ring events; skip PushMouseInput so we do
    // not double-feed type 1/4 from the VMouse ring. MouseRefresh still MoveTo's.
    {
        uint32_t at = mvos_base + 0x8df10;  // PushMouseInput__Fv
        const uint8_t ret = 0xC3;
        m.write(at, &ret, 1);
        std::printf("  [HLE] nop'd PushMouseInput @%#x (SDL owns Intuition pipe)\n", at);
    }
}
