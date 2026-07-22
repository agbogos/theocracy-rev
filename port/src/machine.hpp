// Unicorn i386 wrapper: guest memory, the HLE trap layer, and the
// native->guest "call a guest function" primitive.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <unicorn/unicorn.h>

// Fixed guest memory layout for the M1 host (all inside Unicorn's own space;
// no host<->guest 1:1 aliasing yet — that is the M2+ zero-copy step).
namespace guestmap {
constexpr uint32_t TRAP_BASE = 0x77000000;  // 1 byte per import; RX
constexpr uint32_t VT_TRAP_BASE = 0x78000000; // 1 byte per synthesized vtable slot; RX
constexpr uint32_t STUB_BASE = 0x79000000;    // tiny guest x87 stub for float returns
constexpr uint32_t FLOAT_SCRATCH = STUB_BASE + 0x800;
constexpr uint32_t SINGLETON_BASE = 0x51000000; // backing for pointer singletons
constexpr uint32_t SINGLETON_SIZE = 0x00100000;
constexpr uint32_t NULL_FRAME = 0x52000000;     // shared zeroed "unloaded" anim frame-header
constexpr uint32_t NULL_FRAME_SIZE = 0x00001000;
constexpr uint32_t HEAP_BASE = 0x60000000;  // bump allocator arena
constexpr uint32_t HEAP_SIZE = 0x08000000;  // 128 MB
constexpr uint32_t STACK_TOP = 0x70000000;  // grows down
constexpr uint32_t STACK_SIZE = 0x00200000; // 2 MB
constexpr uint32_t SCRATCH    = 0x50000000; // fake cApplication `this`, etc.
constexpr uint32_t SCRATCH_SIZE = 0x00100000;
constexpr uint32_t STOP_ADDR  = 0xDEAD0000; // sentinel return addr for call()
}

// A trap fires with the guest ESP pointing at the return address; args follow
// (cdecl / g++ 2.95 "this-as-first-stack-arg"). The handler returns the value
// to place in EAX. `slot` is the import index (== EIP - TRAP_BASE).
class Machine;
using TrapFn = std::function<uint32_t(Machine&, uint32_t slot, uint32_t esp)>;

class Machine {
public:
    Machine();
    ~Machine();

    // --- memory ---------------------------------------------------------
    void map(uint32_t addr, uint32_t size, uint32_t uc_prot);
    void write(uint32_t addr, const void* data, uint32_t n);
    void read(uint32_t addr, void* out, uint32_t n) const;
    uint32_t r32(uint32_t addr) const;
    void w32(uint32_t addr, uint32_t v);
    std::string cstr(uint32_t addr, uint32_t max = 4096) const;

    // --- registers ------------------------------------------------------
    uint32_t reg(int uc_reg) const;
    void setreg(int uc_reg, uint32_t v);
    uint32_t esp() const;

    // --- trap layer -----------------------------------------------------
    // Register a scoped code hook over [base, base + nslots): a jump into that
    // window fires `fn(*this, addr-base, esp)`. Used for the import boundary
    // and, separately, for synthesized vtable slots. `map_region` maps the
    // window RX (pass false if it is already mapped, e.g. vtables in .bss).
    void add_code_traps(uint32_t base, uint32_t nslots, TrapFn fn,
                        bool map_region = true);
    // Back-compat shim for the import boundary.
    void install_traps(uint32_t nslots, TrapFn dispatch) {
        add_code_traps(guestmap::TRAP_BASE, nslots, std::move(dispatch));
    }

    // --- execution ------------------------------------------------------
    // Call a guest function with cdecl args; returns EAX. Restores ESP.
    uint32_t call(uint32_t addr, const std::vector<uint32_t>& args = {},
                  uint64_t timeout_us = 0);

    // Return a floating-point value from the current trap: a native handler
    // can't set st0 directly, so we stash the value and redirect execution
    // into a guest `FLD [scratch]; RET` stub — real x87 does the FPU push. The
    // trap's return address stays on the stack for the stub's RET. Call this
    // from a handler instead of returning a value; the handler's return is
    // then ignored.
    void return_double(double v);
    // True if the most recent call() returned to the STOP sentinel (i.e. the
    // guest function returned normally rather than timing out / stopping early).
    bool last_returned() const { return last_returned_; }

    uc_engine* uc() const { return uc_; }

    // Address of the last invalid memory access (for diagnostics).
    uint32_t last_fault_addr() const { return last_fault_addr_; }

private:
    static void code_hook(uc_engine*, uint64_t addr, uint32_t size, void* user);
    static bool mem_hook(uc_engine*, int type, uint64_t addr, int size,
                         int64_t value, void* user);

    struct TrapRegion { uint32_t lo, hi; TrapFn fn; };
    uc_engine* uc_ = nullptr;
    std::vector<TrapRegion> regions_;
    bool last_returned_ = false;
    bool trap_raw_ = false;          // handler took over EIP/stack; skip ret-emulation
    uint32_t last_fault_addr_ = 0;
};
