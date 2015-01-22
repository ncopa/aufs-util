/*
 * Copyright (C) 2005-2015 Junjiro R. Okajima
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
#include <sys/statfs.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/aufs_type.h>
#include "au_util.h"

int au_br(union aufs_brinfo **brinfo, int *nbr, char *root)
{
	int err, fd;
	struct statfs stfs;

	fd = open(root, O_RDONLY /* | O_PATH */);
	if (fd < 0)
		AuFin("%s", root);

	err = fstatfs(fd, &stfs);
	if (err)
		AuFin("internal error, %s", root);
	if (stfs.f_type != AUFS_SUPER_MAGIC)
		AuFin("%s is not aufs", root);

	*nbr = ioctl(fd, AUFS_CTL_BRINFO, NULL);
	if (*nbr <= 0)
		AuFin("internal error, %s", root);

	errno = posix_memalign((void **)brinfo, 4096, *nbr * sizeof(**brinfo));
	if (errno)
		AuFin("posix_memalign");

	err = ioctl(fd, AUFS_CTL_BRINFO, *brinfo);
	if (err)
		AuFin("AUFS_CTL_BRINFO");

	err = close(fd);
	if (err)
		AuFin("internal error, %s", root);

	return 0;
}

#ifdef AUFHSM
int au_nfhsm(int nbr, union aufs_brinfo *brinfo)
{
	int nfhsm, i;

	nfhsm = 0;
	for (i = 0; i < nbr; i++)
		if (au_br_fhsm(brinfo[i].perm))
			nfhsm++;

	return nfhsm;
}

int au_br_qsort_path(const void *_a, const void *_b)
{
	const union aufs_brinfo *a = _a, *b = _b;

	return strcmp(a->path, b->path);
}

void au_br_sort_path(int nbr, union aufs_brinfo *brinfo)
{
	qsort(brinfo, nbr, sizeof(*brinfo), au_br_qsort_path);
}

int au_br_bsearch_path(const void *_path, const void *_brinfo)
{
	char *path = (char *)_path;
	const union aufs_brinfo *brinfo = _brinfo;

	return strcmp(path, brinfo->path);
}

union aufs_brinfo *au_br_search_path(char *path, int nbr,
				     union aufs_brinfo *brinfo)
{
	return bsearch((void *)path, brinfo, nbr, sizeof(*brinfo),
		       au_br_bsearch_path);
}

#endif
