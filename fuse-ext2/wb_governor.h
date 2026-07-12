/**
 * Adaptive writeback governor (issues #3/#4).
 *
 * Bounds the dirty bytes a sustained write can accumulate by forcing a real
 * durability flush (ext2fs_flush: write out dirty bitmaps/superblock/group
 * descriptors via ext2fs_write_bitmaps(), then io_channel_flush -> unix_flush:
 * flush cached blocks + fsync(device fd)) every `bound` bytes written. The
 * blocking flush paces the FUSE write ack to true device speed, which is the
 * backpressure that keeps a slow device + low-RAM host from building memory
 * pressure until the kernel (jetsam on macOS) tears the mount down.
 *
 * NOTE ON WHAT "FLUSH" MEANS HERE: this used to call io_channel_flush()
 * directly, which only flushes blocks libext2fs has already queued to the io
 * channel. It does NOT write dirty in-memory bitmaps to disk — those are only
 * persisted by ext2fs_write_bitmaps(), which runs inside ext2fs_flush(). Since
 * ext2fs_new_inode()/ext2fs_inode_alloc_stats2() mark bitmaps allocated only
 * in memory (while ext2fs_link()/ext2fs_write_new_inode() push the directory
 * entry and inode table entry through the io channel right away), an abnormal
 * daemon death (crash, kill, a wedged FUSE channel) between allocations and a
 * real flush leaves directory entries on disk pointing at inodes the on-disk
 * bitmap still shows as unallocated — e2fsck: "references inode N ... where
 * _INODE_UNINIT is set" / "found in group N's unused inodes area". Calling
 * ext2fs_flush() (a superset that also does the io-channel flush) instead of
 * bare io_channel_flush() closes that gap wherever the governor fires.
 *
 * Policy: the NORMAL bound is a SAFE FLOOR — a hard cap that holds even if no
 * memory-pressure signal ever arrives. On macOS a DISPATCH_SOURCE_TYPE_
 * MEMORYPRESSURE source tightens the bound on WARN/CRITICAL and relaxes it on
 * NORMAL. On other OSes the floor alone applies (Linux's own dirty-page
 * throttling already backpressures buffered writers).
 *
 * Bounds are env-tunable (MiB), read once at init:
 *   FUSE_EXT2_WB_NORMAL_MIB   (default 128)
 *   FUSE_EXT2_WB_WARN_MIB     (default 16)
 *   FUSE_EXT2_WB_CRITICAL_MIB (default 1)
 *
 * A SEPARATE counter covers metadata-mutating ops (create/mkdir/symlink/
 * mknod/unlink/rename/link) that allocate or free inodes/blocks but transfer
 * little or no file data — a create-heavy small-file workload (e.g. an npm
 * node_modules tree) can go the entire byte-based bound without ever writing
 * enough data to trigger a flush, leaving thousands of bitmap updates
 * unpersisted. FUSE_EXT2_WB_ALLOC_OPS (default 256) bounds that independently.
 *
 * Threading: fuse-ext2 forces single-threaded FUSE (-s), so both counters and
 * the flush need no locking; only `bound` is written from the dispatch
 * handler's queue and is therefore atomic.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef WB_GOVERNOR_H_
#define WB_GOVERNOR_H_

#include <stddef.h>

/* Read env bounds, set the floor, and (macOS) subscribe to memory-pressure
 * transitions. Call once, from op_init. */
void wb_governor_init (void);

/* Account `bytes` just written through the mount; when the accumulated total
 * crosses the current bound, force a durability flush (see note above).
 * Call from the write path with the fuse context available (uses
 * current_ext2fs()). */
void wb_governor_note_write (size_t bytes);

/* Account one metadata-mutating op (create/mkdir/symlink/mknod/unlink/rename/
 * link) just completed; when the accumulated count crosses
 * FUSE_EXT2_WB_ALLOC_OPS, force the same durability flush as
 * wb_governor_note_write. Call from each such op after it succeeds. */
void wb_governor_note_alloc (void);

#endif /* WB_GOVERNOR_H_ */
