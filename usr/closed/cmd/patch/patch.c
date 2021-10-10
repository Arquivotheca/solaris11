/*
 * Copyright (c) 1995, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
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
 * File: patch.c
 * Date: Sun Feb 12 18:48:18 PST 1995
 *
 * Description:
 *
 *	patch - a program to apply diffs to original files
 *
 * Modifications:
 * 	$Log$
 */

/*
 * Include files:
 */

#include "common.h"
#include "getresponse.h"

/*
 * Global variables
 */

struct stat		filestat;		/* file statistics area */

/* Temp buffers for character conversion */
char			*buf;			/* general purpose buffer */
wchar_t			*wbuf;			/* general purpose buffer */

/* Flags */
bool			verbose = TRUE;		/* Tell all */
bool			reverse = FALSE;	/* Reverse patch files */
bool			skip_rest_of_patch = FALSE; /* Set on error */
int			strippath = INT_MAX;	/* # path components to strip */
int			cdiff_type = 0;		/* diff type from cmd line */
int			diff_type = 0;		/* working diff type */
int			dont_sync = FALSE;	/* Assume we sync files */
long			max_input;

/*
 * Local variables.
 */

static LINENUM		last_offset = 0;
static bool		force = FALSE;
static bool		noreverse = FALSE;
static bool		canonicalize = FALSE;
static bool		saveorig = FALSE;
static wchar_t		if_defined[128] = { 0 };	/* #ifdef xyzzy */
static wchar_t		if_ndefined[128] = { 0 };	/* #ifndef xyzzy */
static wchar_t		else_defined[] = { L"#else\n" }; /* #else */
static wchar_t		end_defined[128] = { 0 };	/* #endif xyzzy */

static char		*patch_file = NULL;
static char		*output_file = NULL;
static char		*reject_file = NULL;
static char		*input_file = NULL;

static LINENUM		delta;
static int		reject_written = 0;

/*
 * Function: void usage(char *)
 *
 * Description:
 *
 *	Print problem and usage message on stderr and exit with error.
 *
 * Inputs:
 *	problem	-> Message to print.
 */

static void
usage(char *problem)
{

	say("patch: %s.\n", problem);
	fatal(gettext("Usage:\tpatch [-blNR] [-c|-e|-n] [-d dir]"
	    " [-D define] [-i patchfile]\\\n"
	    "\t      [-o outfile] [-p num] [-r rejectfile] [file]\n"));
	exit(ABORT_EXIT_VALUE);
}


/*
 * Function: void reinitialize(void)
 *
 * Description:
 *
 *	Prepare to find the next patch to do in the patch file.
 */

static void
reinitialize(void)
{
	last_offset = 0;
	delta = 0;

	reverse = FALSE;
	skip_rest_of_patch = FALSE;
}


/*
 * Function: void process_arguments(int, char **)
 *
 * Description:
 *
 *	Process commmand line arguments.
 */

static void
process_arguments(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, ":bcd:D:ei:lnNo:p:r:Rsu")) != -1) {
		switch (c) {
		case 'b':	/* Save copy of originals */
			saveorig = TRUE;
			break;

		case 'c':
			/* Interpret patch as context (non-unified) diff */
			if (cdiff_type && cdiff_type != CONTEXT_DIFF) {
				usage(gettext("-c, -e, -n, and -u are mutually"
				    " exclusive options"));
				/* NOTREACHED */
			}
			cdiff_type = CONTEXT_DIFF;
			break;

		case 'd':	/* Chdir to optarg before processing */
			if (chdir(optarg) < 0) {
				fatal(gettext("cd to path %s failed.\n"),
				    optarg);
			}
			break;

		case 'D':	/* #Ifdef changes */
			if (!isalpha(*optarg)) {
				fatal(gettext("Argument to -D"
				    " is not an identifier.\n"));
			}
			(void) mbstowcs(wbuf, optarg,
			    sizeof (wbuf) / sizeof (wchar_t));
			(void) wcscpy(if_defined, L"#ifdef ");
			(void) mbstowcs(&if_defined[wcslen(if_defined)],
			    optarg, 120);
			(void) wcscat(if_defined, L"\n");

			(void) wcscpy(if_ndefined, L"#ifndef ");
			(void) mbstowcs(&if_ndefined[wcslen(if_ndefined)],
			    optarg, 120);
			(void) wcscat(if_ndefined, L"\n");

			(void) wcscpy(end_defined, L"#endif /* ");
			(void) mbstowcs(&end_defined[wcslen(end_defined)],
			    optarg, 115);
			(void) wcscat(end_defined, L" */\n");
			break;

		case 'e':	/* Interpret patch as ed script */
			if (cdiff_type && cdiff_type != ED_DIFF)  {
				usage(gettext("-c, -e, -n, and -u are mutually"
				    " exclusive options"));
				/* NOTREACHED */
			}
			cdiff_type = ED_DIFF;
			break;

		case 'i':	/* Read patch from file rather than stdin */
			patch_file = optarg;	/* patch input file */
			break;

		case 'l':	/* Cause any blank sequences to match */
			canonicalize = TRUE;
			break;

		case 'n':	/* Interpret patch as normal diff */
			if (cdiff_type && cdiff_type != NORMAL_DIFF) {
				usage(gettext("-c, -e, -n, and -u are mutually"
				    " exclusive options"));
				/* NOTREACHED */
			}
			cdiff_type = NORMAL_DIFF;
			break;

		case 'N':	/* Ignore previously applied patches */
			noreverse = TRUE;
			break;

		case 'o':	/* Output to named file */
			/*
			 * If an empty string is passed I.E 'patch -o ""'
			 * we accept this to mean stdout too.
			 */
			if (*optarg == NULL)
				output_file = "-";
			else
				output_file = optarg;
			break;

		case 'p':	/* delete specified # components from paths */
			if (!isdigit(*optarg)) {
				strippath = 0;
				optind--;
			} else {
				strippath = atoi(optarg);
			}
			break;

		case 'r':	/* Send rejects to named file */
			reject_file = optarg;
			break;

		case 'R':	/* reverse patch */
			reverse = TRUE;
			break;

		case 's':	/* Silent operation */
			verbose = FALSE;
			break;

		case 'u':	/* Interpret patch as unified context diff */
			if (cdiff_type && cdiff_type != UNIFIED_DIFF) {
				usage(gettext("-c, -e, -n, and -u are mutually"
				    " exclusive options"));
				/* NOTREACHED */
			}
			cdiff_type = UNIFIED_DIFF;
			break;

		case ':':
			if (optopt == 'p') {
				strippath = 0;
				break;
			}

		default:	/* None of the above */
			usage(gettext("Invalid options"));
			break;
		}
	}

	/*
	 * Get file name if specified.  Otherwise name is derived from
	 * patch file.
	 */
	if (argv[optind] != NULL) {
		(void) mbstowcs(wbuf, argv[optind], max_input);
		if (argv[optind + 1] != NULL) {
			usage(gettext("Too many file arguments"));
		}
		if ((input_file = fetchname(wbuf, 0, 0)) == NULL) {
			fatal(gettext("patch: Could not open \n"),
			    argv[optind]);
		}
	}
}


/*
 * Function: void similar(wchar_t *, wchar_t *)
 *
 * Description:
 *
 *	Do two lines match with canonicalized white space?
 *
 * Inputs:
 *	a	-> A pointer to the first wide character string.
 *	b	-> A pointer to the second wide character string.
 *
 * Returns:
 *	TRUE	-> Lines compare favorably.
 *	FALSE	-> Lines do not compare.
 */

static bool
similar(wchar_t *a, wchar_t *b)
{
	int	len = wcslen(b);

	while (len) {
		if (iswspace(*b)) {	/* whitespace (or \n) to match? */
			/* no corresponding whitespace? */
			if (!iswspace(*a))
				return (FALSE);

			/* skip pattern whitespace */
			while (len && iswspace(*b) && *b != '\n') {
				b++;
				len--;
			}

			/* skip target whitespace */
			while (iswspace(*a) && *a != '\n')
				a++;

			/* should end in sync */
			if (*a == '\n' || *b == '\n')
				return (*a == *b);

			/*
			 * Othewise start matching non-whitespace
			 * characters again (if any characters
			 * are left.
			 */
			continue;
		} else if (*a++ != *b++) {
			/*
			 * Non-whitespace chars did not match
			 * so lines are not similar.
			 */
			return (FALSE);
		}
		/*
		 * Non-whitespace chars matched
		 * so lines are still similar.
		 */
		len--;
	}

	/* Lines match */
	return (TRUE);
}


/*
 * Function: bool patch_match(file_info *, hunk_info *, LINENUM)
 *
 * Description:
 *
 *	Does the hunk patterns match starting at line offset?
 *
 * Inputs:
 *	winfo	-> A pointer to the file descriptor.
 *	hunk	-> A pointer to the patch descriptor.
 *	offset	-> Offset to start checking.
 */

static bool
patch_match(file_info *winfo, hunk_info *hunk, LINENUM offset)
{
	wchar_t	*patch_line, *input_line;
	LINENUM	pline;

	for (pline = 0; pline < hunk->file1_lines; pline++) {
		patch_line = hunk->lines[pline] + 2;
		if ((input_line = fetch_line(winfo, offset + pline)) == NULL)
			return (FALSE);
		if (canonicalize) {
			if (!similar(input_line, patch_line))
				return (FALSE);
		} else if (wcsncmp(input_line, patch_line, wcslen(patch_line)))
			return (FALSE);
	}
	return (TRUE);
}


/*
 * Function: LINENUM locate_hunk(file_info *, hunk_info *, LINENUM)
 *
 * Description:
 *
 *	Attempt to find the right place to apply this hunk of patch.
 *	Fuzz is the current extent of the search.  Most the time this
 *	is 0, but for misplaced patches our caller keeps expanding the
 *	search by one line.  We take advantage of this and only check
 *	the extremes since we know every position in the middle has already
 *	failed.
 *
 * Inputs:
 *	winfo	-> A pointer to the file descriptor.
 *	hunk	-> A pointer to the patch descriptor.
 *	fuzz	-> +/- Offset to check at.
 *
 * Returns:
 *	line number of match or -1 if no match was found yet.
 */

static LINENUM
locate_hunk(file_info *winfo, hunk_info *hunk, LINENUM fuzz)
{
	LINENUM first_guess, max_pos_offset, max_neg_offset;

	first_guess = hunk->file1_start + delta + last_offset - 1;

	/* null range matches always */
	if (hunk->file1_lines == 0)
		return (first_guess);

	max_pos_offset = first_guess + fuzz;
	max_neg_offset = first_guess - fuzz;

	/* do not try lines < 0 */
	if (max_neg_offset < 0)
		max_neg_offset = 0;
	if (max_pos_offset < 0)
		max_pos_offset = 0;

	/* do not try lines > number of lines in file */
	if ((max_neg_offset + hunk->file1_lines) > winfo->line_count)
		max_neg_offset = winfo->line_count - hunk->file1_lines;
	if ((max_pos_offset + hunk->file1_lines) > winfo->line_count)
		max_pos_offset = winfo->line_count - hunk->file1_lines;

	/*
	 * because of the way searches are expanded we only need
	 * to check the extremes...  If fuzz is 0 then we don't
	 * even need to check both since they are identical.
	 */

	if (fuzz && patch_match(winfo, hunk, max_neg_offset)) {
		last_offset = delta + hunk->file1_start - max_neg_offset - 1;
		return (max_neg_offset);
	}

	if (patch_match(winfo, hunk, max_pos_offset)) {
		last_offset = delta + hunk->file1_start - max_pos_offset - 1;
		return (max_pos_offset);
	}
	return (-1);
}


/*
 * Function: LINENUM check_hunk_location(file_info *, hunk_info *, int)
 *
 * Description:
 *
 *
 * Inputs:
 *	winfo		-> A pointer to the file descriptor.
 *	hunk		-> A pointer to the patch descriptor.
 *	hunk_count	-> Current hunk number.
 *
 * Returns:
 *	line number of match or -1 if no match was found.
 */

static LINENUM
check_hunk_location(file_info *winfo, hunk_info *hunk, int hunk_count)
{
	LINENUM	where, fuzz;

	fuzz = 0;
	do {
		where = locate_hunk(winfo, hunk, fuzz);
		if (hunk_count == 1 && where == -1 && !force) {
			/*
			 * dwim for reversed patch?
			 * for normal diffs swapping a pure delete will always
			 * work and this is not what we want...
			 */
			if (diff_type == NORMAL_DIFF && hunk->file2_lines == 0)
				continue;

			pch_swap(hunk);
			reverse = !reverse;

			/* try again */
			where = locate_hunk(winfo, hunk, fuzz);

			/* didn't find it swapped */
			if (where == -1) {
				pch_swap(hunk);
				reverse = !reverse;
			} else if (noreverse) {
				pch_swap(hunk);
				reverse = !reverse;
				say(gettext("Ignoring previously applied "
				    "(or reversed) patch.\n"));
				skip_rest_of_patch = TRUE;
			} else {
				if (reverse) {
					ask(gettext("Reversed (or previously "
					    "applied) patch detected! "
					    "Assume -R [%s] "),
					    yesstr);
				} else {
					ask(gettext(
					    "Unreversed (or previously "
					    "applied) patch detected! "
					    "Ignore -R? [%s] "),
					    yesstr);
				}
				if (yes_check(buf) == 0) {
					/* no */
					ask(gettext("Apply anyway? [%s] "),
					    nostr);
					if (yes_check(buf) == 0) {
						skip_rest_of_patch = TRUE;
					}
					where = NULL;
					reverse = !reverse;
					pch_swap(hunk);
				}
			}
		}
	} while (!skip_rest_of_patch && where == -1 && ++fuzz <= MAXFUZZ);

	return (where);
}


/*
 * Function: void abort_hunk(hunk_info *, file_info *)
 *
 * Description:
 *
 *	We did not find the pattern, dump out the hunk so the user
 *	can handle it.
 *
 * Inputs:
 *	hunk		-> A pointer to the patch descriptor.
 *	reject_info	-> A pointer to the file descriptor.
 */

static void
abort_hunk(hunk_info *hunk, file_info *reject_info)
{
	LINENUM	i;

	reject_written = 1;
	insert_line(reject_info, L"***************\n", reject_info->line_count);

	(void) sprintf(buf, "*** %ld,%ld ****\n", hunk->file1_start,
	    hunk->file1_start + hunk->file1_lines - 1);
	(void) mbstowcs(wbuf, buf, max_input);
	insert_line(reject_info, wbuf, reject_info->line_count);

	for (i = 0; i < hunk->file1_lines; i++) {
		insert_line(reject_info, hunk->lines[i],
		    reject_info->line_count);
	}

	(void) sprintf(buf, "--- %ld,%ld ----\n", hunk->file2_start,
	    hunk->file2_start + hunk->file2_lines - 1);
	(void) mbstowcs(wbuf, buf, max_input);
	insert_line(reject_info, wbuf, reject_info->line_count);

	for (; i < hunk->line_count; i++) {
		insert_line(reject_info, hunk->lines[i],
		    reject_info->line_count);
	}
}


/*
 * Function: void apply_hunk(file_info *, hunk_info *, LINENUM)
 *
 * Description:
 *
 *	We found where to apply it (we hope), so do it.
 *
 *	Hunk has already been error checked so we don't
 *	check for malformed expressions here.
 *
 *	The hunk is applied in 2 phases:
 *	1) Process the original lines (these include deletions, context lines,
 *	   and change lines) we simply delete deletions and change lines.
 *	2) Process the new lines (these include, additions, context lines,
 *	   and change lines) we simply add additions and change lines.
 *
 *	The tricky part here is to properly account for #ifdef lines and
 *	lines that would normally be deleted when ifdefs are required and
 *      to not account so if they are not. The array deltas is used to
 *	accomplish this.  It tracks the number of additional lines that need
 *	to be added to the line count at each sync point (context line) for
 *	each context line in the second phase of application.
 *
 * Inputs:
 *	winfo	-> A pointer to the file descriptor.
 *	hunk	-> A pointer to the patch descriptor.
 *	offset	-> Offset to start checking.
 */

static void
apply_hunk(file_info *winfo, hunk_info *hunk, LINENUM where)
{
	LINENUM	i, wh;
	wchar_t	*line;
	wchar_t	in_define = 0;
	LINENUM	*deltas;
	LINENUM	context_line;

	deltas = allocate(sizeof (LINENUM) * (hunk->line_count + 1));

	/* file1 processing */
	wh = where;
	context_line = 0;
	for (i = 0; i < hunk->file1_lines; i++) {
		line = hunk->lines[i];
		switch (*line) {
		case L' ':	/* Context line */
			if (*if_defined) {
				/* Put out #endif if needed */
				if (in_define) {
					insert_line(winfo, end_defined, wh++);
					deltas[context_line]++;
					delta++;
					in_define = 0;
				}
				context_line++;
			}
			wh++;
			break;

		case L'-':	/* Delete line */
			if (*if_defined) {
				/* Put out #ifndef if needed */
				if (!in_define) {
					in_define = *line;
					insert_line(winfo, if_ndefined, wh++);
					deltas[context_line]++;
					delta++;
				}
				wh++;
			} else {
				delete_line(winfo, wh);
				delta--;
			}
			break;

		case L'!':	/* Change line */
			if (*if_defined) {
				/* Put out #else if needed */
				if (!in_define) {
					in_define = *line;
					insert_line(winfo, else_defined, wh++);
					deltas[context_line]++;
					delta++;
				}
				/*
				 * Deltas are accounted for at matching change
				 */
				wh++;
			} else {
				delete_line(winfo, wh);
				delta--;
			}
			break;
		}
	}

	/* Put out #endif if needed */
	if (*if_defined && in_define) {
		insert_line(winfo, end_defined, wh++);
		deltas[context_line]++;
		delta++;
		in_define = 0;
	}

	/* Finish adding lines */
	wh = where;
	context_line = 0;
	for (i = hunk->file1_lines; i < hunk->line_count; i++) {
		line = hunk->lines[i];
		switch (*line) {
		case L' ':	 /* Context line */
			wh += deltas[context_line++];
			if (*if_defined && in_define == L'+') {
				/* Put out #endif */
				insert_line(winfo, end_defined, wh++);
				delta++;
				in_define = 0;
			}
			wh++;
			break;

		case L'+':	/* Add line */
			if (*if_defined) {
				/* Put out #ifdef if needed */
				if (!in_define) {
					in_define = *line;
					insert_line(winfo, if_defined, wh++);
					delta++;
				}
			}
			insert_line(winfo, line + 2, wh++);
			delta++;
			break;

		case L'!':	/* Change line */
			if (*if_defined) {
				/* Put out #ifdef if needed */
				if (!in_define) {
					in_define = *line;
					insert_line(winfo, if_defined, wh++);
					delta++;
				}
			}
			insert_line(winfo, line + 2, wh++);
			delta++;
			break;
		}
	}

	/* Put out final #endif if needed */
	if (*if_defined && in_define == L'+') {
		insert_line(winfo, end_defined, wh++);
		delta++;
	}
	in_define = 0;

	/* Free up our temp space */
	free(deltas);
}


/*
 * Function: void cleanup(void)
 *
 * Description:
 *
 *	Called on exit.  Writes out any files that have not already
 *	been written to disk.
 *
 * Inputs:
 *	info	-> A pointer to the file descriptor.
 *	data	-> A pointer to the wide character string to add.
 */

void
cleanup(void)
{
	unsigned long	i;

	if (dont_sync == FALSE) {
		for (i = 0; i < opened_file_descriptors; i++) {
			if (opened_files[i]->flags & UPDATE_ON_EXIT) {
				(void) open_file(opened_files[i]->name, 0);
				sync_file(opened_files[i], NULL, 0);
				close_file(opened_files[i]);
			}
		}
	}
	for (i = 0; i < opened_file_descriptors; i++) {
		(void) unlink(opened_files[i]->temp_file);
	}
}


/*
 * Function: void ask_for_name(void)
 *
 * Description:
 *
 *	Ask for a file to patch since none could be intuited.
 *
 * Returns:
 *
 *	Name of a file that exists.
 */

static char *
ask_for_name(void)
{
	struct stat	statbuf;
	char		*name = NULL;

	while (name == NULL) {
		ask(gettext("File to patch: "));
		if (*buf != '\n') {
			(void) mbstowcs(wbuf, buf, max_input);
			/* Process potential file name */
			name = fetchname(wbuf, 0, FALSE);
		}
		if (name == NULL || lstat(name, &statbuf) == -1) {
			ask(gettext("No file found -- skip this patch? [%s] "),
			    nostr);
			if (yes_check(buf) == 0) {	/* not yes */
				continue;
			}
			if (verbose)
				say(gettext("Skipping patch...\n"));

			skip_rest_of_patch = TRUE;
			return (NULL);
		}
	}
	return (savestr(name));
}


/*
 * Function: void main(int, char **)
 *
 * Description:
 *
 *	Process arguments and apply a set of diffs as appropriate.
 */


int
main(int argc, char **argv)
{
	int		winfo, pinfo, reject_info;
	hunk_info	*hunk;
	LINENUM		where;
	struct stat	statbuf;
	char		*workname, *reject_name;
	int		hunk_count;
	int		failed = 0;
	int		failtotal = 0;

	/* Set locale environment variables local definitions */
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it wasn't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*
	 * setup common buffers
	 */
	max_input = MAX_INPUT;
	buf = allocate((max_input + 1) * sizeof (char));
	wbuf = allocate((max_input + 1) * sizeof (wchar_t));
	if (init_yes() < 0) {
		fatal(gettext(ERR_MSG_INIT_YES), strerror(errno));
	}

	/* parse switches */
	process_arguments(argc, argv);

	/* make sure we clean up /tmp and sync files in case of disaster */
	set_signals();
	(void) atexit(cleanup);

	/* For each patch in file open file, apply patch */
	for (pinfo = open_patch_file(patch_file);
	    there_is_another_patch(opened_files[pinfo], &workname);
	    reinitialize()) {
		if (!skip_rest_of_patch) {
			if (input_file == NULL) {
				/*
				 * No file was specified on command line so we
				 * take the file name from patch file (if_any)
				 */
				if (workname == NULL) {
					/* No file name in patch file either! */
					input_file = ask_for_name();
					if (input_file == NULL)
						continue;
					workname = input_file;
				}
			} else {
				/*
				 * file name was specified on command line or
				 * user was prompted for it so the name never
				 * changes
				 */
				workname = input_file;
			}
			winfo = open_file(workname, 0);
			opened_files[winfo]->flags |= UPDATE_ON_EXIT;
			if (saveorig && (output_file == 0))
				opened_files[winfo]->flags |= SAVE_ORIGINAL;
		}

		/* for ed script just up and do it and exit */
		if (diff_type == ED_DIFF) {
			if (!skip_rest_of_patch)
				do_ed_script(opened_files[winfo],
				    opened_files[pinfo]);
			goto next;
		}

		/* Open up reject file */
		if (reject_file != NULL) {
			reject_name = reject_file;
		} else {
			(void) snprintf(buf, max_input, "%s.rej", workname);
			reject_name = savestr(buf);
		}

		/* get an empty file */
		reject_info = open_file(reject_name, 1);

		/* apply each hunk of patch to file */
		failed = hunk_count = 0;
		while ((hunk = another_hunk(opened_files[pinfo])) != NULL) {
			hunk_count++;
			where = check_hunk_location(opened_files[winfo],
			    hunk, hunk_count);
			if (skip_rest_of_patch) {
				abort_hunk(hunk, opened_files[reject_info]);
				failed++;
				if (verbose) {
					say(gettext("Hunk #%d ignored at "
					    "line %ld.\n"), hunk_count, where);
				}
			} else if (where == (LINENUM) -1) {
				abort_hunk(hunk, opened_files[reject_info]);
				failed++;
				if (verbose) {
					say(gettext("Hunk #%d failed at "
					    "line %ld.\n"), hunk_count,
					    hunk->file1_start);
				}
			} else {
				apply_hunk(opened_files[winfo], hunk, where);
				if (verbose) {
					if (last_offset == 1L) {
						say(gettext("Hunk #%d succeeded"
						    " at line %ld (offset 1"
						    " line)\n"), hunk_count,
						    where - delta);
					} else if (last_offset > 1L) {
						say(gettext("Hunk #%d succeeded"
						    " at %ld (offset %ld lines)"
						    "\n"), hunk_count,
						    where - delta,
						    last_offset);
					}
				}
			}
			free_hunk(hunk);
		}

		/* and put the output where desired */
		ignore_signals();

		/* If patch failed write out stats */
		if (failed) {
			opened_files[reject_info]->flags |= UPDATE_ON_EXIT;
			failtotal = FAIL_EXIT_VALUE;
			if (skip_rest_of_patch) {
				say(gettext("%d out of %d hunks "
				    "ignored: saving rejects to %s\n"),
				    failed, hunk_count, reject_name);
			} else {
				say(gettext("%d out of %d hunks "
				    "failed: saving rejects to %s\n"),
				    failed, hunk_count, reject_name);
			}
		}

		/*
		 * If an output has been specified then all output goes to
		 * that file and the original files are not touched.  We also
		 * Flush out the intermediate results after each patch in this
		 * case since the spec says so...
		 *
		 * Note: if create_outputfile is set, then sync_file() will
		 * attempt to change the permissions of the output file to
		 * those of the target file.
		 */
		close_file(opened_files[reject_info]);
next:		if (output_file) {
			int	create_outputfile = 0;

			if (strcmp(output_file, "-") != 0) {
				if (lstat(output_file, &statbuf) != -1) {
					/*
					 * We also need to create a .orig file
					 * if asked to.  If not, we want to
					 * remember that we're not creating
					 * the output file so we don't try
					 * to modify it's permissions.
					 */
					if (saveorig) {
						saveorig = 0;
						create_outputfile = 1;
						(void) snprintf(buf,
						    max_input, "%s.orig",
						    output_file);
						(void) rename(output_file, buf);
					/*
					 * The output file could be a symlink.
					 * Check if the file referenced by the
					 * symlink exists.
					 */
					} else if ((statbuf.st_mode & S_IFMT)
					    == S_IFLNK) {
						if (stat(output_file,
						    &statbuf) == -1) {
							create_outputfile = 1;
						}
					}
				} else {
					create_outputfile = 1;
				}
			}
			opened_files[winfo]->flags &= ~UPDATE_ON_EXIT;
			sync_file(opened_files[winfo], output_file,
			    create_outputfile);
		}
		close_file(opened_files[winfo]);
	}
	if (reject_written)
		exit(REJECT_EXIT_VALUE);
	return (failtotal);
}
