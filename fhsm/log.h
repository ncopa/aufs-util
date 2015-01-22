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
 * aufs FHSM, logging macros
 */

#ifndef AuFhsm_LOG_H
#define AuFhsm_LOG_H

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#define AuFhsmd_OPTION		(LOG_NDELAY | LOG_PID)
#ifdef AUFHSM_UT
#define AuFhsmd_FACILITY	LOG_USER
#else
#define AuFhsmd_FACILITY	LOG_DAEMON
#endif

extern int au_do_syslog;

/* All message strings given should not end the NL char */

#define AuWarn(fmt, ...) do {						\
		int e = errno;						\
		fprintf(stderr, "%s[%d]:%s:%d: " fmt "\n",		\
			program_invocation_short_name, getpid(),	\
			__func__, __LINE__, ##__VA_ARGS__);		\
		errno = e;						\
	} while (0)

#ifdef AUFHSM_UT
#define AuDWarn			AuWarn
#else
#define AuDWarn(fmt, ...)	do {} while (0)
#endif

#define AuDoLog(level, fmt, ...) do {					\
		int e = errno;						\
		if (au_do_syslog) {					\
			syslog(level, "%s:%d: " fmt,			\
			       __func__, __LINE__, ##__VA_ARGS__);	\
		} else							\
			AuWarn(fmt, ##__VA_ARGS__);			\
		errno = e;						\
	} while (0)

#define AuLogErr(fmt, ...)				\
	AuDoLog(LOG_ERR, fmt ", %m", ##__VA_ARGS__)
#define AuLogWarn(fmt, ...)				\
	AuDoLog(LOG_WARNING, fmt, ##__VA_ARGS__)
#define AuLogWarn1(fmt, ...) do {				  \
		static unsigned char cnt;			  \
		if (!cnt++)					  \
			AuDoLog(LOG_WARNING, fmt, ##__VA_ARGS__); \
	} while (0)
#define AuLogInfo(fmt, ...)			\
	AuDoLog(LOG_INFO, fmt, ##__VA_ARGS__)
#define AuLogDbg(fmt, ...)			\
	AuDoLog(LOG_DEBUG, fmt, ##__VA_ARGS__)

#ifdef AUFHSM_DBG
#define AuDbgFhsmLog(fmt, ...)	AuLogDbg(fmt, ##__VA_ARGS__)
#else
#define AuDbgFhsmLog(fmt, ...)	do {} while (0)
#endif

#define AuLogFin(fmt, ...) do {						\
		AuLogErr(fmt, ##__VA_ARGS__);				\
		exit(EXIT_FAILURE);					\
	} while (0)

#endif /* AuFhsm_LOG_H */
