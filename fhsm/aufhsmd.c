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
 * aufs FHSM, the daemon
 */

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include "../au_util.h"
#include "daemon.h"
#include "log.h"

struct aufhsmd fhsmd;

/* ---------------------------------------------------------------------- */

static void usage(void)
{
	fprintf(stderr, "Usage: %s [ options ] aufs_mount_point\n"
		"\t-d DIR | --dir DIR\t\tuse DIR to store the aufhsm file-list\n"
		"\t-v | --verbose\n"
		"\t-V | --version\n"
		"\t-h | --help\n"
		"Instead of '-d', you can use AUFHSM_LIST_DIR\n",
		program_invocation_short_name);
}

static struct option opts[] = {
	{"dir",		required_argument,	NULL, 'd'},
	{"verbose",	no_argument,		NULL, 'v'},

	/* hidden */
	{"no-daemon",	no_argument,		NULL, 'D'},

	/* as usual */
	{"version",	no_argument,		NULL, 'V'},
	{"help",	no_argument,		NULL, 'h'},
	{NULL,		no_argument,		NULL, 0}
};
static const char short_opts[] = "d:v" "D" "Vh";

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
		case 'd':
			dir = optarg;
			break;
		case 'v':
			au_opt_set(fhsmd.optflags, VERBOSE);
			break;

		case 'D':
			au_opt_set(fhsmd.optflags, NODAEMON);
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

static void shm_names(char *path, struct au_name name[], unsigned int nlen)
{
	int err;
	char *p;

	if (path) {
		err = open(path, O_RDONLY);
		if (err < 0)
			AuLogFin("open %s", path);
		fhsmd.fd[AuFd_ROOT] = err;
	}
	err = au_shm_name(fhsmd.fd[AuFd_ROOT], fhsmd.name[AuName_FHSMD].a,
			  nlen);
	if (err)
		AuLogFin("au_shm_name");
	strcpy(fhsmd.name[AuName_LCOPY].a, fhsmd.name[AuName_FHSMD].a);
	p = fhsmd.name[AuName_LCOPY].a + sizeof("aufhsm");
	memmove(p, p + 1, strlen(p)); /* including the terminator */
}

static void comm_fd(char *name, int *fhsmfd, int *msgfd)
{
	*fhsmfd = ioctl(fhsmd.fd[AuFd_ROOT], AUFS_CTL_FHSM_FD,
			O_CLOEXEC | O_NONBLOCK);
	if (*fhsmfd < 0)
		AuLogFin("AUFS_CTL_FHSM_FD");

	*msgfd = au_fhsm_msg(name, AuFhsm_MSG_NONE, /*rootfd*/-1);
	if (*msgfd < 0)
		AuLogFin("msg %s", name);
}

/* ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	int err;
	char *mntpnt;

	opt(argc, argv);
	if (optind == argc) {
		//usage();
		errno = EINVAL;
		AuFin(NULL);
	}

	mntpnt = realpath(argv[optind], NULL);
	if (!mntpnt)
		AuFin("%s", mntpnt);
	err = chdir(mntpnt);
	if (err)
		AuFin("%s", mntpnt);

	if (!au_opt_test(fhsmd.optflags, NODAEMON)) {
		au_do_syslog = 1;
		openlog(program_invocation_short_name, AuFhsmd_OPTION,
			AuFhsmd_FACILITY);
		err = daemon(/*nochdir*/0, /*noclose*/0);
		if (err)
			AuFin("daemon");
	}

	shm_names(mntpnt, fhsmd.name, sizeof(*fhsmd.name));
	free(mntpnt);
	comm_fd(fhsmd.name[AuName_LCOPY].a, &fhsmd.fd[AuFd_FHSM],
		&fhsmd.fd[AuFd_MSG]);
	INIT_LIST_HEAD(&fhsmd.in_ope);
	err = au_fhsmd_load();
	if (err)
		AuLogFin("au_fhsmd_load");

	err = au_epsigfd();
	if (err)
		AuLogFin("create_epsig");
	err = au_ep_add(fhsmd.fd[AuFd_MSG], EPOLLIN | EPOLLPRI);
	if (err)
		AuLogFin("au_ep_add");
	err = au_ep_add(fhsmd.fd[AuFd_FHSM], EPOLLIN | EPOLLPRI);
	if (err)
		AuLogFin("au_ep_add");

	/* main loop */
	err = au_fhsmd_loop();

	AuDbgFhsmLog("exit %d", err);
	return err;
}
