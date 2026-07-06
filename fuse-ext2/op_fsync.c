/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "fuse-ext2.h"

int op_fsync (const char *path, int datasync, struct fuse_file_info *fi)
{
	errcode_t rc;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s (%p)", path, fi);

	rc = ext2fs_flush(e2fs);
	if (rc) {
		return -EIO;
	}

	/* ext2fs_flush only pushes libext2fs's state into the io-channel, whose
	 * writes are buffered by the OS page cache — it is NOT a durability
	 * barrier. io_channel_flush -> unix_flush writes out the channel's cached
	 * blocks and then fsync()s the backing device fd, which is what fsync(2)
	 * semantics actually require (issue #3). */
	rc = io_channel_flush(e2fs->io);
	if (rc) {
		return -EIO;
	}

	debugf("leave");
	return 0;
}
