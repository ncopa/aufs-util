/*
 * Copyright (C) 2005-2016 Junjiro R. Okajima
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

#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/aufs_type.h>
#include "au_util.h"

static int ia_test(ino_t ino)
{
	int i;
	ino_t *p;

	/* todo: hash table */
	ia.p = ia.o;
	p = ia.cur;
	for (i = 0; i < ia.nino; i++)
		if (*p++ == ino)
			return 1;
	return 0;
}


int ftw_list(const char *fname, const struct stat *st, int flags,
	     struct FTW *ftw)
{
	if (!strcmp(fname + ftw->base, AUFS_WH_PLINKDIR))
		return FTW_SKIP_SUBTREE;
	if (flags == FTW_D || flags == FTW_DNR)
		return FTW_CONTINUE;

	if (ia_test(st->st_ino))
		puts(fname);

	return FTW_CONTINUE;
}

int ftw_cpup(const char *fname, const struct stat *st, int flags,
	     struct FTW *ftw)
{
	int err;

	if (!strcmp(fname + ftw->base, AUFS_WH_PLINKDIR))
		return FTW_SKIP_SUBTREE;
	if (flags == FTW_D || flags == FTW_DNR)
		return FTW_CONTINUE;

	/*
	 * do nothing but update something harmless in order to make it copyup
	 */
	if (ia_test(st->st_ino)) {
		Dpri("%s\n", fname);
		if (!S_ISLNK(st->st_mode))
			err = chown(fname, -1, -1);
		else
			err = lchown(fname, -1, -1);
		if (err)
			AuFin("%s", fname);
	}

	return FTW_CONTINUE;
}
