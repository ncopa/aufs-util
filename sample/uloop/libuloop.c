/*
 * aufs sample -- ULOOP driver
 *
 * Copyright (C) 2005-2010 Junjiro Okajima
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

#include <linux/uloop.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct uloop g_uloop;
const struct uloop *uloop = &g_uloop;

/* ---------------------------------------------------------------------- */

#define Dbg(fmt, args...) fprintf(stderr, "%s:%d:" fmt, \
	__func__, __LINE__, ##args)
#define DbgErr(e) if (e) Dbg("err %d\n", e)

/* ---------------------------------------------------------------------- */

int ulo_loop(int sig, ulo_cb_t store, void *arg)
{
	int err;
	static sigset_t sigset;
	union ulo_ctl ctl;

	err = sigemptyset(&sigset);
	if (!err)
		err = sigaddset(&sigset, sig);
	if (!err)
		err = sigprocmask(SIG_BLOCK, &sigset, NULL);

	while (!err) {
		ctl.ready.signum = sig;
		//Dbg("ready\n");
		err = ioctl(g_uloop.fd[ULO_DEV], ULOCTL_READY, &ctl);
		DbgErr(err);
		if (!err)
			err = sigwaitinfo(&sigset, NULL);
		//DbgErr(err);
		if (err == sig)
			err = ioctl(g_uloop.fd[ULO_DEV], ULOCTL_RCVREQ, &ctl);
		DbgErr(err);
		if (!err)
			err = store(ctl.rcvreq.start, ctl.rcvreq.size, arg);
		if (!err) {
			ctl.sndres.start = ctl.rcvreq.start;
			ctl.sndres.size = ctl.rcvreq.size;
			err = ioctl(g_uloop.fd[ULO_DEV], ULOCTL_SNDRES, &ctl);
			DbgErr(err);
		}
	}

	return err;
}

/* ---------------------------------------------------------------------- */

static int ulo_create_size(char *path, unsigned long long size)
{
	int fd, err;
	off_t off;
	ssize_t sz;
	struct stat st;

	err = 0;
	st.st_size = 0;
	fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd < 0)
		return fd;
	err = fstat(fd, &st);
	if (err)
		return err;
	if (st.st_size == size)
		return fd; /* success */

	off = lseek(fd, size - 1, SEEK_SET);
	if (off == -1)
		return -1;
	sz = write(fd, "\0", 1);
	if (sz != 1)
		return -1;
	return fd; /* success */
}

static int ulo_init_loop(char *dev_path, int dev_flags, char *cache_path)
{
	int err;
	struct loop_info64 loinfo64;
	union ulo_ctl ctl;

	err = open(dev_path, dev_flags);
	if (err < 0)
		goto out;
	g_uloop.fd[ULO_DEV] = err;

	err = ioctl(g_uloop.fd[ULO_DEV], LOOP_SET_FD, g_uloop.fd[ULO_CACHE]);
	if (err)
		goto out;

	memset(&loinfo64, 0, sizeof(loinfo64));
	strncpy((void *)(loinfo64.lo_file_name), cache_path, LO_NAME_SIZE);
	loinfo64.lo_encrypt_type = LOOP_FILTER_ULOOP;
	//strncpy((void *)(loinfo64.lo_crypt_name), "ulttp", LO_NAME_SIZE);
	//loinfo64.lo_sizelimit = cache_size;
	err = ioctl(g_uloop.fd[ULO_DEV], LOOP_SET_STATUS64, &loinfo64);
	if (err)
		goto out_loop;

	ctl.setbmp.fd = g_uloop.fd[ULO_BITMAP];
	ctl.setbmp.pagesize = g_uloop.pagesize;
	err = ioctl(g_uloop.fd[ULO_DEV], ULOCTL_SETBMP, &ctl);
        if (!err) {
#if 0
		Dbg("{%d, %d, %d}, pgae %d, tgt %Lu, cache %Lu\n",
		    uloop->fd[0], uloop->fd[1], uloop->fd[2],
		    uloop->pagesize, uloop->tgt_size, uloop->cache_size);
#endif
                return 0;
	}
	DbgErr(err);

 out_loop:
	ioctl(g_uloop.fd[ULO_DEV], LOOP_CLR_FD, g_uloop.fd[ULO_CACHE]);
 out:
	return err;
}

int ulo_init(struct ulo_init *init)
{
	int err;
	unsigned long long mod, sz;

#if 0
	err = EINVAL;
	int i;
	for (i = 0; i < ULO_Last; i++)
		if (!init->path[i])
			goto out;
	if (init->size == -1)
		goto out;
#endif

	g_uloop.cache_size = init->size;
	g_uloop.tgt_size = init->size;

	g_uloop.pagesize = sysconf(_SC_PAGESIZE);
	assert(g_uloop.pagesize > 0);

	err = EINVAL;
	mod = g_uloop.cache_size % g_uloop.pagesize;
	if (mod)
		g_uloop.cache_size += g_uloop.pagesize - mod;
	if (g_uloop.cache_size % g_uloop.pagesize)
		goto out;
	g_uloop.fd[ULO_CACHE] = ulo_create_size(init->path[ULO_CACHE],
						g_uloop.cache_size);
	err = g_uloop.fd[ULO_CACHE];
	if (g_uloop.fd[ULO_CACHE] < 0)
		goto out;

	sz = g_uloop.cache_size;
	sz /= g_uloop.pagesize;
	sz /= CHAR_BIT;
	if (sz < g_uloop.pagesize)
		sz = g_uloop.pagesize;
	else {
		mod = sz % g_uloop.pagesize;
		if (mod)
			sz += g_uloop.pagesize - mod;
	}
	err = EINVAL;
	if (sz % g_uloop.pagesize)
		goto out;
	g_uloop.fd[ULO_BITMAP] = ulo_create_size(init->path[ULO_BITMAP], sz);
	err = g_uloop.fd[ULO_BITMAP];
	if (g_uloop.fd[ULO_BITMAP] < 0)
		goto out;

	err = ulo_init_loop(init->path[ULO_DEV], init->dev_flags,
			    init->path[ULO_CACHE]);
	if (!err)
		return 0;
 out:
	return err;
}
