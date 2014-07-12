/*
 * Copyright (C) 2011-2014 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * aufs FHSM, functions for POSIX Shared Memory management and Semaphore.
 * I'll bet that you would be confused by the terms 'fhsm' and 'shm'.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "comm.h"
#include "log.h"

/* genarate an unique name for a mounted aufs */
int au_shm_name(int rootfd, char name[], int sz)
{
	int err;
	struct stat st;

	err = fstat(rootfd, &st);
	if (err) {
		AuLogErr("fstat");
		goto out;
	}
	/*
	 * statfs.f_fsid may not be maintained.
	 * use device id instead.
	 */
	err = snprintf(name, sz, "/%s-%04x%04x-%llu",
		       program_invocation_short_name,
		       major(st.st_dev), minor(st.st_dev),
		       (unsigned long long)st.st_ino);
	if (0 < err && err < sz) {
		err = 0;
		goto out;
	}

	err = -1;
	errno = E2BIG;
	AuLogErr("internal error, snprintf %d, %d", err, sz);

out:
	return err;
}

/* dir part of POSIX shm */
char *au_shm_dir(int fd)
{
	char *dir, *p, fdpath[32];
	int err;
	struct stat st;
	ssize_t ssz;

	dir = NULL;
	err = snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd);
	if (err >= sizeof(fdpath)) {
		AuLogErr("internal error, %d >= %d", err, (int)sizeof(fdpath));
		goto out;
	}

	err = fstatat(AT_FDCWD, fdpath, &st, AT_SYMLINK_NOFOLLOW);
	if (err) {
		AuLogErr("fstatat");
		goto out;
	}

	dir = malloc(st.st_size + 1);
	if (!dir) {
		AuLogErr("malloc");
		goto out;
	}

	ssz = readlink(fdpath, dir, st.st_size);
	if (ssz < 0 || ssz > st.st_size) {
		AuLogErr("readlink");
		goto out;
	}

	p = memrchr(dir, '/', ssz);
	if (!p) {
		AuLogErr("memrchr");
		goto out;
	}

	if (p != dir)
		*p = '\0';

out:
	return dir;
}
