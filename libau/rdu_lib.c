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

#include <sys/ioctl.h>
#include <sys/vfs.h>    /* or <sys/statfs.h> */
#include <assert.h>
#include <errno.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdu.h"

static struct rdu **rdu;
#define RDU_STEP 8
static int rdu_cur, rdu_lim = RDU_STEP;

/* ---------------------------------------------------------------------- */

static int rdu_getent(struct rdu *p, struct aufs_rdu *param)
{
	int err;

	DPri("param{%llu, %p, %u | %u | %p, %llu, %u, %d |"
	     " %llu, %d, 0x%x, %u}\n",
	     param->sz, param->ent.e,
	     param->verify[AufsCtlRduV_SZ],
	     param->blk,
	     param->tail.e, param->rent, param->shwh, param->full,
	     param->cookie.h_pos, param->cookie.bindex, param->cookie.flags,
	     param->cookie.generation);

	err = ioctl(p->fd, AUFS_CTL_RDU, param);

	DPri("param{%llu, %p, %u | %u | %p, %llu, %u, %d |"
	     " %llu, %d, 0x%x, %u}\n",
	     param->sz, param->ent.e,
	     param->verify[AufsCtlRduV_SZ],
	     param->blk,
	     param->tail.e, param->rent, param->shwh, param->full,
	     param->cookie.h_pos, param->cookie.bindex, param->cookie.flags,
	     param->cookie.generation);

	return err;
}

/* ---------------------------------------------------------------------- */

#ifdef _REENTRANT
pthread_mutex_t rdu_lib_mtx = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * initialize this library, particularly global variables.
 */
int rdu_lib_init(void)
{
	int err;

	err = 0;
	if (rdu)
		goto out;

	rdu_lib_lock();
	if (!rdu) {
		rdu = calloc(rdu_lim, sizeof(*rdu));
		err = !rdu;
	}
	rdu_lib_unlock();

 out:
	return err;
}

static int rdu_append(struct rdu *p)
{
	int err, i;
	void *t;

	rdu_lib_must_lock();

	err = 0;
	if (rdu_cur <= rdu_lim - 1)
		rdu[rdu_cur++] = p;
	else {
		t = realloc(rdu, (rdu_lim + RDU_STEP) * sizeof(*rdu));
		if (t) {
			rdu = t;
			rdu_lim += RDU_STEP;
			rdu[rdu_cur++] = p;
			for (i = 0; i < RDU_STEP - 1; i++)
				rdu[rdu_cur + i] = NULL;
		} else
			err = -1;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

static struct rdu *rdu_new(int fd)
{
	struct rdu *p;
	int err;

	p = malloc(sizeof(*p));
	if (p) {
		rdu_rwlock_init(p);
		p->fd = fd;
		p->de = NULL;
		p->pos = NULL;
		p->sz = BUFSIZ;
		p->ent.e = NULL;
		err = rdu_append(p);
		if (!err)
			goto out; /* success */
	}
	free(p);
	p = NULL;

 out:
	return p;
}

struct rdu *rdu_buf_lock(int fd)
{
	struct rdu *p;
	int i;

	assert(rdu);
	assert(fd >= 0);

	p = NULL;
	rdu_lib_lock();
	for (i = 0; i < rdu_cur; i++)
		if (rdu[i] && rdu[i]->fd == fd) {
			p = rdu[i];
			goto out;
		}

	for (i = 0; i < rdu_cur; i++)
		if (rdu[i] && rdu[i]->fd == -1) {
			p = rdu[i];
			p->fd = fd;
			goto out;
		}
	if (!p)
		p = rdu_new(fd);

 out:
	rdu_lib_unlock();
	if (p) {
		rdu_write_lock(p);
		if (p->fd < 0) {
			rdu_unlock(p);
			p = NULL;
		}
	}

	return p;
}

void rdu_free(struct rdu *p)
{
	assert(p);

	p->fd = -1;
	free(p->pos);
	free(p->ent.e);
	free(p->de);
	p->de = NULL;
	p->pos = NULL;
	p->ent.e = NULL;
	rdu_unlock(p);
}

/* ---------------------------------------------------------------------- */
/* the heart of this library */

static int do_store; /* a dirty interface of tsearch(3) */
static void rdu_store(struct rdu *p, struct au_rdu_ent *ent)
{
	/* DPri("%s\n", ent->name); */
	p->pos[p->idx++] = ent;
}

static int rdu_ent_compar(const void *_a, const void *_b)
{
	int ret;
	const struct au_rdu_ent *a = _a, *b = _b;

	ret = strcmp(a->name, b->name);
	do_store = !!ret;

	/* DPri("%s, %s, %d\n", a->name, b->name, ret); */
	return ret;
}

static int rdu_ent_compar_wh1(const void *_a, const void *_b)
{
	int ret;
	const struct au_rdu_ent *a = _a, *b = _b;

	if (a->nlen <= AUFS_WH_PFX_LEN
	    || memcmp(a->name, AUFS_WH_PFX, AUFS_WH_PFX_LEN))
		ret = strcmp(a->name, b->name + AUFS_WH_PFX_LEN);
	else
		ret = strcmp(a->name + AUFS_WH_PFX_LEN, b->name);

	/* DPri("%s, %s, %d\n", a->name, b->name, ret); */
	return ret;
}

static int rdu_ent_compar_wh2(const void *_a, const void *_b)
{
	int ret;
	const struct au_rdu_ent *a = _a, *b = _b;

	ret = strcmp(a->name + AUFS_WH_PFX_LEN,
		     b->name + AUFS_WH_PFX_LEN);
	do_store = !!ret;

	/* DPri("%s, %s, %d\n", a->name, b->name, ret); */
	return ret;
}

static int rdu_ent_append(struct rdu *p, struct au_rdu_ent *ent)
{
	int err;

	err = 0;
	if (tfind(ent, (void *)&p->wh, rdu_ent_compar_wh1))
		goto out;

	if (tsearch(ent, (void *)&p->real, rdu_ent_compar)) {
		if (do_store)
			rdu_store(p, ent);
	} else
		err = -1;

 out:
	return err;
}

static int rdu_ent_append_wh(struct rdu *p, struct au_rdu_ent *ent)
{
	int err;

	err = 0;
	ent->wh = 1;
	if (tsearch(ent, (void *)&p->wh, rdu_ent_compar_wh2)) {
		if (p->shwh && do_store)
			rdu_store(p, ent);
	} else
		err = -1;

	return err;
}

static void rdu_tfree(void *node)
{
	/* empty */
}

static int rdu_merge(struct rdu *p)
{
	int err;
	unsigned long long ul;
	union au_rdu_ent_ul u;
	void *t;

	err = -1;
#if 0
	u = p->ent;
	for (ul = 0; ul < p->npos; ul++) {
		DPri("%p, %.*s\n", u.e, u.e->nlen, u.e->name);
		u.ul += au_rdu_len(u.e->nlen);
	}
#endif

	p->pos = realloc(p->pos, sizeof(*p->pos) * p->npos);
	if (!p->pos)
		goto out;

	err = 0;
	p->idx = 0;
	p->real = NULL;
	p->wh = NULL;
	u = p->ent;
	for (ul = 0; !err && ul < p->npos; ul++) {
		/* DPri("%s\n", u.e->name); */
		u.e->wh = 0;
		do_store = 1;
		if (u.e->nlen <= AUFS_WH_PFX_LEN
		    || memcmp(u.e->name, AUFS_WH_PFX, AUFS_WH_PFX_LEN))
			err = rdu_ent_append(p, u.e);
		else
			err = rdu_ent_append_wh(p, u.e);
		u.ul += au_rdu_len(u.e->nlen);
	}
	tdestroy(p->real, rdu_tfree);
	tdestroy(p->wh, rdu_tfree);
	if (err) {
		free(p->pos);
		p->pos = NULL;
		goto out;
	} else if (p->idx == p->npos)
		goto out; /* success */

	p->npos = p->idx;
	/* t == NULL is not an error */
	t = realloc(p->pos, sizeof(*p->pos) * p->idx);
	if (t)
		p->pos = t;

	u = p->ent;
	for (ul = 0; ul < p->npos; ul++) {
		if (p->pos[ul] != u.e)
			break;
		u.ul += au_rdu_len(u.e->nlen);
	}
	for (; ul < p->npos; ul++) {
		memmove(u.e, p->pos[ul], au_rdu_len(p->pos[ul]->nlen));
		p->pos[ul] = u.e;
		u.ul += au_rdu_len(u.e->nlen);
	}

 out:
	return err;
}

int rdu_init(struct rdu *p, int want_de)
{
	int err;
	unsigned long long used;
	struct aufs_rdu param;
	char *t;
	struct au_rdu_ent *e;

	memset(&param, 0, sizeof(param));
	param.verify[AufsCtlRduV_SZ] = sizeof(param);
	param.sz = p->sz;
	param.ent = p->ent;
	param.tail = param.ent;
	if (!param.ent.e) {
		err = -1;
		param.ent.e = malloc(param.sz);
		if (!param.ent.e)
			goto out;
		p->ent = param.ent;
	}
	t = getenv("AUFS_RDU_BLK");
	if (t)
		param.blk = strtoul(t + sizeof("AUFS_RDU_BLK"), NULL, 0);

	p->npos = 0;
	while (1) {
		param.full = 0;
		err = rdu_getent(p, &param);
		if (err || !param.rent)
			break;

		p->npos += param.rent;
		if (!param.full)
			continue;

		assert(param.blk);
		e = realloc(p->ent.e, p->sz + param.blk);
		if (e) {
			used = param.tail.ul - param.ent.ul;
			DPri("used %llu\n", used);
			param.sz += param.blk - used;
			DPri("sz %llu\n", param.sz);
			used += param.ent.ul - p->ent.ul;
			DPri("used %lu\n", used);
			p->ent.e = e;
			param.ent.ul = p->ent.ul + used;
			DPri("ent %p\n", param.ent.e);
			param.tail = param.ent;
			p->sz += param.blk;
			DPri("sz %llu\n", p->sz);
		} else {
			err = -1;
			break;
		}
	}

	p->shwh = param.shwh;
	if (!err)
		err = rdu_merge(p);

	if (!err) {
		param.ent = p->ent;
		param.nent = p->npos;
		err = ioctl(p->fd, AUFS_CTL_RDU_INO, &param);
	}

	if (!err && want_de && !p->de) {
		err = -1;
		p->de = malloc(sizeof(*p->de));
		if (p->de)
			err = 0;
	}

	if (err) {
		free(p->ent.e);
		p->ent.e = NULL;
#if 0
	} else {
		unsigned long long ull;
		struct au_rdu_ent *e;
		for (ull = 0; ull < p->npos; ull++) {
			e = p->pos[ull];
			DPri("%p, %.*s\n", e, e->nlen, e->name);
		}
#endif
	}

 out:
	return err;
}

/* ---------------------------------------------------------------------- */

static int (*real_closedir)(DIR *dir);
LibAuDlFunc(closedir);

int closedir(DIR *dir)
{
	int err, fd;
	struct statfs stfs;
	struct rdu *p;

	err = -1;
	if (LibAuTestFunc(Rdu_READDIR)
	    || LibAuTestFunc(Rdu_READDIR_R)
	    || LibAuTestFunc(closedir)) {
		errno = EBADF;
		fd = dirfd(dir);
		if (fd < 0)
			goto out;
		err = fstatfs(fd, &stfs);
		if (err)
			goto out;

		if (stfs.f_type == AUFS_SUPER_MAGIC) {
			p = rdu_buf_lock(fd);
			if (p)
				rdu_free(p);
		}
	}
	if (!libau_dl_closedir())
		err = real_closedir(dir);

 out:
	return err;
}

#if 0
extern int scandir (__const char *__restrict __dir,
		    struct dirent ***__restrict __namelist,
		    int (*__selector) (__const struct dirent *),
		    int (*__cmp) (__const void *, __const void *))
     __nonnull ((1, 2));
extern int scandir64 (__const char *__restrict __dir,
		      struct dirent64 ***__restrict __namelist,
		      int (*__selector) (__const struct dirent64 *),
		      int (*__cmp) (__const void *, __const void *))
     __nonnull ((1, 2));
extern __ssize_t getdirentries (int __fd, char *__restrict __buf,
				size_t __nbytes,
				__off_t *__restrict __basep)
     __THROW __nonnull ((2, 4));
extern __ssize_t getdirentries64 (int __fd, char *__restrict __buf,
				  size_t __nbytes,
				  __off64_t *__restrict __basep)
     __THROW __nonnull ((2, 4));
#endif
