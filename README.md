<div align="center">
  <img src=".github/banner.svg" alt="fuse-ext2 — read ext2/ext3/ext4 drives on macOS" width="760">
</div>

<p align="center">
  <a href="https://github.com/moonsoup/fuse-ext2/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/moonsoup/fuse-ext2?sort=semver&color=success"></a>
  <a href="COPYING"><img alt="License: GPL-2.0" src="https://img.shields.io/badge/license-GPL--2.0-blue.svg"></a>
  <img alt="Platform: macOS (Apple Silicon | Intel)" src="https://img.shields.io/badge/platform-macOS%20%28Apple%20Silicon%20%7C%20Intel%29-lightgrey">
  <a href="https://github.com/moonsoup/homebrew-fuse-ext2"><img alt="Install: Homebrew tap" src="https://img.shields.io/badge/install-Homebrew%20tap-orange"></a>
  <a href="https://github.com/sponsors/moonsoup"><img alt="Sponsor" src="https://img.shields.io/badge/sponsor-%E2%9D%A4-ea4aaa"></a>
</p>

Mount **ext2 / ext3 / ext4** filesystems on **macOS** (Apple Silicon and Intel), read-only, using [macFUSE](https://macfuse.github.io/).

This is a maintained fork of [alperakcan/fuse-ext2](https://github.com/alperakcan/fuse-ext2) with the fixes needed to build and mount on current macOS, where the original no longer compiles and the Homebrew formula has been removed.

Read-only is the intended, supported mode — ideal for pulling data off a Linux drive from a Mac.

### Companion tools

- **[fuse-watchdog](https://github.com/moonsoup/fuse-watchdog)** — an optional, external recovery watchdog that auto-remounts a fuse-ext2 mount if the backing USB device drops off the bus (`ENXIO` / "Device not configured"). It re-verifies the ext4 UUID before remounting, so it can never attach the wrong disk. Built specifically as a workaround for a flaky USB dock; **not needed with reliable hardware** — the real fix for a dropping dock is a better enclosure (UASP/ASMedia or Thunderbolt).

---

## Requirements

macFUSE provides FUSE on macOS:

```bash
brew install --cask macfuse
```

macFUSE is a **kernel extension**, so installing it is not enough — you have to enable it:

1. Open **System Settings → Privacy & Security**, find the blocked *"system software from developer Benjamin Fleischer"* notice, and click **Allow**.
2. **Restart your Mac.** The extension only loads after a reboot.

On **Apple Silicon**, loading any third-party kernel extension the first time also requires lowering the security policy:

1. Shut down, then hold the **power button** until *"Loading startup options"* appears → **Options** → **Continue** (this is Recovery mode).
2. Menu bar **Utilities → Startup Security Utility**, select your disk → **Security Policy…**
3. Choose **Reduced Security** and tick **"Allow user management of kernel extensions from identified developers."**
4. Restart, then do the **Allow + reboot** steps above.

See [macFUSE's documentation](https://macfuse.github.io/) if the extension still won't load.

---

## Install

### Homebrew (recommended)

```bash
brew install moonsoup/fuse-ext2/fuse-ext2
```

### From source

```bash
# build dependencies
brew install autoconf automake libtool pkg-config e2fsprogs

git clone https://github.com/moonsoup/fuse-ext2
cd fuse-ext2
./autogen.sh

E2P="$(brew --prefix e2fsprogs)"
export LIBTOOLIZE=glibtoolize
export CPPFLAGS="-I/usr/local/include -I/usr/local/include/fuse -I${E2P}/include"
export LDFLAGS="-L/usr/local/lib -L${E2P}/lib -F/Library/Filesystems/macfuse.fs/Contents/Frameworks"
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:${E2P}/lib/pkgconfig"
./configure

make -C fuse-ext2 fuse-ext2      # the driver binary
sudo cp fuse-ext2/fuse-ext2 /usr/local/bin/
```

---

## Usage

### The easy way — `fuse-ext2-mount`

Installing via Homebrew also gives you a `fuse-ext2-mount` helper that handles
`sudo`, the right options, and the prerequisite checks for you:

```bash
fuse-ext2-mount                           # list ext partitions on attached drives
fuse-ext2-mount /dev/disk4s3              # mount read-only at /tmp/ext-disk4s3
fuse-ext2-mount /dev/disk4s3 --recovered  # drive from another machine (foreign UIDs)
sudo umount /tmp/ext-disk4s3              # unmount when done
```

Add `--dry-run` to any command to see exactly what it will run.

### Manually

Find the ext partition (`diskutil list` — look for `Linux Filesystem`; macOS
can't read ext, so it won't auto-mount, which is expected). Reading a raw device
requires root, so use `sudo`:

```bash
mkdir -p /tmp/ext
sudo fuse-ext2 /dev/disk4s3 /tmp/ext -o ro,allow_other
# ... browse /tmp/ext ...
sudo umount /tmp/ext
```

`allow_other` lets your normal account see the files even though root mounted it.
For a drive **recovered from another machine**, add `no_default_permissions` so
files owned by a UID that doesn't exist here stay readable:

```bash
sudo fuse-ext2 /dev/disk4s3 /tmp/ext -o ro,allow_other,no_default_permissions
```

## Troubleshooting

- **"Operation not permitted" / can't open the device** — grant your terminal
  **Full Disk Access** (System Settings → Privacy & Security → Full Disk Access),
  then try the `sudo fuse-ext2 …` command again.
- **"the file system is not available" / nothing mounts** — macFUSE isn't loaded.
  Re-check the enablement steps under [Requirements](#requirements) (approve the
  extension, reboot; on Apple Silicon, the Recovery-mode security policy).

---

## What this fork changes

- **Builds on current macOS** — adapts the `getxattr` operation to the Darwin signature (offered upstream as [#154](https://github.com/alperakcan/fuse-ext2/pull/154)).
- **`no_default_permissions` option** — read files owned by a foreign UID (recovered drives) without mounting as root (offered upstream as [#155](https://github.com/alperakcan/fuse-ext2/pull/155)).
- Read-only mount verified on ext2, ext3, and ext4.

---

## Support

Keeping ext2/3/4 access working on macOS takes ongoing effort as each release
changes the ground under it. If this saved you — or your data — please consider
**[sponsoring the work](https://github.com/sponsors/moonsoup)**. 💛

## License

GPL-2.0, same as upstream. See [COPYING](COPYING).

Original work by Alper Akcan and contributors. This fork exists to keep it usable on modern macOS.
