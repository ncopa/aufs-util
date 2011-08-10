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

#include <linux/fs.h>
#include <linux/uloop.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int real_fd;
static char *me, *real_bdev;

#define Dbg(fmt, args...) printf("%s:%d:" fmt, __func__, __LINE__, ##args)
#define DbgErr(e) if (e) Dbg("err %d\n", e)

/* ---------------------------------------------------------------------- */

static int store(unsigned long long start, int size, void *arg)
{
	int err;
	unsigned long long m, tsize;
	char *src, *dst;

	//Dbg("start %Lu, size %d\n", start, size);
	assert(start + size <= uloop->cache_size);

	err = -1;
	m = start % uloop->pagesize;
	start -= m;
	size += m;
	tsize = uloop->tgt_size;
	if (tsize < start + size)
		size = tsize - start;
	src = mmap(NULL, size, PROT_READ, MAP_SHARED, real_fd, start);
	if (src == MAP_FAILED)
		goto out;
	dst = mmap(NULL, size, PROT_WRITE, MAP_SHARED, ulo_cache_fd, start);
	if (dst == MAP_FAILED)
		goto out_src;
	memcpy(dst, src, size);

#if 0
	err = msync(dst, size, MS_SYNC);
	DbgErr(err);
#endif
	err = munmap(dst, size);
 out_src:
	munmap(src, size); /* ignore */
 out:
	DbgErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static unsigned long long init_size(void)
{
	unsigned long long sz;
	int err, i;

	sz = -1;
	real_fd = open(real_bdev, O_RDONLY);
	if (real_fd < 0)
		goto out_err;
	err = ioctl(real_fd, BLKGETSIZE, &i);
	if (err) {
		close(real_fd);
		goto out_err;
	}
	sz = i;
	sz *= 512;
	goto out; /* success */

 out_err:
	me = real_bdev;
 out:
	return sz;
}

/* ---------------------------------------------------------------------- */

static int init(struct ulo_init *init_args)
{
	int err;

	init_args->size = init_size();
	err = init_args->size;
	if (err != -1)
		err = ulo_init(init_args);

	me = real_bdev;
	return err;
}

static void usage(void)
{
	fprintf(stderr, "%s"
		" [-b bitmap]"
		" [-c cache]"
		" /dev/loopN block_device\n",
		me);
	exit(EINVAL);
}

static int parse(int argc, char *argv[], struct ulo_init *init_args)
{
	int opt;
	static char bitmap_def[] = "/tmp/123456.bitmap",
		cache_def[] = "/tmp/123456.cache";

	while ((opt = getopt(argc, argv, "b:c:")) != -1) {
		switch (opt) {
		case 'b':
			init_args->path[ULO_BITMAP] = optarg;
			break;
		case 'c':
			init_args->path[ULO_CACHE] = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	if (argc - optind != 2) {
		usage();
		return EINVAL;
	}

	init_args->path[ULO_DEV] = argv[optind];
	real_bdev = argv[optind + 1];

	if (!init_args->path[ULO_BITMAP]) {
		snprintf(bitmap_def, sizeof(bitmap_def) - 1, "/tmp/%d.bitmap",
			 getpid());
		init_args->path[ULO_BITMAP] = bitmap_def;
	}
	if (!init_args->path[ULO_CACHE]) {
		snprintf(cache_def, sizeof(cache_def) - 1, "/tmp/%d.cache",
			 getpid());
		init_args->path[ULO_CACHE] = cache_def;
	}

	//Dbg("to %d, %s\n", timeout, real_bdev);
	return 0;
}

/* ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	int err;
	pid_t pid;
	struct ulo_init init_args = {
		.dev_flags = O_RDONLY
		//.dev_flags = O_RDWR
	};

	me = argv[0];
	err = parse(argc, argv, &init_args);
	if (!err)
		err = init(&init_args);
	if (!err) {
		pid = fork();
		if (!pid)
			err = ulo_loop(SIGUSR1, store, NULL);
		else if (pid > 0)
			sleep(1);
	}

	if (err && me)
		perror(me);
	return err;
}
