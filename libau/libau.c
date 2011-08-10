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

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libau.h"

int libau_dl(void **real, char *sym)
{
	char *p;

	if (*real)
		return 0;

	dlerror(); /* clear */
	*real = dlsym(RTLD_NEXT, sym);
	p = dlerror();
	if (p)
		fprintf(stderr, "%s\n", p);
	return !!p;
}

/* always retrieve the var from environment, since it can be changed anytime */
int libau_test_func(char *sym)
{
	int ret, l;
	char *e;

	ret = 0;
	e = getenv(LibAuEnv);
	if (!e)
		goto out;
	DPri("e 0x%02x, %s\n", *e, e);

	ret = !*e || !strcasecmp(e, "all");
	if (ret)
		goto out;

	l = strlen(sym);
	while (!ret && (e = strstr(e, sym))) {
		DPri("%s, l %d, %c\n", e, l, e[l]);
		ret = (!e[l] || e[l] == ':');
		e++;
	}

 out:
	DPri("%s %d\n", sym, ret);
	return ret;
}
