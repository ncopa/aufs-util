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
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>

static int timeout = 30;
static char *me, *g_url;
static CURL *ezcurl;
static char range[32];
static struct arg_for_curl {
	char	*p;
	int	written;
	int	size;
	int	err;
} arg_for_curl;

#define Dbg(fmt, args...) printf("%s:%d:" fmt, __func__, __LINE__, ##args)
#define DbgErr(e) if (e) Dbg("err %d\n", e)

/* ---------------------------------------------------------------------- */

static int err_curl(CURLcode curle)
{
	int e;

	e = errno;
	fprintf(stderr, "%s: %s\n", me, curl_easy_strerror(curle));
	me = NULL;
	return e;
}

static size_t store_from_curl(void *got, size_t size, size_t nmemb, void *arg)
{
	int real_bytes;

#if 0
	Dbg("size %u, nmemb %u, arg_for_curl->err %d\n",
	    size, nmemb, arg_for_curl.err);
#endif
	if (!size || !nmemb || arg_for_curl.err)
		return 0;

	real_bytes = size * nmemb;
	if (arg_for_curl.size < arg_for_curl.written + real_bytes) {
		arg_for_curl.err++;
		return 0;
	}

	memcpy(arg_for_curl.p, got, real_bytes);
	arg_for_curl.written += real_bytes;
	arg_for_curl.p += real_bytes;
	return nmemb;
}

static int store(unsigned long long start, int size, void *arg)
{
	CURL *ezcurl = arg;
	CURLcode curle;
	int err;
	unsigned long long m, tsize;
	char *o;

	//Dbg("start %Lu, size %d\n", start, size);
	assert(start + size <= uloop->cache_size);

	m = start % uloop->pagesize;
	start -= m;
	arg_for_curl.size = size + m;
	tsize = uloop->tgt_size;
	if (tsize < start + arg_for_curl.size)
		arg_for_curl.size = tsize - start;
	o = mmap(NULL, arg_for_curl.size, PROT_WRITE, MAP_SHARED, ulo_cache_fd,
		 start);
	if (o == MAP_FAILED)
		return -1;
	arg_for_curl.p = o;
	arg_for_curl.written = 0;
	arg_for_curl.err = 0;

	snprintf(range, sizeof(range) - 1, "%Lu-%Lu",
		 start, start + arg_for_curl.size - 1);
	curle = curl_easy_perform(ezcurl);

	err = munmap(o, arg_for_curl.size);
	if (err)
		return err;
	if (curle != CURLE_OK)
		return err_curl(curle);
	if (arg_for_curl.written != arg_for_curl.size)
		return -1;
	return 0;
}

/* ---------------------------------------------------------------------- */

static unsigned long long get_size(void)
{
	unsigned long long size;
	CURLcode curle;
	char *header, *p;
	const int hsz = 1024;

	size = ULONG_MAX; /* error */
	header = malloc(hsz);
	if (!header)
		return size;
	arg_for_curl.p = header;
	arg_for_curl.size = hsz;
	arg_for_curl.written = 0;
	arg_for_curl.err = 0;

	curle = curl_easy_setopt(ezcurl, CURLOPT_HEADERFUNCTION,
				 store_from_curl);
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_RANGE, "0-1");
#if 0
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_WRITEHEADER,
					 &arg_for_curl);
#endif
	if (curle == CURLE_OK)
		curle = curl_easy_perform(ezcurl);
	if (curle != CURLE_OK) {
		err_curl(curle);
		return size;
	}
	if (arg_for_curl.err) {
		fprintf(stderr, "%s: internal error.\n", me);
		errno = EINVAL;
		return size;
	}

	//Dbg("%s\n", header);
	p = strstr(header, "Content-Range: bytes ");
	if (p)
		p = strchr(p, '/');
	if (!p) {
		fprintf(stderr, "%s: no range header, %s\n", me, g_url);
		errno = EINVAL;
		return size;
	}
	size = strtoull(p + 1, NULL, 10);
	free(header);

	/* reset */
	curle = curl_easy_setopt(ezcurl, CURLOPT_HEADERFUNCTION, NULL);
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_RANGE, NULL);
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_WRITEHEADER, NULL);
	if (curle == CURLE_OK)
		return size; /* success */

	err_curl(curle);
	return ULONG_MAX; /* error */
}

static unsigned long long init_curl_and_size(void)
{
	unsigned long long sz;
	CURLcode curle;

	sz = -1;
	errno = ENOMEM;
	ezcurl = curl_easy_init();
	if (!ezcurl)
		return -1;

	curle = curl_easy_setopt(ezcurl, CURLOPT_URL, g_url);
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_TIMEOUT, timeout);
#if 0
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_VERBOSE, 1);
#endif
#if 0
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_NOPROGRESS, 1);
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_FAILONERROR, 1);
#endif
	if (curle != CURLE_OK)
		goto out_curl;

	errno = ERANGE;
	sz = get_size();
	if (sz == ULONG_MAX)
		goto out_curl;

	curle = curl_easy_setopt(ezcurl, CURLOPT_WRITEFUNCTION,
				 store_from_curl);
#if 0
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_WRITEDATA,
					 &arg_for_curl);
#endif
	if (curle == CURLE_OK)
		curle = curl_easy_setopt(ezcurl, CURLOPT_RANGE, range);
	if (curle == CURLE_OK)
		return sz;

 out_curl:
	err_curl(curle);
	return sz;
}

/* ---------------------------------------------------------------------- */

static int init(struct ulo_init *init_args)
{
	int err;

	init_args->size = init_curl_and_size();
	err = init_args->size;
	if (err != -1)
		err = ulo_init(init_args);
	return err;
}

static void usage(void)
{
	fprintf(stderr, "%s"
		" [-b bitmap]"
		" [-c cache]"
		" [-t timeout]"
		" /dev/loopN url_for_fs_image_file\n"
		"and then, \"mount /dev/loopN /wherever/you/like\n",
		me);
	exit(EINVAL);
}

static int parse(int argc, char *argv[], struct ulo_init *init_args)
{
	int opt;
	static char bitmap_def[] = "/tmp/123456.bitmap",
		cache_def[] = "/tmp/123456.cache";

	while ((opt = getopt(argc, argv, "b:c:t:")) != -1) {
		switch (opt) {
		case 'b':
			init_args->path[ULO_BITMAP] = optarg;
			break;
		case 'c':
			init_args->path[ULO_CACHE] = optarg;
			break;
		case 't':
			errno = 0;
			timeout = strtol(optarg, NULL, 0);
			if (errno) {
				me = optarg;
				return ERANGE;
			}
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
	g_url = argv[optind + 1];

	if (!init_args->path[ULO_CACHE]) {
		snprintf(cache_def, sizeof(cache_def) - 1, "/tmp/%d.cache",
			 getpid());
		init_args->path[ULO_CACHE] = cache_def;
	}
	if (!init_args->path[ULO_BITMAP]) {
		snprintf(bitmap_def, sizeof(bitmap_def) - 1, "/tmp/%d.bitmap",
			 getpid());
		init_args->path[ULO_BITMAP] = bitmap_def;
	}

	//Dbg("to %d, %s\n", timeout, g_url);
	return 0;
}

/* ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	int err;
	pid_t pid;
	struct ulo_init init_args = {
		.dev_flags = O_RDONLY
	};

	me = argv[0];
	err = parse(argc, argv, &init_args);
	if (!err)
		err = init(&init_args);
	if (!err) {
		pid = fork();
		if (!pid) {
			err = ulo_loop(SIGUSR1, store, ezcurl);
			curl_easy_cleanup(ezcurl);
		} else if (pid > 0)
			sleep(1);
	}

	if (err && me)
		perror(me);
	return err;
}
