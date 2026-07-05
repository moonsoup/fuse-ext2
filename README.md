# fuse-ext2

Mount **ext2 / ext3 / ext4** filesystems on **macOS** (Apple Silicon and Intel), read-only, using [macFUSE](https://macfuse.github.io/).

This is a maintained fork of [alperakcan/fuse-ext2](https://github.com/alperakcan/fuse-ext2) with the fixes needed to build and mount on current macOS, where the original no longer compiles and the Homebrew formula has been removed.

Read-only is the intended, supported mode — ideal for pulling data off a Linux drive from a Mac.

---

## Requirements

macFUSE (a system extension that provides FUSE on macOS):

```bash
brew install --cask macfuse
```

After installing, approve it in **System Settings → Privacy & Security** (you may need to restart).

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

Find the device you want to read:

```bash
diskutil list
```

Mount it **read-only** at a directory of your choice:

```bash
mkdir -p /tmp/ext
fuse-ext2 /dev/disk4s3 /tmp/ext -o ro,allow_other
```

Browse it in Finder or the terminal at `/tmp/ext`, then unmount when done:

```bash
umount /tmp/ext
```

### Reading data recovered from another machine

Files owned by a user ID that doesn't exist on your Mac are normally blocked, so you can't read your own recovered data without becoming root. Add `no_default_permissions` and the driver serves them to you directly:

```bash
fuse-ext2 /dev/disk4s3 /tmp/ext -o ro,allow_other,no_default_permissions
```

---

## What this fork changes

- **Builds on current macOS** — adapts the `getxattr` operation to the Darwin signature (offered upstream as [#154](https://github.com/alperakcan/fuse-ext2/pull/154)).
- **`no_default_permissions` option** — read files owned by a foreign UID (recovered drives) without mounting as root (offered upstream as [#155](https://github.com/alperakcan/fuse-ext2/pull/155)).
- Read-only mount verified on ext2, ext3, and ext4.

---

## License

GPL-2.0, same as upstream. See [COPYING](COPYING).

Original work by Alper Akcan and contributors. This fork exists to keep it usable on modern macOS.
