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
 *
 * @(#)$RCSfile: more.msg,v $ $Revision: 1.5.4.5 $ (OSF) $Date: 1992/11/17 20:18:58 $
 */

/*
 * Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Messages for ch.c
 */
#define	PIPE_ERR	"Pipe error"
#define	NOBUFF		"Cannot allocate %d buffers"

/*
 * Messages for command.c
 */
#define	FILEMSG		" file %d/%d"
#define	LINEMSG		" line %d"
#define	BYTEMSG		" byte %d"
#define	PERCENT		"/%d percent %d%%"
#define	EOFNEXT		": END (next file: "
#define	END		": END"
#define	PHELP		"[Press space to continue, q to quit, h for help]"
#define	HELP		"help"
#define	PTAG		"Tag: "
#define	EXAMINE		"Examine: "
#define	NOSTDIN		"Cannot edit standard input"
#define	MARK		"mark: "
#define	GMARK		"goto mark: "
#define	PRESSH		"[Press 'h' for instructions.]"
#define	NOFARGS		"No files provided as arguments."
#define	STDIN		"Standard input"
#define	SHDONE		"!done"

/*
 * Messages for linenum.c
 */
#define	ELINENO		"Calculating line numbers"

/*
 * Messages for main.c
 */
#define	NOCURR		"No current file"
#define	NOPREV		"No previous file"
#define	NOVIEW		"Can view standard input only once"
#define	NOTERM		"Can't take input from a terminal"
#define	NONTHF		"No (N-th) next file"
#define	NONTHF2		"No (N-th) previous file"
#define	NOMEM		"Cannot allocate memory"

/*
 * Messages for option.c
 */
#define	BADSCREEN	"Bad screen size.\n"
#define	BADWARG		"Invalid -W option.\n"
#define	USAGE		"usage: more [-Ncdeisuvz] [-t tag] [-x tabs] [-p " \
			"command] [-n number]\n       [-W option] [file ...]\n"

/*
 * Messages for os.c.c
 */
#define	ISADIR		"%s is a directory"

/*
 * Messages for output.c
 */
#define	PRETURN		"(press RETURN)"
#define	INTRTOAB	"... (interrupt to abort)"

/*
 * Message for prim.c
 */
#define	SKIP		"...skipping...\n"
#define	NOSEEK		"Cannot seek to end of file"
#define	NOBEGIN		"Cannot get to beginning of file"
#define	ONLYN		"File has only %d lines"
#define	NOLENGTH	"Don't know length of file"
#define	CANTSEEK	"Cannot seek to that position"
#define	BADLET		"Choose a letter between 'a' and 'z'"
#define	NOMARK		"mark not set"
#define	NOREG		"No previous regular expression"
#define	NOSEARCH	"Nothing to search"
#define	NOPAT		"Pattern not found"

/*
 * Messages for tags.c
 */
#define	NOTAGF		"No tags file"
#define	NOSUCHTAG	"No such tag in tags file"
#define	TNOTFOUND	"Tag not found"

/*
 * Messages for help.c
 */

/*
 * These messages must remain in order
 */
#define	HELP_01		"  Commands flagged with an asterisk (``*'') may " \
			"be preceded by a number.\n"
#define	HELP_02		"  Commands of the form ``^X'' are control " \
			"characters, i.e. control-X.\n"
#define	HELP_03		"\n"
#define	HELP_04		"  h              Display this help.\n"
#define	HELP_05		"\n"
#define	HELP_06		"  f, ^F, SPACE * Forward  N lines, default one " \
			"screen.\n"
#define	HELP_07		"  b, ^B        * Backward N lines, default one " \
			"screen.\n"
#define	HELP_08		"  j, CR        * Forward  N lines, default 1 line.\n"
#define	HELP_09		"  k            * Backward N lines, default 1 line.\n"
#define	HELP_10		"  d, ^D        * Forward  N lines, default half " \
			"screen or last N to d/u.\n"
#define	HELP_11		"  u, ^U        * Backward N lines, default half " \
			"screen or last N to d/u.\n"
#define	HELP_12		"  g            * Go to line N, default 1.\n"
#define	HELP_13		"  G            * Go to line N, default the end of " \
			"the file.\n"
#define	HELP_14		"  p, %         * Position to N percent into the " \
			"file.\n"
#define	HELP_15		"\n"
#define	HELP_16		"  r, ^L          Repaint screen.\n"
#define	HELP_17		"  R              Repaint screen, discarding " \
			"buffered input.\n"
#define	HELP_18		"\n"
#define	HELP_19		"  m[a-z]         Mark the current position with " \
			"the supplied letter.\n"
#define	HELP_20		"  '[a-z]         Return to the position previously " \
			"marked by this letter.\n"
#define	HELP_21		"  ''             Return to previous position.\n"
#define	HELP_22		"\n"
#define	HELP_23		"  /pattern     * Search forward  for N-th line " \
			"containing the pattern.\n"
#define	HELP_24		"  /!pattern    * Search forward  for N-th line " \
			"NOT containing the pattern.\n"
#define	HELP_25		"  ?pattern     * Search backward for N-th line " \
			"containing the pattern.\n"
#define	HELP_26		"  ?!pattern    * Search backward for N-th line " \
			"NOT containing the pattern.\n"
#define	HELP_27		"  n            * Repeat previous search (for N-th " \
			"occurence).\n"
#define	HELP_28		"\n"
#define	HELP_29		"  :a              Display the list of files.\n"
#define	HELP_30		"  E [file]        Examine a new file.\n"
#define	HELP_31		"  :n, N        *  Examine the next file.\n"
#define	HELP_32		"  :p, P        *  Examine the previous file.\n"
#define	HELP_33		"  :t [tag]        Examine the tag.\n"
#define	HELP_34		"  v               Run an editor on the current file.\n"
#define	HELP_35		"\n"
#define	HELP_36		"  =, ^G           Print current file name and stats.\n"
#define	HELP_37		"\n"
#define	HELP_39		"  q, :q, or ZZ    Exit.\n"

#define	HELPEND		 "End of help"

