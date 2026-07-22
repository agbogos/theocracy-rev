#include "machine.hpp"
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
    uc_err e = uc_emu_start(uc_, addr, STOP_ADDR, timeout_us, 0);
    if (e != UC_ERR_OK) {
        char msg[160];
        std::snprintf(msg, sizeof msg,
                      "%s at eip=%#x accessing %#x", uc_strerror(e),
                      reg(UC_X86_REG_EIP), last_fault_addr_);
        setreg(UC_X86_REG_ESP, saved_sp);
        throw std::runtime_error(msg);
    }
    // Timeout returns UC_ERR_OK but leaves EIP wherever it stopped; a clean
    // return lands on the STOP sentinel.
    last_returned_ = (reg(UC_X86_REG_EIP) == STOP_ADDR);
    uint32_t ret = reg(UC_X86_REG_EAX);
    setreg(UC_X86_REG_ESP, saved_sp);         // cdecl: caller restores stack
    return ret;
}
