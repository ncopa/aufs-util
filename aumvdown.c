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

static struct option opts[] = {
	{"interactive",	no_argument,	NULL,	'i'},
	{"verbose",	no_argument,	NULL,	'v'},
	{"version",	no_argument,	NULL,	'V'},
	{"help",	no_argument,	NULL,	'h'},
	{NULL,		no_argument,	NULL,  0}
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
		"-v | --verbose\n"
		AuVersion "\n", program_invocation_short_name);
}

int main(int argc, char *argv[])
{
	int err, fd, i, c, interactive;
	struct aufs_mvdown mvdown = {
		.flags = 0
	};

	err = 0;
	interactive = 0;
	i = 0;
	while ((c = getopt_long(argc, argv, "ivVh", opts, &i)) != -1) {
		switch (c) {
		case 'i':
			interactive = 1;
			break;
		case 'v':
			mvdown.flags |= AUFS_MVDOWN_VERBOSE;
			break;
		case 'V':
			fprintf(stderr, AuVersion "\n");
			goto out;
		case 'h':
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
		if (interactive) {
			fprintf(stderr, "move down '%s'? ", argv[i]);
			fflush(stderr);
			c = fgetc(stdin);
			c = toupper(c);
			if (c != 'Y')
				continue;
		}
		fd = open(argv[i], O_RDONLY);
		if (fd < 0)
			AuFin("open");
		err = ioctl(fd, AUFS_CTL_MVDOWN, &mvdown);
		if (err)
			AuFin("AUFS_CTL_MVDOWN");
		err = close(fd);
		if (err)
			AuFin("open");
	}

out:
	return err;
}
