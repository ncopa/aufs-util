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

#define _GNU_SOURCE

#include <sys/vfs.h>    /* or <sys/statfs.h> */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "rdu.h"

static int rdu_pos(struct Rdu_DIRENT *de, struct rdu *p, long pos)
{
	int err;
	struct au_rdu_ent *ent;

	err = -1;
	if (pos < p->npos) {
		ent = p->pos[pos];
		de->d_ino = ent->ino;
		de->d_off = pos;
		de->d_reclen = au_rdu_len(ent->nlen);
		de->d_type = ent->type;
		strcpy(de->d_name, ent->name);
		err = 0;
	}
	return err;
}

static int rdu_readdir(DIR *dir, struct Rdu_DIRENT *de, struct Rdu_DIRENT **rde)
{
	int err, fd;
	struct rdu *p;
	long pos;
	struct statfs stfs;

	if (rde)
		*rde = NULL;

	errno = EBADF;
	fd = dirfd(dir);
	err = fd;
	if (fd < 0)
		goto out;

	err = fstatfs(fd, &stfs);
	if (err)
		goto out;

	errno = 0;
	if (stfs.f_type == AUFS_SUPER_MAGIC) {
		err = rdu_lib_init();
		if (err)
			goto out;

		p = rdu_buf_lock(fd);
		if (!p)
			goto out;

		pos = telldir(dir);
		if (!pos || !p->npos) {
			err = rdu_init(p, /*want_de*/!de);
			if (err) {
				int e = errno;
				rdu_free(p);
				errno = e;
				goto out;
			}
		}

		if (!de) {
			de = p->de;
			if (!de) {
				rdu_unlock(p);
				errno = EINVAL;
				err = -1;
				goto out;
			}
		}
		err = rdu_pos(de, p, pos);
		if (!err)
			*rde = de;
		else
			err = 0;
		seekdir(dir, pos + 1);
		rdu_unlock(p);
		errno = 0;
	} else if (!de) {
		if (!Rdu_DL_READDIR()) {
			err = 0;
			*rde = Rdu_REAL_READDIR(dir);
			if (!*rde)
				err = -1;
		}
	} else {
		if (!Rdu_DL_READDIR_R())
			err = Rdu_REAL_READDIR_R(dir, de, rde);
	}
 out:
	/* follow the behaviour of glibc */
	if (err && errno == ENOENT)
		errno = 0;
	return err;
}

struct Rdu_DIRENT *(*Rdu_REAL_READDIR)(DIR *dir);
struct Rdu_DIRENT *Rdu_READDIR(DIR *dir)
{
	struct Rdu_DIRENT *de;
	int err;

	if (LibAuTestFunc(Rdu_READDIR)) {
		err = rdu_readdir(dir, NULL, &de);
		/* DPri("err %d\n", err); */
	} else if (!Rdu_DL_READDIR())
		de = Rdu_REAL_READDIR(dir);
	else
		de = NULL;
	return de;
}

#ifdef _REENTRANT
int (*Rdu_REAL_READDIR_R)(DIR *dir, struct Rdu_DIRENT *de, struct Rdu_DIRENT **rde);
int Rdu_READDIR_R(DIR *dir, struct Rdu_DIRENT *de, struct Rdu_DIRENT **rde)
{
	if (LibAuTestFunc(Rdu_READDIR_R))
		return rdu_readdir(dir, de, rde);
	else if (!Rdu_DL_READDIR_R())
		return Rdu_REAL_READDIR_R(dir, de, rde);
	else
		return errno;
}
#endif
