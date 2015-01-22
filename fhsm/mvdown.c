/*
 * Copyright (C) 2011-2015 Junjiro R. Okajima
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
 * aufs FHSM, retrieve a filename from the list and move it down one by one
 */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include "../au_util.h"
#include "daemon.h"
#include "log.h"

/* file global varibles */
static int listfd, failfd;
static off_t listsz;
static char *listp;
static struct aufs_mvdown mvdown;
static struct au_fname fname;

/*
 * the return value zero means we should continue the operation, otherwise stop
 * it.
 */
static_unless_ut
int do_mvdown1(int *done)
{
	int err, fd, e;
	struct stat st;

	*done = 1;
	AuDbgFhsmLog("%s", fname.name);
	fd = openat(fhsmd.fd[AuFd_ROOT], fname.name, O_RDONLY);
	if (fd < 0) {
		err = -1;
		if (errno == ENOENT)
			err = 0;
		AuLogErr("%s", fname.name);
		goto out;
	}
	err = ioctl(fd, AUFS_CTL_MVDOWN, &mvdown);
	if (!err) {
		err = close(fd);
		if (err)
			AuLogErr("%s", fname.name);
		goto out;
	}

	e = errno;
	AuDbgFhsmLog("AUFS_CTL_MVDOWN %s, %m", fname.name);
	errno = e;
	switch (e) {
	case EROFS:
		/*
		 * the same named file exists on the lower (but upper than next
		 * writable) branch. simply skip it and continue the operation
		 * if another error doesn't happen.
		 */
		if (au_opt_test(fhsmd.optflags, VERBOSE))
			AuLogInfo("ignore %s, %m", fname.name);
		err = close(fd);
		if (err)
			AuLogErr("%s", fname.name);
		break;
	case EBUSY:
		/*
		 * the file is in-use (or other reason), and we could not move
		 * it down. if it is still single hard-linked, we will try again
		 * later.
		 * continue the operation if another error doesn't happen.
		 */
		*done = 0;
		if (au_opt_test(fhsmd.optflags, VERBOSE))
			AuLogInfo("%s, %m", fname.name);
		err = fstat(fd, &st);
		if (err)
			AuLogErr("%s", fname.name);
		if (st.st_nlink == 1)
			err = au_fname_failed(&fname, failfd);
		if (!err) {
			err = close(fd);
			if (err)
				AuLogErr("%s", fname.name);
		} else {
			e = errno;
			if (close(fd))
				AuLogErr("%s", fname.name);
			errno = e;
		}
		break;
	case EINVAL:
		/*
		 * continue the operation if another error doesn't happen.
		 */
		*done = 0;
		if (au_opt_test(fhsmd.optflags, VERBOSE)) {
			char *s = "??";
			if (0 <= mvdown.au_errno && mvdown.au_errno < EAU_Last)
				s = (char *)au_errlist[mvdown.au_errno];
			AuLogInfo("%s, %s", fname.name, s);
		}
		err = close(fd);
		if (err)
			AuLogErr("%s", fname.name);
		break;
	case ENOSPC:
		/*
		 * the target branch is full. stop moving-down.
		 */
		/*FALLTHROUGH*/
	default:
		/* unknown reason */
		*done = 0;
		AuLogErr("%s", fname.name);
		e = errno;
		if (close(fd))
			AuLogErr("%s", fname.name);
		errno = e;
	}

out:
	AuDbgFhsmLog("err %d", err);
	return err;
}

static_unless_ut
int test_usage(struct aufs_stbr *stbr)
{
	float block, inode;
	struct aufhsm_wmark *wm;

	AuDbgFhsmLog("%llu/%llu, %llu/%llu, %d",
		 (unsigned long long)stbr->stfs.f_bavail,
		 (unsigned long long)stbr->stfs.f_blocks,
		 (unsigned long long)stbr->stfs.f_ffree,
		 (unsigned long long)stbr->stfs.f_files,
		 stbr->brid);
	block = (float)stbr->stfs.f_bavail / stbr->stfs.f_blocks;
	inode = (float)stbr->stfs.f_ffree / stbr->stfs.f_files;
	AuDbgFhsmLog("%d, free, block %f, inode %f",
		 stbr->brid, block, inode);

	wm = au_wm_search_brid(stbr->brid, fhsmd.lcopy);
	if (wm)
		/* free ratio */
		return block < wm->block[AuFhsm_WM_UPPER]
			|| (wm->inode[AuFhsm_WM_UPPER] < 1
			    && inode < wm->inode[AuFhsm_WM_UPPER]);
	return 0;
}

static int stbr_compar(const void *_brid, const void *_stbr)
{
	int brid = (long)_brid;
	const struct aufs_stbr *stbr = _stbr;

	return brid - stbr->brid;
}

static struct aufs_stbr *au_stbr_bsearch(int brid, struct aufs_stbr *stbr,
					 int nstbr)
{
	long l = brid;

	return bsearch((void *)l, stbr, nstbr, sizeof(*stbr), stbr_compar);
}

/*
 * return tri-state.
 * plus: the file was skipped, the caller should proceed the list.
 * zero: the file was moved-down and the list is completed.
 * minus: error.
 */
static_unless_ut
int do_mvdown(struct aufs_stbr *cur, struct aufs_stbr **next)
{
	int err, done;

	au_fname_one(listp, listsz, &fname);
	mvdown.flags = AUFS_MVDOWN_FHSM_LOWER | AUFS_MVDOWN_OWLOWER
		| AUFS_MVDOWN_STFS;
	mvdown.stbr[AUFS_MVDOWN_UPPER].brid = cur->brid;
	err = do_mvdown1(&done);
	if (err)
		goto out;

	err = ftruncate(listfd, listsz - fname.len);
	if (err) {
		AuLogErr("list-file after %s", fname.name);
		goto out;
	}

	*next = NULL;
	listsz -= fname.len;
	if (listsz && !done) {
		err = 1;
		goto out;
	}
	if (mvdown.flags & AUFS_MVDOWN_STFS_FAILED) {
		// warning here
		goto out;
	}

	cur->stfs = mvdown.stbr[AUFS_MVDOWN_UPPER].stfs;
	assert(cur->brid == mvdown.stbr[AUFS_MVDOWN_UPPER].brid);
	if (!(mvdown.flags & AUFS_MVDOWN_BOTTOM)) {
		*next = au_stbr_bsearch(mvdown.stbr[AUFS_MVDOWN_LOWER].brid,
					fhsmd.comm->stbr, fhsmd.comm->nstbr);
		if (*next)
			**next = mvdown.stbr[AUFS_MVDOWN_LOWER];
	}

out:
	AuDbgFhsmLog("err %d", err);
	return err;
}

/*
 * In move-down, We have to free the several resources with keeping the error
 * status.  By implementing as a child process, we can do it as simple exit().
 */
static_unless_ut
int mvdown_child(struct aufs_stbr *cur, struct aufs_stbr **next)
{
	int err, brfd;
	ssize_t ssz;
	struct stat st;
	struct aufs_wbr_fd wbrfd;
	struct signalfd_siginfo ssi;

	wbrfd.oflags = O_CLOEXEC;
	wbrfd.brid = cur->brid;
	brfd = ioctl(fhsmd.fd[AuFd_ROOT], AUFS_CTL_WBR_FD, &wbrfd);
	if (brfd < 0) {
		err = brfd;
		AuLogErr("AUFS_CTL_WBR_FD");
		goto out;
	}

	err = au_list(brfd, &listfd, &failfd);
	if (err)
		goto out;

	err = fstat(listfd, &st);
	if (err < 0) {
		AuLogErr("listfd");
		goto out;
	}
	AuDbgFhsmLog("st_size %llu", (unsigned long long)st.st_size);
	if (!st.st_size)
		goto out; /* nothing to move-down */

	listp = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		     listfd, 0);
	if (listp == MAP_FAILED)
		AuLogFin("mmap");

	*next = NULL;
	listsz = st.st_size;
	while (fhsmd.comm->msg != AuFhsm_MSG_EXIT) {
		ssz = read(fhsmd.fd[AuFd_SIGNAL], &ssi, sizeof(ssi));
		if (ssz != -1)
			break;

		err = do_mvdown(cur, next);
		AuDbgFhsmLog("err %d, listsz %llu", err, (unsigned long long)listsz);
		if (err <= 0
		    //|| !listsz
		    || *next
		    || !test_usage(cur))
			break;
	}

out:
	AuDbgFhsmLog("err %d", err);
	return err;
}

int au_mvdown_run(struct aufs_stbr *cur, struct aufs_stbr **next)
{
	int err;
	pid_t pid;
	struct in_ope *in_ope;

	err = 0;
	*next = NULL;
	AuLogInfo("brid %d", cur->brid);
	if (!test_usage(cur))
		goto out;

	if (!au_opt_test(fhsmd.optflags, NODAEMON)) {
		list_for_each_entry(in_ope, &fhsmd.in_ope, list) {
			if (in_ope->brid == cur->brid)
				goto out;
		}

		in_ope = malloc(sizeof(*in_ope));
		if (!in_ope) {
			err = -1;
			AuLogErr("malloc");
		}
		in_ope->brid = cur->brid;
		in_ope->pid = 0;
		list_add(&in_ope->list, &fhsmd.in_ope);

		pid = fork();
		if (!pid) {
			err = mvdown_child(cur, next);
			AuDbgFhsmLog("err %d", err);
			exit(err);
		} else if (pid > 0) {
			in_ope->pid = pid;
		} else if (pid < 0) {
			err = pid;
			AuLogErr("fork");
		}
	} else
		err = mvdown_child(cur, next);
out:
	return err;
}
