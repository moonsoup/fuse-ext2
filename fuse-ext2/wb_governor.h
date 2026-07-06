/**
 * Adaptive writeback governor (issues #3/#4).
 *
 * Bounds the dirty bytes a sustained write can accumulate by forcing a real
 * durability flush (io_channel_flush -> unix_flush: flush cached blocks +
 * fsync(device fd)) every `bound` bytes written. The blocking flush paces the
 * FUSE write ack to true device speed, which is the backpressure that keeps a
 * slow device + low-RAM host from building memory pressure until the kernel
 * (jetsam on macOS) tears the mount down.
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
 * Threading: fuse-ext2 forces single-threaded FUSE (-s), so the byte counter
 * and the flush need no locking; only `bound` is written from the dispatch
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
 * crosses the current bound, force a durability flush of the filesystem's io
 * channel (blocks until the device catches up). Call from the write path with
 * the fuse context available (uses current_ext2fs()). */
void wb_governor_note_write (size_t bytes);

#endif /* WB_GOVERNOR_H_ */
