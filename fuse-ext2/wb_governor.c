/**
 * Adaptive writeback governor — see wb_governor.h for the design and
 * docs/research-pressure-aware-writeback.md for the analysis (issues #3/#4).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "fuse-ext2.h"
#include "wb_governor.h"

#include <stdlib.h>
#include <stdatomic.h>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

/* Only `wb_bound` crosses threads (dispatch handler writes, FUSE thread
 * reads); everything else stays on the single FUSE thread (-s is forced). */
static uint64_t bytes_since_flush;
static _Atomic uint64_t wb_bound;
static uint64_t bound_normal;
static uint64_t bound_warn;
static uint64_t bound_critical;

static uint64_t ops_since_flush;
static uint64_t bound_alloc_ops;
static int wb_disabled;

#ifdef __APPLE__
static dispatch_source_t pressure_source;
#endif

static uint64_t env_mib (const char *name, uint64_t def_mib)
{
	const char *v;
	char *end;
	unsigned long long mib;

	v = getenv(name);
	if (v == NULL || *v == '\0') {
		return def_mib << 20;
	}
	mib = strtoull(v, &end, 10);
	if (*end != '\0' || mib == 0) {
		debugf_main("ignoring invalid %s='%s' (want MiB > 0)", name, v);
		return def_mib << 20;
	}
	return (uint64_t) mib << 20;
}

static uint64_t env_count (const char *name, uint64_t def_count)
{
	const char *v;
	char *end;
	unsigned long long n;

	v = getenv(name);
	if (v == NULL || *v == '\0') {
		return def_count;
	}
	n = strtoull(v, &end, 10);
	if (*end != '\0' || n == 0) {
		debugf_main("ignoring invalid %s='%s' (want a count > 0)", name, v);
		return def_count;
	}
	return (uint64_t) n;
}

void wb_governor_init (void)
{
	const char *dv;

	bound_normal   = env_mib("FUSE_EXT2_WB_NORMAL_MIB",   128);
	bound_warn     = env_mib("FUSE_EXT2_WB_WARN_MIB",      16);
	bound_critical = env_mib("FUSE_EXT2_WB_CRITICAL_MIB",   1);
	bound_alloc_ops = env_count("FUSE_EXT2_WB_ALLOC_OPS", 256);
	bytes_since_flush = 0;
	ops_since_flush = 0;
	atomic_store(&wb_bound, bound_normal);

	/* Diagnostic escape hatch — NOT the default, and not a substitute for the
	 * durability the governor provides. Exists to isolate whether a governor
	 * flush's blocking fsync(2) on a slow/USB device is itself long enough to
	 * make macFUSE's KERNEL side give up on the channel (the daemon recovers
	 * and goes back to idle, but the kernel has already stopped forwarding
	 * requests to it — "ghost mount" symptom, ENODEV on every syscall). If
	 * disabling this makes the wedge go away, that confirms the flush call
	 * itself as a contributing trigger and points at a differently-paced or
	 * async flush strategy, not "no flush." */
	dv = getenv("FUSE_EXT2_WB_DISABLE");
	wb_disabled = (dv != NULL && dv[0] != '\0' && dv[0] != '0');

	debugf_main("writeback governor: floor %llu MiB (warn %llu, critical %llu), alloc-ops bound %llu%s",
			(unsigned long long) (bound_normal >> 20),
			(unsigned long long) (bound_warn >> 20),
			(unsigned long long) (bound_critical >> 20),
			(unsigned long long) bound_alloc_ops,
			wb_disabled ? " [DISABLED via FUSE_EXT2_WB_DISABLE]" : "");

#ifdef __APPLE__
	/* Tighten the bound on WARN/CRITICAL, relax on NORMAL. Transitions only
	 * (the API cannot be polled, and a process started under pressure gets no
	 * event) — which is why the NORMAL bound above is a hard floor and safety
	 * never depends on this signal arriving. */
	pressure_source = dispatch_source_create(
			DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
			DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN |
			DISPATCH_MEMORYPRESSURE_CRITICAL,
			dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
	if (pressure_source != NULL) {
		dispatch_source_set_event_handler(pressure_source, ^{
			unsigned long flags = dispatch_source_get_data(pressure_source);
			uint64_t b = bound_normal;
			if (flags & DISPATCH_MEMORYPRESSURE_CRITICAL) {
				b = bound_critical;
			} else if (flags & DISPATCH_MEMORYPRESSURE_WARN) {
				b = bound_warn;
			}
			atomic_store(&wb_bound, b);
			debugf_main("memory pressure 0x%lx -> writeback bound %llu MiB",
					flags, (unsigned long long) (b >> 20));
		});
		dispatch_resume(pressure_source);
	} else {
		debugf_main("memory-pressure source unavailable; floor bound only");
	}
#endif
}

/* ext2fs_flush(): writes dirty bitmaps/superblock/group descriptors via
 * ext2fs_write_bitmaps(), THEN flushes the io channel (unix_flush: cached
 * blocks + fsync(device fd)). A real superset of the old io_channel_flush()-
 * only call — see the note at the top of wb_governor.h for why the bitmap
 * half matters. */
static void do_flush (ext2_filsys e2fs)
{
	if (e2fs == NULL) {
		return;
	}
	ext2fs_flush(e2fs);
}

void wb_governor_note_write (size_t bytes)
{
	if (wb_disabled) {
		return;
	}

	bytes_since_flush += bytes;
	if (bytes_since_flush < atomic_load(&wb_bound)) {
		return;
	}

	/* Reset before flushing so a flush failure cannot re-trigger on every
	 * subsequent byte; the next window will try again. */
	bytes_since_flush = 0;

	do_flush(current_ext2fs());
}

void wb_governor_note_alloc (void)
{
	if (wb_disabled) {
		return;
	}

	ops_since_flush++;
	if (ops_since_flush < bound_alloc_ops) {
		return;
	}

	ops_since_flush = 0;

	do_flush(current_ext2fs());
}
