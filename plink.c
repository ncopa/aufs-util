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

#define _FILE_OFFSET_BITS	64	/* ftw.h */
#define _XOPEN_SOURCE		500	/* ftw.h */

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/aufs_type.h>
#include "au_util.h"

/* todo: try argz? */
static struct name_array {
	char *o;
	int bytes;

	char *cur;
	int nname;
} na;

static struct ino_array {
	char *o;
	int bytes;

	union {
		char *p;
		ino_t *cur;
	};
	int nino;
} ia;

static int na_append(char *plink_dir, char *name)
{
	int l, sz;
	char *p;
	const int cur = na.cur - na.o;

	l = strlen(plink_dir) + strlen(name) + 2;
	sz = na.bytes + l;
	p = realloc(na.o, sz);
	if (!p)
		AuFin("realloc");

	na.o = p;
	na.bytes = sz;
	na.cur = p + cur;
	na.cur += sprintf(na.cur, "%s/%s", plink_dir, name) + 1;
	na.nname++;

	return 0;
}

static int ia_append(ino_t ino)
{
	int sz;
	char *p;
	const int cur = ia.p - ia.o;

	sz = na.bytes + sizeof(ino_t);
	p = realloc(ia.o, sz);
	if (!p)
		AuFin("realloc");

	ia.o = p;
	ia.bytes = sz;
	ia.p = p + cur;
	*ia.cur++ = ino;
	ia.nino++;

	return 0;
}

static int build_array(char *plink_dir)
{
	int err;
	DIR *dp;
	struct dirent *de;
	char *p;
	ino_t ino;

	err = access(plink_dir, F_OK);
	if (err)
		return 0;

	err = 0;
	dp = opendir(plink_dir);
	if (!dp)
		AuFin("%s", plink_dir);
	while ((de = readdir(dp))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
#if 0
		if (de->d_type == DT_DIR) {
			errno = EISDIR;
			AuFin(de->d_name);
		}
#endif

		err = na_append(plink_dir, de->d_name);
		if (err)
			break;

		p = strchr(de->d_name, '.');
		if (!p) {
			errno = EINVAL;
			AuFin("internal error, %s", de->d_name);
		}
		*p = 0;
		errno = 0;
		ino = strtoull(de->d_name, NULL, 0);
		if (ino == /*ULLONG_MAX*/-1 && errno == ERANGE)
			AuFin("internal error, %s", de->d_name);
		err = ia_append(ino);
		if (err)
			break;
	}
	closedir(dp);

	return err;
}

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

/* ---------------------------------------------------------------------- */

static int ftw_list(const char *fname, const struct stat *st, int flags,
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

static int ftw_cpup(const char *fname, const struct stat *st, int flags,
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

/* ---------------------------------------------------------------------- */

static int proc_fd = -1;
static void au_plink_maint(char *si, int close_on_exec, int *fd)
{
	int err, oflags;
	ssize_t ssz;

	if (si) {
		if (proc_fd >= 0) {
			errno = EINVAL;
			AuFin("proc_fd is not NULL");
		}
		oflags = O_WRONLY;
		if (close_on_exec)
			oflags |= O_CLOEXEC;
		proc_fd = open("/proc/" AUFS_PLINK_MAINT_PATH, oflags);
		if (proc_fd < 0)
			AuFin("proc");
		ssz = write(proc_fd, si, strlen(si));
		if (ssz != strlen(si))
			AuFin("write");
	} else {
		err = close(proc_fd);
		if (err)
			AuFin("close");
		proc_fd = -1;
	}

	if (fd)
		*fd = proc_fd;
}

void au_clean_plink(void)
{
	ssize_t ssz __attribute__((unused));

	ssz = write(proc_fd, "clean", 5);
#ifndef DEBUG
	if (ssz != 5)
		AuFin("clean");
#endif
}

#ifdef NO_LIBC_FTW
#define FTW_ACTIONRETVAL 0
static int au_nftw(const char *dirpath,
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
			AuFin("%s", tmp);
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
	i = snprintf(ftw, sizeof(ftw), "auplink_ftw %s %s %s",
		     tmp, dirpath, action);
	if (i > sizeof(ftw))
		AuFin("snprintf");
	err = system(ftw);
	if (err == -1)
		AuFin("%s", ftw);
	else if (WEXITSTATUS(err))
		AuFin("%s", ftw);

	return err;
}
#else
#define au_nftw nftw
#ifndef FTW_ACTIONRETVAL
#error FTW_ACTIONRETVAL is not defined on your system
#endif
#endif

static int do_plink(char *cwd, int cmd, int nbr, union aufs_brinfo *brinfo)
{
	int err, i, l, nopenfd;
	struct rlimit rlim;
	__nftw_func_t func;
	char *p;
#define OPEN_LIMIT 1024

	err = 0;
	switch (cmd) {
	case AuPlink_FLUSH:
		/*FALLTHROUGH*/
	case AuPlink_CPUP:
		func = ftw_cpup;
		break;
	case AuPlink_LIST:
		func = ftw_list;
		break;
	default:
		errno = EINVAL;
		AuFin(NULL);
		func = NULL; /* never reach here */
	}

	for (i = 0; i < nbr; i++) {
		if (!au_br_writable(brinfo[i].perm))
			continue;

		l = strlen(brinfo[i].path);
		p = malloc(l + sizeof(AUFS_WH_PLINKDIR) + 2);
		if (!p)
			AuFin("malloc");
		sprintf(p, "%s/%s", brinfo[i].path, AUFS_WH_PLINKDIR);
		//puts(p);
		err = build_array(p);
		if (err)
			AuFin("build_array");
		free(p);
	}
	if (!ia.nino)
		goto out;

	if (cmd == AuPlink_LIST) {
		ia.p = ia.o;
		for (i = 0; i < ia.nino; i++)
			printf("%llu ", (unsigned long long)*ia.cur++);
		putchar('\n');
	}

	err = getrlimit(RLIMIT_NOFILE, &rlim);
	if (err)
		AuFin("getrlimit");
	nopenfd = (int)rlim.rlim_cur;
	if (rlim.rlim_cur == RLIM_INFINITY
	    || rlim.rlim_cur > OPEN_LIMIT
	    || nopenfd <= 0)
		nopenfd = OPEN_LIMIT;
	else if (nopenfd > 20)
		nopenfd -= 10;
	au_nftw(cwd, func, nopenfd,
		FTW_PHYS | FTW_MOUNT | FTW_ACTIONRETVAL);
	/* ignore */

	if (cmd == AuPlink_FLUSH) {
		au_clean_plink();

		na.cur = na.o;
		for (i = 0; i < na.nname; i++) {
			Dpri("%s\n", na.cur);
			err = unlink(na.cur);
			if (err)
				AuFin("%s", na.cur);
			na.cur += strlen(na.cur) + 1;
		}
	}

 out:
	free(ia.o);
	free(na.o);
	return err;
#undef OPEN_LIMIT
}

int au_plink(char cwd[], int cmd, unsigned int flags, int *fd)
{
	int err, nbr;
	struct mntent ent;
	char *p, si[3 + sizeof(unsigned long long) * 2 + 1];
	union aufs_brinfo *brinfo;

	err = au_proc_getmntent(cwd, &ent);
	if (err)
		AuFin("no such mount point");
	if (hasmntopt(&ent, "noplink"))
		goto out; /* success */

	if (flags & AuPlinkFlag_OPEN) {
		p = hasmntopt(&ent, "si");
		if (!p)
			AuFin("no aufs mount point");
		strncpy(si, p, sizeof(si));
		p = strchr(si, ',');
		if (p)
			*p = 0;
		au_plink_maint(si, flags & AuPlinkFlag_CLOEXEC, fd);

		/* someone else may modify while we were sleeping */
		err = au_proc_getmntent(cwd, &ent);
		if (err)
			AuFin("no such mount point");
	}

	err = au_br(&brinfo, &nbr, cwd);
	if (err)
		AuFin(NULL);

	err = do_plink(cwd, cmd, nbr, brinfo);
	if (err)
		AuFin(NULL);
	free(brinfo);

	if (flags & AuPlinkFlag_CLOSE)
		au_plink_maint(NULL, 0, fd);

out:
	return err;
}
