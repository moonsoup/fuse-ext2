# fuse-ext2 fork — living testing checklist

Statuses: `[working]` / `[broken]` / `[fixed]` / `[untested]`, with dated evidence.
This is the running record required by the testing cycle; it grows as testing
uncovers new items. GitHub issues are the durable record for every finding.

## macOS functional (issue #6)
- [working] ext2/ext3/ext4 read-only: mount, checksum, symlink, write-rejection, byte-identical — 2026-07-05, `39_test_all_fs.sh` all pass
- [working] ext4 read-write (`rw+`, image): write/edit/append/mkdir/subdir/rename/truncate/2MiB/delete → `e2fsck -fn` clean → persistence across remount — 2026-07-06 re-run, `51_test_readwrite_nopw.sh` all checks passed
- [working] Real-drive rw+ mount + write (4TB dock disk5s3 `/home`): rw mount, marker write/delete, user `rm` of a real folder — 2026-07-06
- [working] Real-USB end-to-end: mke2fs ext4 on 62GB stick → rw+ mount → writes → `e2fsck` clean → remount-ro readback — 2026-07-06 (2MiB checksum check inconclusive that run — expired sudo ticket, not a driver failure; re-verify with #6 consolidation)
- [untested] macOS functional suite re-run after governor lands (must stay green)

## Linux gate (issues #1, #7) — `tests/linux/run.sh` (Docker)
- [working] Builds clean on Linux (validates `#ifdef __APPLE__` portability) — 2026-07-06
- [working] rw+ mount ext4 image, write + read back — 2026-07-06
- [working] `e2fsck` clean after writes — 2026-07-06
- [working] `ro,no_default_permissions` read — 2026-07-06
- [broken→tracked] xattr write through mount: `.setxattr` is NULL upstream → `ENOSYS` — 2026-07-06, issue #11 (limitation, not regression)
- [broken→tracked] getxattr of in-inode (inline) xattrs returns empty — 2026-07-06, **real upstream bug**, issue #12
- [untested] getxattr of external-block xattr via debugfs seed (`mke2fs -I 128`) — gate re-run in progress

## DiskArbitration bundle (issues #5, #8)
- [working] Unit tests: plist lint, probe RECOGNIZED/UNRECOGNIZED/INVAL exit codes, volume-name emit, installer dry-run — 2026-07-06, `test_90_install_fs_bundle.sh`
- [untested] Live: insert ext drive post-reboot → auto-mount, no popup (blocked on reboot; open question whether macOS 26 honors third-party bundles)

## Writeback governor (issues #3, #4, #9, #10)
- [broken] Baseline: sustained ~26 GB write to real ext mount wedges the mount under memory pressure (8 GB host) — 2026-07-06, twice reproduced, kernel log = jetsam/memorystatus churn, no USB events
- [fixed] `op_fsync` durability fix (#3): now calls `io_channel_flush` (cache writeout + device fsync) after `ext2fs_flush` — 2026-07-06, builds clean, macOS RW suite ALL PASS
- [working] Governor implemented (`wb_governor.c/h`, wired op_init/op_write): macOS RW suite ALL PASS at default 128 MiB floor (no behavior change) — 2026-07-06
- [working] Governor flush path exercised: `FUSE_EXT2_WB_NORMAL_MIB=1` forces flush every 1 MiB during the 2 MiB write test; all checks pass, data byte-identical, `e2fsck` clean — 2026-07-06
- [untested] Governor on Linux: gate re-run against the completed tree in progress (earlier green run raced the edits)
- [untested] ACCEPTANCE: `tests/stress/stress_test.sh` on the actual drive (pre-governor baseline = the two wedges above; post-governor must complete, no wedge) — blocked on dock remount after reboot
- [untested] Pressure-transition behavior (WARN/CRITICAL tighten): needs a real memory-pressure episode or `sudo memory_pressure -S -l warn` simulation
