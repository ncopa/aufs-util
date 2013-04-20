/*
 * Copyright (C) 2005-2013 Junjiro R. Okajima
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

#ifndef __AUFS_UTIL_H__
#define __AUFS_UTIL_H__

#include <errno.h>
#include <error.h>

#define AuVersion "aufs-util for aufs3.2 and later"

/*
 * error_at_line() is decleared with (__printf__, 5, 6) attribute,
 * and our compiler produces a warning unless args is not given.
 * __VA_ARGS__ does not help the attribute.
 */
#define AuFin(fmt, ...) \
	error_at_line(errno, errno, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#ifdef DEBUG
#define MTab "/tmp/mtab"
#else
#define MTab "/etc/mtab"
#endif

/* proc_mounts.c */
struct mntent;
int au_proc_getmntent(char *mntpnt, struct mntent *rent);

/* br.c */
int au_br(char ***br, int *nbr, struct mntent *ent);

/* plink.c */
enum {
	AuPlink_FLUSH,
	AuPlink_CPUP,
	AuPlink_LIST
};
#define AuPlinkFlag_OPEN	1UL
#define AuPlinkFlag_CLOEXEC	(1UL << 1)
#define AuPlinkFlag_CLOSE	(1UL << 2)
int au_plink(char cwd[], int cmd, unsigned int flags, int *fd);

/* mtab.c */
void au_print_ent(struct mntent *ent);
int au_update_mtab(char *mntpnt, int do_remount, int do_verbose);

#define _Dpri(fmt, ...)		printf("%s:%d:" fmt, \
				       __func__, __LINE__, ##__VA_ARGS__)
#ifdef DEBUG
#define Dpri(fmt, ...)		_Dpri(fmt, ##__VA_ARGS__)
#else
#define Dpri(fmt, ...)		do { } while(0)
#endif

#endif /* __AUFS_UTIL_H__ */
