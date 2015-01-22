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
 * aufs FHSM, the management by mount/umount helpers
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "au_util.h"

void mng_fhsm(char *cwd, int unmount)
{
	int err, nbr, nfhsm, status;
	union aufs_brinfo *brinfo;
	char *opt;
	pid_t pid, waited;

	opt = "--kill";
	err = au_br(&brinfo, &nbr, cwd);
	if (err)
		perror("au_br");
	nfhsm = au_nfhsm(nbr, brinfo);
	free(brinfo);
	if (!unmount) {
		if (nfhsm >= 2)
			opt = "--quiet";
	} else if (nfhsm < 2)
		return;

	pid = fork();
	if (!pid) {
		char *av[] = {basename(AUFHSM_CMD), opt, cwd, NULL};

#if 0
		int i;

		for (i = 0; av[i] && i < 4; i++)
			puts(av[i]);
		//return;
#endif
		execve(AUFHSM_CMD, av, environ);
		AuFin(__func__);
	} else if (pid > 0) {
		waited = waitpid(pid, &status, 0);
		if (waited == pid) {
			err = WEXITSTATUS(status);
			/* err = !WIFEXITED(status); */
			/* error msgs should be printed by the controller */
		} else {
			/* should not happen */
			err = -1;
			AuFin("waitpid");
		}
	} else if (!unmount)
		AuFin(__func__);
	else
		perror(__func__);
}
