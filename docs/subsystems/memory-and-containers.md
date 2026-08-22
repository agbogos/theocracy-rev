# Memory & Containers

Core primitives every other subsystem builds on. Addresses are in `libmvos.so`.

## cSystemMemory — budgeted, evictable memory manager

`cSystemMemory` is not a plain allocator; it's a **resource cache with a memory
budget**.

- Ctor @ `0x8b090`. Fields: `+0x04` `tList<cMemBlock>` (all live blocks),
  `+0x1c` total budget = **`0x2000000` (32 MB)**, `+0x20` remaining.
- **Alloc** (`0x8b2b0`, `cSystemMemory::Alloc(cMemBlock&)`):
  - If the request fits the remaining budget: `operator new[]` the buffer, store it in the block, push the block on the list, decrement remaining.
  - If it doesn't fit: evict **oldest-first** — walk from the tail sentinel backwards and drop the first UNLOCKED block (`delete[]` its buffer, unlink it, credit its size back), then retry.
  - If nothing is evictable → `Fatal("Out of memory (mvos limit)")`. (A hard `new` failure → `Fatal("Out of memory")`.)

**Eviction order — resolved (audit 2026-07-26, decompilation).** Insertion
pushes at the **head** (`*(this+4) = block`). The evict walk starts at
`*(this+0x14)` — the tail sentinel's `prev` (`tList` sits at `+0x04`, and a
`cList` is head node `+0x00` / tail node `+0x0c`, so the tail sentinel lands at
`+0x10` and its `prev` at `+0x14`) — and follows `prev` backwards.
Newest-at-head + walk-from-tail = **oldest-first eviction**, and `priority`
(`+0x18`) is **never read**. So the field is vestigial as far as `Alloc` is
concerned; it is set to `0x7f` by the ctor and no eviction path consults it.

> **Engine bug spotted in passing.** Each eviction credits the freed size to the budget **twice** — once via `*(this+0x20)` at the top of the loop, then again via the global `SystemMemory+0x20` after the `__builtin_delete` (the inlined `Free`). With `this == SystemMemory` (always, it is a singleton) that over-credits `remaining` by one block per eviction. Cold path — only reachable under budget pressure, which the 32 MB budget makes rare — and harmless to us since the guest heap is ours, but do not "fix" it in any reimplementation without checking what depends on the slack.
- **FlushAll / FlushAllUnlocked** (`0x8b0c0` / `0x8b170`): bulk-evict blocks.

Effectively a game-asset cache: load-on-demand, pin what you're using (Lock),
and let the rest be reclaimed under memory pressure by priority.

## cMemBlock — lockable, evictable heap block

Constructor `cMemBlock(unsigned long size)` @ `0x8b640`. **32-byte layout:**

| Off | Field | Notes |
|-----|-------|-------|
| 0x00 | `next` | intrusive list (cNode) |
| 0x04 | `prev` | intrusive list (cNode) |
| 0x08 | vtable | cNode / tNode<cMemBlock> |
| 0x0c | `data` | buffer from `operator new[]` (start of embedded `cMemBlock_`) |
| 0x10 | `size` | bytes |
| 0x14 | `lockCount` | 0 = evictable; >0 = pinned |
| 0x18 | `priority` | default `0x7f` |
| 0x1c | vtable | payload (`cMemBlock_`) |

- Multiple inheritance: `cMemBlock : tNode<cMemBlock>, cMemBlock_` → hence the
  two vtable slots (`+0x08`, `+0x1c`). `cMemBlock_` holds `{data, size,
  lockCount}` and owns Lock/Unlock/IsLocked/GetAddress/GetSize/Validate.
- **Construct** (`0x8b5c0`): asserts not-already-valid, then `Alloc`. **Free**
  (`0x8b3d0`): unlink from list + `delete[]` buffer.
- **Lock/Unlock** (`0x8b960`/`0x8b950`): ref-count on `+0x14`. `GetAddress`
  requires the block be locked (`Fatal("cMemBlock_::GetAddress(): object is not
  locked")`) — enforces pin-before-use so eviction is safe.
- `cMemBlockPTR` (`0x8b7f0`): a smart-handle/reference to a block. `cMemoryPipe`
  (`0x8ba60`): FIFO buffer built on the same machinery
  (Push/Clear/GetFree/GetLargestFree/IsEmpty).

## cString — a string *is* a memory block

Ctor `cString(const char*)` @ `0x93fd0`. `cString : tMemBlock<char> →
cMemBlock`, same 32-byte layout. Ctor does `strlen`, `Alloc(len+1)` through
`cSystemMemory`, Locks, and copies. So **string storage is a lockable/evictable
managed block**, not a raw `malloc`.

- Full operator set: `= + += == != < <= > >= []`, plus `Cat`, `Copy`, `Sub`,
  `Add`, `GetCString` (`0x94ba0`), `GetLength`.
- `operator+=` (`0x94310`) = build temp via `operator+`, blit 32 bytes over
  `this`, preserve vtable. Value semantics on top of the block.
- `cOldString` (@ `0x94870`): a legacy/simpler string kept alongside
  (`StrCopy`/`StrCmp`/`Copy`). Char helpers in namespaces `CHARCONVERT`,
  `CHARFILTER`.

## cNode / cList — AmigaOS Exec-style lists

- **cNode** @ `0x6f540`: `{+0x00 next, +0x04 prev, +0x08 vtable}`. Methods:
  `AddPrev`/`AddNext`/`UnLink`/`IsLinked`/`GetPrev`/`GetNext`. Standard
  intrusive doubly-linked node.
- **cList** @ `0x6f410`: circular doubly-linked list with **two embedded
  sentinel nodes** (head `+0x00`, tail `+0x0c`), each pointing at the other when
  empty. This is the AmigaOS Exec `List`/`MinList` layout
  (`lh_Head`/`lh_Tail`/`lh_TailPred`) — direct evidence of the engine's Amiga
  heritage.
- Templated instantiations exist per element type: `tList<T>`, `tNode<T>`,
  `tHNode<T>`, plus hashed variants `cHList`/`cHNode`. `tMemBlock<T>`
  specializes `cMemBlock` (e.g. `<char>` = cString, `<cColor>`, `<unsigned
  char>`).

## Notes / provenance
- **Hungarian devs (Philos Laboratories):** `cNode` copy-ctor aborts with
  `Fatal("cNode(const cNode &n): Ezt nem kene!")` — Hungarian for "This
  shouldn't be done!". Expect more Hungarian assert strings elsewhere; useful as
  a signal when reading unfamiliar code.
- Decompiler artifact: constructors that inline `Alloc`+`Lock` (both call the
  `noreturn` `Fatal`) get mis-flattened so the happy path looks like it always
  aborts. Read those as "assert-then-continue", not "always Fatal".

## Open threads
- ~~Confirm `priority` (`+0x18`) role in eviction ordering~~ **RESOLVED
  (2026-07-26)** — `Alloc` evicts **oldest-first** and never reads `priority`;
  see the eviction-order note above.
- `cHeap_Compatibility` / `cHeapBlock` — a second allocator path (compatibility
  heap). How does it relate to `cSystemMemory`?
- `cMemBlockPTR` semantics: does it auto-lock on construct / unlock on destruct
  (RAII pin)?
