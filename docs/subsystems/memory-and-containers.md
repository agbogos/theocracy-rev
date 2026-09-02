# Memory & Containers

Core primitives every other subsystem builds on. Addresses are in `libmvos.so`.

## cSystemMemory — budgeted, evictable memory manager

`cSystemMemory` is a resource cache with a memory budget rather than a plain
allocator.

- Ctor @ `0x8b090`. Fields: `+0x04` `tList<cMemBlock>` (all live blocks),
  `+0x1c` total budget = `0x2000000` (32 MB), `+0x20` remaining.
- Alloc (`0x8b2b0`, `cSystemMemory::Alloc(cMemBlock&)`):
  - If the request fits the remaining budget: `operator new[]` the buffer, store
    it in the block, push the block on the list, decrement remaining.
  - If it does not fit: evict oldest-first, walking from the tail sentinel
    backwards and dropping the first UNLOCKED block (`delete[]` its buffer,
    unlink it, credit its size back), then retry.
  - If nothing is evictable → `Fatal("Out of memory (mvos limit)")`. A hard
    `new` failure → `Fatal("Out of memory")`.

**Eviction order**, from the decompilation. Insertion pushes at the head
(`*(this+4) = block`). The evict walk starts at `*(this+0x14)`, the tail
sentinel's `prev` — `tList` sits at `+0x04`, and a `cList` is head node `+0x00`
/ tail node `+0x0c`, so the tail sentinel lands at `+0x10` and its `prev` at
`+0x14` — and follows `prev` backwards. Newest-at-head plus walk-from-tail gives
oldest-first eviction. `priority` (`+0x18`) is set to `0x7f` by the ctor and is
read by no eviction path, so it is vestigial.

> **Engine bug spotted in passing.** Each eviction credits the freed size to the
> budget twice: once via `*(this+0x20)` at the top of the loop, then again via
> the global `SystemMemory+0x20` after the `__builtin_delete` (the inlined
> `Free`). With `this == SystemMemory`, which it always is since it is a
> singleton, that over-credits `remaining` by one block per eviction. It is a
> cold path, only reachable under budget pressure that the 32 MB budget makes
> rare, and harmless here because the guest heap is ours. Anything
> reimplementing it should check what depends on the slack before correcting it.

- FlushAll / FlushAllUnlocked (`0x8b0c0` / `0x8b170`): bulk-evict blocks.

In use it is a game-asset cache: load on demand, `Lock` what is in use, and let
the oldest unlocked blocks be reclaimed under memory pressure.

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

## cData_* — lazy asset descriptors

`cData_Bitmap`, `cData_AnimBitmap`, `cData_Sample`, `cData_Palette` and
`cData_Font` are `cMemBlock` subclasses that name an asset without loading it.
`cData_Font` derives through `cAnimBitmap`, so the bitmap accessors apply to it
too.

- The load hook is payload vtable slot 2. Callers do `if (!data && !IsValid)
  (*(payloadVtbl + 8))(this)` before touching the block, so an unloaded
  descriptor pulls its own bytes in on first use — `data` at `+0x0c` and the
  payload vtable at `+0x1c`, per the `cMemBlock` layout above.
- `+0x40` is the frame/anim header pointer and `hdr+0x41c` the sprite height.
  The game reads the pair together (`0x816ce13`: `MOV EAX,[EDI+0x40]`, then the
  height off the header), so a descriptor whose `+0x40` is still null
  dereferences `0x41c` rather than reading a size of zero.

Read off the binaries during the 2026-07 pure-HLE bring-up, where they were
exercised only far enough to boot; not re-checked since.

## cString — a string *is* a memory block

Ctor `cString(const char*)` @ `0x93fd0`. `cString : tMemBlock<char> →
cMemBlock`, same 32-byte layout. The ctor does `strlen`, `Alloc(len+1)` through
`cSystemMemory`, Locks, and copies, so string storage is a lockable and
evictable managed block that goes through the budget above.

- Full operator set: `= + += == != < <= > >= []`, plus `Cat`, `Copy`, `Sub`,
  `Add`, `GetCString` (`0x94ba0`), `GetLength`.
- `operator+=` (`0x94310`) = build temp via `operator+`, blit 32 bytes over
  `this`, preserve vtable. Value semantics on top of the block.
- `cOldString` (@ `0x94870`): a legacy/simpler string kept alongside
  (`StrCopy`/`StrCmp`/`Copy`). Char helpers in namespaces `CHARCONVERT`,
  `CHARFILTER`.

## cNode / cList — AmigaOS Exec-style lists

- `cNode` @ `0x6f540`: `{+0x00 next, +0x04 prev, +0x08 vtable}`. Methods
  `AddPrev`/`AddNext`/`UnLink`/`IsLinked`/`GetPrev`/`GetNext` — a standard
  intrusive doubly-linked node.
- `cList` @ `0x6f410`: a circular doubly-linked list with two embedded sentinel
  nodes (head `+0x00`, tail `+0x0c`), each pointing at the other when empty.
  This is the AmigaOS Exec `List`/`MinList` layout
  (`lh_Head`/`lh_Tail`/`lh_TailPred`).
- Templated instantiations exist per element type: `tList<T>`, `tNode<T>`,
  `tHNode<T>`, plus hashed variants `cHList`/`cHNode`. `tMemBlock<T>`
  specializes `cMemBlock` (e.g. `<char>` = cString, `<cColor>`, `<unsigned
  char>`).

## Notes / provenance
- Philos were Hungarian, and the assert strings are too: `cNode`'s copy-ctor
  aborts with `Fatal("cNode(const cNode &n): Ezt nem kene!")`, "This shouldn't
  be done!".
- Decompiler artifact: constructors that inline `Alloc`+`Lock` (both call the
  `noreturn` `Fatal`) get mis-flattened so the happy path looks like it always
  aborts. Read those as "assert-then-continue", not "always Fatal".

## Open threads

- `cHeap_Compatibility` / `cHeapBlock` — a second allocator path (compatibility
  heap). How does it relate to `cSystemMemory`?
- `cMemBlockPTR` semantics: does it auto-lock on construct / unlock on destruct
  (RAII pin)?
