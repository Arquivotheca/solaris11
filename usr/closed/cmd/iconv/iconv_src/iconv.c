/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * iconv.c	code set conversion
 */

#include "iconv_int.h"

#define	MAXLINE		1282		    /* max chars in database line */
#define	MINFLDS		4		    /* min fields in database */
#define	FLDSZ		257		    /* max field size in database */

/*
 * For state dependent encodings, change the state of
 * the conversion to initial shift state.
 */
#define	INIT_SHIFT_STATE(cd, fptr, ileft, tptr, oleft) \
	{ \
		fptr = (char *)NULL; \
		ileft = 0; \
		tptr = to; \
		oleft = BUFSIZ; \
		(void) iconv(cd, &fptr, &ileft, &tptr, &oleft); \
		(void) fwrite(to, 1, BUFSIZ - oleft, stdout); \
	}

static void	usage_iconv(void);
static int	use_iconv_func_init(struct conv_info *);
static int	use_iconv_func(struct conv_info *);
static int	use_iconv_func_fini(struct conv_info *);
static int	use_table(struct conv_info *);
static int	use_table_fini(struct conv_info *);
static int	it_is_a_code_name(char *);

int	search_dbase(char *, char *, char *, char *, char *,
    const char *, const char *);

int
main(int argc, char **argv)
{
	int	ret;
	int	c;
	FILE	*fp;
	char	*fromcode = NULL, *tocode = NULL;
	char	*frommap = NULL, *tomap = NULL;
	struct conv_info	ci = {0};
	int	list_conversion = 0;
	int	errors = 0;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);
	while ((c = getopt(argc, argv, "f:t:csl")) != EOF) {
		switch (c) {
		case 'f':
			if (it_is_a_code_name(optarg) == 1) {
				/* fromcode */
				if (tomap) {
					(void) fprintf(stderr, "%s",
					    gettext("Error: fromcode cannot be "
					    "specified with tomap.\n"));
					usage_iconv();
				}
				fromcode = optarg;
				frommap = NULL;
			} else {
				/* frommap */
				if (tocode) {
					(void) fprintf(stderr, "%s",
					    gettext("Error: frommap cannot be "
					    "specified with tocode.\n"));
					usage_iconv();
				}
				frommap = optarg;
				fromcode = NULL;
			}
			break;

		case 't':
			if (it_is_a_code_name(optarg) == 1) {
				/* tocode */
				if (frommap) {
					(void) fprintf(stderr, "%s",
					    gettext("Error: tocode cannot be "
					    "specified with frommap.\n"));
					usage_iconv();
				};
				tocode = optarg;
				tomap = NULL;
			} else {
				/* tomap */
				if (fromcode) {
					(void) fprintf(stderr, "%s",
					    gettext("Error: tomap cannot be "
					    "specified with fromcode.\n"));
					usage_iconv();
				};
				tomap = optarg;
				tocode = NULL;
			}
			break;

		case 'c':
			ci.flags |= F_NO_INVALID_OUTPUT;
			break;

		case 's':
			ci.flags |= F_SUPPRESS_ERR_MSG;
			break;

		case 'l':
			if (fromcode || frommap || tocode || tomap ||
			    ci.flags &
			    (F_NO_INVALID_OUTPUT | F_SUPPRESS_ERR_MSG)) {
				usage_iconv();
			}
			list_conversion = 1;
			break;

		default:
			usage_iconv();
		}
	}

	if (list_conversion) {
		if (argc > optind) {
			usage_iconv();
		}
		if (list_all_conversion() != 0) {
			/* error happened */
			exit(1);
		}
		exit(0);
	}

	ci.cmdname = argv[0];

	if (fromcode || tocode) {
		if (!fromcode) {
			fromcode = nl_langinfo(CODESET);
		} else if (!tocode) {
			tocode = nl_langinfo(CODESET);
		}
		ci.from = fromcode;
		ci.to = tocode;
		ci.conv_init = use_iconv_func_init;
		ci.conv_main = use_iconv_func;
		ci.conv_fini = use_iconv_func_fini;
	} else if (frommap && tomap) {
		ci.from = frommap;
		ci.to = tomap;
		ci.conv_init = use_charmap_init;
		ci.conv_main = use_charmap;
		ci.conv_fini = use_charmap_fini;
	} else {
		usage_iconv();
		/* NOTREACHED */
	}

	if (ci.conv_init) {
		ret = (*ci.conv_init)(&ci);
		if (ret != 0) {
			exit(1);
		}
	}

	do {
		char	*fname;

		if (optind == argc || strcmp(argv[optind], "-") == 0) {
			/*
			 * no file operand or
			 * '-' is specified
			 */
			fp = stdin;
			fname = "stdin";
			clearerr(fp);
		} else {
			/*
			 * there is an input file
			 */
			if ((fp = fopen(argv[optind], "r")) == NULL) {
				(void) fprintf(stderr,
				    gettext("Can't open %s\n"),
				    argv[optind]);
				errors++;
				continue;
			}
			fname = argv[optind];
		}
		ci.fp = fp;

		ret = (*ci.conv_main)(&ci);
		if (ret != 0) {
			if (!(ci.flags & F_SUPPRESS_ERR_MSG)) {
				(void) fprintf(stderr,
				    gettext("Conversion error detected while "
				    "processing %s.\n"), fname);
			}
			errors++;
		}
		if (fp != stdin) {
			(void) fclose(fp);
		}
	} while (++optind < argc);

	if (ci.conv_fini) {
		ret = (*ci.conv_fini)(&ci);
		if (ret != 0) {
			exit(1);
		}
	}
	return (errors);
}


static int
use_iconv_func_init(struct conv_info *cip)
{
	iconv_t	cd;
	int f;

	/*
	 * If the loadable conversion module is unavailable,
	 * or if there is an error using it,
	 * use the original table driven code.
	 * Otherwise, use the loadable conversion module.
	 */
	cd = iconv_open(cip->to, cip->from);
	if (cd != (iconv_t)-1) {
		/* open succeeded */
		cip->fd.cd = cd;

		if ((cip->flags & F_NO_INVALID_OUTPUT) != 0) {
			f = ICONV_CONV_ILLEGAL_DISCARD |
			    ICONV_CONV_NON_IDENTICAL_DISCARD;
			(void) iconvctl(cd, ICONV_SET_CONVERSION_BEHAVIOR, &f);
		}

		return (0);
	} else {
		char	*d_data_base = ICONV_PATH;
		char	*f_data_base = FILE_DATABASE;
		char	table[FLDSZ];
		char	file[FLDSZ];
		struct kbd_tab	*t;

		if (search_dbase(file, table, d_data_base, f_data_base, NULL,
		    cip->from, cip->to)) {
			/*
			 * got it so set up tables
			 */
			t = gettab(file, table, d_data_base, f_data_base, 0);
			if (!t) {
				(void) fprintf(stderr,
				    gettext("Cannot access conversion "
				    "table %s: errno = %d\n"),
				    table, errno);
				return (1);
			}
			cip->conv_main = use_table;
			cip->conv_fini = use_table_fini;
			cip->fd.t = t;
			return (0);
		} else {
			(void) fprintf(stderr,
			    gettext("Not supported %s to %s\n"),
			    cip->from, cip->to);
			return (1);
		}
	}
}

static int
use_iconv_func_fini(struct conv_info *cip)
{
	(void) iconv_close(cip->fd.cd);
	return (0);
}

/*
 * This function uses the iconv library routines iconv_open,
 * iconv_close and iconv.  These routines use the loadable conversion
 * modules.
 */
static int
use_iconv_func(struct conv_info *cip)
{
	size_t	ileft, oleft;
	char	from[BUFSIZ];
	char	to[BUFSIZ];
	char	*fptr;
	char	*tptr;
	size_t	num;
	iconv_t	cd;
	FILE	*fp;
	const char	*cmd;

	cd = cip->fd.cd;
	fp = cip->fp;
	cmd = cip->cmdname;

	ileft = 0;
	while ((ileft +=
	    (num = fread(from + ileft, 1, BUFSIZ - ileft, fp))) > 0) {
		if (num == 0) {
			INIT_SHIFT_STATE(cd, fptr, ileft, tptr, oleft)
			return (1);
		}

		fptr = from;
		for (; ; ) {
			tptr = to;
			oleft = BUFSIZ;

			if (iconv(cd, &fptr, &ileft, &tptr, &oleft) !=
			    (size_t)-1) {
				(void) fwrite(to, 1, BUFSIZ - oleft, stdout);
				break;
			}

			if (errno == EINVAL) {
				(void) fwrite(to, 1, BUFSIZ - oleft, stdout);
				(void) memcpy(from, fptr, ileft);
				break;
			} else if (errno == E2BIG) {
				(void) fwrite(to, 1, BUFSIZ - oleft, stdout);
				continue;
			} else {		/* may be EILSEQ */
				int	errno_sav = errno;
				(void) fwrite(to, 1, BUFSIZ - oleft, stdout);
				if (errno_sav == EILSEQ &&
				    (!(cip->flags & F_SUPPRESS_ERR_MSG))) {
					(void) fprintf(stderr,
					    gettext("%s: conversion error\n"),
					    cmd);
				}
				INIT_SHIFT_STATE(cd, fptr, ileft, tptr, oleft)
				return (1);
			}
		}
	}

	INIT_SHIFT_STATE(cd, fptr, ileft, tptr, oleft)
	return (0);
}

static void
usage_iconv(void)
{
	(void) fprintf(stderr, "%s",
	    gettext("Usage: iconv [-cs] -f frommap -t tomap [file...]\n"));
	(void) fprintf(stderr, "%s",
	    gettext("       iconv -f fromcode [-cs] [-t tocode] [file...]\n"));
	(void) fprintf(stderr, "%s",
	    gettext("       iconv -t tocode [-cs] [-f fromcode] [file...]\n"));
	(void) fprintf(stderr, "%s",
	    gettext("       iconv -l\n"));
	exit(1);
}

/* ARGSUSED */
static int
use_table_fini(struct conv_info *cip)
{
	return (0);
}

/*
 * This function uses the table driven code ported from the existing
 * iconv command.
 */
static int
use_table(struct conv_info *cip)
{
	int	fd;
	struct kbd_tab	*t;

	fd = fileno(cip->fp);
	t = cip->fd.t;

	return (process(t, fd, 0));
}

int
search_dbase(char *o_file, char *o_table, char *d_data_base,
    char *f_data_base, char *this_table, const char *fcode, const char *tcode)
{
	int	fields;
	int	row;
	char	buff[MAXLINE];
	FILE	*dbfp;
	char	from[FLDSZ];
	char	to[FLDSZ];
	char	data_base[MAXNAMLEN];

	fields = 0;

	from[FLDSZ-1] = '\0';
	to[FLDSZ-1] = '\0';
	o_table[FLDSZ-1] = '\0';
	o_file[FLDSZ-1] =  '\0';
	buff[MAXLINE-2] = '\0';

	(void) snprintf(data_base, sizeof (data_base),
	    "%s%s", d_data_base, f_data_base);

	/* open database for reading */
	if ((dbfp = fopen(data_base, "r")) == NULL) {
		(void) fprintf(stderr,
		    gettext("Cannot access data base %s (%d)\n"),
		    data_base, errno);
		exit(1);
	}

	/* start the search */
	for (row = 1; fgets(buff, MAXLINE, dbfp) != NULL; row++) {

		if (buff[MAXLINE-2] != '\0') {
			(void) fprintf(stderr,
			    gettext("Database Error : row %d has "
			    "more than %d characters\n"), row, MAXLINE-2);
			exit(1);
		}

		fields = sscanf(buff, "%s %s %s %s", from, to, o_table, o_file);
		if (fields < MINFLDS) {
			(void) fprintf(stderr,
			    gettext("Database Error : row %d "
			    "cannot retrieve required %d fields\n"),
			    row, MINFLDS);
			exit(1);
		}

		if ((from[FLDSZ-1] != '\0') || (to[FLDSZ-1] != '\0') ||
		    (o_table[FLDSZ-1] != '\0') || (o_file[FLDSZ-1] != '\0')) {
			(void) fprintf(stderr,
			    gettext("Database Error : row %d has "
			    "a field with more than %d characters\n"),
			    row, FLDSZ-1);
			exit(1);
		}

		if (this_table) {
			if (strncmp(this_table, o_table, KBDNL) == 0) {
				(void) fclose(dbfp);
				return (1);
			}
		} else if (strcmp(fcode, from) == 0 && strcmp(tcode, to) == 0) {
			(void) fclose(dbfp);
			return (1);
		}
	}

	/* not supported */
	(void) fclose(dbfp);
	return (0);
}

/*
 * The min and max lengths of all iconv code conversion behavior
 * modification request indicators at this point are 6 of "IGNORE" and 27 of
 * "NON_IDENTICAL_TRANSLITERATE", respectively.
 */
#define	ICONV_MIN_INDICATOR_LEN		6
#define	ICONV_MAX_INDICATOR_LEN		27

static int
supported_indicator(char *p, size_t len)
{
	int i;
	char s[ICONV_MAX_INDICATOR_LEN + 1];
	char *conv_modifiers[] = {
		"ILLEGAL_DISCARD",
		"ILLEGAL_REPLACE_HEX",
		"ILLEGAL_RESTORE_HEX",
		"NON_IDENTICAL_DISCARD",
		"NON_IDENTICAL_REPLACE_HEX",
		"NON_IDENTICAL_RESTORE_HEX",
		"NON_IDENTICAL_TRANSLITERATE",
		"IGNORE",
		"REPLACE_HEX",
		"RESTORE_HEX",
		"TRANSLIT",
		NULL
	};

	if (len < ICONV_MIN_INDICATOR_LEN || len > ICONV_MAX_INDICATOR_LEN)
		return (0);

	(void) memcpy(s, p, len);
	s[len] = '\0';

	for (i = 0; conv_modifiers[i] != NULL; i++)
		if (strcasecmp(s, conv_modifiers[i]) == 0)
			return (1);

	return (0);
}

/*
 * Check the supplied name to find out if it is a fromcode/tocode name or
 * a pathname to a charmap file.
 *
 * The function figures that out by trying to find from the start of the name
 * if there is any supported iconv code conversion behavior modification
 * request indicator that starts with "//". None of that and having at least
 * a '/' character means it is a pathname to a charmap file. Otherwise,
 * a fromcode/tocode name.
 *
 * Currently, we don't support iconv code conversion behavior modifications
 * with charmap files.
 */
static int
it_is_a_code_name(char *name)
{
	char *start;
	char *prev_start;
	char *end;

	start = strchr(name, '/');

	if (start == NULL)
		return (1);

	if (*(start + 1) != '/')
		return (0);

	prev_start = start += 2;

	while ((end = strchr(start, '/')) != NULL) {
		if (*(end + 1) != '/') {
			start = end + 1;
			continue;
		}

		if (supported_indicator(prev_start, end - prev_start))
			return (1);

		prev_start = start = end + 2;
	}

	if (supported_indicator(prev_start, start + strlen(start) - prev_start))
		return (1);

	return (0);
}
