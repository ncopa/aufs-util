/*
 * Copyright (C) 2013-2016 Junjiro R. Okajima
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error_at_line.h"

/* musl libc has 'program_invocation_name', but doesn't have error_at_line() */
void error_at_line(int status, int errnum, const char *filename,
		   unsigned int linenum, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	fprintf(stderr, "%s:%s:%d: ",
		program_invocation_name, filename, linenum);
	vfprintf(stderr, format, ap);
	fprintf(stderr, ": %s\n", errnum ? strerror(errnum) : "");
	va_end(ap);
	if (status)
		exit(status);
}
