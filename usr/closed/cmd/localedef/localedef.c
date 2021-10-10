/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

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
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: localedef.c,v $ $Revision: 1.5.6.3 $"
 *	" (OSF) $Date: 1992/09/14 15:20:09 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.8  com/cmd/nls/localedef.c, cmdnls, bos320, 9130320 7/17/91 17:39:41
 */
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/systeminfo.h>
#include "localedef.h"
#include "locdef.h"

char	*yyfilenm;		/* global file name pointer */
struct code_conv	*code_conv;
int	Charmap_pass;
int	verbose;
int	warning = FALSE;	/* Suppress warnings flag */
static int	ilp32p = TRUE;		/* TRUE if generating 32-bit obj */
int	lp64p = FALSE;		/* TRUE if generating 64-bit obj */


static const char *options = {
#ifdef ODEBUG
	"nscdvwx:f:i:C:L:P:m:W:u:"
#else /* ODEBUG */
	"cvwx:f:i:C:L:P:m:W:u:"
#endif /* ODEBUG */
};

#define	CS_UTF8	"UTF-8"

static void
do_compile(char *cmdstr, char *filename)
{
	int	ret;

	if (verbose)
		(void) printf("%s\n", cmdstr);

	ret = system(cmdstr);

	if (WIFEXITED(ret)) {
		switch (WEXITSTATUS(ret)) {
		case 0:
			break;			/* Successful compilation */
		case -1:
			perror("localedef"); /* system() failed */
			exit(4);
			break;
		case 127:
			/* cannot exec shell */
			(void) fprintf(stderr, gettext(ERR_NOSHELL));
			exit(4);
			break;
		default:
			/* take a guess.. */
			(void) fprintf(stderr, gettext(ERR_BAD_CHDR));
			exit(4);
			break;
		}
	} else {
		(void) fprintf(stderr, ERR_INTERNAL, filename, 0);
		exit(4);
	}
}

static char *
mkcmdline(char *tpath, char *ccopts, char *soname,
	char *objname, char *filename, char *ldopts, int bits)
{
	size_t	cmdlen;
	char	*cmdbuf;
	char	*s;
	int	i;

	cmdlen = strlen(tpath) + CCPATH_LEN + SPC_LEN +
	    strlen(ccopts) + SPC_LEN +
	    SONAMEF_LEN + SPC_LEN + strlen(soname) + SPC_LEN +
	    OBJNAMEF_LEN + SPC_LEN + strlen(objname) + SPC_LEN +
	    strlen(filename) + SPC_LEN;

	for (i = 0; i <= LAST_METHOD; i++) {
		if (bits == 32) {
			if (!lib_array[i].library)
				break;		/* No more 32-bit libraries */
			cmdlen += strlen(lib_array[i].library) + SPC_LEN;
		} else {
			if (!lib_array[i].library64)
				break;		/* No more 64-bit libraries */
			cmdlen += strlen(lib_array[i].library64) + SPC_LEN;
		}
	}

	cmdlen += strlen(ldopts) + 1;

	cmdbuf = MALLOC(char, cmdlen);

	/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
	s = cmdbuf + sprintf(cmdbuf, CCCMDLINE,
	    tpath, CCPATH, ccopts, soname, objname, filename);

	for (i = 0; i <= LAST_METHOD; i++) {
		if (bits == 32) {
			if (!lib_array[i].library)
				break;		/* No more 32-bit libraries */
			/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
			s += sprintf(s, " %s", lib_array[i].library);
		} else {
			if (!lib_array[i].library64)
				break;		/* No more 64-bit libraries */
			/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
			s += sprintf(s, " %s", lib_array[i].library64);
		}
	}
	/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
	(void) sprintf(s, " %s", ldopts);

	return (cmdbuf);
}

static int
check_isa(void)
{
	char	*buf, *isa;
	size_t	bufsize = BUFSIZ;
	long	ret;
	int	bits;

	buf = MALLOC(char, bufsize);
	do {
		ret = sysinfo(SI_ISALIST, buf, (long)bufsize);
		if (ret == -1l)
			return (0);
		if (ret > bufsize) {
			bufsize = ret;
			buf = REALLOC(char, buf, bufsize);
		} else {
			break;
		}
	} while (buf != NULL);

	bits = 0;
	for (isa = strtok(buf, " "); isa; isa = strtok(NULL, " ")) {
		if (strcmp(isa, ISA32) == 0) {
			bits |= BIT32;
		} else if (strcmp(isa, ISA64) == 0) {
			bits |= BIT64;
		}
	}

	if (bits == 0) {
		free(buf);
		return (0);
	}

	free(buf);
	return (bits);
}

static char *
get_tpath(char *tpath)
{
	size_t	len;
	char	*s;

	if (tpath == NULL) {
		/* return "" */
		s = MALLOC(char, 1);
		*s = '\0';
		return (s);
	}

	len = strlen(tpath);
	if (*(tpath + len - 1) != '/') {
		s = MALLOC(char, len + 2);
		/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
		(void) sprintf(s, "%s/", tpath);
		return (s);
	} else {
		s = MALLOC(char, len + 1);
		(void) strcpy(s, tpath);
		return (s);
	}
}

static char *
get_ccarg(char *optstr)
{
	char	*s, *ms, *cs, *tmps;
	size_t	n, len;

	s = optstr;
	n = strlen(ARGSTR);

	/* check if the first 3 characters are "cc," */
	if (strncmp(s, ARGSTR, n) != 0) {
		return (NULL);			/* error */
	}
	s += n;
	if (!*s) {
		return (NULL);			/* no argument */
	}

	len = strlen(s);
	ms = MALLOC(char, len + 1);
	tmps = ms;
	while ((cs = strchr(s, ',')) != NULL) {
		if (cs == s) {
			s++;
			continue;
		}
		if (*(cs - 1) == '\\') {
			if (cs == (s + 1)) {
				*tmps++ = ',';
				s += 2;
			} else {
				len = cs - s - 1;
				(void) strncpy(tmps, s, len);
				tmps += len;
				*tmps++ = ',';
				s = cs + 1;
			}
		} else {
			len = cs - s;
			(void) strncpy(tmps, s, len);
			tmps += len;
			*tmps++ = ' ';
			s = cs + 1;
		}
	}
	len = strlen(s);
	(void) strncpy(tmps, s, len);
	tmps += len;
	*tmps = '\0';
	return (ms);
}

static void
initparse(void)
{
	initlex();
	initgram();
}

int
main(int argc, char *argv[])
{
#ifdef ODEBUG
	int	sym_dump = FALSE;
	int	no_compile = FALSE;
#endif /* ODEBUG */

	int	c;
	int	force = FALSE;
	int	mflag = FALSE;
	int	bits;
	FILE	*fp;
	int	standardp = TRUE;	/* TRUE if only standard options used */
	char	tmp_file_name[] = "./localeXXXXXX";
	char	*ldopts = NULL, *ccopts = NULL, *tpath = NULL;
	char	*cmapsrc = NULL, *locsrc = NULL, *methsrc = NULL;
	char	*cmdstr, *locname, *dirname;
	char	*tmpfilenm, *s, *lc;
	char	*objname32, *objname64, *objname, *soname;
	char	*ccopts32, *ccopts64, *ldopts32, *ldopts64;

	(void) setlocale(LC_ALL, "");
#if	!defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

#ifdef ODEBUG
	yydebug = FALSE;
#endif

	while ((c = getopt(argc, argv, (char *)options)) != -1) {
		switch (c) {
#ifdef ODEBUG
		case 's':
			sym_dump = TRUE;
			standardp = FALSE;
			break;
		case 'd':	/* parser debug */
			yydebug = TRUE;
			standardp = FALSE;
			break;
		case 'n':
			no_compile = TRUE;
			standardp = FALSE;
			break;
#endif /* ODEBUG */
		case 'u':
			code_conv = MALLOC(struct code_conv, 1);
			code_conv->name = optarg;
			if (strcmp(code_conv->name, CS_UTF8) == 0) {
				code_conv->cd = NULL;
			} else {
				code_conv->cd = iconv_open(code_conv->name,
				    CS_UTF8);
				if (code_conv->cd == (iconv_t)-1) {
					(void) fprintf(stderr,
					    gettext(ERR_ICONV_OPEN_USER_U8),
					    code_conv->name);
					exit(4);
				}
			}
			break;

		case 'w':	/* display duplicate definition warnings */
			warning = TRUE;
			standardp = FALSE;
			break;

		case 'c':	/* generate a locale even if warnings */
			force = TRUE;
			break;

		case 'v':	/* verbose table dump */
			verbose++;
			standardp = FALSE;
			break;

		case 'x':	/* specify method file name */
			methsrc = optarg;
			standardp = FALSE;
			break;

		case 'f':	/* specify charmap file name */
			cmapsrc = optarg;
			break;

		case 'i':	/* specify locale source file */
			locsrc = optarg;
			break;

		case 'P':	/* tool path */
			tpath = optarg;
			standardp = FALSE;
			break;

		case 'C':	/* special compiler options */
			ccopts = MALLOC(char, strlen(optarg) + 1);
			(void) strcpy(ccopts, optarg);
			standardp = FALSE;
			break;

		case 'L':	/* special linker options */
			ldopts = optarg;
			standardp = FALSE;
			break;

		case 'm':	/* -m lp64 or -m ilp32 */
			if (mflag == TRUE) {
				(void) fprintf(stderr,
				    gettext(ERR_MULTIPLE_M_OPT));
				usage(4);
			} else {
				mflag = TRUE;
			}
			if (strcmp(optarg, "lp64") == 0) {
				ilp32p = FALSE;
				lp64p = TRUE;
			} else if (strcmp(optarg, "ilp32") == 0) {
				ilp32p = TRUE;
				lp64p = FALSE;
			} else {
				usage(4);	/* Never returns */
			}
			standardp = FALSE;
			break;

		case 'W':	/* -W cc,arg: cc opts */
			if ((ccopts = get_ccarg(optarg)) == NULL) {
				usage(4);
			}
			standardp = FALSE;
			break;

		default:	/* Bad option or invalid flags */
			usage(4);		/* Never returns! */
		}
	}

	if (optind < argc) {
		/*
		 * Create the locale name
		 */
		char	*p;
		size_t	lclen;

		lclen = strlen(argv[optind]);
		if (lclen == 0) {
			/* null string */
			usage(4);
		}
		lc = MALLOC(char, lclen + 1);
		(void) strcpy(lc, argv[optind]);
		if ((p = strrchr(lc, '/')) == NULL) {
			/*
			 * locale name doesn't contain '/'.
			 */
			dirname = NULL;
			locname = MALLOC(char, lclen + 1);
			(void) strcpy(locname, lc);
		} else {
			/*
			 * locale name contains '/'.
			 */
			locname = MALLOC(char, strlen(p));
			(void) strcpy(locname, p + 1);
			dirname = MALLOC(char, (p - lc) + 1);
			(void) strncpy(dirname, lc, (p - lc));
			*(dirname + (p - lc)) = '\0';
		}
	} else {
		usage(4);			/* NEVER returns */
	}

	if (standardp == TRUE) {
		/*
		 * only standard options (-c, -f, and -i) are specified.
		 * localedef generates 32-bit locale object by default.
		 * If the system running localedef supports 64-bit env,
		 * localedef also generates 64-bit locale object.
		 */
		bits = check_isa();
		if (!bits) {
			/*
			 * no ISA info found.
			 */
			(void) fprintf(stderr,
			    gettext(ERR_NO_ISA_FOUND));
			exit(4);
		}
		if (bits & BIT32) {
			ilp32p = TRUE;
			ccopts32 = NULL;
			ldopts32 = NULL;
		}
		if (bits & BIT64) {
			lp64p = TRUE;
			ccopts64 = NULL;
			ldopts64 = NULL;
		}
	} else {
		ccopts32 = ccopts;
		ccopts64 = ccopts;
		ldopts32 = ldopts;
		ldopts64 = ldopts;
	}

	(void) setlocale(LC_CTYPE, "C");
	(void) setlocale(LC_COLLATE, "C");
	/*
	 * seed symbol table with default values for mb_cur_max, mb_cur_min,
	 * and codeset
	 */
	if (cmapsrc != NULL)
		init_symbol_tbl(FALSE);	/* don't seed the symbol table */
	else
		init_symbol_tbl(TRUE);
			/* seed the symbol table with POSIX PCS */

	/* if there is a method source file, process it */

	if (methsrc != NULL) {
		yyfilenm = methsrc;	/* set filename begin parsed for */
				/* error reporting.  */
		infp = fopen(methsrc, "r");
		if (infp == NULL) {
			(void) fprintf(stderr,
			    gettext(ERR_OPEN_READ), methsrc);
			exit(4);
		}

		initparse();
		(void) yyparse();	/* parse the methods file */

		/* restore stdin */
		(void) fclose(infp);	/* close methods file */
	}

	/* process charmap if present */
	if (cmapsrc != NULL) {
		if (charmap.cm_fc_type == _FC_EUC &&
		    charmap.cm_pc_type == _PC_EUC) {
			/*
			 * first charmap pass
			 * only do if we are euc filecode
			 */
			yyfilenm = cmapsrc;
				/* set filename begin parsed for */
				/* error reporting.  */
			infp = fopen(cmapsrc, "r");
			if (infp == NULL) {
				(void) fprintf(stderr,
				    gettext(ERR_OPEN_READ), cmapsrc);
				exit(4);
			}

			initparse();
			Charmap_pass = 1;
			(void) yyparse();	/* parse charmap file */

			(void) fclose(infp);	/* close charmap file */
		}

		/* second charmap pass */

		yyfilenm = cmapsrc;	/* set filename begin parsed for */
			/* error reporting.  */
		infp = fopen(cmapsrc, "r");
		if (infp == NULL) {
			(void) fprintf(stderr,
			    gettext(ERR_OPEN_READ), cmapsrc);
			exit(4);
		}

		initparse();
		Charmap_pass = 2;
		(void) yyparse();		/* parse charmap file */

		/* restore stdin */
		(void) fclose(infp);	/* close charmap file */
	} else {
		define_all_wchars();	/* Act like all code points legal */
	}


	/*
	 * process locale source file.  if locsrc specified use it,
	 * otherwise process input from standard input
	 */

	check_methods();
	/*
	 * if no extension file and no charmap then we
	 * need this.
	 */
	if (locsrc != NULL) {
		yyfilenm = locsrc;
			/* set file name being parsed for */
			/* error reporting. */
		infp = fopen(locsrc, "r");
		if (infp == NULL) {
			(void) fprintf(stderr,
			    gettext(ERR_OPEN_READ), locsrc);
			exit(4);
		}
	} else {
		yyfilenm = "stdin";
		infp = stdin;
	}

	initparse();
	(void) yyparse();

	if (infp != stdin) {
		(void) fclose(infp);
	}

#ifdef ODEBUG
	if (sym_dump) {
		/* dump symbol table statistics */
		dump_symtab();
	}
#endif /* ODEBUG */

	if (!force && err_flag)	/* Errors or Warnings without -c present */
		exit(4);

	/* Open temporary file for locale source.  */
	s = mktemp(tmp_file_name);
	if (*s == '\0') {
		/*
		 * mktemp failed to create  a unique name.
		 */
		(void) fprintf(stderr,
		    gettext(ERR_MKTEMP_FAILED));
		exit(4);
	}
	tmpfilenm = MALLOC(char, strlen(s) + 3); /* Space for ".[co]\0" */
	(void) strcpy(tmpfilenm, s);
	(void) strcat(tmpfilenm, ".c");
	fp = fopen(tmpfilenm, "w");
	if (fp == NULL) {
		(void) fprintf(stderr,
		    gettext(ERR_WRT_PERM), tmpfilenm);
		exit(4);
	}

	/* generate the C code which implements the locale */
	gen(fp);
	(void) fclose(fp);

	/*
	 * check and initialize if necessary
	 * linker/compiler opts and tool paths.
	 */
	soname = MALLOC(char, strlen(locname) + 4 + 10 + 1);
	/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
	(void) sprintf(soname, SONAME, locname, _LC_VERSION_MAJOR);
	objname = MALLOC(char, strlen(lc) + 4 + 10 + 1);
	/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
	(void) sprintf(objname, SONAME, lc, _LC_VERSION_MAJOR);

	if (ilp32p == TRUE) {
		if (ldopts32 == NULL) {
			if (user_specified_libc == FALSE)
				ldopts32 = LINKC;
			else
				ldopts32 = "";
		}
		if (ccopts32 == NULL)
			ccopts32 = CCFLAGS;
		objname32 = objname;
	}
	if (lp64p == TRUE) {
		if (ldopts64 == NULL) {
			if (user_specified_libc == FALSE)
				ldopts64 = LINKC;
			else
				ldopts64 = "";
		}
		if (ccopts64 == NULL)
			ccopts64 = CCFLAGS64;
		if (ilp32p == FALSE) {
			/*
			 * Generate both 32-bit and 64-bit locale object.
			 */
			objname64 = objname;
		} else {
			/*
			 * 64-bit object will be built in ISA64 dir
			 */
			struct stat64	statbuf;
			char	*s;

			errno = 0;
			if (dirname) {
				objname64 = MALLOC(char,
				    strlen(dirname) + SLASH_LEN +
				    strlen(ISA64) + SLASH_LEN +
				    strlen(locname) + SOSFX_LEN + 1);
				/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
				s = objname64 + sprintf(objname64, "%s/%s",
				    dirname, ISA64);
			} else {
				objname64 = MALLOC(char,
				    strlen(ISA64) + SLASH_LEN +
				    strlen(locname) + SOSFX_LEN + 1);
				/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
				s = objname64 + sprintf(objname64, "%s", ISA64);
			}

			if (stat64(objname64, &statbuf) == 0) {
				/* stat succeeded */
				if (S_ISDIR(statbuf.st_mode)) {
					if (access(objname64, W_OK|X_OK) == 0) {
						/*
						 * objname64 is a directory and
						 * writable.
						 */
				/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
						(void) sprintf(s, SLASH_SONAME,
						    locname,
						    _LC_VERSION_MAJOR);
					} else {
						(void) fprintf(stderr,
						    gettext(ERR_ACCESS_DIR),
						    objname64);
						lp64p = FALSE;
					}
				} else {
					(void) fprintf(stderr,
					    gettext(ERR_NON_DIR_EXIST),
					    objname64);
					lp64p = FALSE;
				}
			} else {
				/* stat failed */
				if (errno == ENOENT) {
					/* directory doesn't exist */
					if (mkdir(objname64, 0777) == 0) {
						/* mkdir objname64 succeeded */
				/* LINTED E_SEC_SPRINTF_UNBOUNDED_COPY */
						(void) sprintf(s, SLASH_SONAME,
						    locname,
						    _LC_VERSION_MAJOR);
					} else {
						(void) fprintf(stderr,
						    gettext(ERR_CREATE_DIR),
						    objname64);
						lp64p = FALSE;
					}
				} else {
					(void) fprintf(stderr,
					    gettext(ERR_STAT_FAILED),
					    objname64);
					lp64p = FALSE;
				}
			}

			if (lp64p == FALSE) {
				/* error occurred */
				if (!force) {
					(void) unlink(tmpfilenm);
					exit(4);
				}
				(void) fprintf(stderr,
				    gettext(ERR_NO_64_OBJ));
				err_flag++;
				free(objname64);
			}
		}
	}

	free(dirname);
	tpath = get_tpath(tpath);

	/* compile the C file created */

#ifdef	ODEBUG
	if (no_compile == FALSE) {
#endif
	if (ilp32p == TRUE) {
		/* Generating 32-bit locale object */
		cmdstr = mkcmdline(tpath, ccopts32, soname,
		    objname32, tmpfilenm, ldopts32, 32);
		do_compile(cmdstr, tmpfilenm);
		free(objname32);
		free(cmdstr);
	}
	if (lp64p == TRUE) {
		/* Generating 64-bit locale object */
		cmdstr = mkcmdline(tpath, ccopts64, soname,
		    objname64, tmpfilenm, ldopts64, 64);
		do_compile(cmdstr, tmpfilenm);
		free(objname64);
		free(cmdstr);
	}
#ifdef	ODEBUG
	}
#endif
	free(ccopts);
	free(tpath);

	if (!verbose)
		(void) unlink(tmpfilenm);
	else {
		/* rename to localename.c */
		char *s;

		s = MALLOC(char, strlen(lc) + 3);
		(void) strcpy(s, lc);
		(void) strcat(s, ".c");
		if (rename(tmpfilenm, s) == -1) {
			/* error */
			(void) unlink(tmpfilenm);
		}
		free(s);
	}
	free(tmpfilenm);

	/*
	 * create a text file with 'locname' to keep VSC happy
	 */
	fp = fopen(lc, "w");
	if (fp == NULL) {
		(void) fprintf(stderr,
		    gettext(ERR_WRT_PERM), locname);
		exit(4);
	}
	if (ilp32p == TRUE) {
		(void) fprintf(fp,
		    gettext(DIAG_32_NAME), soname);
		if (copyflag)
			output_copy_notice(fp, 32);
		if (lp64p == TRUE) {
			(void) fprintf(fp, "-----\n");
			(void) fprintf(fp, gettext(DIAG_64_DIR_NAME),
			    ISA64, soname);
			if (copyflag)
				output_copy_notice(fp, 64);
		}
	} else if (lp64p == TRUE) {
		(void) fprintf(fp, gettext(DIAG_64_NAME),
		    soname);
		if (copyflag)
			output_copy_notice(fp, 64);
	}

	(void) fclose(fp);
	free(lc);
	free(locname);
	free(soname);
	return (err_flag != 0);	/* 1=>created with warnings */
				/* 0=>no problems */
}
