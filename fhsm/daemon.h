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
 * aufs FHSM, theDaemon specific declarations
 */

#ifndef AuFhsm_DAEMON_H
#define AuFhsm_DAEMON_H

#include <linux/aufs_type.h>

#include "comm.h"
#include "linux-list.h"

enum {
	AuName_LCOPY,	/* local copy of watermarks */
	AuName_FHSMD,
	AuName_Last
};

/* name of POSIX shared memory */
struct au_name {
	char a[32];
};

/* POSIX shared memory for parent and child */
struct aufhsmd_comm {
	aufhsm_msg_t		msg;

	int			nstbr;
	struct aufs_stbr	stbr[0];
};

enum {
	AuFd_ROOT,
	AuFd_FHSM,
	AuFd_SIGNAL,
	AuFd_EPOLL,
	AuFd_MSG,
	AuFd_Last
};

/*
 * global variables in the daemon.
 * maintained by parent. readonly for child.
 */
struct aufhsmd {
	/* local copy of watermarks (free ratio), sorted by brid */
	struct aufhsm		*lcopy;

	struct aufhsmd_comm	*comm;
	int			fd[AuFd_Last];
	unsigned long		optflags;
	struct au_name		name[AuName_Last];

	/* in move-down operation */
	struct list_head	in_ope;
};

extern struct aufhsmd fhsmd;

struct in_ope {
	struct list_head	list;
	int16_t			brid;
	pid_t			pid;
};

/* ---------------------------------------------------------------------- */

/* command line options for the daemon */
enum {
	OptFhsmd_NODAEMON,
	OptFhsmd_VERBOSE
};

#define au_opt_set(f, name)	(f) |= 1 << OptFhsmd_##name
#define au_opt_clr(f, name)	(f) &= ~(1 << OptFhsmd_##name)
#define au_opt_test(f, name)	((f) & (1 << OptFhsmd_##name))

/* ---------------------------------------------------------------------- */

/* event.c */
int au_fhsmd_load(void);
int au_epsigfd(void);
int au_ep_add(int fd, uint32_t event);
int au_fhsmd_loop(void);

/* mvdown.c */
int au_mvdown_run(struct aufs_stbr *cur, struct aufs_stbr **next);

/* ---------------------------------------------------------------------- */

static inline off_t au_fhsmd_comm_len(int nstbr)
{
	return sizeof(*fhsmd.comm) * sizeof(*fhsmd.comm->stbr) * nstbr;
}

#endif /* AuFhsm_DAEMON_H */
