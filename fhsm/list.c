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
 * aufs FHSM, functions to handle the list
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "comm.h"
#include "log.h"

/*
 * for those who don't want to consume tmpfs, the directory to place several
 * (possibly large) file-list can be customized.
 */

static char *list_dir;

int au_list_dir_set(char *dir, int need_ck)
{
	int err;
	struct stat st;

	errno = EINVAL;
	err = -1;
	if (!dir || !*dir) {
		AuLogErr("internal error, empty string is not allowed");
		goto out;
	}

	err = 0;
	if (!need_ck)
		goto out_set;

	err = access(dir, R_OK | W_OK | X_OK);
	if (err) {
		AuLogErr("%s", dir);
		goto out;
	}
	err = stat(dir, &st);
	if (err) {
		AuLogErr("%s", dir);
		goto out;
	}
	if (!S_ISDIR(st.st_mode)) {
		err = -1;
		errno = ENOTDIR;
		AuLogErr("%s", dir);
	}

out_set:
	list_dir = dir;
out:
	return err;
}

char *au_list_dir(void)
{
	return list_dir;
}

char *au_list_dir_def(void)
{
	char *dir, *name = "/aufs-dummy";
	int err, fd;

	dir = NULL;
	fd = shm_open(name, O_RDWR | O_CREAT | O_CLOEXEC,
		      S_IRUSR | S_IWUSR);
	if (fd < 0) {
		AuLogErr("shm_open");
		goto out;
	}
	dir = au_shm_dir(fd);

	/* always remove */
	err = shm_unlink(name);
	if (err) {
		AuLogErr("shm_unlink");
		free(dir);
		dir = NULL;
	}

out:
	return dir;
}

/* ---------------------------------------------------------------------- */

/*
 * Move the contents of failfd to listfd.
 */
static_unless_ut
int move_failed(int listfd, int failfd)
{
	int err, left, l;
	ssize_t ssz;
	off_t off;
	struct stat st;
	char *o, *src, *rev, *tgt, *succeeded;

	err = fstat(failfd, &st);
	if (err) {
		AuLogErr("fstat");
		goto out;
	}
	if (!st.st_size)
		goto out; /* success */

	o = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, failfd, 0);
	if (o == MAP_FAILED) {
		err = -1;
		AuLogErr("mmap");
		goto out;
	}

	rev = malloc(st.st_size + 1);
	if (!rev) {
		err = -1;
		AuLogErr("malloc, %llu", (unsigned long long)st.st_size);
		goto out_unmap;
	}

	tgt = rev;
	*tgt++ = '\0';
	left = st.st_size - 1;
	src = o;
	while (left > 0) {
		src = memrchr(o, '\0', left);
		if (src)
			src++;
		else
			src = o;
		l = strlen(src) + 1; /* including the terminator */
		memcpy(tgt, src, l);
		tgt += l;
		left -= l;
	}
	if (tgt != rev + st.st_size + 1)
		AuLogFin("internal error, tgt %p, rev %p, sz %llu",
			 tgt, rev, (unsigned long long)st.st_size);

	ssz = write(listfd, rev, st.st_size + 1);
	if (ssz != st.st_size + 1)
		goto out_ssz;

	err = ftruncate(failfd, 0);
	if (err)
		/* should not happen */
		AuLogErr("ftruncate");
	goto out_free;

out_ssz:
	err = -1;
	AuLogErr("failed moving failfd %llu, %zd",
		 (unsigned long long)st.st_size, ssz);
	if (ssz > 0) {
		/* wrote partially */
		succeeded = memrchr(rev + ssz, '\0', ssz);
		off = lseek(failfd, SEEK_END, -(rev + ssz - succeeded));
		if (off != -1) {
			if (ftruncate(listfd, off)) {
				/* should not happen */
				AuLogErr("ftruncate, %llu",
					 (unsigned long long)off);
			}
		} else {
			/* should not happen */
			AuLogErr("SEEK_END, %llu",
				 (unsigned long long)(rev + ssz - succeeded));
		}
	}
out_free:
	free(rev);
out_unmap:
	if (munmap(o, st.st_size))
		AuLogErr("munmap");
out:
	return err;
}

/*
 * if any signal is sent to the aufhsm-list process and killed, the result
 * list-file may be incomplete, which leads FHSM to inefficient behaviour.
 * in this case, the user should remove the list-file under /dev/shm manually.
 */
static_unless_ut
int run_cmd(int brfd, char *dir, char *name)
{
	int err, status;
	pid_t pid, waited;
	sigset_t new, old;
	char *av[] = {basename(AUFHSM_LIST_CMD), dir, name, NULL};

	sigemptyset(&new);
	sigaddset(&new, SIGCHLD);
	err = sigprocmask(SIG_UNBLOCK, &new, &old);
	if (err) {
		AuLogErr("sigprocmask");
		goto out;
	}

	pid = fork();
	if (!pid) {
		/* child */
		err = fchdir(brfd);
		if (!err) {
			execve(AUFHSM_LIST_CMD, av, environ);
			AuLogFin("aufhsm-list");
		} else
			AuLogFin("fchdir");
	} else if (pid > 0) {
		waited = waitpid(pid, &status, 0);
		if (waited == pid)
			err = WEXITSTATUS(status);
		else {
			/* should not happen */
			err = -1;
			AuLogErr("waitpid");
		}
	} else {
		err = pid;
		AuLogErr("fork");
	}

	if (sigprocmask(SIG_BLOCK, &old, NULL))
		AuLogErr("sigprocmask");

out:
	return err;
}

int au_list(int brfd, int *listfd, int *failfd)
{
	int err, e, l, dirfd;
	char name[64], *dir;
	struct stat st;

	/* while their name contain 'shm', the file is not shared actually */
	err = au_shm_name(brfd, name, sizeof(name));
	if (err)
		goto out;

	dir = au_list_dir();
	if (!dir) {
		AuLogErr("internal error, list dir is empty");
		goto out;
	}
	dirfd = open(dir, O_RDONLY | O_PATH);
	if (dirfd < 0) {
		AuLogErr("%s", dir);
		goto out;
	}
	*listfd = openat(dirfd, name + 1,
			 O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC,
			 S_IRUSR | S_IWUSR);
	err = *listfd;
	if (err < 0) {
		AuLogErr("%s/%s", dir, name + 1);
		goto out_dirfd;
	}

	l = strlen(name);
	strcpy(name + l, "-failed");
	*failfd = openat(dirfd, name + 1,
			 O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC,
			 S_IRUSR | S_IWUSR);
	err = *failfd;
	if (err < 0) {
		AuLogErr("%s/%s", dir, name + 1);
		goto out_listfd;
	}

	/*
	 * if the list generated previously is not processed entirely,
	 * then continue it.
	 * but the previously failed files should be tried first.
	 */
	err = fstat(*listfd, &st);
	if (err) {
		AuLogErr("fstat");
		goto out_failfd;
	}
	if (st.st_size) {
		/* the list-file is not emptry, re-use it */
		err = move_failed(*listfd, *failfd);
		if (!err)
			goto out; /* success */
		goto out_failfd;
	}

	/* the list-file is emptry, re-generate it */
	name[l] = '\0';
	err = run_cmd(brfd, dir, name + 1);
	if (!err)
		goto out_dirfd; /* success */

out_failfd:
	e = errno;
	if (close(*failfd))
		AuLogErr("close");
	errno = e;
out_listfd:
	e = errno;
	if (close(*listfd))
		AuLogErr("close");
	errno = e;
out_dirfd:
	e = errno;
	if (close(dirfd))
		AuLogErr("close");
	errno = e;
out:
	return err;
}

/* get a single filename (from listfd) */
void au_fname_one(char *o, off_t len, struct au_fname *fname)
{
	char *end;

	fname->atime = memrchr(o, 0, len - 1);
	if (fname->atime) {
		end = o + len;
		fname->len = end - fname->atime - 1;
		fname->atime++;
	} else {
		fname->len = len;
		fname->atime = o;
	}
	fname->sz = strchr(fname->atime, ' ');
	if (!fname->sz)
		AuLogFin("%s", fname->atime);
	fname->sz++;
	fname->name = strchr(fname->sz, ' ');
	if (!fname->name)
		AuLogFin("%s", fname->atime);
	fname->name++;
}

/* store the filename to failfd */
int au_fname_failed(struct au_fname *fname, int failfd)
{
	int err, l, e;
	ssize_t ssz;
	off_t off;

	/* AuDbgFhsmLog("%s", fname->atime); */

	err = 0;
	l = strlen(fname->atime) + 1; /* including the terminator */
	ssz = write(failfd, fname->atime, l);
	if (ssz == l)
		goto out; /* success */

	err = -1;
	e = errno;
	AuLogInfo("failed appending %s (%zd), skipped", fname->atime, ssz);
	if (ssz > 0) {
		/* wrote partially */
		off = lseek(failfd, SEEK_END, -ssz);
		if (off != -1) {
			if (ftruncate(failfd, off)) {
				/* should not happen */
				AuLogErr("ftruncate, %llu",
					 (unsigned long long)off);
			}
		} else {
			/* should not happen */
			AuLogErr("lseek SEEK_END, %zd", -ssz);
		}
	}
	errno = e;

out:
	return err;
}
