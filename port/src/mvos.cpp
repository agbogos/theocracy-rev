#include "mvos.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>

// Theocracy config/text cipher (see tools/theocracy_crypt.py). "RSA4096" is a
// joke header; the body is XOR'd against two short repeating keys.
static std::string decrypt_body(const std::string& raw) {
    static const char* K1 = "theocracy sux";       // period 13
    static const char* K2 = "mutant technology";   // period 17
    if (raw.size() < 7 || raw.compare(0, 7, "RSA4096") != 0) return raw;
    std::string out;
    out.reserve(raw.size() - 7);
    for (size_t i = 7; i < raw.size(); ++i) {
        size_t j = i - 7;
        out.push_back(char(raw[i] ^ K2[j % 17] ^ K1[j % 13]));
    }
    return out;
}

using namespace guestmap;

// cMemBlock 32-byte layout (docs/subsystems/memory-and-containers.md):
//   +0x0c data  +0x10 size  +0x14 lockCount  +0x18 priority  +0x1c payload vtbl
// The cMemBlock_ base subobject starts at +0x0c, so within cMemBlock_:
//   +0x00 data  +0x04 size  +0x08 lockCount  +0x10 vtable
namespace mb {
constexpr uint32_t DATA = 0x0c, SIZE = 0x10, LOCK = 0x14, PRIO = 0x18, VTBL = 0x1c;
}

// Pointer singletons: the game reads these .bss words as `cX*` and dereferences
// them. Back each with a zeroed guest object so a deref reads zeros, not a
// fault. (Non-pointer COPY data — flags, palettes, typeinfo, inline EnvSystem —
// stays zero for now.)
static const std::set<std::string> kPointerSingletons = {
    "SystemMemory", "Intuition", "IPCSystem", "VCD", "VVC", "SoundCard",
    "VKeyboard", "VMouse", "LocaleDataBase", "RandomServer",
};

std::string Mvos::resolve_path(const std::string& guest_name) const {
    // The game runs with a `data` symlink in its CWD; our extracted tree lives
    // under data_root_ (default "data/game", overridable via $THEOC_DATA).
    return data_root_ + "/" + guest_name;
}

void Mvos::apply_copyrelocs() {
    const char* env = std::getenv("THEOC_DATA");
    data_root_ = env ? env : "data/game";
    singleton_next_ = SINGLETON_BASE;
    m_.map(SINGLETON_BASE, SINGLETON_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    // A single zeroed "unloaded" anim frame-header. Every cData_* bitmap
    // descriptor points its cAnimBitmap frame pointer (+0x40) here so the
    // cSprite ctor's `*(hdr+0x41c)` dimension read yields 0 (a zero-size,
    // not-yet-decoded sprite) instead of dereferencing null. The real asset
    // lazy-load (payload vtable slot 2) will overwrite +0x40 per object once
    // binary cFile/.raw/.mft decoding lands.
    m_.map(NULL_FRAME, NULL_FRAME_SIZE, UC_PROT_READ | UC_PROT_WRITE);

    uint32_t nvt = 0, nsingle = 0, nother = 0;
    // Pass 1: register vtables (assign global slot ranges) and wire singletons.
    for (const auto& r : img_.relocs()) {
        if (r.type != elf32::R_386_COPY) continue;
        const auto& s = img_.sym(r.sym);
        if (s.name.rfind("__vt_", 0) == 0) {
            uint32_t nslots = s.size / 4;
            vts_.push_back({s.name, r.offset, nslots, vt_slot_count_});
            vt_addr_[s.name] = r.offset;
            vt_slot_count_ += nslots;
            nvt++;
        } else if (kPointerSingletons.count(s.name)) {
            uint32_t obj = singleton_next_;
            singleton_next_ += 0x400;          // roomy zeroed object
            m_.w32(r.offset, obj);
            singleton_obj_[s.name] = obj;
            nsingle++;
        } else {
            nother++;
        }
    }

    // Reserve a pool of extra vtable-trap slots (beyond the copy-reloc'd tables)
    // for synthesized singleton vtables, so the single VT_TRAP hook covers both.
    constexpr uint32_t kSynthVtPool = 1024;
    copyreloc_vt_slots_ = vt_slot_count_;
    svt_next_ = vt_slot_count_;
    vt_slot_count_ += kSynthVtPool;

    // Pass 2: fill each vtable's slots with a unique trap address, and build the
    // reverse map for dispatch.
    slot_to_vt_.resize(vt_slot_count_);
    vt_hits_.assign(vt_slot_count_, 0);
    for (uint32_t vi = 0; vi < vts_.size(); ++vi) {
        const auto& vt = vts_[vi];
        for (uint32_t k = 0; k < vt.nslots; ++k) {
            uint32_t g = vt.first_slot + k;
            slot_to_vt_[g] = vi;
            m_.w32(vt.addr + 4 * k, VT_TRAP_BASE + g);
        }
    }
    m_.add_code_traps(VT_TRAP_BASE, vt_slot_count_,
                      [this](Machine& mm, uint32_t slot, uint32_t esp) {
                          return dispatch_vtable(mm, slot, esp);
                      });

    std::fprintf(stderr, "mvos copyrelocs: %u vtables (%u copyreloc slots + %u pool), "
                "%u singletons wired, %u other left zero\n", nvt, copyreloc_vt_slots_,
                vt_slot_count_ - copyreloc_vt_slots_, nsingle, nother);

    wire_singleton_vtables();
    register_handlers();
}

uint32_t Mvos::alloc_guest(uint32_t size) {
    uint32_t p = singleton_next_;
    singleton_next_ += (size + 3u) & ~3u;      // SINGLETON region is pre-zeroed
    return p;
}

uint32_t Mvos::make_vtable(uint32_t nslots) {
    uint32_t vt = alloc_guest(nslots * 4);
    for (uint32_t k = 0; k < nslots; ++k)
        m_.w32(vt + 4 * k, VT_TRAP_BASE + svt_next_++);
    return vt;
}

void Mvos::hook_vslot(uint32_t vtable, uint32_t slot, TrapLayer::Handler fn) {
    uint32_t g = m_.r32(vtable + 4 * slot) - VT_TRAP_BASE;
    vhandlers_[g] = std::move(fn);
}

// Give each framework singleton's backing object a synthesized vtable so the
// game's polymorphic calls route to dispatch_vtable, then install native
// overrides for the slots the boot actually invokes.
void Mvos::wire_singleton_vtables() {
    for (const auto& [name, obj] : singleton_obj_)
        m_.w32(obj, make_vtable(32));

    // IPCSystem single-instance lock. cApplication::Start does
    //   lock = IPCSystem->vtbl[3](IPCSystem, 0, 5043, 1)   (offset 0xc)
    // and Fatals if it returns null ("run only one Theocracy"). Hand back a fake
    // lock object; its own vtable slot 2 (cleanup: lock->vtbl[2](lock,3)) routes
    // to dispatch_vtable as a harmless no-op.
    uint32_t ipc = singleton_obj_.count("IPCSystem") ? singleton_obj_["IPCSystem"] : 0;
    if (ipc) {
        uint32_t lock = alloc_guest(0x20);
        m_.w32(lock, make_vtable(8));
        hook_vslot(m_.r32(ipc), 3,
                   [lock](Machine&, uint32_t) -> uint32_t { return lock; });
    }

    // VCD (virtual CD / RedBook audio): a worker thread ctor calls
    //   X = *(VCD+0xc); (*(X+0x1c))(VCD)   [a driver-method table at +0xc].
    // Point +0xc at a synth vtable so slot 7 routes to dispatch (no-op).
    if (singleton_obj_.count("VCD"))
        m_.w32(singleton_obj_["VCD"] + 0xc, make_vtable(16));
}

uint32_t Mvos::dispatch_vtable(Machine& m, uint32_t slot, uint32_t esp) {
    auto it = vhandlers_.find(slot);
    if (it != vhandlers_.end()) return it->second(m, esp);   // native override
    if (slot >= vt_hits_.size()) return 0;
    vt_hits_[slot]++;
    if (vt_hits_[slot] == 1) {
        if (slot < copyreloc_vt_slots_) {
            const auto& vt = vts_[slot_to_vt_[slot]];
            std::fprintf(stderr, "  [vtable] TODO %s[%u]\n",
                         vt.name.c_str(), slot - vt.first_slot);
        } else {
            std::fprintf(stderr, "  [vtable] TODO singleton-vslot #%u\n", slot);
        }
    }
    return 0;
}

void Mvos::register_handlers() {
    using T = TrapLayer;

    // cData_* : lazy asset descriptors that ARE a cMemBlock (confirmed from the
    // libmvos ctors — cData_Bitmap `0x60...`, cData_Sample `0x60640`, etc.).
    // Construct an empty (unloaded) block with valid primary (+0x08) and payload
    // (+0x1c) vtables so the caller's `if (!data && !IsValid) vptr[2](this)`
    // load hook resolves to a vtable trap instead of a null-vtable deref. Fall
    // back to the base cMemBlock vtables for classes whose own vtable isn't
    // COPY-relocated (e.g. cData_Font).
    auto make_descriptor = [this](const char* primary, const char* payload) {
        uint32_t vp = vtable_addr(primary);
        uint32_t vd = vtable_addr(payload);
        if (!vp) vp = vtable_addr("__vt_9cMemBlock");
        if (!vd) vd = vtable_addr("__vt_10cMemBlock_");
        return [vp, vd](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            m.w32(self + 0x00, 0);             // cNode next
            m.w32(self + 0x04, 0);             // cNode prev
            m.w32(self + 0x08, vp);            // primary (tNode) vtable
            m.w32(self + mb::DATA, 0);
            m.w32(self + mb::SIZE, 0);
            m.w32(self + mb::LOCK, 0);
            m.w32(self + mb::PRIO, 0x7f);
            m.w32(self + mb::VTBL, vd);        // cMemBlock_ payload vtable
            m.w32(self + 0x40, NULL_FRAME);    // cAnimBitmap frame-header ptr (unloaded)
            return self;                       // ctors return `this`
        };
    };
    traps_.register_handler("__12cData_BitmapPCcb",
        make_descriptor("__vt_12cData_Bitmap", "__vt_12cData_Bitmap.10cMemBlock_"));
    traps_.register_handler("__16cData_AnimBitmapPCcb",
        make_descriptor("__vt_16cData_AnimBitmap", "__vt_16cData_AnimBitmap.10cMemBlock_"));
    traps_.register_handler("__12cData_SamplePCc",
        make_descriptor("__vt_12cData_Sample", "__vt_12cData_Sample.10cMemBlock_"));
    traps_.register_handler("__13cData_PalettePCc",
        make_descriptor("__vt_13cData_Palette", "__vt_13cData_Palette.10cMemBlock_"));
    traps_.register_handler("__10cData_FontPCcb",
        make_descriptor("__vt_10cData_Font", "__vt_10cData_Font.10cMemBlock_"));

    // cRandom::Rnd() — LCG (constants read from libmvos): state = state*A + C;
    // returns state/DIV as a double in st0. Layout: +0x00 call counter, +0x04
    // state. Uses the x87 return path (return_double).
    traps_.register_handler("Rnd__7cRandom",
        [](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            uint32_t state = m.r32(self + 4) * 0xFFFFFFF1u + 0x7FFFFFFFu;  // A, C
            m.w32(self + 4, state);
            m.w32(self, m.r32(self) + 1);
            m.return_double((double)state / 4294967295.0);                // DIV = 2^32-1
            return 0;
        });

    // cNode / cHNode::UnLink() — splice out of the intrusive list (next @+0,
    // prev @+4). cHNode also clears its +0xc hash link.
    auto unlink = [](bool hnode) {
        return [hnode](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            uint32_t next = m.r32(self), prev = m.r32(self + 4);
            if (next && prev) {
                m.w32(next + 4, prev);
                m.w32(prev, next);
                m.w32(self, 0);
                m.w32(self + 4, 0);
            }
            if (hnode) m.w32(self + 0xc, 0);
            return 0;
        };
    };
    traps_.register_handler("UnLink__5cNode", unlink(false));
    traps_.register_handler("UnLink__6cHNode", unlink(true));

    // cList::UnLinkList() — unlink every node from the list.
    traps_.register_handler("UnLinkList__5cList",
        [](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            uint32_t node = m.r32(self);
            if (m.r32(node) == 0) node = 0;
            while (node) {
                uint32_t nextnode = m.r32(node);
                if (m.r32(nextnode) == 0) nextnode = 0;
                uint32_t nx = m.r32(node), pv = m.r32(node + 4);
                if (nx && pv) { m.w32(nx + 4, pv); m.w32(pv, nx);
                                m.w32(node, 0); m.w32(node + 4, 0); }
                node = nextnode;
            }
            return 0;
        });

    // cLocaleEntry(const char*) — node with vtable @+0x08, name @+0x0c. The
    // real ctor also registers into the global LocaleDataBase list; deferred
    // until the locale lookup path is needed (menu text).
    traps_.register_handler("__12cLocaleEntryPCc",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            m.w32(self + 0x00, 0);
            m.w32(self + 0x04, 0);
            m.w32(self + 0x08, vtable_addr("__vt_12cLocaleEntry"));
            m.w32(self + 0x0c, T::arg_at(m, esp, 1));   // key/name
            // Placeholder text = the key string, so GetText (an inlined read of
            // the +0x10 text field) is non-null until cLocaleDataBase::Load is
            // implemented to fill in the real localized strings.
            m.w32(self + 0x10, T::arg_at(m, esp, 1));
            uint8_t z = 0; m.write(self + 0x14, &z, 1);
            return self;
        });

    // cMemBlock_::IsValid(this) — "has data been loaded?" this points at the
    // cMemBlock_ subobject (== cMemBlock + 0x0c), so data is at +0x00.
    traps_.register_handler("IsValid__10cMemBlock_",
        [](Machine& m, uint32_t esp) -> uint32_t {
            return m.r32(T::arg_at(m, esp, 0)) != 0 ? 1 : 0;
        });

    // cAnimBitmap::GetBoundingBox() returns a 16-byte box by value (hidden
    // return pointer is arg0). Unloaded -> zero box.
    traps_.register_handler("GetBoundingBox__11cAnimBitmap",
        [](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t out = T::arg_at(m, esp, 0);
            for (int i = 0; i < 16; i += 4) m.w32(out + i, 0);
            return out;
        });

    // ---- cFile / cTextFile: native reimplementation over a decrypted body ----
    // Object layout (from libmvos): +0x08 open-flag (0=open), +0x0c fs object,
    // +0x14 filename, +0x20 encrypted marker. We key host file state by the
    // guest object address and serve reads from an in-memory decrypted body.
    constexpr uint32_t F_OPEN = 0x08, F_NAME = 0x14, F_ENC = 0x20;

    traps_.register_handler("OpenR__9cTextFileb",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            std::string name = m.cstr(m.r32(self + F_NAME));
            std::ifstream in(resolve_path(name), std::ios::binary);
            if (!in) {
                std::fprintf(stderr, "  [cTextFile] OpenR MISS %s\n", name.c_str());
                return 0;
            }
            std::string raw((std::istreambuf_iterator<char>(in)), {});
            files_[self] = {decrypt_body(raw), 0};
            m.w32(self + F_OPEN, 0);
            uint8_t one = 1; m.write(self + F_ENC, &one, 1);
            return 1;
        });

    traps_.register_handler("ReadLine__9cTextFilePci",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0), buf = T::arg_at(m, esp, 1);
            int max = (int)T::arg_at(m, esp, 2);
            auto it = files_.find(self);
            if (it == files_.end() || max <= 0) return 0;
            OpenFile& f = it->second;
            int n = 0;
            while (f.pos < f.body.size()) {
                char c = f.body[f.pos++];
                if (c == '\n' || n >= max - 1) break;
                if (c != '\r') { uint8_t b = (uint8_t)c; m.write(buf + n, &b, 1); n++; }
            }
            uint8_t z = 0; m.write(buf + n, &z, 1);
            return (uint32_t)n;
        });

    traps_.register_handler("CountLines__9cTextFilePi",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0), out = T::arg_at(m, esp, 1);
            auto it = files_.find(self);
            if (it == files_.end()) return 0;
            const std::string& b = it->second.body;
            int cnt = 0, cur = 0, maxl = 0;
            for (size_t p = it->second.pos; p < b.size(); ++p) {
                if (b[p] == '\n') { if (maxl < cur) maxl = cur; cur = 0; cnt++; }
                else cur++;
            }
            if (out) m.w32(out, maxl + 1);
            return (uint32_t)cnt;
        });

    traps_.register_handler("Close__5cFile",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            files_.erase(self);
            m.w32(self + F_OPEN, 1);           // 0=open -> nonzero=closed
            return 0;
        });

    traps_.register_handler("IsOpen__C5cFile",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            return files_.count(T::arg_at(m, esp, 0)) ? 1 : 0;
        });

    // cVVC::OpenDisplay(cVModeRequest&) — the game's video bring-up. In libmvos
    // this drives a dlopen'd backend mode-setter and sets up double-buffered
    // cScreen/cGD objects; we collapse it to "open an SDL window + RGB565
    // framebuffer". Request layout (confirmed from libmvos 0x95ce0): +0 w, +4 h,
    // +8 depthCode. We also record w/h/depth into the guest cVVC object at the
    // same offsets libmvos uses (+0x20 w, +0x24 h, +0x1c depth) so any later
    // guest read sees a consistent mode. Returns a nonzero char = success.
    traps_.register_handler("OpenDisplay__4cVVCRC13cVModeRequest",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t self = T::arg_at(m, esp, 0);
            uint32_t req  = T::arg_at(m, esp, 1);
            int w = (int)m.r32(req + 0), h = (int)m.r32(req + 4);
            int depth = (int)m.r32(req + 8);
            std::fprintf(stderr, "  [cVVC::OpenDisplay] request %dx%d depth-code %d\n", w, h, depth);
            bool ok = video_.open(w, h, depth);
            if (ok && self) {
                m.w32(self + 0x1c, (uint32_t)depth);
                m.w32(self + 0x20, (uint32_t)w);
                m.w32(self + 0x24, (uint32_t)h);
            }
            return ok ? 1u : 0u;
        });

    // ---- Render loop: cIntuition::ActivateScreen + cScreen refresh ----------
    // The game builds a cScreen whose header IS a cVModeRequest (w@+0, h@+4,
    // depth@+8) and whose root cVObject is at +0x14. The real ActivateScreen
    // (libmvos 0x9d830) calls OpenDisplay(VVC, screen) then stores the screen at
    // Intuition+0x24 (the active screen the whole render path reads). We collapse
    // it: open the SDL window from the screen header and set Intuition+0x24.
    traps_.register_handler("ActivateScreen__10cIntuitionP7cScreen",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t intuition = T::arg_at(m, esp, 0);
            uint32_t screen    = T::arg_at(m, esp, 1);
            if (!screen) { if (intuition) m.w32(intuition + 0x24, 0); return 1; }
            video_.open((int)m.r32(screen + 0), (int)m.r32(screen + 4),
                        (int)m.r32(screen + 8));
            if (intuition) m.w32(intuition + 0x24, screen);   // active screen
            // Placeholder background so it's visible that the game activated and
            // is driving its own screen. Real content arrives when PaintTree +
            // the cGD draw primitives paint the widget tree into Video::fb().
            if (video_.is_open()) {
                uint16_t* fb = video_.fb();
                int w = video_.width(), h = video_.height();
                for (int y = 0; y < h; ++y) {
                    uint16_t g = (uint16_t)(y * 10 / h);      // subtle dark gradient
                    uint16_t px = (uint16_t)((g << 11) | (g << 6) | (g + 2));
                    for (int x = 0; x < w; ++x) fb[y * w + x] = px;
                }
            }
            return 1;                                          // nonzero = success
        });

    // cVObject::CalcAbsCoordTree(tPoint origin, cRectangle clip) — the layout
    // pass (libmvos 0x9cc40). Args after `this`: tPoint{x,y} then cRectangle
    // {left,top,width,height}, all by value on the stack. We run the real
    // recursion natively so every widget's absolute, parent-clipped rect lands
    // at +0x5e (what PaintTree/paint_node read).
    traps_.register_handler("CalcAbsCoordTree__8cVObjectGt6tPoint1ZlG10cRectangle",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            calc_abs_coord(T::arg_at(m, esp, 0),
                           (int32_t)T::arg_at(m, esp, 1), (int32_t)T::arg_at(m, esp, 2),
                           (int32_t)T::arg_at(m, esp, 3), (int32_t)T::arg_at(m, esp, 4),
                           (int32_t)T::arg_at(m, esp, 5), (int32_t)T::arg_at(m, esp, 6));
            return 0;
        });

    // cScreen::BeginRefresh (real 0x9d2a0: ProcessInputs + RemoveTree +
    // RefreshFocus). We pump the SDL event queue; widget tree upkeep is deferred.
    traps_.register_handler("BeginRefresh__7cScreen",
        [this](Machine&, uint32_t) -> uint32_t { video_.pump(); return 0; });

    // cScreen::EndRefresh (real 0x9d2d0: PaintTree(root, VVC+0x14) + SwapBuffers).
    // `this` is the cScreen; its root cVObject is at +0x14. We walk that tree
    // natively (paint_tree) and present. The gradient laid down by ActivateScreen
    // stays as the background; the walk overdraws widget rectangles onto it.
    traps_.register_handler("EndRefresh__7cScreen",
        [this](Machine& m, uint32_t esp) -> uint32_t {
            uint32_t screen = T::arg_at(m, esp, 0);
            if (screen) paint_tree(m.r32(screen + 0x14));
            video_.present();
            return 0;
        });

    // Fatal(char*) is noreturn in the game (terminate-with-message). During
    // bring-up we print each distinct message once and continue, so a run
    // surfaces every assert at once instead of dying on the first.
    traps_.register_handler("Fatal__FPc",
        [](Machine& m, uint32_t esp) -> uint32_t {
            static std::set<std::string> seen;
            std::string msg = m.cstr(T::arg_at(m, esp, 0));
            if (seen.insert(msg).second)
                std::fprintf(stderr, "  [Fatal] %s\n", msg.c_str());
            return 0;
        });
}

// ---- native PaintTree (cVObject tree -> Video::fb) ----------------------
// cVObject layout, from libmvos tHNode base ctor (0xa6290) + PaintTree (0x9caf0):
//   +0x00 sibling-next   +0x08 vtable   +0x10 child-list head (first child =
//   *(node+0x10); an empty list is self-referential to node+0x1c)
//   +0x44 dirty short    +0x56 tPoint origin{x,y}
//   +0x5e cRectangle {left@+0, top@+4, right@+8, bottom@+0xc} (int32, abs coords)
// PaintTree's visibility gate is exactly "rect.right != 0 && rect.bottom != 0"
// (the +0x66/+0x6a it tests are the rect's right/bottom words), i.e. the widget
// has a non-empty box. We render each such node's real rectangle as a wireframe
// (per-class colour hashed from the vtable) until per-widget Paint + real
// fonts/bitmaps land — this validates tree topology and geometry against the
// actual menu layout.
namespace {
constexpr uint32_t VO_VTABLE   = 0x08;
constexpr uint32_t VO_CHILDREN = 0x10;
constexpr uint32_t VO_RECT     = 0x5e;

inline uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}
void fb_hline(uint16_t* fb, int W, int H, int x0, int x1, int y, uint16_t c) {
    if (y < 0 || y >= H) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = x0 < 0 ? 0 : x0; x1 = x1 >= W ? W - 1 : x1;
    for (int x = x0; x <= x1; ++x) fb[y * W + x] = c;
}
void fb_vline(uint16_t* fb, int W, int H, int x, int y0, int y1, uint16_t c) {
    if (x < 0 || x >= W) return;
    if (y0 > y1) std::swap(y0, y1);
    y0 = y0 < 0 ? 0 : y0; y1 = y1 >= H ? H - 1 : y1;
    for (int y = y0; y <= y1; ++y) fb[y * W + x] = c;
}
}  // namespace

void Mvos::calc_abs_coord(uint32_t node, int32_t px, int32_t py,
                          int32_t cl, int32_t ct, int32_t cw, int32_t ch) {
    if (!node) return;
    int32_t absX = (int32_t)m_.r32(node + 0x28) + px;   // relX + parent origin
    int32_t absY = (int32_t)m_.r32(node + 0x2c) + py;   // relY + parent origin
    m_.w32(node + 0x56, (uint32_t)absX);
    m_.w32(node + 0x5a, (uint32_t)absY);
    int32_t l = absX < cl ? cl : absX;                  // clip left  to parent
    int32_t t = absY < ct ? ct : absY;                  // clip top   to parent
    int32_t right  = absX + (int32_t)m_.r32(node + 0x30);
    int32_t w = right - l;
    if (cl + cw < right) w = (cl + cw) - l;             // clip right to parent
    int32_t bottom = absY + (int32_t)m_.r32(node + 0x34);
    int32_t h = bottom - t;
    if (ct + ch < bottom) h = (ct + ch) - t;            // clip bottom to parent
    if (w < 1 || h < 1) { l = 0; t = 0; w = 0; h = 0; } // fully clipped -> empty
    m_.w32(node + 0x5e, (uint32_t)l);
    m_.w32(node + 0x62, (uint32_t)t);
    m_.w32(node + 0x66, (uint32_t)w);
    m_.w32(node + 0x6a, (uint32_t)h);
    uint32_t child = m_.r32(node + VO_CHILDREN);
    int guard = 0;
    while (child != 0 && guard++ < 4096) {
        if (m_.r32(child) == 0) break;
        calc_abs_coord(child, absX, absY, l, t, w, h);  // origin + clipped rect
        child = m_.r32(child);
    }
}

void Mvos::paint_node(uint32_t node, int depth, bool dump) {
    if (!node || depth > 64) return;
    int32_t l = (int32_t)m_.r32(node + VO_RECT + 0);
    int32_t t = (int32_t)m_.r32(node + VO_RECT + 4);
    int32_t w = (int32_t)m_.r32(node + VO_RECT + 8);    // rect is {l,t,W,H}
    int32_t h = (int32_t)m_.r32(node + VO_RECT + 0xc);
    uint32_t vt = m_.r32(node + VO_VTABLE);
    bool visible = (w > 0 && h > 0);
    if (dump)
        std::fprintf(stderr, "  %*s node %08x vt %08x  x=%d y=%d w=%d h=%d%s\n",
                    depth * 2, "", node, vt, l, t, w, h,
                    visible ? "" : " (empty)");
    if (visible && video_.is_open()) {
        uint16_t* fb = video_.fb();
        int W = video_.width(), H = video_.height();
        int r = l + w - 1, b = t + h - 1;
        // Colour per widget class (hash the vtable), so distinct widgets read
        // as distinct boxes.
        uint16_t c = rgb565((int)((vt >> 4) & 0xff) | 0x40,
                            (int)((vt >> 12) & 0xff) | 0x40,
                            (int)((vt >> 20) & 0xff) | 0x40);
        fb_hline(fb, W, H, l, r, t, c);
        fb_hline(fb, W, H, l, r, b, c);
        fb_vline(fb, W, H, l, t, b, c);
        fb_vline(fb, W, H, r, t, b, c);
    }
    // Children: first = *(node+0x10); each child's next = *(child); the tail
    // sentinel stores 0. Mirror PaintTree's iteration exactly.
    uint32_t child = m_.r32(node + VO_CHILDREN);
    int guard = 0;
    while (child != 0 && guard++ < 4096) {
        if (m_.r32(child) == 0) break;      // sentinel reached
        paint_node(child, depth + 1, dump);
        child = m_.r32(child);              // next sibling
    }
}

void Mvos::paint_tree(uint32_t root) {
    static bool dumped = false;
    bool dump = false;
    if (!dumped && std::getenv("THEOC_TREE")) { dump = true; dumped = true; }
    if (dump) std::fprintf(stderr, "[paint_tree] root %08x\n", root);
    paint_node(root, 0, dump);
}

void Mvos::report() const {
    uint32_t hit = 0; uint64_t calls = 0;
    for (uint32_t i = 0; i < vt_hits_.size(); ++i)
        if (vt_hits_[i]) { hit++; calls += vt_hits_[i]; }
    std::fprintf(stderr, "  vtable slots hit: %u / %u  (%llu calls)\n", hit,
                vt_slot_count_, (unsigned long long)calls);
}
