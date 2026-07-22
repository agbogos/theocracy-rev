// Native MVOS layer (M2): turns the R_386_COPY framework data the game
// references directly into working guest state —
//   * __vt_* vtables  -> synthesized tables whose every slot is a trap, so a
//                        guest virtual dispatch lands in dispatch_vtable();
//   * pointer singletons (SystemMemory, Intuition, ...) -> real guest objects;
// and registers native handlers for the imported MVOS methods the boot hits.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "elf32.hpp"
#include "machine.hpp"
#include "traps.hpp"
#include "video.hpp"

class Mvos {
public:
    Mvos(Machine& m, const elf32::Image& img, TrapLayer& traps)
        : m_(m), img_(img), traps_(traps) {}

    Video& video() { return video_; }

    // Process R_386_COPY relocs: fill vtables with trap slots, wire singletons,
    // install the vtable-trap hook, and register MVOS method handlers.
    void apply_copyrelocs();

    // Guest address of a synthesized vtable by symbol name (0 if absent).
    uint32_t vtable_addr(const std::string& name) const {
        auto it = vt_addr_.find(name);
        return it == vt_addr_.end() ? 0 : it->second;
    }

    void report() const;

private:
    uint32_t dispatch_vtable(Machine& m, uint32_t slot, uint32_t esp);
    void register_handlers();
    void wire_singleton_vtables();

    // Native reimplementation of cVObject::PaintTree (libmvos 0x9caf0): walk the
    // cScreen's cVObject tree and render each visible widget into Video::fb().
    // The real per-widget Paint methods live in libmvos (trapped, no guest
    // code), so the walk lives here. `dump` prints the tree once for validation.
    void paint_tree(uint32_t root);
    void paint_node(uint32_t node, int depth, bool dump);

    // Native cVObject::CalcAbsCoordTree (libmvos 0x9cc40): the layout pass that
    // turns each node's relative pos/size (+0x28/+0x2c/+0x30/+0x34) into an
    // absolute, parent-clipped rectangle at +0x56 (origin) / +0x5e (rect).
    void calc_abs_coord(uint32_t node, int32_t px, int32_t py,
                        int32_t cl, int32_t ct, int32_t cw, int32_t ch);

    // Singleton virtual dispatch: the game calls polymorphic methods on the
    // framework singletons (IPCSystem, SoundCard, ...) whose class vtables are
    // NOT copy-relocated. We give each singleton's backing object a synthesized
    // vtable (slots -> VT traps in a reserved pool), so a virtual call lands in
    // dispatch_vtable instead of a null deref, and register native overrides for
    // the slots that need real behavior.
    uint32_t alloc_guest(uint32_t size);            // zeroed bump in SINGLETON space
    uint32_t make_vtable(uint32_t nslots);          // pool-backed synth vtable
    void hook_vslot(uint32_t vtable, uint32_t slot, TrapLayer::Handler fn);

    Machine& m_;
    const elf32::Image& img_;
    TrapLayer& traps_;

    struct Vt { std::string name; uint32_t addr, nslots, first_slot; };
    std::vector<Vt> vts_;
    std::vector<uint32_t> slot_to_vt_;       // global vt-slot -> index into vts_
    std::vector<uint64_t> vt_hits_;          // per global vt-slot
    std::unordered_map<std::string, uint32_t> vt_addr_;
    uint32_t vt_slot_count_ = 0;
    uint32_t copyreloc_vt_slots_ = 0;        // slots owned by copy-reloc'd __vt_*
    uint32_t svt_next_ = 0;                   // next free pool slot for synth vtables
    uint32_t singleton_next_ = 0;            // bump within SINGLETON_BASE
    std::unordered_map<std::string, uint32_t> singleton_obj_;   // name -> backing obj
    std::unordered_map<uint32_t, TrapLayer::Handler> vhandlers_; // global slot -> native

    // Host-side cFile/cTextFile state, keyed by the guest object address. The
    // decrypted file body lives here; the guest object only carries the fields
    // the game inlines (filename, flags).
    struct OpenFile { std::string body; size_t pos = 0; };
    std::unordered_map<uint32_t, OpenFile> files_;
    std::string data_root_;                  // host dir the guest "data/..." maps under
    std::string resolve_path(const std::string& guest_name) const;

    Video video_;                            // native SDL VVC/GD backend
};
