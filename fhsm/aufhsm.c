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

/*
 * aufs FHSM, the controller
 */

#include <sys/mman.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <mntent.h>
#include <unistd.h>
#include <linux/aufs_type.h>

#include "../au_util.h"
#include "comm.h"
#include "log.h"

static void usage(void)
{
	fprintf(stderr, "Usage: %s [ options ] aufs_mount_point "
		"[ BranchPath=UPPER-LOWER ... ] | [ UPPER-LOWER ]\n"
		"\t-i | --inode\t\tset watermark for inode\n"
		"\t-d DIR | --dir DIR\t\tuse DIR to store the aufhsm file-list\n"
		"\t-r | --recreate\t\trecreate the data file\n"
		"\t-k | --kill\t\tkill aufhsmd\n"
		"\t-q | --quiet\n"
		"\t-v | --verbose\n"
		"\t-V | --version\n"
		"\t-h | --help\n"
		"Instead of '-d', you can use AUFHSM_LIST_DIR\n",
		program_invocation_short_name);
}

enum {
	OptFhsm_INODE,
	OptFhsm_RECREATE,
	OptFhsm_KILL,
	OptFhsm_QUIET,
	OptFhsm_VERBOSE
};

static struct option opts[] = {
	{"inode",	no_argument,		NULL, 'i'},
	{"dir",		required_argument,	NULL, 'd'},
	{"recreate",	no_argument,		NULL, 'r'},
	{"kill",	no_argument,		NULL, 'k'},
	{"quiet",	no_argument,		NULL, 'q'},
	{"verbose",	no_argument,		NULL, 'v'},

	/* as usual */
	{"version",	no_argument,		NULL, 'V'},
	{"help",	no_argument,		NULL, 'h'},
	{NULL,		no_argument,		NULL, 0}
};
static const char short_opts[] = "id:rkqv" "Vh";
static unsigned long optflags;

#define opt_set(f, name)	(f) |= 1 << OptFhsm_##name
#define opt_clr(f, name)	(f) &= ~(1 << OptFhsm_##name)
#define opt_test(f, name)	((f) & (1 << OptFhsm_##name))

static void opt(int argc, char *argv[])
{
	int opt, i, err, need_ck, done;
	char *dir, *p;

	done = 0;
	dir = getenv("AUFHSM_LIST_DIR");
	for (i = 1; !done && i < argc; i++) {
		opt = getopt_long(argc, argv, short_opts, opts, NULL);
		switch (opt) {
		case -1:
			done = 1;
			break;
		case 'i':
			opt_set(optflags, INODE);
			break;
		case 'd':
			dir = optarg;
			break;
		case 'r':
			opt_set(optflags, RECREATE);
			break;
		case 'k':
			opt_set(optflags, KILL);
			break;
		case 'q':
			opt_set(optflags, QUIET);
			break;
		case 'v':
			opt_set(optflags, VERBOSE);
			break;
		case 'V':
			printf("%s version %s\n",
			       program_invocation_short_name, AuVersion);
			exit(0);
		case 'h':
		case '?':
			usage();
			exit(0);
		default:
			//usage();
			exit(EINVAL);
		}
	}

	if (dir) {
		p = realpath(dir, NULL);
		if (!p || !*p)
			AuFin("%s", dir);
		dir = strdup(p);
		if (!dir)
			AuFin("%s", p);
		need_ck = 1;
	} else {
		dir = au_list_dir_def();
		if (!dir)
			AuFin("au_list_dir_def");
		need_ck = 0;
	}
	err = au_list_dir_set(dir, need_ck);
	if (err)
		AuFin("au_list_dir_set");
	/* unfree dir */
}

/* ---------------------------------------------------------------------- */

/*
 * Set the watermarks to 'wm'.
 */
static void do_wmark(struct aufhsm_wmark *wm, char *str)
{
	int err;
	float a[AuFhsm_WM_Last];

	err = sscanf(str, "%f-%f", a + AuFhsm_WM_UPPER, a + AuFhsm_WM_LOWER);
	if (err != 2
	    || a[AuFhsm_WM_UPPER] > 100 || a[AuFhsm_WM_UPPER] < 0
	    || a[AuFhsm_WM_LOWER] > 100 || a[AuFhsm_WM_LOWER] < 0
	    || a[AuFhsm_WM_UPPER] < a[AuFhsm_WM_LOWER]) {
		errno = EINVAL;
		AuFin("%s", str);
	}

	/* free ratio */
	a[AuFhsm_WM_UPPER] = (100 - a[AuFhsm_WM_UPPER]) / 100;
	a[AuFhsm_WM_LOWER] = (100 - a[AuFhsm_WM_LOWER]) / 100;
	if (!opt_test(optflags, INODE))
		memcpy(wm->block, a, sizeof(wm->block));
	else
		memcpy(wm->inode, a, sizeof(wm->inode));
}

static void wmark(char *str, struct aufhsm *fhsm, int nbr,
		  union aufs_brinfo *brinfo)
{
	int i, nwmark;
	char *p;
	struct aufhsm_wmark *wm;

	wm = NULL;
	p = strrchr(str, '=');
	if (p) {
		*p = '\0';
		brinfo = au_br_search_path(str, nbr, brinfo);
		if (brinfo) {
			wm = au_wm_lfind(brinfo->id, fhsm->wmark, fhsm->nwmark);
			if (wm)
				do_wmark(wm, p + 1);
		} else {
			errno = 0;
			AuFin("no such branch, %s", str);
		}
	} else {
		nwmark = fhsm->nwmark;
		wm = fhsm->wmark;
		do_wmark(wm, str);
		if (!opt_test(optflags, INODE))
			for (i = 1; i < nwmark; i++)
				memcpy(wm[i].block, wm[i - 1].block,
				       sizeof(wm->block));
		else
			for (i = 1; i < nwmark; i++)
				memcpy(wm[i].inode, wm[i - 1].inode,
				       sizeof(wm->inode));
	}
}

/* ---------------------------------------------------------------------- */

static int au_run_fhsmd(char *mntpnt, int verbose)
{
	int err, waited, status, i;
	char *av[6];
	pid_t pid;

	i = 0;
	av[i++] = basename(AUFHSMD_CMD);
	av[i++] = "--dir";
	av[i++] = au_list_dir();
	if (verbose)
		av[i++] = "--verbose";
	av[i++] = mntpnt;
	av[i] = NULL;
	assert(i < sizeof(av) / sizeof(*av));

	pid = fork();
	if (!pid) {
#if 0
		int i;

		for (i = 0; av[i]; i++)
			puts(av[i]);
		//return;
#endif
		execve(AUFHSMD_CMD, av, environ);
		AuFin(AUFHSMD_CMD);
	} else if (pid > 0) {
		waited = waitpid(pid, &status, 0);
		if (waited == pid) {
			err = WEXITSTATUS(status);
			/* error msgs should be printed by the controller */
		} else {
			/* should not happen */
			err = -1;
			AuLogErr("waitpid");
		}
	} else {
		err = pid;
		AuLogErr("fork");
	}

	return err;
}

int main(int argc, char *argv[])
{
	int err, nbr, nfhsm, rootfd, i, do_notify, shmfd;
	struct statfs stfs;
	char name[32];
	struct aufhsm *fhsm;
	char *mntpnt;
	union aufs_brinfo *brinfo, *sorted;

	do_notify = 0;
	/* better to test the capability? */
	if (getuid()) {
		errno = EPERM;
		AuFin(NULL);
	}

	opt(argc, argv);
	if (optind == argc) {
		usage();
		errno = EINVAL;
		AuFin(NULL);
	}

	mntpnt = realpath(argv[optind], NULL);
	if (!mntpnt)
		AuFin("%s", mntpnt);

	rootfd = open(mntpnt, O_RDONLY | O_CLOEXEC /* | O_PATH */);
	if (rootfd < 0)
		AuFin("%s", mntpnt);
	err = fstatfs(rootfd, &stfs);
	if (err)
		AuFin("%s", mntpnt);
	if (stfs.f_type != AUFS_SUPER_MAGIC) {
		errno = EINVAL;
		AuFin("%s is not aufs (0x%lx)", mntpnt, (long)stfs.f_type);
	}

	err = au_shm_name(rootfd, name, sizeof(name));
	if (err)
		AuFin("au_shm_name");
	if (opt_test(optflags, RECREATE)) {
		do_notify = 1;
		if (shm_unlink(name) && errno != ENOENT)
			AuWarn("%s, %m", name);
	} else if (opt_test(optflags, KILL)) {
		if (au_fhsm_msg(name, AuFhsm_MSG_EXIT, rootfd))
			AuWarn("%s, %m", name);
		err = 0;
		errno = 0;
		goto out;
	}

	err = au_br(&brinfo, &nbr, mntpnt);
	if (err)
		goto out;
	nfhsm = au_nfhsm(nbr, brinfo);
	if (nfhsm < 2) {
		errno = EINVAL;
		AuFin("few fhsm branches for %s", mntpnt);
	}

	/* shmfd will be locked */
	err = au_fhsm(name, nfhsm, nbr, brinfo, &shmfd, &fhsm);
	if (err) {
		AuWarn("au_fhsm, %m");
		goto out;
	}

	/* set the watermarks */
	sorted = calloc(nbr, sizeof(*brinfo));
	if (!sorted)
		AuFin("calloc");
	memcpy(sorted, brinfo, nbr * sizeof(*brinfo));
	au_br_sort_path(nbr, sorted);

	for (i = optind + 1; i < argc; i++) {
		wmark(argv[i], fhsm, nbr, sorted);
		do_notify = 1;
	}

	if (!opt_test(optflags, QUIET))
		au_fhsm_dump(mntpnt, fhsm, brinfo, nbr);
	free(brinfo);
	free(sorted);

	au_fhsm_sign(fhsm);

	/* shmfd will be unlocked */
	err = close(shmfd);
	if (err) {
		AuWarn("close, %m");
		goto out;
	}

	if (do_notify)
		au_fhsm_msg(name, AuFhsm_MSG_READ, /*rootfd*/-1);
	err = au_run_fhsmd(mntpnt, opt_test(optflags, VERBOSE));

out:
	return err;
}
