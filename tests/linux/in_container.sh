#!/usr/bin/env bash
# Runs INSIDE a Debian container (see run.sh) to build + smoke-test fuse-ext2 on
# Linux. This is the multi-OS gate: it proves the shared code + every
# `#ifdef __APPLE__` branch still COMPILES and BEHAVES on Linux before any driver
# change (getxattr shim, no_default_permissions, and — when it lands — the
# writeback governor) is pushed.
#
# Source is bind-mounted read-only at /src; we build in a writable copy.
set -uo pipefail
FAILS=0
ok()  { echo "  ok:   $1"; }
bad() { echo "  FAIL: $1" >&2; FAILS=$((FAILS + 1)); }

echo "=== install build deps (FUSE 2 + e2fsprogs + attr) ==="
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends \
  build-essential autoconf automake libtool pkg-config m4 \
  libfuse-dev libext2fs-dev e2fsprogs attr fuse >/dev/null 2>&1 \
  || { echo "apt install failed"; exit 2; }

echo "=== build the fork (validates Linux / #ifdef __APPLE__ portability) ==="
cp -a /src /work-src && cd /work-src
: >/tmp/build.log
./autogen.sh   >>/tmp/build.log 2>&1
./configure    >>/tmp/build.log 2>&1
if make -C fuse-ext2 fuse-ext2 >>/tmp/build.log 2>&1; then
  ok "builds clean on Linux"
else
  bad "build FAILED"; tail -30 /tmp/build.log
fi
DRIVER=/work-src/fuse-ext2/fuse-ext2
[ -x "$DRIVER" ] || { bad "no driver binary produced"; echo "TOTAL_FAILS=$FAILS"; exit 1; }

if [ ! -e /dev/fuse ]; then
  echo "  (no /dev/fuse in this container — build check done, mount tests skipped)"
  echo "TOTAL_FAILS=$FAILS"; [ "$FAILS" -eq 0 ] && exit 0 || exit 1
fi

echo "=== ext4 image: mount rw+, write/read, xattr round-trip, e2fsck ==="
IMG=/tmp/t.ext4; MNT=/tmp/mnt; mkdir -p "$MNT"
dd if=/dev/zero of="$IMG" bs=1M count=32 2>/dev/null
mke2fs -t ext4 -F -q "$IMG"
"$DRIVER" "$IMG" "$MNT" -o rw+ >/tmp/mnt.log 2>&1
for _ in $(seq 1 20); do mount | grep -qF "$MNT" && break; sleep 0.3; done
mount | grep -qF "$MNT" && ok "rw+ mount" || { bad "rw+ mount (see /tmp/mnt.log)"; cat /tmp/mnt.log; }
{ echo hello > "$MNT/f.txt"; [ "$(cat "$MNT/f.txt" 2>/dev/null)" = hello ]; } && ok "write + read back" || bad "write/read"
fusermount -u "$MNT" 2>/dev/null || umount "$MNT" 2>/dev/null

# fuse-ext2 is READ-ONLY for xattrs (.setxattr is NULL), so we can't set via the
# mount. Exercise the getxattr shim's Linux (#else / 4-arg) path correctly: set an
# xattr with the NATIVE kernel ext4 driver, then READ it back through fuse-ext2.
echo "=== getxattr read path (shim #else branch) ==="
XI=/tmp/x.ext4; XM=/tmp/xm; mkdir -p "$XM"
# -I 128 (128-byte inodes) leaves no room for inline xattrs, so the attribute goes
# to an EXTERNAL block — the path op_getxattr currently supports. (Inline/in-inode
# xattrs are not yet read: bug #12. This test validates the shim's read path via
# the external-block case; add an inline-xattr case once #12 is fixed.)
dd if=/dev/zero of="$XI" bs=1M count=16 2>/dev/null; mke2fs -t ext4 -F -q -I 128 "$XI"
# Seed a file + an xattr WITHOUT any privileged mount, using debugfs (same
# libext2fs xattr API fuse-ext2 reads), then read it back through fuse-ext2.
debugfs -w "$XI" >/tmp/dbg.log 2>&1 <<'DBG'
write /etc/hostname xf.txt
ea_set xf.txt user.t val
quit
DBG
"$DRIVER" "$XI" "$XM" -o ro >/tmp/xm.log 2>&1
for _ in $(seq 1 20); do mount | grep -qF "$XM" && break; sleep 0.3; done
xv=$(getfattr -n user.t --only-values "$XM/xf.txt" 2>/dev/null)
if [ "$xv" = val ]; then
  ok "getxattr reads debugfs-set xattr (shim Linux path exercised)"
else
  bad "getxattr read (got '$xv'; debugfs tail: $(tail -2 /tmp/dbg.log 2>/dev/null | tr '\n' ' '))"
fi
fusermount -u "$XM" 2>/dev/null || umount "$XM" 2>/dev/null
e2fsck -fn "$IMG" >/dev/null 2>&1 && ok "e2fsck clean after writes" || bad "e2fsck found errors"

echo "=== no_default_permissions read-only mount ==="
"$DRIVER" "$IMG" "$MNT" -o ro,no_default_permissions >/tmp/mnt2.log 2>&1
for _ in $(seq 1 20); do mount | grep -qF "$MNT" && break; sleep 0.3; done
[ "$(cat "$MNT/f.txt" 2>/dev/null)" = hello ] && ok "ro,no_default_permissions read" || bad "no_default_permissions"
fusermount -u "$MNT" 2>/dev/null || umount "$MNT" 2>/dev/null

echo ""
[ "$FAILS" -eq 0 ] && echo "LINUX TESTS PASSED" || echo "$FAILS LINUX TEST(S) FAILED"
echo "TOTAL_FAILS=$FAILS"
exit "$FAILS"
