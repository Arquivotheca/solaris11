/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * G. S. Fowler
 * AT&T Bell Laboratories
 *
 * command line option parse assist
 *
 *	-- or ++ terminates option list
 *
 *	return:
 *		0	no more options
 *		'?'	unknown option opt_option
 *		':'	option opt_option requires an argument
 *		'#'	option opt_option requires a numeric argument
 *  other character	option found
 *
 *	conditional compilation:
 *
 *		KSHELL	opt_num and opt_argv disabled
 */

#include	"csi.h"
#ifdef KSHELL
#include	"sh_config.h"
#endif

static char *parse_short_option(char **argv,
				char *opts,
				int *flag_value_return);

static char *parse_long_option(char **argv,
				char *opts,
				int *flag_value_return);

static char *find_short_option(char *optstring, int flagChar);

longlong_t	opt_index;		/* argv index			*/
int		opt_char;		/* char pos in argv[opt_index]	*/
int		opt_plus = 0;		/* if set, allow + for flags */
#ifndef KSHELL
long		opt_num;		/* # numeric argument		*/
char 		**opt_argv;		/* argv				*/
#endif
char		*opt_arg;		/* {:,#} string argument	*/
char		opt_option[256];	/* current option, including +/- */
					/* e.g., "-x" "--longopt" */

longlong_t	opt_pindex;		/* prev opt_index for backup	*/
int		opt_pchar;		/* prev opt_char for backup	*/

extern char	*strchr();

#ifndef KSHELL
extern long	strtol();
#endif

/*
 * ASSUMES: The last element of argv is NULL. This assumption has always
 *	been made in this function -- without taking a size argument
 *	(such as argc), there is no other way to know the size of
 *	the argv array. This is often called with opt_index == argc.
 *
 * CSI assumption1(ascii) made here. See csi.h.
 */
int
optget(argv, optstring)
register char 	**argv;
char		*optstring;
{
	int		c = '\0'; /* char flag = argv[opt_index][opt_char] */
	register char	*s;	/* transient string pointer */
	int		is_long_option = 0;	/* true long option */
#ifndef KSHELL
	char		*e;
#endif
	opt_pindex = opt_index;
	opt_pchar = opt_char;
	/*
	 * VSC/tp6 - Case 6:
	 * optstring=":" optvar="" args="-a fred" OPTIND="2" OPTARG="a fred"
	 * getopts "$optstring" optvar $args
	 * OPTARG should be unset
	 */
	opt_arg = NULL;

	if (argv[opt_index] == NULL) {
	    /* No more options */
	    return (opt_char = 0);
	}

	if ((opt_char == 0) && ((*(s = argv[opt_index])) != '\0') &&
	    ((*++s) != '\0') && ((*++s) != '\0')) {
	    is_long_option =
		((((*(s = argv[opt_index])) == '-') && (s[1] == '-')) ||
		    (opt_plus && ((*s) == '+') && (s[1] == '+')));
	}
	if (is_long_option) {
	    s = parse_long_option(argv, optstring, &c);
	} else {
	    s = parse_short_option(argv, optstring, &c);
	}
	if (s == NULL) {
		return (c);
	}

#ifndef KSHELL
	opt_num = 0;
#endif
	c = (*s++);	/* get flag, move ptr one past flag in optstring */
	if (opt_arg != NULL)
	{
	/*
	 * option argument was specified as part of long-option on
	 * command line. Check it.
	 */
	    if (((*s) != ':') && ((*s) != '#'))
	    {
		/* This option should not have an argument */
		opt_arg = NULL;
	    }
#ifndef KSHELL
	    if (((*s) == '#') && (opt_arg != NULL))
	    {
		opt_num = strtol(opt_arg, &e, 0);
		if (*e) opt_arg = 0;
	    }
#endif
	    if (argv[opt_index][opt_char] == 0)
	    {
		    opt_char = 0;
		    opt_index++;
	    }
	} else if ((*s == ':') || (*s == '#')) {
		/* Option has an argument -- find it */

		if (!*(opt_arg = &argv[opt_index++][opt_char]))
		{
			if (!(opt_arg = argv[opt_index]))
			{
				if (*(s + 1) != '?') c = ':';
			}
			else
			{
				opt_index++;
				if (*(s + 1) == '?')
				{
					if ((*opt_arg == '-') ||
						(opt_plus && *opt_arg == '+'))
					{
						if (*(opt_arg + 1)) opt_index--;
						opt_arg = 0;
					}
#ifndef KSHELL
					else if (*s++ == '#')
					{
						opt_num =
							strtol(opt_arg, &e, 0);
						if (*e) opt_arg = 0;
					}
#endif
				}
			}
		}
#ifndef KSHELL
		if (*s == '#' && opt_arg)
		{
			opt_num = strtol(opt_arg, &e, 0);
			if (*e) c = '#';
		}
#endif
		opt_char = 0;
	} else if (argv[opt_index][opt_char] == 0) {
		opt_char = 0;
		opt_index++;
	}

	return (c);
} /* optget() */

/*
 * This code existed prior the the implementation of long command-line
 * options. It was moved from optget() and comments were added during
 * the process.
 *
 * Looks at the next flag on the command line, and finds the definition
 * of the option in the string 'optstring'. Returns a pointer to the flag
 * in the opts string, if the flag is found.
 *
 * If the return value is not null, the character will be returned in
 * flag_value_return. If the return value is null, (*flag_value_return)
 * will be one of ('?' '#' 0), as defined at the top of this file.
 */
static char *
parse_short_option(char **argv,
		char *optstring,
		int *flag_value_return) {
	char *s = NULL;		/* transient string pointer */
	int c = '\0';		/* current character flag */
	(*flag_value_return) = c;

	for (;;)
	{
		if (!opt_char)
		{
			if (!opt_index)
			{
				opt_index++;
#ifndef KSHELL
				opt_argv = argv;
#endif
			}
			if (!(s = argv[opt_index]) ||
				(((opt_option[0] = *s++) != '-') &&
				(!opt_plus || opt_option[0] != '+')) || !*s) {
				/*
				 * Reset optchar as opt_index has changed
				 * VSC/getopts.sh/tp6-Case 3
				 */
				if (opt_index > opt_pindex)
					opt_char = 0;
				(*flag_value_return) = 0;
				return (NULL);
			}
			if (*s++ == opt_option[0] && !*s)
			{
				/*
				 * Reset optchar as opt_index has changed
				 * VSC/getopts.sh/tp6-Case 3
				 */
				opt_char = 0;
				opt_index++;
				(*flag_value_return) = 0;
				return (NULL);
			}
			opt_char++;
		}
		if (opt_option[1] = c = argv[opt_index][opt_char++]) break;
		opt_char = 0;
		opt_index++;
	}
#ifndef KSHELL
	opt_num = 0;
#endif
	if ((c == ':') || (c == '#') || (c == '?') ||
	    ((s = find_short_option(optstring, c)) == NULL))
	{
#ifdef KSHELL
		/*
		 * Reset optchar as opt_index has changed
		 * VSC/getopts.sh/tp6-Case 3
		 */
		opt_char = 0;
		opt_index++;	/* Increment OPTIND before return */
		(*flag_value_return) = '?';
		return (NULL);
#else
		if ((c < '0') || (c > '9') ||
		    ((s = strchr(optstring, '#')) == NULL) ||
		    (s == optstring))
		{
		    (*flag_value_return) = '?';
		    return (NULL);
		}
		c = *--s;
#endif
	}

	(*flag_value_return) = c;
	return (s);
} /* parse_short_option */

/*
 * Looks for the option in argv[opt_index] an alias for
 * a single-character flag (short option). If found, returns
 * a pointer into optstring of the equivalent one-character
 * flag. Otherwise, returns NULL.
 *
 * Returns the character flag in flag_value_return or one
 * of ['#' '?' 0] if not found. See comments at top of
 * this file for details.
 *
 * If the option contains an argument (e.g., --longopt=arg), then
 * the global variable opt_arg is set to point to the beginning of
 * the argument.
 *
 * Copies the short flag into opt_option (including leading '-' or '+'),
 * and a terminating 0.
 *
 * ASSUMES: parse_short_option() will be called AFTER this function
 *          because that function determines the correct return
 *	    value if no valid option nor flag are found.
 *
 * ASSUMES: opt_char == 0
 * ASSUMES: argv[opt_index] begins with "--" or "++"
 * ASSUMES: argv[opt_index] is at least 3 characters in length
 */
static char *
parse_long_option(char **argv,
		char *optstring,
		int *flag_value_return) {

	char *cpLongOption;
	char *cpOptString = optstring;
	char *cpFlagStart;		/* pointer to flag in optstring */
	char *theFlag = NULL;		/* pointer to the equivalent flag */
	int tmpChar;			/* transient char value */

	cpLongOption = &(argv[opt_index][2]);	/* see assumptions above */
	cpFlagStart = cpOptString;
	while ((theFlag == NULL) && ((*cpOptString) != '\0')) {
		/* long options are enclosed in parens -- look for open paren */
		while (((*cpOptString) != '\0') &&
			((*cpOptString) != '(')) {

			tmpChar = (*cpOptString);
			if ((tmpChar != '(') &&
				(tmpChar != ')') &&
				(tmpChar != '#') &&
				(tmpChar != ':')) {
				/* The beginning of an option */
				cpFlagStart = cpOptString;
			}
			++cpOptString;
		}
		if ((*cpOptString) == '(') {
			++cpOptString;	/* skip paren to find option name */
		}

		/*
		 * We found an open paren or end of optString. See if longOption
		 * matches value in parens.
		 */
		while (((*cpLongOption) == (*cpOptString)) &&
			((*cpLongOption) != '\0') &&
			((*cpOptString) != ')') &&
			((*cpOptString) != '\0')) {
			++cpLongOption;
			++cpOptString;
		}
		if ((((*cpLongOption) == '\0') || ((*cpLongOption) == '=')) &&
			((*cpOptString) == ')')) {
			/* we found the long option in optString */
			theFlag = cpFlagStart;
		} else {
			/*
			 * Keep looking
			 * Reset pointer to beginning of argv option
			 */
			cpLongOption = &(argv[opt_index][2]);

			/* skip the remainder of this long-option name */
			while (((*cpOptString) != '\0') &&
					((*cpOptString) != ')')) {
				++cpOptString;
			}
		}
	} /* while theFlag==NULL */

	/* advance to end of what we parsed (the whole argument) */
	while (argv[opt_index][opt_char] != 0) {
		++opt_char;
	}

	opt_arg = NULL;
	if (theFlag == NULL) {
		/* failed -- no such long option */
		(*flag_value_return) = '?'; /* unknown argument */

		/*
		 * Unrecognized long option -- return the bad long option in
		 * $OPTARG, since there is no equivalent short-option
		 * to return.
		 */
		strncpy(opt_option, argv[opt_index], sizeof (opt_option) -1);
		opt_option[sizeof (opt_option) - 1] = 0;

		/* eliminate the argument from opt_option[] */
		if ((cpLongOption = strchr(opt_option, '=')) != NULL) {
			(*cpLongOption) = 0;
		}
	} else {
		/* success - found equivalent flag */
		(*flag_value_return) = (*theFlag);
		if ((*cpLongOption) == '=') {
			/* found an argument to go with it */
			opt_arg = cpLongOption + 1;
		}

		/* put the value in opt_option */
		opt_option[0] = argv[opt_index][0];	/* + or - */
		opt_option[1] = *theFlag;
		opt_option[2] = 0;
	}

	return (theFlag);
} /* parse_long_option() */

/*
 * Finds a definition of flagChar in optstring. This is not a simple
 * mbschr() any longer because long options appear in optstring inside
 * parentheses and must be ignored.
 *
 * optget() was updated to use mbschr() (from strchr) in 1995 in one
 * place - this function replaces that call. No other string manipulation
 * in this file seems to be safe for multibyte character strings.
 *
 * returns a pointer to the flag in optstring, or null if not found
 */
static char *
find_short_option(char *optstring, int flagChar) {
	const char *s = optstring;
	wchar_t wc = 0;

	while (((wc = mb_peekc(s)) > 0) && ((wc) != flagChar)) {
		/* skip long name in parentheses */
		mb_nextc(&s);
		wc = mb_peekc(s);
		if (wc == '(') {
			while ((wc != 0) && (wc != ')')) {
				mb_nextc(&s);
				wc = mb_peekc(s);
			}
		}
	}
	if ((*s) == '\0') {
		/* not found */
		s = NULL;
	}

    return ((char *)s);
} /* find_short_option() */
