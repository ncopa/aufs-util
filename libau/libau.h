/*
 * Copyright (C) 2009-2011 Junjiro R. Okajima
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

#ifndef __libau_h__
#define __libau_h__

#include <stdio.h>

int libau_dl(void **real, char *sym);
int libau_test_func(char *sym);

#define LibAuEnv	"LIBAU"

#define LibAuDlFunc(sym) \
static inline int libau_dl_##sym(void) \
{ \
	return libau_dl((void *)&real_##sym, #sym); \
}

#define LibAuStr(sym)		#sym
#define LibAuStr2(sym)		LibAuStr(sym)
#define LibAuTestFunc(sym)	libau_test_func(LibAuStr2(sym))

/* ---------------------------------------------------------------------- */

/* #define LibAuDebug */
#ifdef LibAuDebug
#define DPri(fmt, ...)	fprintf(stderr, "%s:%d: " fmt, \
				__func__, __LINE__, ##__VA_ARGS__)
#else
#define DPri(fmt, ...)	do {} while (0)
#endif

#endif /* __libau_h__ */
