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
 * aufs FHSM, core, mainly for the contoller
 */

#include <sys/mman.h>
#include <stddef.h>
#include <unistd.h>
#include <linux/aufs_type.h>

#include "../au_util.h"
#include "comm.h"
#include "log.h"

static void wm_init(struct aufhsm_wmark *wm, union aufs_brinfo *brinfo)
{
	wm->brid = brinfo->id;

	/* 75% - 50% by default */
	wm->block[AuFhsm_WM_UPPER] = 0.25;
	wm->block[AuFhsm_WM_LOWER] = 0.5;
	wm->inode[AuFhsm_WM_UPPER] = 1;
	wm->inode[AuFhsm_WM_LOWER] = 1;
}

/* ---------------------------------------------------------------------- */

/*
 * follow the branch management
 */
static_unless_ut
int fhsm_shrink(int shmfd, struct aufhsm **rfhsm, int nfhsm)
{
	int err;
	off_t len, cur_len;
	struct aufhsm *fhsm;

	fhsm = *rfhsm;
	cur_len = au_fhsm_size(fhsm->nwmark);
	len = au_fhsm_size(nfhsm);
	AuDbgFhsmLog("%lld --> %lld", (long long)cur_len, (long long)len);

	err = ftruncate(shmfd, len);
	if (err) {
		AuLogErr("ftruncate %lld, %lld", (long long)cur_len,
			 (long long)len);

		goto out;
	}
	fhsm = mremap(fhsm, len, cur_len, MREMAP_MAYMOVE);
	if (fhsm != MAP_FAILED) {
		*rfhsm = fhsm;
		fhsm->nwmark = nfhsm;
		goto out; /* success */
	}

	/* revert */
	err = -1;
	AuLogErr("mremap %lld, %lld", (long long)cur_len, (long long)len);
	if (ftruncate(shmfd, cur_len))
		AuLogErr("ftruncate %lld", (long long)cur_len);

out:
	return err;
}

static_unless_ut
int fhsm_expand(int shmfd, struct aufhsm **rfhsm, int nfhsm)
{
	int err, i;
	off_t len, cur_len;
	struct aufhsm *fhsm;
	struct aufhsm_wmark *wm;

	fhsm = *rfhsm;
	cur_len = au_fhsm_size(fhsm->nwmark);
	len = au_fhsm_size(nfhsm);
	AuDbgFhsmLog("%lld --> %lld", (long long)cur_len, (long long)len);
	err = ftruncate(shmfd, len);
	if (err) {
		AuLogErr("ftruncate %lld, %lld",
			 (long long)cur_len, (long long)len);
		goto out;
	}
	fhsm = mremap(fhsm, len, cur_len, MREMAP_MAYMOVE);
	if (fhsm != MAP_FAILED) {
		*rfhsm = fhsm;
		wm = fhsm->wmark + fhsm->nwmark;
		for (i = fhsm->nwmark; i < nfhsm; i++, wm++)
			wm->brid = -1;
		fhsm->nwmark = nfhsm;
		goto out; /* success */
	}

	/* revert */
	err = -1;
	AuLogErr("mremap %lld, %lld", (long long)cur_len, (long long)len);
	if (ftruncate(shmfd, cur_len))
		AuLogErr("ftruncate %lld", (long long)cur_len);

out:
	return err;
}

/*
 * nfhsm (the number of FHSM paticipant branches) is just a hint since aufs
 * branches may be changed after we check them.
 */
static_unless_ut
int fhsm_create(char *name, int nfhsm, int nbr, union aufs_brinfo *brinfo,
		int *rfd, struct aufhsm **rfhsm)
{
	int err, i;
	off_t len;
	struct aufhsm_wmark *wm;
	struct aufhsm *p;

	len = au_fhsm_size(nfhsm);
	err = au_shm_create(name, len, rfd, rfhsm);
	if (err)
		goto out;

	p = *rfhsm;
	p->nwmark = nfhsm;
	wm = p->wmark;
	for (i = 0; i < nfhsm; i++, wm++)
		wm->brid = -1;

	wm = p->wmark;
	for (i = 0; i < nbr; i++)
		if (au_br_fhsm(brinfo[i].perm))
			wm_init(wm++, brinfo + i);

out:
	return err;
}

/* make sure there is no invalid entries */
static_unless_ut
int fhsm_invalid(struct aufhsm *fhsm, union aufs_brinfo *brinfo, int nbr)
{
	int err, i, j, nwmark, found;
	struct aufhsm_wmark *wm;

	err = 0;
	nwmark = fhsm->nwmark;
	wm = fhsm->wmark;
	for (i = 0; i < nwmark; i++, wm++) {
		if (wm->brid < 0) {
			err++;
			continue;
		}
		found = 0;
		for (j = 0; !found && j < nbr; j++)
			found = (wm->brid == brinfo[i].id);
		if (!found) {
			wm->brid = -1;
			err++;
		}
	}

	return err;
}

/* re-initialize the un-initialized entries */
static_unless_ut
void fhsm_reinit(struct aufhsm *fhsm, union aufs_brinfo *brinfo, int nbr)
{
	int i, nwmark;
	struct aufhsm_wmark *wm, *prev;

	prev = NULL;
	nwmark = fhsm->nwmark;
	for (i = 0; i < nbr; i++) {
		if (!au_br_fhsm(brinfo[i].perm))
			continue;

		if (brinfo[i].id < 0) {
			/* should not happen */
			errno = EINVAL;
			AuFin("%s", brinfo[i].path);
		}

		/* no bsearch, since it is unsorted */
		wm = au_wm_lfind(brinfo[i].id, fhsm->wmark, nwmark);
		if (wm) {
			prev = wm;
			continue;
		}

		wm = au_wm_lfind(-1, fhsm->wmark, nwmark);
		if (wm) {
			wm_init(wm, brinfo + i);
			if (prev) {
				memcpy(wm->block, prev->block,
				       sizeof(wm->block));
				memcpy(wm->inode, prev->inode,
				       sizeof(wm->inode));
			}
		} else {
			/* should not happen */
			errno = EINVAL;
			AuFin("%s", brinfo[i].path);
		}
	}
}

/*
 * create struct aufhsm by mapping POSIX shared memory
 */
static int fhsm_map(char *name, int *rshmfd, struct aufhsm **rfhsm)
{
	int err;
	struct aufhsm *p;

	err = au_shm_map(name, rshmfd, rfhsm);
	if (err)
		goto out;
	p = *rfhsm;
	if (au_fhsm_sign_verify(p))
		goto out;

	err = -1;
	errno = EINVAL;
	AuLogErr("%s has broken signature", name);
	if (munmap(p, au_fhsm_size(p->nwmark)))
		AuLogErr("%s", name);
	if (close(*rshmfd))
		AuLogErr("%s", name);

out:
	return err;
}

/*
 * create struct aufhsm on shared memory from brinfo
 */
int au_fhsm(char *name, int nfhsm, int nbr, union aufs_brinfo *brinfo,
	    int *rshmfd, struct aufhsm **rfhsm)
{
	int err, invalid;
	struct aufhsm *p;

	err = fhsm_create(name, nfhsm, nbr, brinfo, rshmfd, rfhsm);
	p = *rfhsm;
	if (!err)
		goto out_sort;
	if (errno != EEXIST)
		goto out;

	err = fhsm_map(name, rshmfd, rfhsm);
	if (err)
		goto out;

	p = *rfhsm;
	if (p->nwmark < nfhsm)
		err = fhsm_expand(*rshmfd, rfhsm, nfhsm);
	else //if (p->nwmark > nfhsm)
		err = fhsm_shrink(*rshmfd, rfhsm, nfhsm);
	if (err)
		goto out_unmap;
	p = *rfhsm;

	invalid = fhsm_invalid(p, brinfo, nfhsm);
	if (invalid)
		fhsm_reinit(p, brinfo, nfhsm);

out_sort:
	au_fhsm_sort_brid(p);
	if (!err) {
		au_fhsm_sign(p);
		goto out; /* success */
	}
out_unmap:
	if (munmap(p, au_fhsm_size(p->nwmark)))
		AuLogErr("unmap");
out:
	return err;
}

unsigned int au_fhsm_csum(struct aufhsm *fhsm)
{
	unsigned int csum;
	off_t len;
	char *p;

	csum = 0;
	len = au_fhsm_size(fhsm->nwmark);
	len -= offsetof(struct aufhsm, nwmark);
	p = (void *)fhsm;
	p += offsetof(struct aufhsm, nwmark);
	for (; len; len--, p++)
		csum += *p;

	return csum;
}

/* ---------------------------------------------------------------------- */

/*
 * load struct aufhsm from the given shared memory
 */
struct aufhsm *au_fhsm_load(char *name)
{
	struct aufhsm *fhsm, *p;
	off_t len;
	int err, fd;

	p = NULL;
	err = fhsm_map(name, &fd, &fhsm);
	if (err)
		goto out;

	len = au_fhsm_size(fhsm->nwmark);
	p = malloc(len);
	if (!p) {
		AuLogErr("malloc %d", fhsm->nwmark);
		goto out_fd;
	}
	memcpy(p, fhsm, len);
	err = munmap(fhsm, len);
	if (err) {
		p = NULL;
		AuLogErr("unmap");
	}

out_fd:
	err = close(fd);
	if (err) {
		p = NULL;
		AuLogErr("close");
	}
out:
	return p;
}

/* ---------------------------------------------------------------------- */

void au_fhsm_dump(char *mntpnt, struct aufhsm *fhsm, union aufs_brinfo *brinfo,
		  int nbr)
{
	int i;
	struct aufhsm_wmark *wm;

	printf("%s, %d watermark(s)\n", mntpnt, fhsm->nwmark);
	for (i = 0; i < nbr; i++) {
		wm = au_wm_search_brid(brinfo[i].id, fhsm);
		if (wm)
			printf("%s, %d, %.2f-%.2f %.2f-%.2f\n",
			       brinfo[i].path, brinfo[i].id,
			       (1 - wm->block[AuFhsm_WM_UPPER]) * 100,
			       (1 - wm->block[AuFhsm_WM_LOWER]) * 100,
			       (1 - wm->inode[AuFhsm_WM_UPPER]) * 100,
			       (1 - wm->inode[AuFhsm_WM_LOWER]) * 100);
	}
}
