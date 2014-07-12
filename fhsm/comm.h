/*
 * Copyright (C) 2011-2014 Junjiro R. Okajima
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
 * aufs FHSM, global declarations, common to the controller and the daemon
 */

#ifndef AuFhsm_COMMON_H
#define AuFhsm_COMMON_H

#include <fcntl.h>
#include <stdlib.h>

#ifndef O_PATH
#define O_PATH		010000000
#endif

#ifdef AUFHSM_UT
#define static_unless_ut
#else
#define static_unless_ut	static
#endif

/* ---------------------------------------------------------------------- */

/* the format of single line in the list */
struct au_fname {
	char	*atime;
	char	*sz;
	char	*name;
	int	len;
};

/* ---------------------------------------------------------------------- */

/* list.c */
int au_list_dir_set(char *dir, int need_ck);
char *au_list_dir(void);
int au_fname_failed(struct au_fname *fname, int failfd);
void au_fname_one(char *o, off_t len, struct au_fname *fname);
int au_list(int brfd, int *listfd, int *failfd);
char *au_list_dir_def(void);

/* shm.c */
int au_shm_name(int rootfd, char name[], int sz);
char *au_shm_dir(int fd);

#endif /* AuFhsm_COMMON_H */
