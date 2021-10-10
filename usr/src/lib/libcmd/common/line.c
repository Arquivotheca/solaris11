/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*          Copyright (c) 1982-2007 AT&T Intellectual Property          *
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
*		  Glenn Fowler <gsf@research.att.com>		       *
*		   David Korn <dgk@research.att.com>		       *
*                                                                      *
***********************************************************************/
#pragma prototyped

#include <stdio.h>
#include <cmd.h>

static const char usage[] =
"[-?\n@(#)$Id: line (AT&T Research) 2010-05-14 $\n]"
USAGE_LICENSE
"[+NAME?line - read one line]"
"[+DESCRIPTION?The \bline\b utility copies one line (up to and "
	"including  a newline) from the standard input and writes "
	"it on the standard output. It returns an exit status "
	"of 1 on EOF and always prints at least a newline. " 
	"It is often used within shell files to read from the "
	"user's terminal.]"
"[+SEE ALSO?\bread\n(1)]"
;

// fixme: Write test module with "print $'a\nb' | (line ; print "X" ; /bin/cat )"
int
b_line(int argc, char** argv, void* context)
{
	const char *s;

	cmdinit(argc, argv, context, ERROR_CATALOG, 0);
	for (;;)
	{
		switch (optget(argv, usage))
		{
			case '?':
				error(ERROR_usage(2), "%s", opt_info.arg);
				continue;
			case ':':
				error(2, "%s", opt_info.arg);
				continue;
		}
		break;
	}
	argv += opt_info.index;
	argc -= opt_info.index;
	if(error_info.errors || argc != 0)
		error(ERROR_usage(2),"%s", optusage(NiL));

	s = sfgetr(sfstdin, '\n', SF_STRING);
	if (!s)
	{
		s = sfgetr(sfstdin, 0, SF_STRING|SF_LASTR);
		if (!s)
			s="";
	}
	sfputr(sfstdout, s, '\n');

	return sfeof(sfstdin)!=0;
}
