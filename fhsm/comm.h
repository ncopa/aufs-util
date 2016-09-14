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
 * aufs FHSM, global declarations, common to the controller and the daemon
 */

#ifndef AuFhsm_COMMON_H
#define AuFhsm_COMMON_H

#include <fcntl.h>
#include <stdint.h>	/* musl libc */
#include <stdlib.h>
#include <string.h>

#ifndef O_PATH
#define O_PATH		010000000
#endif

#define AUFHSM_MAGIC	"AUFHSM"

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

/* messages between the controller and daemon */
typedef enum {
	AuFhsm_MSG_NONE,
	AuFhsm_MSG_READ,
	AuFhsm_MSG_EXIT
} aufhsm_msg_t;

/* watermark */
enum {
	AuFhsm_WM_UPPER,
	AuFhsm_WM_LOWER,
	AuFhsm_WM_Last
};

struct aufhsm_wmark {
	/* branch id */
	int16_t	brid;

	/* free ratio */
	float	block[AuFhsm_WM_Last];
	float	inode[AuFhsm_WM_Last];
};

/* contents of shm */
struct aufhsm {
	char			magic[8];
	unsigned int		csum;

	int			nwmark;
	struct aufhsm_wmark	wmark[0];
};

/* ---------------------------------------------------------------------- */

/* fhsm.c */
union aufs_brinfo;
int au_fhsm(char *name, int nfhsm, int nbr, union aufs_brinfo *brinfo,
	    int *rshmfd, struct aufhsm **rfhsm);
unsigned int au_fhsm_csum(struct aufhsm *fhsm);
void au_fhsm_dump(char *mntpnt, struct aufhsm *fhsm, union aufs_brinfo *brinfo,
		  int nbr);
struct aufhsm *au_fhsm_load(char *name);

/* list.c */
int au_list_dir_set(char *dir, int need_ck);
char *au_list_dir(void);
int au_fname_failed(struct au_fname *fname, int failfd);
void au_fname_one(char *o, off_t len, struct au_fname *fname);
int au_list(int brfd, int *listfd, int *failfd);
char *au_list_dir_def(void);

/* msg.c */
int au_fhsm_msg(char *name, aufhsm_msg_t msg, int rootfd);

/* shm.c */
int au_shm_name(int rootfd, char name[], int sz);
char *au_shm_dir(int fd);
int au_shm_create(char *name, off_t len, int *rfd, void *_p);
int au_shm_map(char *name, int *rfd, void *_p);

/* ---------------------------------------------------------------------- */

static inline off_t au_fhsm_size(int nbr)
{
	struct aufhsm *p;

	return sizeof(*p) + nbr * sizeof(*p->wmark);
}

static inline void au_fhsm_sign(struct aufhsm *fhsm)
{
	strncpy(fhsm->magic, AUFHSM_MAGIC, sizeof(fhsm->magic));
	fhsm->csum = au_fhsm_csum(fhsm);
}

static inline int au_fhsm_sign_verify(struct aufhsm *fhsm)
{
	return !strncmp(fhsm->magic, AUFHSM_MAGIC, sizeof(fhsm->magic))
		&& fhsm->csum == au_fhsm_csum(fhsm);
}

/* ---------------------------------------------------------------------- */

/* quick sort */
static inline int au_wm_qsort_brid(const void *_a, const void *_b)
{
	const struct aufhsm_wmark *a = _a, *b = _b;

	return a->brid - b->brid;
}

static inline void au_fhsm_sort_brid(struct aufhsm *fhsm)
{
	qsort(fhsm->wmark, fhsm->nwmark, sizeof(*fhsm->wmark),
	      au_wm_qsort_brid);
}

/* binary search */
static inline int au_wm_bsearch_brid(const void *_brid, const void *_wm)
{
	int brid = (long)_brid;
	const struct aufhsm_wmark *wm = _wm;

	return brid - wm->brid;
}

static inline
struct aufhsm_wmark *au_wm_search_brid(int brid, struct aufhsm *fhsm)
{
	long l = brid;

	return bsearch((void *)l, fhsm->wmark, fhsm->nwmark,
		       sizeof(*fhsm->wmark), au_wm_bsearch_brid);
}

/* linear search */
static inline
struct aufhsm_wmark *au_wm_lfind(int brid, struct aufhsm_wmark *wm, int nwm)
{
	while (nwm-- > 0) {
		if (wm->brid == brid)
			return wm;
		wm++;
	}
	return NULL;
}

#endif /* AuFhsm_COMMON_H */
