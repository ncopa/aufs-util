/*
 * Copyright (C) 2011 Junjiro R. Okajima
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
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/aufs_type.h>

static void usage(char *me)
{
	fprintf(stderr, "usage: %s mntpnt bindex [inum ...]\n", me);
}

static int do_ibusy(char *inum, int fd, struct aufs_ibusy *ibusy)
{
	int err;

	err = -1;
	errno = 0;
	ibusy->ino = strtoul(inum, NULL, 0);
	if (errno)
		goto out;

	err = 0;
	if (ibusy->ino == AUFS_ROOT_INO)
		goto out;

	err = ioctl(fd, AUFS_CTL_IBUSY, ibusy);
	if (!err && ibusy->h_ino)
		printf("i%llu\tb%d\thi%llu\n",
		       (unsigned long long)ibusy->ino, ibusy->bindex,
		       (unsigned long long)ibusy->h_ino);

out:
	return err;
}

int main(int argc, char *argv[])
{
	int err, fd, i;
	struct aufs_ibusy ibusy;
	char a[16], *eprefix;
	DIR *dp;

	err = -1;
	errno = EINVAL;
	eprefix = argv[0];
	if (argc < 3) {
		usage(argv[0]);
		goto out;
	}

	eprefix = argv[1];
	dp = opendir(argv[1]);
	if (!dp)
		goto out;
	fd = dirfd(dp);

	eprefix = argv[2];
	errno = 0;
	ibusy.bindex = strtoul(argv[2], NULL, 0);
	if (errno)
		goto out;

	if (argc > 3) {
		for (i = 3; i < argc; i++) {
			eprefix = argv[i];
			err = do_ibusy(argv[i], fd, &ibusy);
			if (err)
				break;
		}
	} else {
		eprefix = a;
		while (fgets(a, sizeof(a), stdin)) {
			err = do_ibusy(a, fd, &ibusy);
			if (err)
				break;
		}
	}

out:
	if (err)
		perror(eprefix);
	return err;
}
