/*
 * Copyright (C) 2016 Junjiro R. Okajima
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

#include <sys/stat.h>
#include <sys/types.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>

#include "au_util.h"

/* dummy */
int ftw_list(const char *fname, const struct stat *st, int flags,
	     struct FTW *ftw)
{
	return 0;
}

/* dummy */
int ftw_cpup(const char *fname, const struct stat *st, int flags,
	     struct FTW *ftw)
{
	return 0;
}

int au_nftw(const char *dirpath,
	    int (*fn) (const char *fpath, const struct stat *sb,
		       int typeflag, struct FTW *ftwbuf),
	    int nopenfd, int flags)
{
	int err, fd, i;
	mode_t mask;
	FILE *fp;
	ino_t *p;
	char *action, ftw[1024], tmp[] = "/tmp/auplink_ftw.XXXXXX";

	mask = umask(S_IRWXG | S_IRWXO);
	fd = mkstemp(tmp);
	if (fd < 0)
		AuFin("mkstemp");
	umask(mask);
	fp = fdopen(fd, "r+");
	if (!fp)
		AuFin("fdopen");

	ia.p = ia.o;
	p = ia.cur;
	for (i = 0; i < ia.nino; i++) {
		err = fprintf(fp, "%llu\n", (unsigned long long)*p++);
		if (err < 0)
			break;
	}
	err = fflush(fp) || ferror(fp);
	if (err)
		AuFin("%s", tmp);
	err = fclose(fp);
	if (err)
		AuFin("%s", tmp);

	action = "list";
	if (fn == ftw_cpup)
		action = "cpup";
	else
		fflush(stdout); /* inode numbers */
	i = snprintf(ftw, sizeof(ftw), AUPLINK_FTW_CMD " %s %s %s",
		     tmp, dirpath, action);
	if (i > sizeof(ftw))
		AuFin("snprintf");
	err = system(ftw);
	err = WEXITSTATUS(err);
	if (err)
		AuFin("%s", ftw);

	return err;
}
