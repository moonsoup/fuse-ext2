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

void wb_governor_init (void)
{
	bound_normal   = env_mib("FUSE_EXT2_WB_NORMAL_MIB",   128);
	bound_warn     = env_mib("FUSE_EXT2_WB_WARN_MIB",      16);
	bound_critical = env_mib("FUSE_EXT2_WB_CRITICAL_MIB",   1);
	bytes_since_flush = 0;
	atomic_store(&wb_bound, bound_normal);

	debugf_main("writeback governor: floor %llu MiB (warn %llu, critical %llu)",
			(unsigned long long) (bound_normal >> 20),
			(unsigned long long) (bound_warn >> 20),
			(unsigned long long) (bound_critical >> 20));

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

void wb_governor_note_write (size_t bytes)
{
	ext2_filsys e2fs;

	bytes_since_flush += bytes;
	if (bytes_since_flush < atomic_load(&wb_bound)) {
		return;
	}

	/* Reset before flushing so a flush failure cannot re-trigger on every
	 * subsequent byte; the next window will try again. */
	bytes_since_flush = 0;

	e2fs = current_ext2fs();
	if (e2fs == NULL || e2fs->io == NULL) {
		return;
	}
	/* unix_flush: write out the io-channel's cached blocks, then fsync the
	 * device fd. Blocking here paces the FUSE ack to real device speed — this
	 * IS the backpressure. */
	io_channel_flush(e2fs->io);
}
