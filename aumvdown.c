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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/aufs_type.h>
#include "au_util.h"

enum {
	INTERACTIVE	= 1,
	VERBOSE		= (1 << 1),
};

static struct option opts[] __attribute__((unused)) = {
	{"lower-branch-id",	required_argument,	NULL,	'b'},
	{"upper-branch-id",	required_argument,	NULL,	'B'},
	{"interactive",		no_argument,		NULL,	'i'},
	{"keep-upper",		no_argument,		NULL,	'k'},
	{"overwrite-lower",	no_argument,		NULL,	'o'},
	{"allow-ro-lower",	no_argument,		NULL,	'r'},
	{"allow-ro-upper",	no_argument,		NULL,	'R'},
	{"verbose",		no_argument,		NULL,	'v'},
	{"version",		no_argument,		NULL,	'V'},
	{"help",		no_argument,		NULL,	'h'},
	/* hidden */
	{"dmsg",		no_argument,		NULL,	'd'},
	{"stfs",		no_argument,		NULL,	's'},
	{NULL,			no_argument,		NULL,  0}
};

#define OPTS_FORM	"b:B:ikorRvVh" "ds"

static __attribute__((unused)) void usage(void)
{
	fprintf(stderr,
		"usage: %s [options] file ...\n"
		"move-down the specified file (an opposite action of copy-up)\n"
		"from the highest branch where the file exist to the next\n"
		"lower writable branch.\n"
		"options:\n"
		"-b | --lower-branch-id brid\n"
		"-B | --upper-branch-id brid\n"
		"-i | --interactive\n"
		"-k | --keep-upper\n"
		"-o | --overwrite-lower\n"
		"-r | --allow-ro-lower\n"
		"-R | --allow-ro-upper\n"
		"-v | --verbose\n"
		"-V | --version\n"
		AuVersion "\n", program_invocation_short_name);
}

static __attribute__((unused)) long cvt(char *str)
{
	long ret;

	errno = 0;
	ret = strtol(str, NULL, 10);
	if ((ret == LONG_MAX || ret == LONG_MIN)
	    && errno)
		ret = -1;
	return ret;
}

static __attribute__((unused)) void pr_stbr(struct aufs_stbr *stbr)
{
	printf("b%d %d%%(%llu/%llu), %d%%(%llu/%llu) free\n",
	       stbr->bindex,
	       (int)(stbr->stfs.f_bavail * 100.0 / stbr->stfs.f_blocks),
	       (unsigned long long)stbr->stfs.f_bavail,
	       (unsigned long long)stbr->stfs.f_blocks,
	       (int)(stbr->stfs.f_ffree * 100.0 / stbr->stfs.f_files),
	       (unsigned long long)stbr->stfs.f_ffree,
	       (unsigned long long)stbr->stfs.f_files);
}

#define AuMvDownFin(mvdown, str) do {					\
		static int e;						\
		static char a[1024];					\
		e = errno;						\
		snprintf(a, sizeof(a), "%s:%d: %s",			\
			 __FILE__, __LINE__, str);			\
		errno = e;						\
		au_errno = (mvdown)->au_errno;				\
		au_perror(a);						\
		if (errno)						\
			exit(errno);					\
	} while (0)

int main(int argc, char *argv[])
{
	int err, fd, i, c;
	unsigned int user_flags;
	struct aufs_mvdown mvdown = {
		.flags = 0
	};

	err = 0;
	user_flags = 0;
	i = 0;
	while ((c = getopt_long(argc, argv, OPTS_FORM, opts, &i)) != -1) {
		switch (c) {
		case 'b':
			err = cvt(optarg);
			if (err < 0) {
				perror(optarg);
				goto out;
			}
			mvdown.flags |= AUFS_MVDOWN_BRID_LOWER;
			mvdown.stbr[AUFS_MVDOWN_LOWER].brid = err;
			break;
		case 'B':
			err = cvt(optarg);
			if (err < 0) {
				perror(optarg);
				goto out;
			}
			mvdown.flags |= AUFS_MVDOWN_BRID_UPPER;
			mvdown.stbr[AUFS_MVDOWN_UPPER].brid = err;
			break;
		case 'i':
			user_flags |= INTERACTIVE;
			break;
		case 'k':
			mvdown.flags |= AUFS_MVDOWN_KUPPER;
			break;
		case 'o':
			mvdown.flags |= AUFS_MVDOWN_OWLOWER;
			break;
		case 'r':
			mvdown.flags |= AUFS_MVDOWN_ROLOWER;
			break;
		case 'R':
			mvdown.flags |= AUFS_MVDOWN_ROUPPER;
			break;
		case 'v':
			user_flags |= VERBOSE;
			break;
		case 'V':
			fprintf(stderr, AuVersion "\n");
			goto out;

		/* hidden */
		case 'd':
			mvdown.flags |= AUFS_MVDOWN_DMSG;
			break;
		case 's':
			mvdown.flags |= AUFS_MVDOWN_STFS;
			break;

		case 'h':
		default:
			usage();
			goto out;
		}
	}

	err = EINVAL;
	if (optind == argc) {
		usage();
		goto out;
	}

	for (i = optind; i < argc; i++) {
		if (user_flags & INTERACTIVE) {
			fprintf(stderr, "move down '%s'? ", argv[i]);
			fflush(stderr);
			c = fgetc(stdin);
			c = toupper(c);
			if (c != 'Y')
				continue;
		}
		fd = open(argv[i], O_RDONLY);
		if (fd < 0)
			AuMvDownFin(&mvdown, argv[i]);
		err = ioctl(fd, AUFS_CTL_MVDOWN, &mvdown);
		if (err)
			AuMvDownFin(&mvdown, argv[i]);
		if (user_flags & VERBOSE) {
			char *u = "", *l = "";
			if (mvdown.flags & AUFS_MVDOWN_ROLOWER_R)
				l = "(RO)";
			if (mvdown.flags & AUFS_MVDOWN_ROUPPER_R)
				u = "(RO)";
			printf("'%s' b%d(brid%d)%s --> b%d(brid%d)%s\n",
			       argv[i],
			       mvdown.stbr[AUFS_MVDOWN_UPPER].bindex,
			       mvdown.stbr[AUFS_MVDOWN_UPPER].brid,
			       u,
			       mvdown.stbr[AUFS_MVDOWN_LOWER].bindex,
			       mvdown.stbr[AUFS_MVDOWN_LOWER].brid,
			       l);
			if (mvdown.flags & AUFS_MVDOWN_STFS) {
				if (!(mvdown.flags & AUFS_MVDOWN_STFS_FAILED)) {
					pr_stbr(mvdown.stbr + AUFS_MVDOWN_UPPER);
					pr_stbr(mvdown.stbr + AUFS_MVDOWN_LOWER);
				} else {
					fprintf(stderr, "STFS failed, ignored\n");
					fflush(stderr);
				}
			}
		}
		err = close(fd);
		if (err)
			AuMvDownFin(&mvdown, argv[i]);
	}

out:
	return err;
}
