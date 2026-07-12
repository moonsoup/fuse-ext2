# [DRAFT — not filed yet, pending more testing] macOS disk sleep can wedge/corrupt long read-write mounts

## Summary

macOS's `disksleep` power-management setting (non-zero by default on most
Macs) can drop an idle *mounted* external drive into an unpowered sleep
state (`D3Cold`) instead of a merely-idle-but-powered one (`D3Hot`). To the
drive and the kernel's USB/SATA stack, this is functionally identical to
the drive being physically unplugged mid-operation.

On a long-running `fuse-ext2` read-write mount, this has been observed to:

- Wedge the mount — the `fuse-ext2` daemon enters uninterruptible kernel
  I/O sleep (`ps` shows state `U`/`D`) waiting on a request the drive never
  answers. The process cannot be killed by any signal, including
  `SIGKILL`, until the kernel gives up or the device reconnects.
- Corrupt block/inode bitmaps if the sleep event fires mid-write,
  confirmed via `e2fsck -f` showing bitmap checksum mismatches in the
  affected block groups after the fact.

## Why this was hard to diagnose

The failure mode looks exactly like driver or hardware flakiness — a
wedged mount, filesystem corruption, or a "flaky enclosure" — not a power
setting. In our case it cost significant time misdiagnosing it as
enclosure/cable hardware instability before the actual mechanism was
confirmed via `log show` kernel logs, which showed a real
`IOPortTransportState` power-source removal/re-addition event on the
USB-C port at the exact moment a mount wedged.

## Evidence

- Kernel log excerpt at the moment of a wedge:
  ```
  IOPortFeaturePower::removePowerSources(): [Port-USB-C@2/Power In] Removing all power sources...
  ...
  IOPortFeaturePower::_addPowerSource(): [Port-USB-C@2/Power In] Adding power source (powerSourceName: USB-PD)...
  ```
- `ps` showing the `fuse-ext2`/driver-adjacent daemon stuck in
  uninterruptible sleep (`U` state) at the same timestamp.
- `sudo pmset -a disksleep 0` (disabling disk sleep entirely) — after
  applying this, two consecutive full read-write endurance tests (12GB
  each, sustained ~15 minutes, checksum-verified per chunk) completed with
  zero wedges and zero corruption, on the same drive/enclosure that had
  previously wedged/corrupted repeatedly.

## Fix implemented

See commit `3054634` on the `writeback-governor` branch: a mount-time
advisory warning (`warn_if_disksleep_enabled_darwin()` in
`fuse-ext2/fuse-ext2.c`), guarded by `#ifdef __APPLE__`, that checks
`pmset -g` at the start of a read-write mount and prints a loud warning
(never blocks the mount) if `disksleep` is non-zero, recommending
`sudo pmset -a disksleep 0` before any extended write session.

## Status

Not yet filed as a real issue / not yet pushed upstream — holding for
additional real-world testing before publishing. This file is a local
draft of what the issue would say once we're ready.
