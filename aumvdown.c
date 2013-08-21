/*
 * Copyright (C) 2011-2013 Junjiro R. Okajima
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
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/aufs_type.h>
#include "au_util.h"

enum {
	INTERACTIVE	= 1,
	VERBOSE		= (1 << 1),
};

static struct option opts[] = {
	{"interactive",		no_argument,	NULL,	'i'},
	{"keep-upper",		no_argument,	NULL,	'k'},
	{"overwrite-lower",	no_argument,	NULL,	'o'},
	{"verbose",		no_argument,	NULL,	'v'},
	{"version",		no_argument,	NULL,	'V'},
	{"help",		no_argument,	NULL,	'h'},
	/* hidden */
	{"dmsg",		no_argument,	NULL,	'd'},
	{NULL,			no_argument,	NULL,  0}
};

static void usage(void)
{
	fprintf(stderr,
		"usage: %s [options] file ...\n"
		"move-down the specified file (an opposite action of copy-up)\n"
		"from the highest branch where the file exist to the next\n"
		"lower writable branch.\n"
		"options:\n"
		"-i | --interactive\n"
		"-k | --keep-upper\n"
		"-o | --overwrite-lower\n"
		"-v | --verbose\n"
		"-V | --version\n"
		AuVersion "\n", program_invocation_short_name);
}

#define AuMvDownFin(mvdown, str) do {					\
		static int e;						\
		static char a[1024];					\
		e = errno;						\
		snprintf(a, sizeof(a), "%s:%d: %s",			\
			 __FILE__, __LINE__, str);			\
		errno = e;						\
		au_errno = (mvdown)->output.au_errno;			\
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
	while ((c = getopt_long(argc, argv, "ikovVhd", opts, &i)) != -1) {
		switch (c) {
		case 'i':
			user_flags |= INTERACTIVE;
			break;
		case 'k':
			mvdown.flags |= AUFS_MVDOWN_KUPPER;
			break;
		case 'o':
			mvdown.flags |= AUFS_MVDOWN_OWLOWER;
			break;
		case 'v':
			user_flags |= VERBOSE;
			break;
		case 'V':
			fprintf(stderr, AuVersion "\n");
			goto out;
		case 'h':
			usage();
			goto out;
		/* hidden */
		case 'd':
			mvdown.flags |= AUFS_MVDOWN_DMSG;
			break;
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
		if (user_flags & VERBOSE)
			printf("'%s' b%d --> b%d\n",
			       argv[i], mvdown.output.bsrc, mvdown.output.bdst);
		err = close(fd);
		if (err)
			AuMvDownFin(&mvdown, argv[i]);
	}

out:
	return err;
}
