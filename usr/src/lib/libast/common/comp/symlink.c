/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*          Copyright (c) 1985-2011 AT&T Intellectual Property          *
*                      and is licensed under the                       *
*                  Common Public License, Version 1.0                  *
*                    by AT&T Intellectual Property                     *
*                                                                      *
*                A copy of the License is available at                 *
*            http://www.opensource.org/licenses/cpl1.0.txt             *
*         (with md5 checksum 059e8cd6165cb4c31e351f2b69388fd9)         *
*                                                                      *
*              Information and Software Systems Research               *
*                            AT&T Research                             *
*                           Florham Park NJ                            *
*                                                                      *
*                 Glenn Fowler <gsf@research.att.com>                  *
*                  David Korn <dgk@research.att.com>                   *
*                   Phong Vo <kpv@research.att.com>                    *
*                                                                      *
***********************************************************************/
#pragma prototyped

#include <ast.h>

#if _lib_symlink

NoN(symlink)

#else

#include "fakelink.h"

#include <error.h>

#ifndef ENOSYS
#define ENOSYS	EINVAL
#endif

int
symlink(const char* a, char* b)
{
	if (*a == '/' && (*(a + 1) == 'd' || *(a + 1) == 'p' || *(a + 1) == 'n') && (!strncmp(a, "/dev/tcp/", 9) || !strncmp(a, "/dev/udp/", 9) || !strncmp(a, "/proc/", 6) || !strncmp(a, "/n/", 3)))
	{
		int	n;
		int	fd;

		if ((fd = open(b, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)) < 0)
			return -1;
		n = strlen(a) + 1;
		n = (write(fd, FAKELINK_MAGIC, sizeof(FAKELINK_MAGIC)) != sizeof(FAKELINK_MAGIC) || write(fd, a, n) != n) ? -1 : 0;
		close(fd);
		return n;
	}
	errno = ENOSYS;
	return -1;
}

#endif
