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
 * aufs FHSM, events on the daemon
 */

#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

#include "daemon.h"
#include "log.h"

/*
 * make the Daemon's local copy of watermarks and other data sharable
 */
static_unless_ut
struct aufhsmd_comm *fhsmd_comm(void)
{
	struct aufhsmd_comm *p;
	int err, len, fd;
	char *name = fhsmd.name[AuName_FHSMD].a;

	len = au_fhsmd_comm_len(fhsmd.lcopy->nwmark);
	err = au_shm_create(name, len, &fd, &p);
	if (err)
		goto out;
	err = close(fd);
	if (err) {
		AuLogErr("close %s", name);
		goto out_unmap;
	}
	err = shm_unlink(name);
	if (err) {
		AuLogErr("unlink %s", name);
		goto out_unmap;
	}

	p->nstbr = fhsmd.lcopy->nwmark;
	/* todo: initial statfs */
	goto out; /* success */

out_unmap:
	err = munmap(p, len);
	if (err)
		AuLogErr("%s", name);
	p = NULL;
out:
	return p;
}

/*
 * load the Daemon's local copy of watermarks and other data
 */
int au_fhsmd_load(void)
{
	int err, len, n;
	aufhsm_msg_t msg;

	err = -1;
	n = 0;
	if (fhsmd.lcopy) {
		n = fhsmd.lcopy->nwmark;
		free(fhsmd.lcopy);
	}
	fhsmd.lcopy = au_fhsm_load(fhsmd.name[AuName_LCOPY].a);
	if (!fhsmd.lcopy)
		goto out;
	errno = 0;
	AuDbgFhsmLog("n %d --> %d", n, fhsmd.lcopy->nwmark);
	if (n > fhsmd.lcopy->nwmark) {
		AuLogWarn("unmatching watermarks. re-run aufhsm");
		goto out;
	}

	err = 0;
	msg = AuFhsm_MSG_NONE;
	if (fhsmd.comm) {
		msg = fhsmd.comm->msg;
		n = fhsmd.comm->nstbr;
		len = au_fhsmd_comm_len(n);
		err = munmap(fhsmd.comm, len);
		if (err) {
			AuLogErr("munmap");
			goto out;
		}
	}
	fhsmd.comm = fhsmd_comm();
	if (fhsmd.comm) {
		fhsmd.comm->msg = msg;
		goto out; /* success */
	}
	err = -1;

out:
	return err;
}

/*
 * read and handle a notification from aufs
 */
static_unless_ut
int handle_fhsm(void)
{
	int err, i, n, nstbr, len, me_again;
	ssize_t ssz;
	struct aufs_stbr *stbr, *next, *cur;

	err = 0;
read:
	stbr = fhsmd.comm->stbr;
	nstbr = fhsmd.comm->nstbr;
	len = nstbr * sizeof(*stbr);
	ssz = read(fhsmd.fd[AuFd_FHSM], stbr, len);
	AuDbgFhsmLog("ssz %zd", ssz);
	if (!ssz)
		goto out;

	if (ssz > 0) {
		errno = 0;
		n = ssz / sizeof(*stbr);
		assert(ssz == n * sizeof(*stbr));
		for (i = 0; !err && i < n; i++, stbr++) {
			me_again = 0;
			cur = stbr;
			while (1) {
				err = au_mvdown_run(cur, &next);
				if (err || !next)
					break; /* inner while-loop */
				assert(next != cur);
				cur = next;
				me_again = 1;
			}
			if (!err && me_again) {
				/* process the same branch again */
				stbr--;
				i--;
			}
		}
		goto out;
	}

	err = -1;
	switch (errno) {
	case EMSGSIZE:
		/* more branches */
		err = au_fhsmd_load();
		if (!err)
			goto read; /* again */
		break;
	default:
		AuLogErr("AuFd_FHSM");
		//??
	}

out:
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * read and handle a message from the controller
 */
static_unless_ut
int handle_msg(int *msg_exit)
{
	int err;
	ssize_t ssz;
	aufhsm_msg_t msg;

	err = 0;
	*msg_exit = 0;
	ssz = read(fhsmd.fd[AuFd_MSG], &msg, sizeof(msg));
	if (ssz == -1) {
		if (errno == EAGAIN)
			goto out; /* message is not sent, shoud not stop */

		/* should not happen */
		err = -1;
		AuLogErr("AuFd_MSG");
		goto out;
	}

	AuDbgFhsmLog("Got message %d", msg);
	switch (msg) {
	case AuFhsm_MSG_READ:
		err = au_fhsmd_load();
		break;
	case AuFhsm_MSG_EXIT:
		fhsmd.comm->msg = msg;
		*msg_exit = 1;
		break;
	default:
		/* should not happen */
		err = -1;
		AuLogErr("msg %d", msg);
	}

out:
	return err;
}

/*
 * read and handle a signal from user
 */
static_unless_ut
int handle_sig(int *status)
{
	int err, found;
	struct signalfd_siginfo ssi;
	ssize_t ssz;
	pid_t pid;
	struct in_ope *in_ope;

	err = 0;
	ssz = read(fhsmd.fd[AuFd_SIGNAL], &ssi, sizeof(ssi));
	if (ssz == -1) {
		if (errno == EAGAIN)
			goto out; /* signal is not sent, shoud not stop */

		/* should not happen */
		err = -1;
		AuLogErr("AuFd_SIGNAL");
		goto out;
	}
	AuDbgFhsmLog("[%d] got signal %u, pid %d, status %d",
		     getpid(), ssi.ssi_signo, ssi.ssi_pid, ssi.ssi_status);

	err = ssi.ssi_signo;
	switch (ssi.ssi_signo) {
	case SIGCHLD:
		found = 0;
		list_for_each_entry(in_ope, &fhsmd.in_ope, list) {
			if (in_ope->pid == ssi.ssi_pid) {
				list_del(&in_ope->list);
				free(in_ope);
				found = 1;
				break;
			}
		}
		if (!found)
			AuLogErr("unknown child [%d]", ssi.ssi_pid);

		pid = waitpid(ssi.ssi_pid, status, 0);
		if (pid != ssi.ssi_pid) {
			/* should not happen */
			err = -1;
			AuLogErr("pid %d, %d", pid, ssi.ssi_pid);
		}
		/*FALLTHROUGH*/
	case SIGHUP:
		/* simply ignore */
		err = 0;
		break;
	default:
		/* signal is sent, should stop */
		break;
	}

out:
	return err;
}

static_unless_ut
int sigfd(void)
{
	int err, i;
	int sig[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGCHLD};
	sigset_t mask;

	sigemptyset(&mask);
	for (i = 0; i < sizeof(sig) / sizeof(*sig); i++)
		sigaddset(&mask, sig[i]);
	err = sigprocmask(SIG_BLOCK, &mask, NULL);
	if (!err) {
		err = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
		if (err < 0)
			AuLogErr("signalfd");
	} else
		AuLogErr("sigprocmask");

	return err;
}

int au_ep_add(int fd, uint32_t event)
{
	int err;
	struct epoll_event ev = {
		.events		= event,
		.data.fd	= fd
	};

	err = epoll_ctl(fhsmd.fd[AuFd_EPOLL], EPOLL_CTL_ADD, fd, &ev);
	if (err)
		AuLogErr("EPOLL_CTL_ADD");
	return err;
}

int au_epsigfd(void)
{
	int err;

	fhsmd.fd[AuFd_SIGNAL] = sigfd();
	err = fhsmd.fd[AuFd_SIGNAL];
	if (fhsmd.fd[AuFd_SIGNAL] < 0)
		goto out;

	fhsmd.fd[AuFd_EPOLL] = epoll_create1(EPOLL_CLOEXEC);
	err = fhsmd.fd[AuFd_EPOLL];
	if (fhsmd.fd[AuFd_EPOLL] < 0) {
		AuLogErr("epoll_create1");
		goto out_sigfd;
	}

	err = au_ep_add(fhsmd.fd[AuFd_SIGNAL], EPOLLIN | EPOLLPRI);
	if (!err)
		goto out; /* success */

	/* revert */
	if (close(fhsmd.fd[AuFd_EPOLL]))
		AuLogErr("close");

out_sigfd:
	if (close(fhsmd.fd[AuFd_SIGNAL]))
		AuLogErr("close");
out:
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * event loop
 */
#define EVENTS 3 /* fhsmfd, sigfd, and msgfd */
int au_fhsmd_loop(void)
{
	int err, done, nev, i, sig, status, msg_exit;
	struct epoll_event events[EVENTS];

	err = 0;
	done = 0;
	while (!done || !list_empty(&fhsmd.in_ope)) {
		nev = epoll_wait(fhsmd.fd[AuFd_EPOLL], events, EVENTS, -1);
		if (nev < 0) {
			if (errno == EINTR)
				continue; //??
			AuLogFin("epoll_wait"); //??
			break;
		}
		for (i = 0; i < nev; i++) {
			if (events[i].data.fd == fhsmd.fd[AuFd_SIGNAL]) {
				sig = handle_sig(&status);
				if (sig == SIGCHLD) {
					err = WEXITSTATUS(status);
					if (err)
						AuLogInfo("child status %d",
							  err);
				} else if (sig)
					done = 1;
				AuDbgFhsmLog("sig %d, done %d", sig, done);
			} else if (events[i].data.fd == fhsmd.fd[AuFd_MSG]) {
				err = handle_msg(&msg_exit);
				if (!err && msg_exit) {
					done = 1;
					AuDbgFhsmLog("done %d", done);
				}
			} else if (events[i].data.fd == fhsmd.fd[AuFd_FHSM]) {
				if (!done) {
					err = handle_fhsm();
					if (err) {
						done = 1;
						AuDbgFhsmLog("done %d", done);
					}
				}
			} else {
				errno = ENOSYS;
				err = errno;
				AuLogFin("internal error, %p",
					 events[i].data.ptr);
			}
		}
	}

	return err;
}
