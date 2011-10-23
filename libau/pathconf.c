/*
 * Copyright (C) 2009-2011 Junjiro R. Okajima
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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>    /* or <sys/statfs.h> */
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/aufs_type.h>

#include "libau.h"

static long (*real_pathconf)(const char *path, int name);
LibAuDlFunc(pathconf)
static long (*real_fpathconf)(int fd, int name);
LibAuDlFunc(fpathconf)

static long do_fpathconf(int fd, int name)
{
	long err;
	int wbr_fd, e;

	err = -1;
	wbr_fd = ioctl(fd, AUFS_CTL_WBR_FD, NULL);
	if (wbr_fd >= 0) {
		if (!libau_dl_fpathconf())
			err = real_fpathconf(wbr_fd, name);
		e = errno;
		close(wbr_fd);
		errno = e;
	}

	return err;
}

static int open_aufs_fd(const char *path, DIR **rdp)
{
	int err, l, e;
	struct stat base, st;
	char *parent, *p;

	*rdp = NULL;
	err = open(path, O_RDONLY);
	if (err >= 0)
		goto out; /* success */

	switch (errno) {
	case EISDIR:
		*rdp = opendir(path);
		if (*rdp) {
			err = dirfd(*rdp);
			goto out; /* success */
		}
		break;
	case EACCES:
		/*FALLTHROUGH*/
	case EPERM:
		/* let's try with the parent dir again */
		break;
	default:
		/* no way */
		goto out;
	}

	/*
	 * when open(2) for the specified path failed,
	 * then try opening its ancestor instead in order to get a file
	 * descriptor in aufs.
	 */
	err = stat(path, &base);
	if (err)
		goto out;
	parent = malloc(strlen(path) + sizeof("/.."));
	if (!parent)
		goto out;
	l = strlen(path);
	while (path[l - 1] == '/')
		l--;
	memcpy(parent, path, l);
	parent[l - 1] = 0;
	while (1) {
		strcat(parent, "/..");
		err = stat(parent, &st);
		if (err)
			break;
		err = -1;
		errno = ENOTSUP;
		if (st.st_dev != base.st_dev) {
			error_at_line(0, errno, __FILE__, __LINE__,
				      "cannot handle %s\n", path);
			break;
		}
		*rdp = opendir(parent);
		if (*rdp) {
			err = dirfd(*rdp);
			break; /* success */
		}
		p = realloc(parent, strlen(parent) + sizeof("/.."));
		if (p)
			parent = p;
		else
			break;
	}
	e = errno;
	free(parent);
	errno = e;

 out:
	return err;
}

static long libau_pathconf(const char *path, int name)
{
	long err;
	struct statfs stfs;
	int fd, e;
	DIR *dp;

	err = statfs(path, &stfs);
	if (err)
		goto out;

	err = -1;
	if (stfs.f_type == AUFS_SUPER_MAGIC) {
		fd = open_aufs_fd(path, &dp);
		if (fd >= 0) {
			err = do_fpathconf(fd, name);
			e = errno;
			if (!dp)
				close(fd); /* ignore */
			else
				closedir(dp); /* ignore */
			errno = e;
		}
	} else if (!libau_dl_pathconf())
		err = real_pathconf(path, name);

 out:
	return err;
}

long pathconf(const char *path, int name)
{
	long ret;

	ret = -1;
	if (name == _PC_LINK_MAX
	    && (LibAuTestFunc(pathconf) || LibAuTestFunc(fpathconf)))
		ret = libau_pathconf(path, name);
	else if (!libau_dl_pathconf())
		ret = real_pathconf(path, name);

	return ret;
}

/* ---------------------------------------------------------------------- */

static long libau_fpathconf(int fd, int name)
{
	long err;
	struct statfs stfs;

	err = fstatfs(fd, &stfs);
	if (err)
		goto out;

	err = -1;
	if (stfs.f_type == AUFS_SUPER_MAGIC)
		err = do_fpathconf(fd, name);
	else if (!libau_dl_fpathconf())
		err = real_fpathconf(fd, name);

 out:
	return err;
}

long fpathconf(int fd, int name)
{
	long ret;

	ret = -1;
	if (name == _PC_LINK_MAX
	    && (LibAuTestFunc(pathconf) || LibAuTestFunc(fpathconf)))
		ret = libau_fpathconf(fd, name);
	else if (!libau_dl_fpathconf())
		ret = real_fpathconf(fd, name);

	return ret;
}
