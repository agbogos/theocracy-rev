# Other-OS ports — next

**Status: not started.** The port is macOS / Apple Silicon only. Windows and
Linux hosts are the next direction.

Nothing is designed yet. Two things already known to be waiting:

- **Teardown.** `CloseSubsystems` is deliberately skipped because process exit on
  macOS reclaims everything it would release. A platform where that is less true
  is the stated revisit trigger — see
  [host-architecture.md](host-architecture.md), "Why teardown skips
  `CloseSubsystems`".
- **The host boundary is the whole of the work.** The guest side is
  platform-independent by construction: both binaries run as original i386 code
  under Unicorn, and only `port/` touches the OS. The BSD-vs-Linux socket
  translations already in `traps.cpp` are a preview of the shape — each failed
  *silently* rather than loudly.
