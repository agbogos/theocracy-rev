// HLE trap layer: maps each imported symbol (by slot) to a native handler.
// Unimplemented imports log once and return 0 — that log IS the M1/M2 worklist.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "machine.hpp"

class TrapLayer {
public:
    // names[slot] = imported symbol name in trap-slot order.
    explicit TrapLayer(std::vector<std::string> names);

    // TrapFn entry point (bind to Machine::install_traps).
    uint32_t dispatch(Machine& m, uint32_t slot, uint32_t esp);

    // Print the tally of which imports were hit (implemented vs. TODO).
    void report() const;

    uint32_t nslots() const { return (uint32_t)names_.size(); }

    // Guest trap address for an imported symbol (TRAP_BASE + slot), or 0 if the
    // symbol isn't imported. Lets the host invoke an import handler directly.
    uint32_t trap_addr(const std::string& name) const {
        for (uint32_t i = 0; i < names_.size(); ++i)
            if (names_[i] == name) return guestmap::TRAP_BASE + i;
        return 0;
    }

    using Handler = std::function<uint32_t(Machine&, uint32_t esp)>;
    // Inject/override a native handler for an imported symbol (used by the MVOS
    // layer). cdecl arg i is at [esp + 4 + 4*i]; return value goes to EAX.
    void register_handler(const std::string& name, Handler fn) {
        table_[name] = std::move(fn);
    }
    static uint32_t arg_at(Machine& m, uint32_t esp, int i) {
        return m.r32(esp + 4 + 4u * i);
    }

private:

    void register_builtins();
    // convenience: cdecl arg i (0-based), ESP points at return addr on entry.
    static uint32_t arg(Machine& m, uint32_t esp, int i) {
        return m.r32(esp + 4 + 4u * i);
    }
    std::string format(Machine& m, const std::string& fmt, uint32_t esp, int argidx);

    std::vector<std::string> names_;
    std::vector<uint64_t>    hits_;
    std::unordered_map<std::string, Handler> table_;

    // bump allocator over guestmap::HEAP_BASE
    uint32_t heap_next_;
    std::unordered_map<uint32_t, uint32_t> alloc_sz_;
    uint32_t bump_alloc(uint32_t size);
};
