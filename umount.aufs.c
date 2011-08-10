/*
 * Copyright (C) 2010 Junjiro R. Okajima
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
 * The main purpose of this script is calling auplink.
 */

#include <linux/aufs_type.h>
#include <mntent.h>
#include <stdio.h>
#include <unistd.h>
#include "au_util.h"

int main(int argc, char *argv[])
{
	int err, i, j;
	struct mntent ent;
	char *mntpnt, *av[argc + 1];

	if (argc < 2) {
		puts(AuVersion);
		errno = EINVAL;
		goto out;
	}

	mntpnt = argv[1];
	err = au_proc_getmntent(mntpnt, &ent);
	if (err)
		AuFin("no such mount point");
	if (!hasmntopt(&ent, "noplink")) {
		err = au_plink(mntpnt, AuPlink_FLUSH,
			       AuPlinkFlag_OPEN | AuPlinkFlag_CLOEXEC,
			       /*fd*/NULL);
		if (err)
			AuFin(NULL);
	}

	i = 0;
	av[i++] = "umount";
	av[i++] = "-i";
	for (j = 2; j < argc; j++)
		av[i++] = argv[j];
	av[i++] = mntpnt;
	av[i++] = NULL;
	execvp(MOUNT_CMD_PATH "umount", av);

out:
	AuFin("umount");
	return errno;
}
