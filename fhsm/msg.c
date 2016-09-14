/*
 * Copyright (C) 2011-2016 Junjiro R. Okajima
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
 * aufs FHSM, messaging between the controller and the daemon
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/aufs_type.h>

#include "comm.h"
#include "log.h"

/* wait for the daemon terminate */
static void wait_for_terminate(int rootfd)
{
	int fhsmfd;
	struct timespec ts;
	time_t until;

	until = time(NULL) + 15;
	do {
		fhsmfd = ioctl(rootfd, AUFS_CTL_FHSM_FD, /*oflags*/0);
		if (fhsmfd > 0) {
			close(fhsmfd);
			break;
		} else if (fhsmfd == -1
			   && (errno == EOPNOTSUPP || errno == EPERM))
			break;
		ts.tv_sec = 0;
		ts.tv_nsec = 100 * 1000;
		nanosleep(&ts, NULL);
	} while (until > time(NULL));
}

/* messaging between the controller and daemon */
int au_fhsm_msg(char *name, aufhsm_msg_t msg, int rootfd)
{
	int err, fd;
	ssize_t ssz;
	char a[64], *dir;

	dir = au_list_dir();
	if (!dir) {
		AuLogErr("internal error, au_list_dir");
		err = -1;
		goto out;
	}
	err = snprintf(a, sizeof(a), "%s%s.msg", dir, name);
	if (err >= sizeof(a)) {
		AuLogErr("internal error, %d", err);
		err = -1;
		goto out;
	}
	err = mknod(a, S_IFIFO | S_IRUSR | S_IWUSR, /*dev*/0);
	if (err && errno != EEXIST) {
		AuLogErr("%s", a);
		goto out;
	}
	fd = open(a, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	err = fd;
	if (fd < 0) {
		AuLogErr("%s", a);
		goto out; /* do not unlink the fifo */
	}
	if (msg == AuFhsm_MSG_NONE)
		goto out; /* success, keep 'fd' opened and return it */

	ssz = write(fd, &msg, sizeof(msg));
	err = (ssz != sizeof(msg));
	if (err) {
		AuLogErr("%s, %zd", a, ssz);
		goto out;
	}
	if (msg == AuFhsm_MSG_EXIT && rootfd >= 0)
		wait_for_terminate(rootfd);
	if (close(fd)) {
		err = -1;
		AuLogErr("close");
	}
	/* do not unlink the fifo */

out:
	return err;
}
