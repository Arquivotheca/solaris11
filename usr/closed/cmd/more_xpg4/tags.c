/*
 * COPYRIGHT NOTICE
 * 
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 * 
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED 
 */

/*
 * OSF/1 1.2
 */

#if !defined(lint) && !defined(_NOIDENT)
static char rcsid[] = "@(#)$RCSfile: tags.c,v $ $Revision: 1.1.2.2 $ (OSF) $Date: 1992/08/24 18:19:33 $";
#endif

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Copyright (c) 1988 Mark Nudleman
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1995,1996, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef lint
static char sccsid[] = "@(#)tags.c	5.5 (Berkeley) 6/1/90";
#endif /* not lint */

#if defined(sun)
#include <stdio.h>
#include <libintl.h>
#include <limits.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>
#include <sys/types.h>
#include "less.h"
#include "more_msg.h"

static off_t taglinenum = -1;
#endif

#define	WHITESP(c)	((c)==' ' || (c)=='\t')

char *tagfile;
#if defined(sun)
char tagpattern[LINE_MAX];
#else
char *tagpattern;
#endif

static char *tags = "tags";

extern int linenums;
extern int sigs;
extern wchar_t *line;

extern void	conv_wc2mb(char *, wchar_t *);

/*
 * Find a tag in the "tags" file.
 * Sets "tagfile" to the name of the file containing the tag,
 * and "tagpattern" to the search pattern which should be used
 * to find the tag.
 */
void
findtag(register char *tag)
{
	register char *p;
	register FILE *f;
	register int taglen;
	int search_char;
	static char tline[200];
#if defined(sun)
	char *tp;
#endif

	if ((f = fopen(tags, "r")) == NULL)
	{
#if defined(sun)
		error(gettext("No tags file"));
#else
		error(MSGSTR(NOTAGF, "No tags file"));
#endif
		tagfile = NULL;
		return;
	}

	taglen = strlen(tag);

	/*
	 * Search the tags file for the desired tag.
	 */
	while (fgets(tline, sizeof(tline), f) != NULL)
	{
#if defined(sun)
		if (strncmp(tag, tline, taglen) != 0 || tline[taglen] != '\t')
#else
		if (strncmp(tag, tline, taglen) != 0 || !WHITESP(tline[taglen]))
#endif
			continue;

		/*
		 * Found it.
		 * The line contains the tag, the filename and the
		 * pattern, separated by white space.
		 * The pattern is surrounded by a pair of identical
		 * search characters.
		 * Parse the line and extract these parts.
		 */
#if defined(sun)
		tagfile = NULL;

		/* initialization */
		taglinenum = -1;		/* POSIX.2 */
		strcpy(tagpattern, "");

		/* Skip over the tag name */
		p = strchr(tline, '\t');

		/* Tab must follow tag name */
		if (*p != '\t')
			continue;
		p++;
#else
		tagfile = tagpattern = NULL;

		/*
		 * Skip over the whitespace after the tag name.
		 */
		for (p = tline;  !WHITESP(*p) && *p != '\0';  p++)
			continue;
		while (WHITESP(*p))
			p++;
#endif
		if (*p == '\0')
			/* File name is missing! */
			continue;

#if defined(sun)

		/*
		 * Save the file name.
		 * Find the end of the file name and stick a NULL on it.
		 * Then, check for the tab separating it from the pattern
		 * or line number.
		 */
		tagfile = p;
		p = strchr(p, '\t');

		if (*p != '\t')
			continue;
		*p++ = '\0';
#else
		/*
		 * Save the file name.
		 * Skip over the whitespace after the file name.
		 */
		tagfile = p;
		while (!WHITESP(*p) && *p != '\0')
			p++;
		*p++ = '\0';
		while (WHITESP(*p))
			p++;
#endif
		if (*p == '\0')
			/* Pattern is missing! */
			continue;

#if defined(sun)
		/*
		 * POSIX.2b
		 * The tag can be specified using a pattern or a line number.
		 * If it is a pattern, it will be surrounded by '/'  or '?'
		 * and needs to be prep'ed for regex.
		 * If it is a line number, it will be digits.
		 */
		if (*p == '/') {
			p++;
			tp = tagpattern;
			while (*p != '/' && *p != '\0') {
				if (*p == '*' || *p == '.' || *p == '[') {
					/* quote for regex */
					*tp++ = '\\';
					*tp++ = *p++;
				}
				else if (*p == '\\') {
					*p++;
					if (*p == '\\') {
						*tp++ = '\\';
						*tp++ = '\\';
						*p++;
					}
					else if (*p == '/')
						*tp++ = *p++;
					/* else ignore it */
				}
				else {
					*tp++ = *p++;
				}
			}
			*tp = '\0';
		}
		else if (*p == '?') {
			p++;
			tp = tagpattern;

			while (*p != '?' && *p != '\0') {
				if (*p == '*' || *p == '.' || *p == '[') {
					/* quote for regex */
					*tp++ = '\\';
					*tp++ = *p++;
				}
				else if (*p == '\\') {
					*p++;
					if (*p == '\\') {
						*tp++ = '\\';
						*tp++ = '\\';
						*p++;
					}
					else if (*p == '?')
						*tp++ = *p++;
					/* else ignore it */
				}
				else {
					*tp++ = *p++;
				}
			}

			*tp = '\0';
		}
		else {
			if ((taglinenum = atoll(p)) == 0) {
				taglinenum = -1;
				continue;
			}
		}
#else
		/*
		 * Save the pattern.
		 * Skip to the end of the pattern.
		 * Delete the initial "^" and the final "$" from the pattern.
		 */
		search_char = *p++;
		if (*p == '^')
			p++;
		tagpattern = p;
		while (*p != search_char && *p != '\0')
			p++;

		if (p[-1] == '$')
			p--;
		*p = '\0';
#endif

		(void)fclose(f);
		return;
	}
	(void)fclose(f);
#if defined(sun)
	error(gettext("No such tag in tags file"));
#else
	error(MSGSTR(NOSUCHTAG, "No such tag in tags file"));
#endif
	tagfile = NULL;
}

#if defined(sun)
/*
 * Search for a tag.
 * This is a stripped-down version of search().
 * We don't use search() for some reason:
 *   -	We don't want to blow away any search string we may have saved.
 */
#else
/*
 * Search for a tag.
 * This is a stripped-down version of search().
 * We don't use search() for several reasons:
 *   -	We don't want to blow away any search string we may have saved.
 *   -	The various regular-expression functions (from different systems:
 *	regcmp vs. re_comp) behave differently in the presence of 
 *	parentheses (which are almost always found in a tag).
 */
#endif
int
tagsearch(void)
{
#if defined(sun)
	off_t pos, linepos;
	regex_t regexp;
	char	temp_line[BUFSIZ * MB_LEN_MAX];
#else
	off_t pos, linepos, forw_raw_line();
#endif
	off_t linenum;

#if defined(sun)
	if (taglinenum > 0) {			/* POSIX.2 */
		/* make the tag linenumber the current line */
		jump_back(taglinenum);
		return (0);
	}

	if (regcomp(&regexp, tagpattern, 0) != 0) {
		error(gettext("Invalid tag entry"));
		return (1);
	}
#endif

	pos = (off_t)0;
	linenum = find_linenum(pos);

	for (;;)
	{
		/*
		 * Get lines until we find a matching one or 
		 * until we hit end-of-file.
		 */
		if (sigs)
			return (1);

		/*
		 * Read the next line, and save the 
		 * starting position of that line in linepos.
		 */
		linepos = pos;
		pos = forw_raw_line(pos);
		if (linenum != 0)
			linenum++;

		if (pos == NULL_POSITION)
		{
			/*
			 * We hit EOF without a match.
			 */
#if defined(sun)
			error(gettext("Tag not found"));
#else
			error(MSGSTR(TNOTFOUND, "Tag not found"));
#endif
			return (1);
		}

		/*
		 * If we're using line numbers, we might as well
		 * remember the information we have now (the position
		 * and line number of the current line).
		 */
		if (linenums)
			add_lnum(linenum, pos);

		/*
		 * Test the line to see if we have a match.
		 */
#if defined(sun)
		(void) conv_wc2mb(temp_line, line);
		if (regexec(&regexp, temp_line, (size_t)0, NULL, 0) == 0)
			break;
#else
		if (strcmp(tagpattern, line) == 0)
			break;
#endif
	}

	jump_loc(linepos);
	return (0);
}
