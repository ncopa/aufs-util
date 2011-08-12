/*
 * Copyright (C) 2010 Junjiro R. Okajima
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

#include <linux/aufs_type.h>
#include <stdio.h>
#include <string.h>
#include "au_util.h"

int main(int argc, char *argv[])
{
	if (!strncmp(AUFS_VERSION, "3.0", 3)
	    && (sizeof(AUFS_VERSION) - 1 == 3
		|| AUFS_VERSION[3] == '-'))
		return 0;

	puts("Wrong version!\n"
	     AuVersion ", but aufs is " AUFS_VERSION ".");
	return -1;
}
