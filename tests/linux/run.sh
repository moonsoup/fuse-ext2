#!/usr/bin/env bash
# fuse-ext2 multi-OS gate: build + smoke-test the fork on Linux in a throwaway
# Debian container. Run from anywhere:  tests/linux/run.sh
#
# Requires Docker. Mount/xattr tests also need /dev/fuse (Docker Desktop exposes
# it); if the host can't provide the device, it falls back to a build-only check
# (which is still the most important portability signal).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="debian:stable-slim"
echo "fuse-ext2 Linux gate — source: $REPO"

docker run --rm --device /dev/fuse --cap-add SYS_ADMIN \
  --security-opt apparmor=unconfined -v "$REPO":/src:ro "$IMG" \
  bash /src/tests/linux/in_container.sh
rc=$?

if [ "$rc" -eq 125 ]; then
  # 125 = docker couldn't create/start the container (usually /dev/fuse missing).
  echo ""
  echo "(container couldn't start with /dev/fuse — retrying build-only)"
  docker run --rm -v "$REPO":/src:ro "$IMG" bash /src/tests/linux/in_container.sh
  rc=$?
fi
exit "$rc"
