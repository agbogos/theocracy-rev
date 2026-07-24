#include "machine.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace guestmap;

static void uc_check(uc_err e, const char* what) {
    if (e != UC_ERR_OK) {
        std::fprintf(stderr, "unicorn error in %s: %s\n", what, uc_strerror(e));
        throw std::runtime_error(what);
    }
}

Machine::Machine() {
    uc_check(uc_open(UC_ARCH_X86, UC_MODE_32, &uc_), "uc_open");
    // Record the address of any invalid access so faults are diagnosable.
    uc_hook h;
    uc_hook_add(uc_, &h, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                UC_HOOK_MEM_FETCH_UNMAPPED, (void*)&Machine::mem_hook, this, 1, 0);
}

bool Machine::mem_hook(uc_engine*, int, uint64_t addr, int, int64_t, void* user) {
    static_cast<Machine*>(user)->last_fault_addr_ = (uint32_t)addr;
    return false;   // unhandled -> the access still faults
}

void Machine::request_stop() {
    stop_requested_ = true;
    setreg(UC_X86_REG_EIP, STOP_ADDR);
    trap_raw_ = true;                 // skip ret-emulation; leave EIP on STOP
    uc_emu_stop(uc_);                 // end the current uc_emu_start promptly
}

void Machine::return_double(double v) {
    // Lazily install the `FLD qword [FLOAT_SCRATCH]; RET` stub (RWX page).
    static bool installed = false;
    if (!installed) {
        map(STUB_BASE, 0x1000, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
        uint8_t code[7] = {0xDD, 0x05,          // FLD m64fp, [disp32]
                           (uint8_t)FLOAT_SCRATCH, (uint8_t)(FLOAT_SCRATCH >> 8),
                           (uint8_t)(FLOAT_SCRATCH >> 16), (uint8_t)(FLOAT_SCRATCH >> 24),
                           0xC3};              // RET
        write(STUB_BASE, code, sizeof code);
        installed = true;
    }
    write(FLOAT_SCRATCH, &v, sizeof v);
    setreg(UC_X86_REG_EIP, STUB_BASE);   // stack untouched -> stub's RET returns to caller
    trap_raw_ = true;
}

void Machine::redirect_guest(uint32_t eip, uint32_t esp) {
    setreg(UC_X86_REG_EIP, eip);
    setreg(UC_X86_REG_ESP, esp);
    trap_raw_ = true;
}

Machine::~Machine() { if (uc_) uc_close(uc_); }

void Machine::map(uint32_t addr, uint32_t size, uint32_t prot) {
    uc_check(uc_mem_map(uc_, addr, size, prot), "uc_mem_map");
}
void Machine::write(uint32_t addr, const void* data, uint32_t n) {
    uc_check(uc_mem_write(uc_, addr, data, n), "uc_mem_write");
}
void Machine::read(uint32_t addr, void* out, uint32_t n) const {
    uc_check(uc_mem_read(uc_, addr, out, n), "uc_mem_read");
}
uint32_t Machine::r32(uint32_t addr) const { uint32_t v; read(addr, &v, 4); return v; }
void Machine::w32(uint32_t addr, uint32_t v) { write(addr, &v, 4); }

std::string Machine::cstr(uint32_t addr, uint32_t max) const {
    std::string s;
    for (uint32_t i = 0; i < max; ++i) {
        uint8_t c; read(addr + i, &c, 1);
        if (!c) break;
        s.push_back((char)c);
    }
    return s;
}

uint32_t Machine::reg(int r) const { uint32_t v; uc_reg_read(uc_, r, &v); return v; }
void Machine::setreg(int r, uint32_t v) { uc_reg_write(uc_, r, &v); }
uint32_t Machine::esp() const { return reg(UC_X86_REG_ESP); }

// ---- guest EIP profiler (THEOC_PROFILE) --------------------------------------
// UC_HOOK_BLOCK fires once per basic-block entry with the block's byte size; we
// accumulate size per block-start EIP (Σ instruction bytes ≈ work ≈ time). Only
// guest code (game/mvos, < 0x50000000) is counted; host trap/stub/scratch pages
// are skipped. Dumped as a rolling window so the top-N tracks the current screen.
void Machine::count_hook(uc_engine*, uint64_t addr, uint32_t, void* user) {
    auto* self = static_cast<Machine*>(user);
    if ((uint32_t)addr >= 0x50000000) return;    // host trap/stub/scratch — ignore
    self->blocks_++;
}

void Machine::enable_block_counter() {
    if (profiling_) return;                      // block_hook already counts
    uc_hook h;
    uc_check(uc_hook_add(uc_, &h, UC_HOOK_BLOCK, (void*)&Machine::count_hook,
                         this, 1, 0),
             "uc_hook_add(count)");
}

void Machine::block_hook(uc_engine*, uint64_t addr, uint32_t size, void* user) {
    auto* self = static_cast<Machine*>(user);
    uint32_t a = (uint32_t)addr;
    if (a >= 0x50000000) return;                 // host trap/stub/scratch — ignore
    self->blocks_++;
    self->prof_[a] += size;
    if ((++self->prof_ticks_ & 0x3ffff) == 0) {  // check the clock ~every 256k blocks
        auto now = std::chrono::steady_clock::now();
        if (now - self->prof_last_ >= std::chrono::seconds(3)) {
            self->prof_dump();
            self->prof_last_ = now;
            self->prof_.clear();
        }
    }
}

void Machine::prof_dump() {
    if (prof_.empty()) return;
    std::vector<std::pair<uint32_t, uint64_t>> v(prof_.begin(), prof_.end());
    std::sort(v.begin(), v.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    uint64_t total = 0;
    for (auto& e : v) total += e.second;
    std::fprintf(stderr, "\n--- [profile] top guest blocks (window Σ=%llu) ---\n",
                 (unsigned long long)total);
    uint32_t mv = prof_mvos_base_ ? prof_mvos_base_ : 0x10000000u;
    for (size_t i = 0; i < v.size() && i < 15; ++i) {
        uint32_t a = v[i].first;
        double pct = total ? 100.0 * (double)v[i].second / (double)total : 0.0;
        char lbl[40];
        if (a >= mv && a < mv + 0x200000) std::snprintf(lbl, sizeof lbl, "mvos+%#x", a - mv);
        else std::snprintf(lbl, sizeof lbl, "game %#010x", a);
        std::fprintf(stderr, "  %5.1f%%  %-18s (Σ%llu)\n", pct, lbl,
                     (unsigned long long)v[i].second);
    }
}

void Machine::enable_profiling(uint32_t mvos_base) {
    profiling_ = true;
    prof_mvos_base_ = mvos_base;
    prof_last_ = std::chrono::steady_clock::now();
    uc_hook h;
    // begin=1 > end=0 → hook the whole address space; we filter in the callback.
    uc_check(uc_hook_add(uc_, &h, UC_HOOK_BLOCK, (void*)&Machine::block_hook,
                         this, 1, 0),
             "uc_hook_add(profile)");
    std::fprintf(stderr, "[profile] guest block profiler ON "
                 "(rolling top-15 every 3s; mvos base %#x)\n", mvos_base);
}

void Machine::add_code_traps(uint32_t base, uint32_t nslots, TrapFn fn,
                             bool map_region) {
    if (map_region) {
        // Back the window with executable memory (content never executes; the
        // hook rewrites EIP first) so a jump there is a hook hit, not a fault.
        uint32_t page = (nslots + 0xfff) & ~0xfffu;
        if (page < 0x1000) page = 0x1000;
        map(base, page, UC_PROT_READ | UC_PROT_EXEC);
    }
    regions_.push_back({base, base + nslots, std::move(fn)});
    uc_hook h;
    uc_check(uc_hook_add(uc_, &h, UC_HOOK_CODE, (void*)&Machine::code_hook,
                         this, base, base + nslots - 1),
             "uc_hook_add");
}

// Fires for EIP inside a registered trap window. Emulate a cdecl callee: read
// the return address off the stack, run the native handler, then "ret" (pop the
// return addr, jump to it) with the result in EAX. Args stay on the stack —
// cdecl is caller-cleanup.
void Machine::code_hook(uc_engine*, uint64_t addr, uint32_t, void* user) {
    Machine* m = static_cast<Machine*>(user);
    for (auto& r : m->regions_) {
        if (addr < r.lo || addr >= r.hi) continue;
        uint32_t sp = m->esp();
        uint32_t retaddr = m->r32(sp);
        uint32_t eax = r.fn(*m, (uint32_t)addr - r.lo, sp);
        if (m->trap_raw_) { m->trap_raw_ = false; return; }  // handler set EIP/stack itself
        m->setreg(UC_X86_REG_ESP, sp + 4);
        m->setreg(UC_X86_REG_EAX, eax);
        m->setreg(UC_X86_REG_EIP, retaddr);
        return;
    }
}

uint32_t Machine::call(uint32_t addr, const std::vector<uint32_t>& args,
                       uint64_t timeout_us) {
    uint32_t saved_sp = esp();
    uint32_t sp = saved_sp;
    for (auto it = args.rbegin(); it != args.rend(); ++it) { sp -= 4; w32(sp, *it); }
    sp -= 4; w32(sp, STOP_ADDR);              // return address sentinel
    setreg(UC_X86_REG_ESP, sp);
    stop_requested_ = false;
    last_aborted_ = false;
    uc_err e = uc_emu_start(uc_, addr, STOP_ADDR, timeout_us, 0);
    if (e != UC_ERR_OK && !stop_requested_) {
        last_fault_eip_ = reg(UC_X86_REG_EIP);
        last_fault_esp_ = reg(UC_X86_REG_ESP);
        last_fault_stack_n_ = 0;
        for (int i = 0; i < 16; ++i) {
            try {
                last_fault_stack_[i] = r32(last_fault_esp_ + 4u * i);
                last_fault_stack_n_ = i + 1;
            } catch (...) { break; }
        }
        char msg[160];
        std::snprintf(msg, sizeof msg,
                      "%s at eip=%#x accessing %#x", uc_strerror(e),
                      last_fault_eip_, last_fault_addr_);
        setreg(UC_X86_REG_ESP, saved_sp);
        throw std::runtime_error(msg);
    }
    // Timeout returns UC_ERR_OK but leaves EIP wherever it stopped; a clean
    // return lands on the STOP sentinel. abort/exit uses request_stop().
    last_aborted_ = stop_requested_;
    last_returned_ = (reg(UC_X86_REG_EIP) == STOP_ADDR) && !stop_requested_;
    uint32_t ret = reg(UC_X86_REG_EAX);
    setreg(UC_X86_REG_ESP, saved_sp);         // cdecl: caller restores stack
    return ret;
}
