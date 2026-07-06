#!/usr/bin/env bash
# Large-block + large-batch stress test for fuse-ext2.
#
# This is the writeback-governor ACCEPTANCE test, and it must run against the
# ACTUAL slow drive (a real ext mount over USB/SATA) — throwaway images on fast
# internal storage do NOT reproduce the sustained-write memory-pressure behavior.
#
# It writes a large volume in big blocks AND a large batch of small files, watches
# for the mount getting torn down under load ("wedge"), then verifies integrity.
#   Pre-governor: expected to WEDGE (documents the bug).
#   Post-governor: acceptance bar is "no wedge, all data intact".
#
# Usage: stress_test.sh <mountpoint> [--large-gb N] [--small-count M] [--file-mb K]
set -uo pipefail

MNT="${1:?usage: stress_test.sh <mountpoint> [--large-gb N] [--small-count M] [--file-mb K]}"; shift || true
LARGE_GB=4; SMALL_COUNT=5000; FILE_MB=256
while [ $# -gt 0 ]; do case "$1" in
  --large-gb)    LARGE_GB="$2";    shift 2;;
  --small-count) SMALL_COUNT="$2"; shift 2;;
  --file-mb)     FILE_MB="$2";     shift 2;;
  *) echo "unknown arg: $1" >&2; exit 2;;
esac; done

mem() { echo -n "free=$(vm_stat 2>/dev/null | awk '/Pages free/{gsub(/\./,"",$3);print $3}')pg "; sysctl -n vm.swapusage 2>/dev/null | sed 's/  */ /g'; }
is_mount() { mount | grep -qF "$MNT" && [ -w "$MNT" ]; }

is_mount || { echo "FAIL: $MNT is not a writable mount"; exit 1; }
DIR="$MNT/.stress.$$"
mkdir -p "$DIR/large" "$DIR/small" || { echo "FAIL: can't create test dirs (already wedged?)"; exit 1; }
trap 'rm -rf "$DIR" 2>/dev/null || true' EXIT

FAILS=0; WEDGED=0
n_large=$(( LARGE_GB * 1024 / FILE_MB ))
echo "=== stress -> $MNT : ${LARGE_GB}GB in ${FILE_MB}MB blocks (${n_large} files) + ${SMALL_COUNT} small files ==="
echo "  start mem: $(mem)"

echo "--- large-block phase ---"
declare -A SUM
for i in $(seq 1 "$n_large"); do
  f="$DIR/large/big_$i.bin"
  if ! dd if=/dev/urandom of="$f" bs=1m count="$FILE_MB" 2>/dev/null; then
    echo "  !! write failed at large file $i — mount wedged under load"; WEDGED=1; break
  fi
  SUM["$i"]=$(shasum -a 256 "$f" 2>/dev/null | awk '{print $1}')
  [ -z "${SUM[$i]}" ] && { echo "  !! readback/checksum failed at $i — wedged"; WEDGED=1; break; }
  [ $(( i % 4 )) -eq 0 ] && echo "  $i/$n_large large; $(mem)"
done

if [ "$WEDGED" -eq 0 ]; then
  echo "--- large-batch phase ---"
  for i in $(seq 1 "$SMALL_COUNT"); do
    if ! printf 'small %d\n' "$i" > "$DIR/small/f_$i.txt" 2>/dev/null; then
      echo "  !! write failed at small file $i — wedged"; WEDGED=1; break
    fi
    [ $(( i % 1000 )) -eq 0 ] && echo "  $i/$SMALL_COUNT small; $(mem)"
  done
fi

sync 2>/dev/null

echo "--- verify ---"
if [ "$WEDGED" -ne 0 ]; then echo "  WEDGED under load"; FAILS=1; fi
got=$(ls -1 "$DIR/small" 2>/dev/null | wc -l | tr -d ' ')
echo "  small files: $got / $SMALL_COUNT"; [ "$got" = "$SMALL_COUNT" ] || FAILS=1
# re-read a sample of large files and compare checksums
bad=0
for i in 1 $(( n_large / 2 )) "$n_large"; do
  [ -n "${SUM[$i]:-}" ] || continue
  now=$(shasum -a 256 "$DIR/large/big_$i.bin" 2>/dev/null | awk '{print $1}')
  [ "$now" = "${SUM[$i]}" ] || { echo "  checksum MISMATCH on big_$i.bin"; bad=1; }
done
[ "$bad" -eq 0 ] && echo "  large-file checksums: OK (sampled)" || FAILS=1

echo ""
[ "$FAILS" -eq 0 ] && echo "STRESS PASSED — no wedge, data intact" || echo "STRESS FAILED — see above (pre-governor this is the expected baseline)"
exit "$FAILS"
