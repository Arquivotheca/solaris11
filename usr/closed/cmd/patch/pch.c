/*
 * Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1993
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * OSF/1 1.2
 *
 * Copyright 1986, Larry Wall
 *
 * This program may be copied as long as you don't try to make any
 * money off of it, or pretend that you wrote it.
 */

/*
 * File: pch.c
 * Date: Sun Feb 12 18:48:18 PST 1995
 *
 * Description:
 *
 *	Routines for processing patch files.
 *
 * Modifications:
 * 	$Log$
 */

/*
 * Include files:
 */
#include "common.h"

/* Patch (diff listing) abstract type. */


static char	*inputfile = NULL;
static char	*reversefile = NULL;

static LINENUM	p_input_line = 0;	/* current line # from patch file */
static LINENUM	p_base = 0;		/* where to intuit this time */



/*
 * Function: int open_patch_file(char *)
 *
 * Description:
 *
 *	Open the patch file at the beginning of time, load into our file
 *	handling scheme and strip out common indentation right up front.
 *
 * Inputs:
 *	filename	-> A pointer to the file name.
 *			   We accept a NULL pointer or the name "-"
 *			   to mean stdin.
 *
 * Returns:
 *	Pointer to patch file info.
 */

int
open_patch_file(char *filename)
{
	wchar_t		*ret, *sp;
	int		info, i, indent;

	if (filename == NULL || !*filename ||
	    (strcmp(filename, "-") == 0)) {
		/* Use stdin */
		info = open_file("", 0);
	} else {
		/* Use given filename */
		info = open_file(filename, 0);
	}

	/* scan for common indentation characters */
	indent = INT_MAX;
	for (i = 0; indent > 0 && i < opened_files[info]->line_count; i++) {
		ret = fetch_line(opened_files[info], i);
		if (i == 0) {
			/* Find initial spaces */
			for (sp = ret; iswspace(*sp); sp++)
				;

			/*
			 * Copy into our holding area for
			 * later comparison
			 */
			indent = sp - ret;
			if (indent) {
				(void) wcsncpy(wbuf, ret, indent);
				wbuf[indent] = 0;
			}
		} else {
			/*
			 * Start searching for the maximum
			 * initial sequences of spaces
			 * common amoung all lines
			 *
			 * When indent is 0 then there
			 * is no common sequence.
			 */
			for (; indent > 0; indent--) {
				if (wcswcs(ret, wbuf) == ret)
					break;
				wbuf[indent] = 0;
			}
		}
	}

	/*
	 * If there is an identifiable indent then
	 * go ahead and strip the leading characters
	 * from the patch lines.
	 */
	if (indent) {
		for (i = 0; i < opened_files[info]->line_count; i++) {
			opened_files[info]->lines[i].offset +=
			    indent * sizeof (wchar_t);
			opened_files[info]->lines[i].length -=
			    indent * sizeof (wchar_t);
		}
		if (verbose && indent == 1)
			say(gettext("(Patch is indented 1 space.)\n"));
		else if (verbose)
			say(gettext("(Patch is indented %d spaces.)\n"),
			    indent);
	}
	return (info);
}


/*
 * Function: int intuit_diff_type(file_info *)
 *
 * Description:
 *
 *	Determine what kind of diff is in the remaining part of the patch file.
 *
 * Inputs:
 *	pinfo	-> A pointer to the patch file info.
 */

static int
intuit_diff_type(file_info *pinfo)
{
	long	fcl_line;
	bool	stars_last_line = FALSE;
	int	retval;
	wchar_t	*s, *t;
	char	*index_file = NULL;
	char	*star_file = NULL;
	char	*minus_file = NULL;

	fcl_line = -1;
	retval = NULL;
	for (p_input_line = p_base; ; p_input_line++) {
		if ((s = fetch_line(pinfo, p_input_line)) == NULL) {
			retval = 0;
			goto scan_exit;
		}

		for (t = s; iswdigit(*t) || *t == L','; t++)
			;

		/*
		 * Possible normal diff or ed command
		 */
		if (fcl_line < 0L && (iswdigit(*s) &&
		    (*t == L'd' || *t == L'c' || *t == L'a'))) {
			fcl_line = p_input_line;
			if (*(t + 1) == L'\n')
				retval = ED_DIFF;
			else
				retval = NORMAL_DIFF;
			goto scan_exit;
		}
		if (fcl_line < 0L && (iswdigit(*s) &&
		    (*t == L'i' || *t == L's'))) {
			retval = ED_DIFF;
			goto scan_exit;
		}

		/* Context diff has stars */
		if (!stars_last_line && !wcsncmp(s, L"*** ", 4)) {
			/* Get input file name */
			for (s += 4; *s && iswspace(*s); s++)
				;
			for (t = s; *t && !iswspace(*t); t++)
				;
			(void) wcsncpy(wbuf, s, t - s);
			wbuf[t - s] = 0;
			star_file = fetchname(wbuf, strippath, 0);
		} else if (!wcsncmp(s, L"--- ", 4)) {
			/* Get reverse file name */
			for (s += 4; *s && iswspace(*s); s++)
				;
			for (t = s; *t && !iswspace(*t); t++)
				;
			(void) wcsncpy(wbuf, s, t - s);
			wbuf[t - s] = 0;
			minus_file = fetchname(wbuf, strippath, 0);
		} else if (!wcsncmp(s, L"+++ ", 4)) {
			/* Get reverse file name */
			for (s += 4; *s && iswspace(*s); s++)
				;
			for (t = s; *t && !iswspace(*t); t++)
				;
			(void) wcsncpy(wbuf, s, t - s);
			wbuf[t - s] = 0;
			star_file = minus_file;
			minus_file = fetchname(wbuf, strippath, 0);
			fcl_line = p_input_line+1;
			retval = UNIFIED_DIFF;
			goto scan_exit;
		} else if (!wcsncmp(s, L"Index:", 6)) {
			/* Get input file name */
			for (s += 6; *s && iswspace(*s); s++)
				;
			for (t = s; *t && !iswspace(*t); t++)
				;
			(void) wcsncpy(wbuf, s, t - s);
			wbuf[t - s] = 0;
			index_file = fetchname(wbuf, strippath, 0);
		} else if (stars_last_line && !wcsncmp(s, L"*** ", 4)) {
			/*
			 * if this is a new context diff the character just
			 * before the newline is a '*'.
			 */
			while (*s != L'\n')
				s++;

			fcl_line = p_input_line;
			if (*(s - 1) == L'*')
				retval = NEW_CONTEXT_DIFF;
			else
				retval = CONTEXT_DIFF;
			goto scan_exit;
		}
		stars_last_line = !wcsncmp(s, L"********", 8);
	}
scan_exit:
	if (star_file && minus_file)
		inputfile = index_file;
	else if (star_file)
		inputfile = star_file;
	else if (minus_file)
		inputfile = minus_file;
	else
		inputfile = index_file;

	p_base = fcl_line;
	return (retval);
}


/* constructing messages is a no-no for i18n */
static struct {
	int mesno;
	const char *dflt;
} messages[] = {
	{ 0, 0 }, { 0, 0 },
	{ 1,  "  Looks like a context diff to me...\n" },
	{ 2,  "  The next patch looks like a context diff.\n" },
	{ 3,  "  Looks like a normal diff.\n" },
	{ 4,  "  The next patch looks like a normal diff.\n" },
	{ 5,  "  Looks like an ed script.\n" },
	{ 6,  "  The next patch looks like an ed script.\n" },
	{ 7,  "  Looks like a new-style context diff.\n" },
	{ 8,  "  The next patch looks like a new-style context diff.\n" },
	{ 9,  "  Looks like a unified context diff.\n" },
	{ 10, "  The next patch looks like a unified context diff.\n" },
	{ 11, "  Looks like a modified ed diff.\n" },
	{ 12, "  The next patch looks like a modified ed diff.\n" },
};


/*
 * Function: void there_is_another_patch(file_info *, char **)
 *
 * Description:
 *
 *	True if the remainder of the patch file contains a diff of some sort.
 *
 * Inputs:
 *	pinfo	-> A pointer to the file info.
 *	data	-> A address to return the file name to patch.
 *
 * Returns:
 *	True if there is another patch otherwise false.
 *	If true then the name of file to patch is plased in *name.
 */


bool
there_is_another_patch(file_info *pinfo, char **name)
{
	int start = p_base;

	/*
	 * start out not knowing what the file name is...
	 */

	if (inputfile) {
		free(inputfile);
	}
	inputfile = NULL;

	if (reversefile) {
		free(reversefile);
	}
	reversefile = NULL;

	if (p_base != 0L && p_base >= pinfo->line_count) {
		if (verbose)
			say(gettext("done\n"));
		return (NULL);
	}

	/* The -c, -u, -e and -n overrule any kind of diff listing */
	if (!cdiff_type) {
		diff_type = intuit_diff_type(pinfo);
		if (!diff_type) {
			if (p_base > 0L) {
				if (verbose)
					say(gettext("  Ignoring the "
					    "trailing garbage.\ndone\n"));
			} else {
				say(gettext("  I can't seem to find a "
				    "patch in there anywhere.\n"));
			}
			return (FALSE);
		}
	} else {
		diff_type = cdiff_type;
		if (diff_type == CONTEXT_DIFF || diff_type == UNIFIED_DIFF) {
			int temp;

			/*
			 * Position file at the beginning of the hunk.
			 */
			temp = intuit_diff_type(pinfo);

			/* New context diffs are acceptable for -c flag */
			if (diff_type == CONTEXT_DIFF && temp ==
			    NEW_CONTEXT_DIFF)
				diff_type = temp;
		}
	}


	if (verbose)
		say(gettext(
		    messages[diff_type*2 + (start != 0L)].dflt));

	/* Reverse the patch now if requested */
	if (reverse && reversefile && inputfile) {
		char		*temp;

		temp = reversefile;
		reversefile = inputfile;
		inputfile = temp;
	}
	*name = inputfile;

	return (diff_type ? TRUE : FALSE);
}


/*
 * Function: void malformed(char *)
 *
 * Description:
 *
 *	Print an error message about a malformed patch and exit.
 *
 * Inputs:
 *
 *	reason	-> Message indicating specific reason
 */


static void
malformed(LINENUM line, char *reason)
{
	/* Print out generic malformed message */
	say(gettext("Malformed patch at line %ld:\n"), line);

	/* Now print out the specific reason and die */
	fatal(gettext(reason));
	/* NOTREACHED */
}


/*
 * Function: hunk_info *add_to_hunk(hunk_info *, wchar_t *)
 *
 * Description:
 *
 *	Append a patch line to the hunk file.  Reallocate hunk if
 *	necessary.
 *
 * Inputs:
 *	hunk	-> A pointer to the hunk.
 *	line	-> A pointer to the wide character string to add.
 *
 * Returns:
 *
 *	a new pointer to new hunk.
 */

static hunk_info *
add_to_hunk(hunk_info *hunk, wchar_t *line)
{
	if (hunk->line_count >= hunk->max_lines) {
		hunk->max_lines += LINE_REALLOC_INCR;
		hunk = reallocate(hunk, sizeof (hunk_info) +
		    (sizeof (wchar_t *) * hunk->max_lines));
	}
	hunk->lines[hunk->line_count++] = wsavestr(line);
	return (hunk);
}


/*
 * Function: hunk_info another_context_hunk(file_info *)
 *
 * Description:
 *
 * 	Build a hunk from the patch file which has the expected form:
 *
 *	*** filename1 <Last modification date/time>
 *	--- filename2 <Last modification date/time>
 *	***************
 *	*** %d,%d
 *	  %s				; Unaffected lines
 *	- %s				; deleted lines
 *	! %s				; changed lines
 *
 *	--- %d,%d
 *	  %s				; Unaffected lines>
 *	+ %s				; New lines
 *	- %s				; deleted lines
 *	! %s				; changed lines
 *
 *
 * Inputs:
 *	pinfo	-> A pointer to the patch file info.
 *
 * Returns:
 *	address of hunk.
 */

static hunk_info *
another_context_hunk(file_info *pinfo)
{
	int		replace_count, in_replace;
	LINENUM		line;
	wchar_t		*s, *ret;
	hunk_info	*hunk;
	wchar_t		**file1_context, **file2_context;
	/*
	 * if neither '!' nor '+' appears in the first-half of hunk,
	 * the second-half of hunk (replacement) could be omitted.
	 */
	bool	repl_could_be_missing = TRUE;

	p_input_line = p_base;

	/* Allocate hunk head */
	hunk = (hunk_info *) allocate(sizeof (hunk_info) +
	    (sizeof (wchar_t *) * LINE_REALLOC_INCR));
	(void) memset(hunk, 0, sizeof (hunk_info));
	hunk->max_lines = LINE_REALLOC_INCR;
	replace_count = 0;
	in_replace = 0;
	do {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			/* No start of pattern found */
			return (NULL);
		}
		if (wcsncmp(ret, L"Index:", 6) == 0) {
			p_base = p_input_line - 1;
			return (NULL);
		}

		if ((wcsncmp(ret, L"*** ", 4) == 0) &&
		    (ret[wcslen(ret) - 2] == '*')) {
			p_base = p_input_line - 1;
			return (NULL);
		}
	} while (*ret != L'*');

	/* Validate file1 *** %d,%d line */
	if (wcsncmp(ret, L"*** ", 4) != 0) {
		malformed(p_input_line, "Expected line beginning with '*** '");
		/* NOTREACHED */
	}

	s = ret + 4;
	while (!iswdigit(*s))
		s++;
	if (!iswdigit(*s)) {
		malformed(p_input_line, "No pattern lines found in "
		    "'*** ' line.\n");
		/* NOTREACHED */
	}

	hunk->file1_start = watol(s);

	while (iswdigit(*s))
		s++;
	if (*s == ',') {
		while (!iswdigit(*s))
			s++;

		if (!*s)  {
			malformed(p_input_line, "No digits were found "
			    "following ','.\n");
			/* NOTREACHED */
		}
		hunk->file1_lines = watol(s) - hunk->file1_start + 1;
	} else
		hunk->file1_lines = 1;

	/* Process lines above "--- %d,%d */
	line = p_input_line;
	for (;;) {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			/* Early end of file */
			malformed(p_input_line, "Premature end of file.\n");
			/* NOTREACHED */
		}

		/* empty lines should be ignored */
		if (*ret == L'\n')
			continue;

		/* Beginning of file 2 loop */
		if (wcsncmp(ret, L"--- ", 4) == 0) {
			break;
		}

		switch (*ret) {
		case L'-':
			/* FALLSTHROUGH */

		case L' ':
			in_replace = FALSE;
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character "
				    "must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		case L'!':
			/*
			 * '!' found.  Replacement cannot be omitted.
			 */
			repl_could_be_missing = FALSE;
			if (!in_replace) {
				in_replace = TRUE;
				replace_count++;
			}
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character of "
				    "change line must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		case L'+':
			/*
			 * '+' found.  Replacement cannot be omitted.
			 */
			repl_could_be_missing = FALSE;
			malformed(p_input_line, "File1 lines cannot contain "
			    "inserted lines.\n");
			/* NOTREACHED */
			break;

		case L'*':
			if (wcsncmp(ret, L"********", 8) == 0) {
				malformed(p_input_line, "New hunk detected "
				    "before file2 lines were found.\n");
				/* NOTREACHED */
			}
			if (wcsncmp(ret, L"*** ", 4) == 0) {
				malformed(p_input_line, "Unexpected *** found "
				    "in file1 lines.\n");
				/* NOTREACHED */
			}
			/* FALLSTHROUGH */

		default :
			malformed(p_input_line, "File 1 lines must begin with "
			    "'- ', '  ', or '! '.\n");
			break;
		}

		hunk = add_to_hunk(hunk, ret);
		line++;
	}

	hunk->file1_lines = hunk->line_count;

	/* Validate file2 --- %d,%d line */
	s = ret + 4;
	while (!iswdigit(*s))
		s++;
	if (!iswdigit(*s)) {
		malformed(p_input_line, "No pattern lines found in "
		    "'*** ' line.\n");
		/* NOTREACHED */
	}

	hunk->file2_start = watol(s);

	while (iswdigit(*s))
		s++;
	if (*s == ',') {
		while (!iswdigit(*s))
			s++;

		if (!*s)  {
			malformed(p_input_line, "No digits were found "
			    "following ','.\n");
			/* NOTREACHED */
		}
		hunk->file2_lines = watol(s) - hunk->file2_start + 1;
	} else
		hunk->file2_lines = 1;

	/*
	 * 1st half of context diff is empty get
	 * context lines from second half
	 */
	if (hunk->line_count == 0) {
		LINENUM	eline = 0;
		line = p_input_line;
		for (;;) {
			/* end of file */
			if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
				break;
			}

			/* Beginning of next hunkp */
			if (wcsncmp(ret, L"********", 8) == 0) {
				break;
			}
			if (wcsncmp(ret, L"Index:", 6) == 0) {
				p_base = p_input_line--;
				break;
			}
			if (eline >= hunk->file2_lines) {
				p_base = p_input_line--;
				break;
			}
			if (*ret == L' ') {
				if (*(ret + 1) != L' ') {
					malformed(p_input_line,
					    "Second character "
					    "must be a ' ' character.\n");
					/* NOTREACHED */
				}
				hunk = add_to_hunk(hunk, ret);
			}
			eline++;
		}
		p_input_line = line;
	}

	/* Process file2 lines */
	in_replace = FALSE;
	line = 0;
	for (;;) {
		/* end of file */
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			break;
		}

		/* empty lines should be ignored */
		if (*ret == L'\n')
			continue;

		/* Beginning of next hunk */
		if (wcsncmp(ret, L"********", 8) == 0) {
			break;
		}
		if ((wcsncmp(ret, L"Index:", 6) == 0) ||
		    (wcsncmp(ret, L"*** ", 4) == 0)) {
			p_input_line--;
			break;
		}
		if (line >= hunk->file2_lines) {
			p_input_line--;
			break;
		}

		switch (*ret) {
		case L' ':
			/* FALLSTHROUGH */
		case L'+':
			in_replace = FALSE;
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character "
				    "must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		case L'!':
			if (!in_replace) {
				in_replace = TRUE;
				replace_count--;
			}
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character of "
				    "change line must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		default :
			if (line == 0 && repl_could_be_missing == TRUE) {
				p_input_line--;
				goto hunk_done;
			}
			malformed(p_input_line, "Line must begin with '+ ', "
			    "'  ', or '! '.\n");
			break;
		}

		hunk = add_to_hunk(hunk, ret);
		line++;
	}

hunk_done:
	/*
	 * End of file or ***** was found, or
	 * the second-half of hunk exceeded the specified number of lines.
	 * Either way we start here next time
	 */
	p_base = p_input_line;

	if (replace_count) {
		malformed(p_input_line, "Premature end of hunk (Unbalanced "
		    "change lines).\n");
		/* NOTREACHED */
	}
	hunk->file2_lines = hunk->line_count - hunk->file1_lines;

	/*
	 * The original diff goofed up on context lines.
	 * If a patch is close enough to the provious patch to include
	 * a previously changed line as context then the context line
	 * for file 1 does not reflect the changes.  If this is not
	 * worked around then the hunk will fail since the context lines
	 * will not exist in our current file.
	 */
	file1_context = hunk->lines;
	file2_context = hunk->lines + hunk->file1_lines;
	for (line = 0; line < hunk->file1_lines; line++) {
		if ((**file1_context != L' ') || (**file2_context != L' '))
			break;

		if (wcscmp(*file1_context, *file2_context) != 0) {
			free(*file1_context);
			*file1_context = wsavestr(*file2_context);
		}
		file1_context++;
		file2_context++;
	}

	return (hunk);
}


/*
 * Function: hunk_info another_new_context_hunk(file_info *)
 *
 * Description:
 *
 * 	Build a hunk from the patch file which has the expected form:
 *
 *	*** filename1 <Last modification date/time>
 *	--- filename2 <Last modification date/time>
 *	***************
 *	*** %d,%d ****
 *	  %s				; Unaffected lines
 *	- %s				; deleted lines
 *	! %s				; changed lines
 *	--- %d,%d ----
 *	  %s				; Unaffected lines>
 *	+ %s				; New lines
 *	- %s				; deleted lines
 *	! %s				; changed lines
 *
 * Inputs:
 *	pinfo	-> A pointer to the patch file info.
 *
 * Returns:
 *	address of hunk.
 */

static hunk_info *
another_new_context_hunk(file_info *pinfo)
{
	int		replace_count, in_replace;
	LINENUM		line;
	wchar_t		*s, *ret;
	hunk_info	*hunk;
	/*
	 * if neither '!' nor '+' appears in the first-half of hunk,
	 * the second-half of hunk (replacement) could be omitted.
	 */
	bool	repl_could_be_missing = TRUE;

	p_input_line = p_base;

	/* Allocate hunk head */
	hunk = (hunk_info *) allocate(sizeof (hunk_info) +
	    (sizeof (wchar_t *) * LINE_REALLOC_INCR));
	(void) memset(hunk, 0, sizeof (hunk_info));
	hunk->max_lines = LINE_REALLOC_INCR;
	replace_count = 0;
	in_replace = 0;
	do {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			/* No start of pattern found */
			return (NULL);
		}
		if (wcsncmp(ret, L"Index:", 6) == 0) {
			p_base = p_input_line - 1;
			return (NULL);
		}
		if ((wcsncmp(ret, L"*** ", 4) == 0) &&
		    (ret[wcslen(ret) - 2] != L'*')) {
			p_base = p_input_line - 1;
			return (NULL);
		}
	} while (*ret != L'*');

	/* Validate file1 *** %d,%d *** line */
	if (wcsncmp(ret, L"*** ", 4) != 0) {
		malformed(p_input_line, "Expected line beginning with '*** '");
		/* NOTREACHED */
	}

	s = ret + 4;
	while (!iswdigit(*s))
		s++;
	if (!iswdigit(*s)) {
		malformed(p_input_line, "No pattern lines found in "
		    "'*** ' line.\n");
		/* NOTREACHED */
	}

	hunk->file1_start = watol(s);

	while (iswdigit(*s))
		s++;
	if (*s == ',') {
		while (!iswdigit(*s))
			s++;

		if (!*s)  {
			malformed(p_input_line, "No digits were found "
			    "following ','.\n");
			/* NOTREACHED */
		}
		hunk->file1_lines = watol(s) - hunk->file1_start + 1;
	} else
		hunk->file1_lines = 1;

	/* Process lines above "--- %d,%d ----" */
	line = p_input_line;
	for (;;) {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			/* Early end of file */
			malformed(p_input_line, "Premature end of file.\n");
			/* NOTREACHED */
		}

		/* Beginning of file 2 loop */
		if (wcsncmp(ret, L"--- ", 4) == 0) {
			break;
		}

		switch (*ret) {
		case L'-':
			/* FALLSTHROUGH */

		case L' ':
			in_replace = FALSE;
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character "
				    "must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		case L'!':
			/*
			 * '!' found.  Replacement cannot be omitted.
			 */
			repl_could_be_missing = FALSE;
			if (!in_replace) {
				in_replace = TRUE;
				replace_count++;
			}
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character of "
				    "change line must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		case L'+':
			/*
			 * '+' found.  Replacement cannot be omitted.
			 */
			repl_could_be_missing = FALSE;
			malformed(p_input_line, "File1 lines cannot contain "
			    "inserted lines.\n");
			/* NOTREACHED */
			break;

		case L'*':
			if (wcsncmp(ret, L"********", 8) == 0) {
				malformed(p_input_line, "New hunk detected "
				    "before file2 lines were found.\n");
				/* NOTREACHED */
			}
			if (wcsncmp(ret, L"*** ", 4) == 0) {
				malformed(p_input_line, "Unexpected *** found "
				    "in file1 lines.\n");
				/* NOTREACHED */
			}
			/* FALLSTHROUGH */

		case L'\n':
			break;

		default :
			malformed(p_input_line, "File 1 lines must begin with "
			    "'- ', '  ', or '! '.\n");
			break;
		}

		hunk = add_to_hunk(hunk, ret);
		line++;
	}

	hunk->file1_lines = hunk->line_count;

	/* Validate file2 --- %d,%d --- line */
	s = ret + 4;
	while (!iswdigit(*s))
		s++;
	if (!iswdigit(*s)) {
		malformed(p_input_line, "No pattern lines found in "
		    "'*** ' line.\n");
		/* NOTREACHED */
	}

	hunk->file2_start = watol(s);

	while (iswdigit(*s))
		s++;
	if (*s == ',') {
		while (!iswdigit(*s))
			s++;

		if (!*s)  {
			malformed(p_input_line, "No digits were found "
			    "following ','.\n");
			/* NOTREACHED */
		}
		hunk->file2_lines = watol(s) - hunk->file2_start + 1;
	} else
		hunk->file2_lines = 1;


	/*
	 * 1st half of context diff is empty get
	 * context lines from second half
	 */
	if (hunk->line_count == 0) {
		LINENUM	eline = 0;
		line = p_input_line;
		for (;;) {
			/* end of file */
			if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
				break;
			}

			/* Beginning of next hunkp */
			if (wcsncmp(ret, L"********", 8) == 0) {
				break;
			}
			if (wcsncmp(ret, L"Index:", 6) == 0) {
				p_base = p_input_line--;
				break;
			}
			if (eline >= hunk->file2_lines) {
				p_base = p_input_line--;
				break;
			}
			if (*ret == L' ') {
				if (*(ret + 1) != L' ') {
					malformed(p_input_line,
					    "Second character "
					    "must be a ' ' character.\n");
					/* NOTREACHED */
				}
				hunk = add_to_hunk(hunk, ret);
			}
			eline++;
		}
		p_input_line = line;
		hunk->file1_lines = hunk->line_count;
	}

	/* Process file2 lines */
	in_replace = FALSE;
	line = 0;
	for (;;) {
		/* end of file */
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			break;
		}

		/* Beginning of next hunkp */
		if (wcsncmp(ret, L"********", 8) == 0) {
			break;
		}
		if ((wcsncmp(ret, L"Index:", 6) == 0) ||
		    (wcsncmp(ret, L"*** ", 4) == 0)) {
			p_input_line--;
			break;
		}
		if (line >= hunk->file2_lines) {
			p_input_line--;
			break;
		}

		switch (*ret) {
		case L' ':
			/* FALLSTHROUGH */
		case L'+':
			in_replace = FALSE;
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character "
				    "must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		case L'!':
			if (!in_replace) {
				in_replace = TRUE;
				replace_count--;
			}
			if (*(ret + 1) != L' ') {
				malformed(p_input_line, "Second character of "
				    "change line must be a ' ' character.\n");
				/* NOTREACHED */
			}
			break;

		default :
			if (line == 0 && repl_could_be_missing == TRUE) {
				p_input_line--;
				goto hunk_done;
			}
			malformed(p_input_line, "Line must begin with '+ ', "
			    "'  ', or '! '.\n");
			break;
		}

		hunk = add_to_hunk(hunk, ret);
		line++;

	}

hunk_done:
	/*
	 * End of file or ***** was found, or
	 * the second-half of hunk exceeded the specified number of lines.
	 * Either way we start here next time
	 */
	p_base = p_input_line;

	if (replace_count) {
		malformed(p_input_line, "Premature end of hunk (Unbalanced "
		    "change lines).\n");
		/* NOTREACHED */
	}
	if (line == 0) {
		for (line = 0; line < hunk->file1_lines; line++) {
			if (*(hunk->lines[line]) == L' ') {
				hunk = add_to_hunk(hunk, hunk->lines[line]);
			}
		}
	}
	hunk->file2_lines = hunk->line_count - hunk->file1_lines;
	return (hunk);
}



/*
 * Function: hunk_info another_normal_hunk(file_info *)
 *
 * Description:
 *
 * 	Compile a hunk from the patch file which has the expected form:
 *
 * 	Normal diffs have the form:
 *
 *	%da%d\n
 *	%da%d,%d\n
 *	%dd%d\n
 *	%d,%dd%d\n
 *	%dc%d\n
 *	%d,%dc%d\n
 *	%dc%d,%d\n
 *	%d,%dc%d,%d\n
 *
 *	Each command is followed by all the lines affected in file1 and file 2:
 *
 *	< %s			(file 1 lines)
 *	---\n			(separator for change)
 *	> %s			(file 2 lines)
 *
 * Inputs:
 *	pinfo	-> A pointer to the patch file info.
 *
 * Returns:
 *	address of hunk.
 */

static hunk_info *
another_normal_diff_hunk(file_info *pinfo)
{
	hunk_info	*hunk;
	wchar_t		*ret, *s, hunk_type;
	int		i;

	/* Allocate hunk head */
	hunk = (hunk_info *)allocate(sizeof (hunk_info) +
	    (sizeof (wchar_t *) * LINE_REALLOC_INCR));
	(void) memset(hunk, 0, sizeof (hunk_info));
	hunk->max_lines = LINE_REALLOC_INCR;

	/* Search for start of hunk */
	do {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			return (NULL);
		}
		if (wcsncmp(ret, L"Index:", 6) == 0) {
			p_base = p_input_line - 1;
			return (NULL);
		}
	} while (!iswdigit(*ret));

	/* Now process hunk */
	hunk->file1_start = (LINENUM)watol(ret);
	for (s = ret; iswdigit(*s); s++)
		;

	if (*s == ',') {
		hunk->file1_lines = watol(++s) - hunk->file1_start + 1;
		while (iswdigit(*s))
			s++;
	} else {
		hunk->file1_lines = 1;
	}

	hunk_type = *s;

	hunk->file2_start = (LINENUM)watol(++s);

	while (iswdigit(*s))
		s++;

	if (*s == L',')
		hunk->file2_lines = (LINENUM)watol(++s) - hunk->file2_start + 1;
	else
		hunk->file2_lines = 1;

	switch (hunk_type) {
	case L'a' :		/* Add lines */
		/* Compile all lines starting with < */
		for (i = 0; i < hunk->file2_lines; i++) {
			if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
				malformed(p_input_line, "Unexpected end of "
				    "file.\n");
			}
			if (*ret != '>') {
				malformed(p_input_line, "'> ' expected at "
				    "start of line.\n");
			}
#if !(defined(lint) && (__SUNPRO_C <= 0x5100))
			(void) sprintf(buf, "+ %S", ret + 2);
#endif
			(void) mbstowcs(wbuf, buf, max_input);
			hunk = add_to_hunk(hunk, wbuf);
		}
		hunk->file1_start++;
		hunk->file1_lines = 0;
		break;

	case L'd' :		/* Delete lines */
		/* Compile all lines starting with < */
		for (i = 0; i < hunk->file1_lines; i++) {
			if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
				malformed(p_input_line, "Unexpected end of "
				    "file.\n");
			}
			if (*ret != '<') {
				malformed(p_input_line, "'< ' expected at "
				    "start of line.\n");
			}
#if !(defined(lint) && (__SUNPRO_C <= 0x5100))
			(void) sprintf(buf, "- %S", ret + 2);
#endif
			(void) mbstowcs(wbuf, buf, max_input);
			hunk = add_to_hunk(hunk, wbuf);
		}
		hunk->file2_start++;
		hunk->file2_lines = 0;
		break;

	case L'c' :		/* Change lines */
		/* Compile all lines starting with < */
		for (i = 0; i < hunk->file1_lines; i++) {
			if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
				malformed(p_input_line, "Unexpected end of "
				    "file.\n");
			}
			if (*ret != '<') {
				malformed(p_input_line, "'< ' expected at "
				    "start of line.\n");
			}
#if !(defined(lint) && (__SUNPRO_C <= 0x5100))
			(void) sprintf(buf, "! %S", ret + 2);
#endif

			(void) mbstowcs(wbuf, buf, max_input);
			hunk = add_to_hunk(hunk, wbuf);
		}

		/* Make sure next line is a --- line */
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			malformed(p_input_line, "Unexpected end of file.\n");
			/* NOTREACHED */
		}
		if (wcscmp(ret, L"---\n") != NULL) {
			malformed(p_input_line, "'---\\n' expected.\n");
			/* NOTREACHED */
		}

		/* Compile all lines starting with < */
		for (i = 0; i < hunk->file2_lines; i++) {
			if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
				malformed(p_input_line, "Unexpected end of "
				    "file.\n");
			}
			if (*ret != '>') {
				malformed(p_input_line, "'>' expected at start "
				    "of line.\n");
			}
#if !(defined(lint) && (__SUNPRO_C <= 0x5100))
			(void) sprintf(buf, "! %S", ret + 2);
#endif
			(void) mbstowcs(wbuf, buf, max_input);
			hunk = add_to_hunk(hunk, wbuf);
		}
		break;

	default :
		malformed(p_input_line, "Unknown diff operator");
		/* NOTREACHED */
	}
	/*
	 * End of file or ***** was found either way we start here next time
	 */
	p_base = p_input_line;

	return (hunk);
}


/*
 * Function: hunk_info another_unified_hunk(file_info *)
 *
 * Description:
 *
 * 	Build a hunk from the patch file which has the expected form:
 *
 *	--- filename1 <Last modification date/time>
 *	+++ filename2 <Last modification date/time>
 *	@@ %d,%d %d,%d @@
 *	 %s				; Unaffected lines
 *	-%s				; deleted lines
 *	+%s				; changed lines
 *
 * Inputs:
 *	pinfo	-> A pointer to the patch file info.
 *
 * Returns:
 *	address of hunk.
 */

static hunk_info *
another_unified_hunk(file_info *pinfo)
{
	LINENUM		line;
	wchar_t		*s, *ret;
	hunk_info	*hunk;

	p_input_line = p_base;

	/* Allocate hunk head */
	hunk = (hunk_info *) allocate(sizeof (hunk_info) +
	    (sizeof (wchar_t *) * LINE_REALLOC_INCR));
	(void) memset(hunk, 0, sizeof (hunk_info));
	hunk->max_lines = LINE_REALLOC_INCR;

	do {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL) {
			/* No start of pattern found */
			return (NULL);
		}
		if (wcsncmp(ret, L"--- ", 4) == 0) {
			p_base = p_input_line - 1;
			return (NULL);
		}
		if (wcsncmp(ret, L"Index:", 6) == 0) {
			p_base = p_input_line - 1;
			return (NULL);
		}
		if (wcsncmp(ret, L"@@ ", 3) == 0 &&
		    *(ret + wcslen(ret) - 2) != L'@') {
			p_base = p_input_line - 1;
			return (NULL);
		}
	} while (*ret != L'@');

	/* Validate file2 "@@ %d,%d %d,%d @@" line */
	if (wcsncmp(ret, L"@@ ", 3) != 0) {
		malformed(p_input_line, "Expected line beginning with '@@ '");
		/* NOTREACHED */
	}

	s = ret + 3;
	while (!iswdigit(*s))
		s++;
	if (!iswdigit(*s)) {
		malformed(p_input_line, "No pattern lines found in "
		    "'@@ ' line.\n");
		/* NOTREACHED */
	}

	hunk->file1_start = watol(s);

	while (iswdigit(*s))
		s++;
	if (*s == ',') {
		s++;
		if (!iswdigit(*s)) {
			malformed(p_input_line, "No digits were found "
			    "following ','.\n");
			/* NOTREACHED */
		}
		hunk->file1_lines = watol(s) - hunk->file1_start + 1;
		while (iswdigit(*s))
			s++;
	} else
		hunk->file1_lines = 1;

	while (!iswdigit(*s))
		s++;
	if (!iswdigit(*s)) {
		malformed(p_input_line, "No pattern lines found in "
		    "to file range line.\n");
		/* NOTREACHED */
	}

	hunk->file2_start = watol(s);

	while (iswdigit(*s))
		s++;
	if (*s == ',') {
		while (!iswdigit(*s))
			s++;

		if (!*s)  {
			malformed(p_input_line, "No digits were found "
			    "following ','.\n");
			/* NOTREACHED */
		}
		hunk->file2_lines = watol(s) - hunk->file1_start + 1;
		while (iswdigit(*s))
			s++;
	} else
		hunk->file2_lines = 1;


	/* Process lines in two passes */
	line = p_input_line;
	for (;;) {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL)
			break;

		if (*ret == L'+')
			continue;

		if (*ret == L'-' || *ret == L' ') {
			wbuf[0] = *ret;
			wbuf[1] = L' ';
			(void) wcsncpy(&wbuf[2], ret+1, max_input-3);
			wbuf[max_input-1] = L'\0';
			hunk = add_to_hunk(hunk, wbuf);
			continue;
		}
		break;
	}
	hunk->file1_lines = hunk->line_count;
	p_input_line = line;

	for (;;) {
		if ((ret = fetch_line(pinfo, p_input_line++)) == NULL)
			break;

		if (*ret == L'-')
			continue;

		if (*ret == L'+' || *ret == L' ') {
			wbuf[0] = *ret;
			wbuf[1] = L' ';
			(void) wcsncpy(&wbuf[2], ret+1, max_input-3);
			wbuf[max_input-1] = L'\0';
			hunk = add_to_hunk(hunk, wbuf);
			continue;
		}
		break;
	}
	hunk->file2_lines = hunk->line_count - hunk->file1_lines;

	/*
	 * End of file or @@ was found either way we start here next time
	 */
	p_base = p_input_line - 1;

	return (hunk);
}


/*
 * Function: void free_hunk(hunk_info *)
 *
 * Description:
 *
 *	Free all memory allocated for hunk.
 *
 * Inputs:
 *	hunk -> address of hunk to free.
 */

void
free_hunk(hunk_info *hunk)
{
	LINENUM	i;

	for (i = hunk->line_count-1; i >= 0; i--) {
		free((void *)hunk->lines[i]);
	}
	free((void *)hunk);
}


/*
 * Function: void pch_swap(hunk_info *)
 *
 * Description:
 *
 *	Reverse the old and new portions of the current hunk in place.
 *
 * Inputs:
 *	hunk	-> A pointer to the hunk to swap.
 *
 * Returns:
 *	nothing
 */

void
pch_swap(hunk_info *hunk)
{
	hunk_info	*temp;
	LINENUM		i;
	int		size;

	/* Allocate temp hunk head and pointer space */
	size = hunk->line_count * sizeof (wchar_t *);
	size += sizeof (hunk_info);
	temp = (hunk_info *)allocate(size);
	(void) memcpy(temp, hunk, size);

	hunk->max_lines = temp->max_lines;
	hunk->line_count = temp->line_count;

	hunk->file1_start = temp->file2_start;
	hunk->file1_lines = temp->file2_lines;

	hunk->file2_start = temp->file1_start;
	hunk->file2_lines = temp->file1_lines;

	/*
	 * Change all delete lines to add lines
	 * and swap with file 2 lines;
	 */
	for (i = 0; i < temp->file1_lines; i++) {
		if (*(temp->lines[i]) == L'-')
			*(temp->lines[i]) = L'+';
		hunk->lines[hunk->file1_lines + i] = temp->lines[i];
	}

	/*
	 * Change all add lines to delete lines
	 * and swap with file 1 lines;
	 */
	for (i = 0; i < temp->file2_lines; i++) {
		if (*(temp->lines[temp->file1_lines + i]) == L'+')
			*(temp->lines[temp->file1_lines + i]) = L'-';
		hunk->lines[i] = temp->lines[temp->file1_lines + i];
	}

	/* Free up the old unused header space */
	(void) free(temp);
}


/*
 * Function: hunk_info *another_hunk(file_info *)
 *
 * Description:
 *
 *	Return next hunk to process if there is more of the current diff
 *	listing to process.
 *
 * Inputs:
 *	pinfo	-> A pointer to the patch file descriptor.
 *
 * Returns:
 *
 *	address of hunk or FALSE if nore more hunks are available.
 */


hunk_info *
another_hunk(file_info *pinfo)
{
	hunk_info	*hunk;

	switch (diff_type) {
	case CONTEXT_DIFF :
		if ((hunk = another_context_hunk(pinfo)) == NULL)
			return (FALSE);
		break;

	case NEW_CONTEXT_DIFF :
		if ((hunk = another_new_context_hunk(pinfo)) == NULL)
			return (FALSE);
		break;

	case NORMAL_DIFF :
		if ((hunk = another_normal_diff_hunk(pinfo)) == NULL)
			return (FALSE);
		break;

	case UNIFIED_DIFF :
		if ((hunk = another_unified_hunk(pinfo)) == NULL)
			return (FALSE);
		break;

	default :
		fatal(gettext("patch: Unrecognized diff type"));
		break;
	}

	if (reverse)			/* backwards patch? */
		pch_swap(hunk);

	return (hunk);
}


/*
 * Function: void do_ed_script(file_info *, file_info *)
 *
 * Description:
 *
 *	Apply an ed script by feeding ed itself.  Reintegrate results into
 *	our internal data structures.
 *
 * Inputs:
 *	winfo	-> A pointer to the file info.
 *	pinfo	-> A pointer to the patch file info.
 */


void
do_ed_script(file_info *winfo, file_info *pinfo)
{
	FILE	*pipefp;
	wchar_t	*start, *t;
	char	*temp, *tempname;

	ignore_signals();
	tempname = winfo->name;
	winfo->name = temp = tmpnam(NULL);
	sync_file(winfo, "", 0);
	winfo->name = tempname;
	if (!skip_rest_of_patch) {
		if (verbose)
			(void) snprintf(buf, max_input, "%s %s",
			    _PATH_ED, temp);
		else
			(void) snprintf(buf, max_input, "%s - %s",
			    _PATH_ED, temp);
		if ((pipefp = popen(buf, "w")) == (FILE *)-1) {
			int save = errno;

			(void) unlink(temp);
			errno = save;
			pfatal("ed");
			/* NOTREACHED */
		}
	}
	for (;;) {
		if ((start = fetch_line(pinfo, p_base++)) == NULL) {
			break;
		}
		if (wcsncmp(start, L"Index:", 6) == 0) {
			p_base--;
			break;
		}
		if (wcsncmp(start, L"***", 3) == 0) {
			p_base--;
			break;
		}

		/* search for possible second line in range */

		for (t = start; iswdigit(*t) || *t == L','; t++)
			;

		if (iswdigit(*start) && (*t == L'd' || *t == L's')) {
			if (!skip_rest_of_patch)
				(void) fputws(start, pipefp);
		} else if (*t == L'c' || *t == L'a' || *t == L'i') {
			if (!skip_rest_of_patch)
				(void) fputws(start, pipefp);

			while (start = fetch_line(pinfo, p_base++)) {
				if (!skip_rest_of_patch) {
					(void) fputws(start, pipefp);
				}
				if (wcscmp(start, L".\n") == 0)
					break;
			}
		} else {
			break;
		}
	}
	if (!skip_rest_of_patch) {
		(void) fprintf(pipefp, "w\n");
		(void) fprintf(pipefp, "q\n");
		(void) fflush(pipefp);
		(void) pclose(pipefp);
		(void) wait(NULL);
		update_with_file_contents(winfo, temp);
	}
	set_signals();
	(void) unlink(temp);
}
