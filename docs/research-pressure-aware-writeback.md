# Research: pressure-aware writeback for fuse-ext2 on constrained hosts

Status: **research / design** (no implementation yet). Foundation for a possible
fork feature: a fuse-ext2 that stays a good citizen under host memory pressure.

Tracked in: **#2** (buffered write-back → broken backpressure, root cause),
**#3** (op_fsync doesn't fsync the device), **#4** (adaptive pressure-aware
writeback governor). No code is pushed until these are worked per the issues.

## Problem

Sustained large writes through fuse-ext2 to a **slow** backing device on a
**low-RAM** host trigger host memory pressure, which tears down the FUSE
connection and wedges the mount.

Observed (2026-07-06): 8 GB Mac, already swapping (~2.3 GB swap used), system disk
100% full, mirroring ~26 GB to an ext4 partition over USB. Copy died ~3–5 GB in
with `rsync: error: renameat: Socket is not connected` / `Device not configured`.
Kernel log at the drop shows **memory-pressure churn** (jetsam / watchdogd /
memorystatus) and a JetsamEvent — and **zero USB/disk/link-reset events**. So it
is memory pressure, not the (previously suspected) dock hardware.

## Root cause

Dirty pages from the buffered device writes + macFUSE's in-flight kernel request
queue accumulate faster than they flush to the slow device. The kernel's
writeback throttling — strong on Linux (`balance_dirty_pages`), weaker on
macOS/macFUSE — fails to backpressure the writer in time, so pressure builds until
jetsam fires and the FUSE connection is severed.

## Current fuse-ext2 write path

- Opens the fs via `unix_io_manager` (libext2fs); device I/O is **buffered** by the
  OS page cache.
- Uses high-level `fuse_main()` with **default** options — no `max_background`,
  `direct_io`, writeback-cache, or congestion tuning.
- `op_write` → `do_write` calls `ext2fs_file_flush` after **every** write, so
  libext2fs is NOT the hoarder. The accumulation is upstream/outside it.

Where memory piles up:
```
app write -> [macFUSE kernel request queue] -> fuse-ext2 -> libext2fs
          -> device write() -> [OS page cache for /dev/diskNsM] -> slow device
```
Two accumulation points, both outside libext2fs: (a) macFUSE's in-flight FUSE
request queue, (b) the OS page cache for the buffered device writes.

## THE DISCONNECT (found by auditing whether the backpressure controls are hooked up)

The natural FUSE write-backpressure is: *don't ack a write until it is durable*.
fuse-ext2 breaks that link in two places, so writes ack at **memory speed, not
device speed**, and the kernel never learns it should throttle the writer:

1. **`op_init` opens the io-channel buffered write-back.** The call is
   `ext2fs_open(device, EXT2_FLAG_RW, 0, 0, unix_io_manager, &e2fs)` — the io/channel
   flags are `0`. NEITHER `CHANNEL_FLAGS_WRITETHROUGH` (0x01) NOR `IO_FLAG_DIRECT_IO`
   (0x0004) is set (both defined in `ext2fs/ext2_io.h`). So the daemon's `write()`
   to `/dev/diskN` lands in the **OS page cache and returns immediately**.
2. **`op_write` acks after only `ext2fs_file_flush`** — which pushes the file's
   blocks into the (buffered) io-channel, not to durable media — then returns
   `wsize`. The FUSE reply says "done" while the bytes are still dirty in RAM.
3. **`op_fsync` calls `ext2fs_flush` only** (flush libext2fs's cache), not an fsync
   of the backing fd — so even the explicit durability barrier is soft.

Net: the kernel sees writes complete at RAM speed → applies no throttling → rsync
streams in at full speed → dirty pages for the device accumulate faster than the
slow disk drains → memory pressure → jetsam → the mount is severed. **The
backpressure control is hooked to the fast buffered layer, not the slow device.**

Reconnect levers (all standard, portable to Linux):
- Open write-through / direct: set `CHANNEL_FLAGS_WRITETHROUGH` on the channel
  (or open with `IO_FLAG_DIRECT_IO` / `EXT2_FLAG_DIRECT_IO`) so device writes are
  durable/unbuffered and the ack reflects real device latency.
- Make `op_fsync` actually fsync the backing fd (or `io_channel_flush` a
  write-through channel).
- Plus `-o direct_io` at the macFUSE layer for accumulation point (a).
(Verify the exact ext2fs_open2 / channel-flag call and measure the throughput cost;
write-through on every op may be too slow — hence the adaptive governor below.)

## Mechanisms available

### Static backpressure (always-on, standard, robust)
- **`-o direct_io`** (macFUSE): bypasses the unified buffer cache; writes go
  straight through — no page-cache dirty-page pileup. Cost: throughput, no mmap.
- **`-o nolocalcaches`**: disables UBC + name/attr caching + readahead (more
  aggressive; every op hits the daemon).
- **Conservative `max_background`** (set at FUSE init): bounds in-flight async
  requests so the writer blocks when the pipeline is full. Standard FUSE practice.
- **Periodic `io_channel`/backing-fd flush**: cap device-side dirty pages by
  flushing on a byte/time cadence.

### Dynamic, pressure-aware (the ideal)
- **macOS**: `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE` (10.9+) — a dispatch source that
  fires on NORMAL / WARN / CRITICAL **transitions**.
  - LIMITATION (Apple docs): event-on-transition only; you **cannot poll current
    pressure**, and a process that starts while pressure is already elevated never
    gets the event. Best-effort; pressure can spike faster than notify+respond.
- **Linux (for portability)**: PSI (`/proc/pressure/memory`, pollable), cgroup
  `memory.high`/`memory.pressure`, or simply rely on the kernel's native
  dirty-page throttling (which may make a governor a no-op there).

## Design options

1. **Static `direct_io` / bounded** — solve by construction, always on. Simple,
   robust, portable. Cost: throughput even when RAM is plentiful.
2. **Pure pressure-aware** — full speed normally, throttle only under pressure.
   Ideal throughput but fragile: the poll blind-spot, reactivity lag, and the
   started-under-pressure case can all still let it blow up.
3. **Adaptive with a safe floor (RECOMMENDED)** — default to a BOUNDED write path
   (a periodic-flush cadence + modest `max_background` = a floor it can never
   exceed), and use the pressure signal to **relax** the bound when memory is
   plentiful and **tighten** toward synchronous/flush-now under WARN/CRITICAL.
   Never crashes (floor holds even with no signal), near-native throughput with
   headroom, graceful under pressure.

## Recommended direction (option 3)

Architecture sketch:
- A small `writeback_governor`: holds the current bound (max dirty bytes in flight
  / flush cadence) behind one accessor.
- macOS: subscribe to a `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE` source
  (`#ifdef __APPLE__`); WARN → tighten, CRITICAL → synchronous + flush-now,
  NORMAL → relax.
- Linux: poll PSI `/proc/pressure/memory`, or no-op and defer to the kernel.
- `do_write` consults the governor: after N dirty bytes since the last flush,
  force an `io_channel_flush` (+ fsync of the backing fd) — blocking the writer =
  natural backpressure.
- Safe floor: the byte-cadence flush caps dirty pages even if no pressure signal
  ever arrives.

## Governor design (v0)

Feasibility confirmed: `io_channel_flush(e2fs->io)` → libext2fs `unix_flush` →
`flush_cached_blocks()` + `fsync(dev)`. A real, reachable durability barrier. No
raw-fd access, no io-manager shim, no `-o` change, no `O_DIRECT`/write-through open.

### State
```c
struct wb_governor {
    _Atomic uint64_t bytes_since_flush;  // bytes written since last forced flush
    _Atomic uint64_t bound;              // current flush threshold (bytes)
    uint64_t bound_normal, bound_warn, bound_critical;  // policy, env-tunable
};
```

### Policy (the SAFE FLOOR is the point)
- **NORMAL**  = `bound_normal` (e.g. 128 MiB) — the floor. Flush rarely → today's
  fast, fully-cached, mmap-intact behaviour. But it is a HARD CAP: even with no
  pressure signal ever (Linux, or a macOS process that started already-pressured
  and thus never gets the event), dirty pages can never exceed ~128 MiB → it
  cannot wedge on any sane host. The pressure signal only ever makes it *tighter*.
- **WARN**  = `bound_warn` (e.g. 16 MiB) — flush often.
- **CRITICAL** = `bound_critical` (e.g. 1 MiB) — near-synchronous.

### Pressure signal (op_init)
```c
#ifdef __APPLE__
  src = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
        DISPATCH_MEMORYPRESSURE_NORMAL|_WARN|_CRITICAL, q);
  dispatch_source_set_event_handler(src, ^{
    unsigned long f = dispatch_source_get_data(src);
    gov.bound = (f & DISPATCH_MEMORYPRESSURE_CRITICAL) ? gov.bound_critical
              : (f & DISPATCH_MEMORYPRESSURE_WARN)     ? gov.bound_warn
              :                                          gov.bound_normal;
  });
  dispatch_resume(src);
#else
  /* Linux: leave bound = bound_normal (floor); the kernel's balance_dirty_pages
     already throttles well. Optional later: poll /proc/pressure/memory (PSI). */
#endif
```

### Write-path gate (`do_write`, right after the existing `ext2fs_file_flush`)
```c
uint64_t n = atomic_fetch_add(&gov.bytes_since_flush, wsize) + wsize;
if (n >= atomic_load(&gov.bound)) {
    atomic_store(&gov.bytes_since_flush, 0);   // reset first: concurrent writers don't all flush
    io_channel_flush(e2fs->io);                // cache + fsync(dev); BLOCKS -> backpressure
}
```
The blocking flush is the whole mechanism: the write op now takes real device
time, so the FUSE ack is paced to the disk, the kernel throttles the writer, and
dirty pages stay bounded by `bound`.

### Threading — SETTLED
fuse-ext2 runs **single-threaded**: it forces `-s` into the FUSE args
(`fuse-ext2.c:398`, `fuse_opt_add_arg(&fargs, "-s")`) and has **no locking anywhere**
(no mutex/pthread/atomic in the source) — because the single event-loop thread
already serializes every op and all libext2fs access. Consequences for the governor:
- `bytes_since_flush` needs **no atomics / no lock** (only the FUSE thread touches it);
  `io_channel_flush` in the gate is serialized for free.
- The **only** cross-thread field is `bound` (written by the macOS memory-pressure
  dispatch handler on its own queue, read by the FUSE thread) → keep it `_Atomic`;
  the rest is plain `uint64_t`.
- Caveat: a blocking flush briefly pauses ALL ops (reads too) on the single thread —
  acceptable for the write-heavy target, and consistent with fuse-ext2 already
  choosing `-s`.

No mutex or io-manager shim is required. Concurrency gate cleared.

### Tunability (to "play with the gov order" and test the theory)
Read `bound_normal/warn/critical` from env (e.g. `FUSE_EXT2_WB_NORMAL_MIB`, ...) so
the thresholds can be swept without recompiling. Test: set a small bound, run a
large sustained write, watch `vm_stat`/pressure — confirm dirty pages plateau at
~`bound` and no wedge; sweep to find the throughput/safety knee.

## Open questions / to verify next
- Does `-o direct_io` alone eliminate the crash in practice? (cheap to test — it's
  the realist near-term mitigation below.)
- Can fuse-ext2 reach the backing fd to fsync, or must it flush via the io_manager?
  (unix_io holds the fd; a thin io_manager wrapper could fsync on a cadence.)
- macFUSE's effective default `max_background`, and whether lowering it helps.
- Instrumentation: log `vm_stat`/pressure while writing to quantify each lever.

## Realist near-term mitigation (falls straight out of this research)
Remount the write target with **`-o direct_io`** (bypass the UBC) and free host RAM
(quit Chrome; optionally `purge`), then retry. No code change; attacks the exact
dirty-page mechanism. If the 26 GB completes cleanly, it both unblocks the data
move AND validates `direct_io` as the floor for option 3.

## Sources
- macFUSE Mount Options — https://github.com/macfuse/macfuse/wiki/Mount-Options
- FUSE I/O modes (kernel.org) — https://www.kernel.org/doc/html/latest/filesystems/fuse-io.html
- DISPATCH_SOURCE_TYPE_MEMORYPRESSURE — https://developer.apple.com/documentation/dispatch/dispatch_source_type_memorypressure
