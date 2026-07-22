#include "traps.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
    register_builtins();
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

    // Harmless startup / teardown stubs.
    for (const char* nm : {"__libc_init_first", "pthread_key_delete",
                           "monstartup", "_mcleanup", "atexit", "__deregister_frame_info"})
        t[nm] = [](Machine&, uint32_t) -> uint32_t { return 0; };
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
