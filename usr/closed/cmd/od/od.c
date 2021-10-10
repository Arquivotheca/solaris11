/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

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
 * (C) COPYRIGHT International Business Machines Corp. 1989, 1990
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 * Copyright 1976, Bell Telephone Laboratories, Inc.
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"
/*
 * od -- octal (also hex, decimal, and character) dump
 */

#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wchar.h>
#include <wctype.h>

#define	DBUF_SIZE	BUFSIZ
#define	BSIZE		512
#define	KSIZE		1024
#define	MSIZE		1048576
#define	LBUFSIZE	16
#define	NO		0
#define	YES		1
#define	UNSIGNED	0
#define	SIGNED		1
#define	PADDR		1
#define	MAX_CONV	32	/* Limit on number of conversions */

static struct dfmt {
	int	df_field;	/* external field required for object */
	int	df_size;	/* size (bytes) of object */
	int	df_radix;	/* conversion radix */
	int	df_signed;	/* signed? flag */
	int	df_paddr;	/* "put address on each line?" flag */
	int	(*df_put)();	/* function to output object */
	const char	*df_fmt;	/* output string format */
} *conv_vec[MAX_CONV]; /* vector of conversions to be done */

static void put_addr(off_t addrs, char chr);
static void line(off_t n);
static int s_put(unsigned short *n, struct dfmt *d);
static int us_put(unsigned short *n, struct dfmt *d);
static int l_put(unsigned long *n, struct dfmt *d);
static int sl_put(unsigned long *n, struct dfmt *d);
static int ll_put(uint64_t *n, struct dfmt *d);
static int sll_put(uint64_t *n, struct dfmt *d);
static int d_put(double *f, struct dfmt *d);
static int ld_put(long double *ld, struct dfmt *d);
static int f_put(float *f, struct dfmt *d);
static int a_put(unsigned char *cc, struct dfmt *d);
static int b_put(unsigned char *b, struct dfmt *d);
static int sb_put(char *bc, struct dfmt *d);
static char *scvt(int c, struct dfmt *d);
static char *icvt(off_t value, int radix, int issigned, int ndigits);
static off_t get_addr(char *s);
static off_t get_offset(char *s);
static int offset(off_t a, off_t *bytes_read);
static void usage(char *progname);
static int mbswidth(const char *s, size_t n);
static int C_put(unsigned char *cc, struct dfmt *d);
#ifndef XPG4
static int c_put(unsigned char *cc, struct dfmt *d);
static void cput(int c);
#endif
static void odopen(int *argcnt, char **argvec, int *exitstat, int fd,
			off_t max_bytes);
static void pre(int f, int n);
static void putn(uint64_t n, int b, int c);
static void last_addr();

/* -t a */
static struct dfmt ascii = {3, sizeof (char), 10, 0, PADDR,  a_put, 0};
/* -t o1, -b */
static struct dfmt byte = {3, sizeof (char), 8, UNSIGNED, PADDR, b_put, 0};
#ifndef XPG4
/* Solaris -c */
static struct dfmt cchar = {3, sizeof (char), 8, UNSIGNED, PADDR, c_put, 0};
#endif
/* -t c, -C, XPG4 -c */
static struct dfmt Cchar = {3, sizeof (char), 8, UNSIGNED, PADDR, C_put, 0};
/* -t o2, -o */
static struct dfmt u_s_oct = {6, sizeof (short), 8, UNSIGNED, PADDR, us_put, 0};
/* -t u1 */
static struct dfmt u_c_dec = {3, sizeof (char), 10, UNSIGNED, PADDR, b_put, 0};
/* -t u2, -d */
static struct dfmt u_s_dec = {5, sizeof (short), 10, UNSIGNED, PADDR, us_put,
									0};
/* -t x2, -x */
static struct dfmt u_s_hex = {4, sizeof (short), 16, UNSIGNED, PADDR, us_put,
									0};
/* -t x1 */
static struct dfmt u_c_hex = {2, sizeof (char), 16, UNSIGNED, PADDR, b_put, 0};
/* -t o4, -O */
static struct dfmt u_l_oct = {11, sizeof (long), 8, UNSIGNED, PADDR, l_put, 0};
/* -t u4, -D */
static struct dfmt u_l_dec = {10, sizeof (long), 10, UNSIGNED, PADDR, l_put, 0};
/* -t x4, -X */
static struct dfmt u_l_hex = {8, sizeof (long), 16, UNSIGNED, PADDR, l_put, 0};
/* -t d1 */
static struct dfmt s_c_dec = {3, sizeof (char), 10, SIGNED, PADDR, sb_put, 0};
/* -t d2, -s */
static struct dfmt s_s_dec = {6, sizeof (short), 10, SIGNED, PADDR, s_put, 0};
/* -t d4, -S */
static struct dfmt s_l_dec = {11, sizeof (long), 10, SIGNED, PADDR, sl_put, 0};
/* -t d8 */
static struct dfmt s_8_dec = {20, sizeof (int64_t), 10, SIGNED, PADDR, sll_put,
									0};
/* -t o8 */
static struct dfmt u_8_oct = {22, sizeof (uint64_t), 8, UNSIGNED, PADDR, ll_put,
									0};
/* -t x8 */
static struct dfmt u_8_hex = {16, sizeof (uint64_t), 16, UNSIGNED, PADDR,
								ll_put, 0};
/* -t u8 */
static struct dfmt u_8_dec = {20, sizeof (uint64_t), 10, UNSIGNED, PADDR,
								ll_put, 0};
/* -t f4, -f */
static struct dfmt flt = {14, sizeof (float), 10, SIGNED, PADDR, f_put, 0};
/* -t f8, -F */
static struct dfmt dble = {21, sizeof (double), 10, SIGNED, PADDR, d_put, 0};
/* -t fL */
static struct dfmt ldble = {21, sizeof (long double), 10, SIGNED, PADDR,
ld_put, 0};


static char	dbuf[DBUF_SIZE];	/* input buffer */
static char	mbuf[DBUF_SIZE];	/* buffer used if 2-byte crosses line */
static char	lastdbuf[DBUF_SIZE];
static int	addr_base	= 8;	/* default address base is OCTAL */
static off_t	addr		= 0L;	/* current file offset */
static int	dbuf_size	= 16;	/* file bytes / display line */
static const char	fmt[]	= "            %s";	/* 12 blanks */

/*
 * force wdbuf to be doubleword aligned
 */
static union {
	double x;
	wchar_t	wdbuf[DBUF_SIZE/sizeof (wchar_t)+1];
} wdun;

/*
 * We must save global values and reset them for each call to double byte
 * routines.  For example if you call -CCCCCC, you want print the same line
 * six times.
 */
static int straggle = 0;		/* Did word cross line boundary? */
static int straggle_save = 0;
static int nls_shift_save = 0;		/* last character was a shift char */
static int nls_skip_save = 0;		/* need to skip this byte	   */
static int already_read = 0;		/* next buffer already read in.    */
static int nls_skip = 0;
static int changed = 0;
static int nls_shift = 0;
static int Aflag = 0; /* -A option */
static int jflag = 0; /* -j option */
static int tflag = 0; /* -t option */
static int Nflag = 0; /* -N option */
static int max_llen = 0;
static off_t bytes = 0; /* number of bytes read */
static off_t addrbytes = 0;
static int npmb = 0;

int
main(int argc, char *argv[])
{
	char *p;
	int same;
	struct dfmt	*d = NULL;
	struct dfmt	**cv = conv_vec;
	int	showall = NO;
	int	field, llen, nelm;
	off_t	max_bytes = 0;
	int	posixflag = 0;
	char 	*ptr;
	char 	*addr_ptr;
	int	xbytes;
	extern 	char *optarg;		/* getopt support */
	extern	int optind;
	int	c;
	int	fd;
	int	retval = 0;
	int	offsetfound = 0;
	off_t	seekval, bytes_read;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"  /* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	errno = 0;

	/*
	 * Parse arguments and set up conversion vector
	 */

	while ((c = getopt(argc, argv,
		":A:bCcDdFfj:N:oOsSt:vxX?0123456789")) != EOF) {

	switch (c) {
	/*
	 * Specify the input offset base
	 */
	case 'A':
		Aflag = 1;
		switch (*optarg) {
		case 'd':
			addr_base = 10;
			break;
		case 'o':
			addr_base = 8;
			break;
		case 'n':
			Aflag = 2;
			break;
		case 'x':
			addr_base = 16;
			break;
		default:
			(void) fprintf(stderr, gettext(
		"-A option only accepts the following:  d, o, n, and x\n"));
			usage("od");
		}
		continue;
	case 'b':
		d = &byte;
		break;
	case 'C':
		d = &Cchar;
		break;
	case 'c':
#ifdef XPG4
		d = &Cchar;
#else
		d = &cchar;
#endif
		break;
	case 'd':
		d = &u_s_dec;
		break;
	case 'D':
		d = &u_l_dec;
		break;
	case 'F':
		d = &dble;
		break;
	case 'f':
		d = &flt;
		break;
	case 'x':
		d = &u_s_hex;
		break;
	case 'X':
		d = &u_l_hex;
		break;
	case 's':
		d = &s_s_dec;
		break;
	case 'S':
		d = &s_l_dec;
		break;
	/*
	 * Specify number of bytes to interpret
	 */
	case 'N':
		Nflag++;
		max_bytes = (off_t)strtoll(optarg, &ptr, 0);
		if ((max_bytes <= 0) || errno || (optarg == ptr))   {
			(void) fprintf(stderr, gettext(
				"Invalid number of bytes to interpret\n"));
			usage("od");
		}
		continue;
	case 'o':
		d = &u_s_oct;
		break;
	case 'O':
		d = &u_l_oct;
		break;
	/*
	 * Specify number of bytes to skip
	 */
	case 'j':
		jflag++;
		addr_ptr = optarg;
		continue;
	/*
	 * Parse type_string
	 */
	case 't':
		tflag++;
		p = optarg;
		for (; *p; p++) {
			switch (*p) {
			case 'a':
				d = &ascii;
				break;
			case 'c':
				d = &Cchar;
				break;

			/*
			 *  Type specifier d may be followed
			 *  by a letter or number indicating
			 *  the number
			 *  of bytes to be transformed.  If
			 *  invalid, use default.
			 */
			case 'd':
				d = &s_l_dec;
				if (isupper(*(p+1)) || isdigit(*(p+1))) {
					switch (*(++p)) {
					case '1':
					case 'C':
						d = &s_c_dec;
						break;
					case '2':
					case 'S':
						d = &s_s_dec;
						break;
					case '4':
					case 'I':
						break;
					case 'L':
						d = &s_l_dec;
						break;
					case '8':
						d = &s_8_dec;
						break;
					default:
						(void) fprintf(stderr,
	    gettext("d may only be followed with C, S, I, L, 1, 2, 4, or 8\n"));
						usage("od");
					}
				}
				break;

			/*
			 *  Type specifier f may be followed
			 *  by F, D, or L indicating that
			 *  coversion should be applied to
			 *  an item of type float, double
			 *  or long double.  OR a number
			 *  indicating the number of bytes.
			 */
			case 'f':
				d = &dble;
				if (isupper(*(p+1)) || isdigit(*(p+1))) {
					switch (*(++p)) {
					case '4':
					case 'F':
						d = &flt;
						break;
					case '8':
					case 'D':
						break;
					case 'L':
						d = &ldble;
						break;
					default:
						(void) fprintf(stderr,
		gettext("f may only be followed with F, D, L, 4, or 8\n"));
						usage("od");
					}
				}

				break;

			/*
			 *  Type specifier o may be followed
			 *  by a letter or a number indicating the number
			 *  of bytes to be transformed.  If
			 *  invalid, use default.
			 */
			case 'o':
				d = &u_l_oct;
				if (isupper(*(p+1)) || isdigit(*(p+1))) {
					switch (*(++p)) {
					case '1':
					case 'C':
						d = &byte;
						break;
					case '2':
					case 'S':
						d = &u_s_oct;
						break;
					case '4':
					case 'I':
						break;
					case 'L':
						d = &u_l_oct;
						break;
					case '8':
						d = &u_8_oct;
						break;
					default:
						(void) fprintf(stderr,
	gettext("o may only be followed with C, S, I, L, 1, 2, 4, or 8\n"));
						usage("od");
					}
				}
				break;

			/*
			 *  Type specifier u may be followed
			 *  by a letter or a number indicating the number
			 *  of bytes to be transformed.  If
			 *  invalid, use default.
			 */
			case 'u':
				d = &u_l_dec;
				if (isupper(*(p+1)) || isdigit(*(p+1))) {
					switch (*(++p)) {
					case '1':
					case 'C':
						d = &u_c_dec;
						break;
					case '2':
					case 'S':
						d = &u_s_dec;
						break;
					case '4':
					case 'I':
						break;
					case 'L':
						d = &u_l_dec;
						break;
					case '8':
						d = &u_8_dec;
						break;
					default:
						(void) fprintf(stderr,
	gettext("u may only be followed with C, S, I, L, 1, 2, 4, or 8\n"));
						usage("od");
					}
				}
				break;

			/*
			 *  Type specifier x may be followed
			 *  by a letter or a number indicating the number
			 *  of bytes to be transformed.  If
			 *  invalid, use default.
			 */
			case 'x':
				d = &u_l_hex;
				if (isupper(*(p+1)) || isdigit(*(p+1))) {
					switch (*(++p)) {
					case '1':
					case 'C':
						d = &u_c_hex;
						break;
					case '2':
					case 'S':
						d = &u_s_hex;
						break;
					case '4':
					case 'I':
						break;
					case 'L':
						d = &u_l_hex;
						break;
					case '8':
						d = &u_8_hex;
						break;
					default:
						(void) fprintf(stderr,
	    gettext("x may only be followed with C, S, I, L, 1, 2, 4, or 8\n"));
						usage("od");
					}
				}
				break;
			default:
				usage("od");
			}
			if ((cv - conv_vec) < MAX_CONV)
				*(cv++) = d;
			else {
				(void) fprintf(stderr,
				gettext("Too many conversions specified.\n"));
				usage("od");
			}
		}
		continue;
	case 'v':
		showall = YES;
		continue;

	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		usage("od");
		continue;

	case '?':
	default:
		usage("od");
	}
	if (cv - conv_vec < MAX_CONV)
		*(cv++) = d;
	else {
	    (void) fprintf(stderr, gettext("Too many conversions specified\n"));
	    usage("od");
	}

	} /* while getopt */

	if (Aflag || jflag || Nflag || tflag)
		posixflag++;			/* Filename can begin w/ '+' */

	argv += optind;
	argc -= optind;

	/*
	 * if nothing spec'd, setup default conversion.
	 */
	if (cv == conv_vec)
		*(cv++) = &u_s_oct;

	*cv = (struct dfmt *)0;

	/*
	 * calculate display parameters
	 */
	for (cv = conv_vec; (d = *cv) != 0; cv++) {
		nelm = (dbuf_size + d->df_size - 1) / d->df_size;
		llen = nelm * (d->df_field + 1);
		if (llen > max_llen)
			max_llen = llen;
	}

	/*
	 * setup df_fmt to point to uniform output fields.
	 */
	for (cv = conv_vec; (d = *cv) != 0; cv++) {
		if (d->df_field)	/* only if external field is known */
		{
			nelm = (dbuf_size + d->df_size - 1) / d->df_size;
			field = max_llen / nelm;
			d->df_fmt = fmt + 12 - (field - d->df_field);
		}
	}

	fd = dup(0);
	if (argc > 0) {
		/*
		 * Check first operand whether input file specified.  If so,
		 * freopen stdin as new file.
		 */
		if (argc > 1 || **argv != '+' || posixflag) {
			/* this operand is not an offset; open the file */
#ifndef XPG4
			if (argv[0][0] != '-' || argv[0][1] != '\0') {
				/*
				 * Not "-", ie: didn't specify to use stdin in
				 * addition to any files given.
				 */
				odopen(&argc, argv, &retval, fd, max_bytes);
			}
#else
			odopen(&argc, argv, &retval, fd, max_bytes);
#endif
			argv++;
			argc--;
		}
	}

	/*
	 *  Advance into the concatenated input files the number of bytes
	 *  specified via -j
	 */
	if (jflag) {
		seekval = get_addr(addr_ptr);
		/* file already open */
		while (!offsetfound && argc >= 0) {
			if (offset(seekval, &bytes_read))
				offsetfound = 1;
			else {
				seekval -= bytes_read;
				/* open next file */
				odopen(&argc, argv, &retval, fd, max_bytes);
				argv++;
				argc--;
			}
		}
		if (!offsetfound) {
			(void) fprintf(stderr, gettext("EOF\n"));
			return (1);
		}
#ifdef XPG4
	} else if (argc == 1 && !posixflag &&
			(**argv == '+' || isdigit(**argv))) {
		/*
		 * XPG4:
		 * If there are no more than two operands,
		 * none of the -A, -j, -N, or -t options is specified,
		 * and any of the following are true:
		 * 1. the first character of the last operand is a
		 *    plus sign (+)
		 * 2. the first character of the second operand is
		 *    numeric
		 * then the corresponding operand is assumed to be an
		 * offset operand rather than a file operand.
		 */
#else
	} else if (argc == 1 && !posixflag &&
			(**argv == '+' || isdigit(**argv) ||
			(**argv == '.' && argv[0][1] == '\0') ||
			(**argv == 'x' && argv[0][1] == '\0') ||
			(**argv == 'x' && isxdigit(argv[0][1]) &&
			argv[0][1] != 'A' && argv[0][1] != 'B' &&
			argv[0][1] != 'C' && argv[0][1] != 'D' &&
			argv[0][1] != 'E' && argv[0][1] != 'F'))) {
		/*
		 * Solaris:
		 * If there are no more than two operands,
		 * none of the -A, -j, -N, or -t options is specified,
		 * and any of the following are true:
		 * 1. the first character of the last operand is a
		 *    plus sign (+)
		 * 2. the first character of the second operand is
		 *    numeric
		 * 3. the second operand is named "x"
		 * 4. the second operand is named "."
		 * 5. the first character of the second operand is x
		 *    and the the second character of the second
		 *    operand is a lower case hex character or digit
		 * then the corresponding operand is assumed to be an
		 * offset operand rather than a file operand.
		 */
#endif
		addr = get_offset(*argv);
		if (!offset(addr, &bytes_read)) {
			put_addr(addr, '\n');
			return (0);
		}
		argv++;
		argc--;
	}

	same = -1;
	/*
	 *  Process either file or stdin.  Open new files and
	 *  continue processing as necessary.
	 */
	while (argc >= 0) {
		/*
		 * main dump loop
		 */
		while (already_read || (bytes = fread((void *)dbuf,
				(size_t)1, (size_t)dbuf_size, stdin)) > 0) {
			if (already_read) {
				already_read = 0;
				(void) memcpy(dbuf, mbuf, dbuf_size);
			} else {
				if (bytes < dbuf_size) {
					(void) memset((void*)(dbuf + bytes),
					    (uchar_t)0, (size_t)(dbuf_size -
					    bytes));
				}
			}

			/*
			 *  If more than one file is specified and
			 *  the current file does not fill the buffer,
			 *  open the next file and continue processing.
			 */
			while ((bytes < dbuf_size) && (argc > 0)) {
				if (Nflag && bytes >= max_bytes) {
					/*
					 * We have already read the amount of
					 * data requested via -N option.
					 */
					break;
				}
				odopen(&argc, argv, &retval, fd, max_bytes);
				argv++;
				argc--;
				ptr = dbuf + bytes;
				if ((xbytes = fread((void *)ptr, (size_t)1,
					(size_t)(dbuf_size-bytes), stdin)) > 0)
					bytes += xbytes;
			}
			if (same >= 0 && !showall && memcmp((void *)dbuf,
			    (void *)lastdbuf, (size_t)dbuf_size) == 0) {
				if (same == 0) {
					(void) printf("*\n");
					same = 1;
				}
			} else {
				same = 0;
				(void) memcpy(lastdbuf, dbuf, dbuf_size);
			}
			/*
			 * If Nflag is specified, only print
			 * max_bytes bytes
			 * update max_bytes correctly even when Nflag is
			 * specified.
			 */
			if (!Nflag) {
				if (!same)
					line(bytes);
			} else {
				if (max_bytes > bytes) {
					if (!same)
						line(bytes);
					max_bytes -= bytes;
				} else {
					if (max_bytes < bytes) {
						(void) memset(dbuf+max_bytes,
							0, bytes - max_bytes);
						if (!same)
							line(max_bytes);
	/*
	 * XCU4:  P1003.2/D8 Sect. 4.45.5.2 052(A)
	 * GA34: using the 'file' operand.
	 * When a standard utility reads a seekable input file and
	 * terminates without an error before it reaches end-of-file, then the
	 * utility ensures that the file offset in the open file description is
	 * properly positioned just past the last byte processed by the utility.
	 */
						/*
						 * not an error if
						 * fseek() fails
						 * ie: not a seekable
						 * input file
						 */
						(void) fseeko(stdin,
						    -(bytes - max_bytes),
						    SEEK_CUR);
					} else if (!same)
						line(max_bytes);
					addr += max_bytes;
					last_addr();
					return (retval);
				}
			}
			if (addrbytes > 0)
				addr += addrbytes;
			else
				addr += bytes;

		}

		/*
		 * If there are more files, open and
		 * continue processing.
		 */
		if (argc == 0)
			break;
		else
		{
			odopen(&argc, argv, &retval, fd, max_bytes);
			argv++;
			argc--;
		}
	}

	/*
	 * Some conversions require "flushing".
	 */
	bytes = 0;
	for (cv = conv_vec; *cv; cv++) {
		if ((*cv)->df_paddr) {
			if (bytes++ == 0) {
				if (Aflag == 2)
					(void) fputc('\n', stdout);
				else if (argc <= 0)
					put_addr(addr, '\n');
			}
		}
		else
			(*((*cv)->df_put))(0, *cv);
	}
	return (retval);
}

/*
 * NAME: put_addr
 *
 * FUNCTION:  Print out the current file address.
 *
 */

void
put_addr(off_t addrs, char chr)
{
	(void) fputs(icvt(addrs, addr_base, UNSIGNED, 7), stdout);
	if (chr != '\0')
		(void) putchar(chr);

}

/*
 * NAME: line
 *
 * FUNCTION:  When line is called we have determined the line is different
 *		from the previous line.  We then print it out in all the
 *		formats called for in the command line.
 *
 */
void
line(off_t n)
{
	off_t i;
	int first;
	struct dfmt *c;
	struct dfmt **cv = conv_vec;
	int 	newstraggle = 0,
		newnls_skip = 0,
		newnls_shift = 0;

	straggle_save = straggle;
	nls_shift_save = nls_shift;
	nls_skip_save = nls_skip;
	first = YES;
	while ((c = *cv++)) {
		straggle = straggle_save;
		nls_shift = nls_shift_save;
		nls_skip = nls_skip_save;
		if (c->df_paddr) {
			if (first) {
				if (Aflag == 2)
					(void) fputc('\t', stdout);
				else
					put_addr(addr, '\0');
				first = NO;
			}
			else
				(void) putchar('\t');
		}
		i = 0;
		changed = 0;
#ifdef XPG4
		if (c == &Cchar) {
#else
		if (c == &Cchar || c == &cchar) {
#endif
			/*
			 * -t c, -C, -c
			 * Keep data in dbuf for char handling.
			 */
			while (i < n)
				i += (*(c->df_put))(dbuf+i, c);
		} else {
			/*
			 * Data is copied to wdbuf to avoid address alignment
			 * problems caused by passing dbuf.
			 */
			while (i < n) {
				(void) memcpy(wdun.wdbuf, dbuf+i, c->df_size);
				i += (*(c->df_put))(wdun.wdbuf, c);
			}
		}

		if (changed) {
			newstraggle |= straggle;
			newnls_shift |= nls_shift;
			newnls_skip |= nls_skip;
		}

		if (c->df_paddr)
			(void) putchar('\n');
	}
	straggle = newstraggle;
	nls_skip = newnls_skip;
	nls_shift = newnls_shift;
}

/*
 * NAME: s_put
 *
 * FUNCTION: Print out a signed short.
 *
 * RETURN VALUE DESCRIPTION:  size of a short.
 *
 */

int
s_put(unsigned short *n, struct dfmt *d)
{
	(void) putchar(' ');
	if (*n > 32767) {
		pre(d->df_field, d->df_size);
		(void) putchar('-');
		*n = (~(*n) + 1) & 0177777;
	} else
		pre(d->df_field-1, d->df_size);
	putn((uint64_t)*n, d->df_radix, d->df_field-1);
	return (d->df_size);
}

/*
 * NAME: us_put
 *
 * FUNCTION: Print out an unsigned short (hex, oct, dec).
 *
 * RETURN VALUE DESCRIPTION: size of a short
 *
 */
int
us_put(unsigned short *n, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	putn((uint64_t)*n, d->df_radix, d->df_field);
	return (d->df_size);
}

#ifndef XPG4
/*
 * NAME: c_put
 *
 * FUNCTION: print out a single byte character
 *
 * RETURN VALUE:  number of bytes in character (1)
 *
 */
int
c_put(unsigned char *cc, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	cput(*cc);
	return (d->df_size);
}

void
cput(int c)
{
	c &= 0377;
	if (c >= 0200 && MB_CUR_MAX > 1)
		putn((uint64_t)c, 8, 3); /* Multi Envir. with Multi byte char */
	else {
		if (isprint(c)) {
			(void) printf("  ");
			(void) putchar(c);
			return;
			}
		switch (c) {
		case '\0':
			(void) printf(" \\0");
			break;
		case '\b':
			(void) printf(" \\b");
			break;
		case '\f':
			(void) printf(" \\f");
			break;
		case '\n':
			(void) printf(" \\n");
			break;
		case '\r':
			(void) printf(" \\r");
			break;
		case '\t':
			(void) printf(" \\t");
			break;
		default:
			putn((uint64_t)c, 8, 3);
		}
	}
}
#endif /* XPG4 not defined, ie: Solaris -c option */

/*
 * NAME: l_put
 *
 * FUNCTION:  Print out an unsigned long (octal, hex, decimal).
 *
 * RETURN VALUE DESCRIPTION:  size of a long.
 *
 */
int
l_put(unsigned long *n, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	putn((uint64_t)*n, d->df_radix, d->df_field);
	return (d->df_size);
}

/*
 * NAME: sl_put
 *
 * FUNCTION:  Print out a signed long.
 *
 * RETURN VALUE DESCRIPTION:  size of a long.
 *
 */
int
sl_put(unsigned long *n, struct dfmt *d)
{
	if (*n > 2147483647) {
		pre(d->df_field, d->df_size);
		(void) putchar('-');
		*n = (~(*n)+1) & 037777777777;
	}
	else
		pre(d->df_field-1, d->df_size);
	putn((uint64_t)*n, d->df_radix, d->df_field-1);
	return (d->df_size);
}

/*
 * NAME: ll_put
 *
 * FUNCTION:  Print out an unsigned long long (unsigned
 * 64-bit value) in octal, hex, or decimal.
 *
 * RETURN VALUE DESCRIPTION:  size of a long long.
 *
 */
int
ll_put(uint64_t *n, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	putn(*n, d->df_radix, d->df_field);
	return (d->df_size);
}

/*
 * NAME: sll_put
 *
 * FUNCTION:  Print out a signed long long (signed 64-bit value).
 *
 * RETURN VALUE DESCRIPTION:  size of a long long.
 *
 */
int
sll_put(uint64_t *n, struct dfmt *d)
{
	if (*n > INT64_MAX) {
		pre(d->df_field, d->df_size);
		(void) putchar('-');
		*n = (~(*n)+1) & 01777777777777777777777ULL;
	}
	else
		pre(d->df_field-1, d->df_size);
	putn(*n, d->df_radix, d->df_field-1);
	return (d->df_size);
}

void
pre(int f, int n)
{
	int i, m;

	m = (max_llen/(LBUFSIZE/n)) - f;
	for (i = 0; i < m; i++)
		(void) putchar(' ');
}

void
putn(uint64_t n, int b, int c)
{
	uint64_t d;

	if (!c)
		return;
	putn(n/b, b, c-1);
	d = n%b;
	if (d > 9)
		(void) putchar(d-10+'a');
	else
		(void) putchar(d+'0');
}

/*
 * NAME: d_put
 *
 * FUNCTION: Print out a double.
 *
 * RETURN VALUE DESCRIPTION: size of a double
 *
 */
int
d_put(double *f, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	(void) printf("%21.14e", *f);
	return (d->df_size);
}

/*
 * NAME: ld_put
 *
 * FUNCTION: Print out a long double.
 *
 * RETURN VALUE DESCRIPTION: size of a long double
 *
 */
int
ld_put(long double *ld, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	(void) printf("%21.14Le", *ld);
	return (d->df_size);
}

/*
 * NAME: f_put
 *
 * FUNCTION: Print out a float.
 *
 * RETURN VALUE DESCRIPTION: size of a float.
 *
 */
int
f_put(float *f, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	(void) printf("%14.7e", *f);
	return (d->df_size);
}


static char	asc_name[34][4] = {
/* 000 */	"nul",	"soh",	"stx",	"etx",	"eot",	"enq",	"ack",	"bel",
/* 010 */	" bs",	" ht",	" lf",	" vt",	" ff",	" cr",	" so",	" si",
/* 020 */	"dle",	"dc1",	"dc2",	"dc3",	"dc4",	"nak",	"syn",	"etb",
/* 030 */	"can",	" em",	"sub",	"esc",	" fs",	" gs",	" rs",	" us",
/* 040 */	" sp",	"del"
};

/*
 * NAME: a_put
 *
 * FUNCTION:  print out value in ascii, using known values for unprintables.
 *
 * RETURN VALUE:  1
 *
 */
int
a_put(unsigned char *cc, struct dfmt *d)
{
	int c = *cc;
	char	s[4] = {' ', ' ', ' ', 0 };	/* 3 spaces, NUL terminated */

	c &= 0177;
	if (isgraph(c)) {
		s[2] = c;
		(void) printf(d->df_fmt, s);
	}
	else
	{
		if (c == 0177)
			c = ' ' + 1;
		(void) printf(d->df_fmt, asc_name[c]);
	}
	return (1);
}

/*
 * NAME: b_put
 *
 * FUNCTION: Print out an unsigned byte (hex, oct, dec).
 *
 * RETURN VALUE:  1
 *
 */
int
b_put(unsigned char *b, struct dfmt *d)
{
	pre(d->df_field, d->df_size);
	putn((uint64_t)(*b)&0377, d->df_radix, d->df_field);
	return (1);
}

/*
 * NAME: sb_put
 *
 * FUNCTION: Print out a signed byte.
 *
 * RETURN VALUE:  1
 *
 */
int
sb_put(char *bc, struct dfmt *d)
{
	(void) printf(d->df_fmt,
		icvt((off_t)*bc, d->df_radix, d->df_signed, d->df_field));
	return (1);
}

/*
 * NAME: C_put
 *
 * FUNCTION: print out a character (multibyte or single byte)
 *
 * RETURN VALUE:  number of bytes in character (usually 1)
 *
 */

int
C_put(unsigned char *cc, struct dfmt *d)
{
	register char	*s;
	register int	n;
	register int	c = *cc;
	int mbcnt;
	int mbufcnt = 0;
	char mbchar[MB_LEN_MAX+1];
	static wchar_t  pcode;
	int rc;
	int numbytes;

	addrbytes = 0;
	changed++;
	mbcnt = mblen((const char *)cc, MB_CUR_MAX);
	if ((!nls_shift) && (mbcnt != 0)) {
	    npmb = 0;
	    if ((mbcnt == 1) && (!((c) & 0200))) {
			/* one byte char and high bit is not set */
			s = scvt(c, d);
	    } else {
		/*
		 * Either found the start of a multibyte character (mbcnt > 1)
		 * or a single byte character with high bit set,
		 * or an illegal sequence, _OR_ we have a character crossing a
		 * block-boundary (and need to read some more in...)
		 */
		int mbl;

		if (mbcnt == 1)
			mbl = 0;
		else
			mbl = 1;


		nls_shift += (mbcnt > 0)? (mbcnt - 1) : 0;
		nls_skip += (mbcnt > 0)? (mbcnt - 1) : 0;
		mbchar[0] = c;

loop:
		do {
		    if (cc[mbl] == '\0') /* Was end of buffer, get some more */
		    {
			straggle++;
			if (!already_read) {
				if ((numbytes = fread((void *)mbuf, (size_t)1,
					(size_t)dbuf_size, stdin)) <= 0) {
					/*
					 * Only part of a double byte character
					 * found.
					 */
					straggle = 0;
					already_read = 0;
					mbchar[mbl] = 0;
					mbcnt = mbl;
				}
				else
				{
					/*
					 * addrbytes is used to save the byte
					 * value for incrementing the address
					 * when C_put() reads more data into
					 * mbuf.  In all other cases, addrbytes
					 * will be 0 and bytes will be used to
					 * increment the address.
					 */
					addrbytes = bytes;
					bytes = numbytes;
					already_read++;
					mbchar[mbl] = mbuf[mbufcnt];
					mbl++;
					mbufcnt++;
				}
			} else {
				mbchar[mbl] = mbuf[mbufcnt];
				mbl++;
				mbufcnt++;
				if (mbl > bytes) {
					mbchar[mbl] = '\0';
					mbcnt = nls_skip = nls_shift = mbl;
					goto next;
				}
			}
		    } else
			mbchar[mbl] = cc[mbl];
		    mbl++;

		} while (mbl < mbcnt);

		mbchar[mbl] = '\0';
		mbcnt = mblen(mbchar, MB_CUR_MAX);
		if (!nls_shift)
			nls_shift += (mbcnt > 0)? (mbcnt - 1) : 0;
		if (!nls_skip)
			nls_skip += (mbcnt > 0)? (mbcnt - 1) : 0;
		/*
		 *  we must handle the case if the
		 *  multi byte char is split on a line
		 *  boundary, mblen will return -1, but
		 *  we have to check next char to see if
		 *  it's part of multi byte char therefore
		 *  we go to loop.
		 */
		if (mbcnt == -1) {
			if (mbl < (int)MB_CUR_MAX)
				goto loop;
			else {
				mbcnt = 1;
				mbchar[mbcnt] = '\0';
				nls_skip = 0;
				nls_shift = 0;
			}
		}
next:
		rc = mbtowc(&pcode, mbchar, sizeof (mbchar));
		if (rc <= 0 || !iswprint(pcode)) {
			/*
			 * Set the npmb flag to indicate that all bytes of
			 * this non-printable multibyte character should be
			 * printed as three-digit octal numbers, even if
			 * one of the bytes is printable on its own.
			 */
			++npmb;
			s = scvt(c, d);
		} else
			s = mbchar;
	    }

	} else if (nls_skip) {
		nls_skip = (nls_skip > 0)? nls_skip - 1: 0;
		nls_shift = (nls_shift > 0)? nls_shift - 1: 0;
		if (npmb) {
			/* non-printable multi-byte char */
			s = scvt(c, d);
			(void) printf(d->df_fmt, s);
		} else {
			/* printable multi-byte char */
			(void) fputs("  **", stdout);
		}

		straggle = 0;
		return (1);
	} else {
		npmb = 0;
		s = scvt(c, d);
	}
	for (n = d->df_field - mbswidth(s, d->df_field); n > 0; n--)
		(void) putchar(' ');
	(void) printf(d->df_fmt, s);

	return ((nls_skip > 0 && mbcnt > 2)? mbcnt - nls_skip : 1);
}

/*
 * NAME: scvt
 *
 * FUNCTION:  convert the character to a representable string.
 *
 * RETURN VALUE:  A pointer to the string.
 *
 */
char *
scvt(int c, struct dfmt *d)
{
	static char s[2];

	c &= 0377;
	if (npmb) {
		/*
		 * Print the 3 digit octal value for the byte in the
		 * multibyte char, regardless of whether this byte is
		 * printable on its own.
		 */
		return (icvt((off_t)c, d->df_radix, d->df_signed, d->df_field));
	} else {
		switch (c) {
		case '\0':
			return ("\\0");

		case '\a':
			if (tflag)
				return ("\\a");
			else
				return (icvt((off_t)c, d->df_radix,
					d->df_signed, d->df_field));

		case '\b':
			return ("\\b");

		case '\f':
			return ("\\f");

		case '\n':
			return ("\\n");

		case '\r':
			return ("\\r");

		case '\t':
			return ("\\t");

		case '\v':
			if (tflag)
				return ("\\v");
			else
				return (icvt((off_t)c, d->df_radix,
					d->df_signed, d->df_field));

		default:
			if (isprint(c)) {
				/*
				 * Depends on "s" being STATIC
				 * initialized to zero
				 */
				s[0] = c;
				return (s);
			}
			return (icvt((off_t)c, d->df_radix, d->df_signed,
							d->df_field));
		}
	}
}

/*
 * integer to ascii conversion
 *
 * This code has been rearranged to produce optimized runtime code.
 */

#define	MAXINTLENGTH	32
static char	_digit[] = "0123456789abcdef";
static char	_icv_buf[MAXINTLENGTH+1];
static long	_mask = 0x7fffffff;

/*
 * NAME: icvt
 *
 * FUNCTION: return from a given stream the first value.
 *
 * RETURN VALUE:  the value is an ascii stream printed in the RADIX given.
 */
char *
icvt(off_t value, int radix, int issigned, int ndigits)
{
	off_t	val = value;
	off_t	rad = (off_t)radix;
	char	*b = &_icv_buf[MAXINTLENGTH];
	char	*d = _digit;
	off_t	tmp1;
	off_t	tmp2;
	off_t	rem;
	off_t	kludge;
	int	sign;

	if (val == 0) {
		*--b = '0';
		sign = 0;
		goto done;
	}

	if (issigned && (sign = (val < 0)))	/* signed conversion */
	{
		/*
		 * It is necessary to do the first divide
		 * before the absolute value, for the case -2^31
		 *
		 * This is actually what is being done...
		 * tmp1 = (int)(val % rad);
		 * val /= rad;
		 * val = -val
		 * *--b = d[-tmp1];
		 */
		tmp1 = val / rad;
		*--b = d[(tmp1 * rad) - val];
		val = -tmp1;
	} else				/* unsigned conversion */
	{
		sign = 0;
		if (val < 0) {
			/* ALL THIS IS TO SIMULATE UNSIGNED LONG MOD & DIV */
			kludge = _mask - (rad - 1);
			val &= _mask;
			/*
			 * This is really what's being done...
			 * rem = (kludge % rad) + (val % rad);
			 * val = (kludge / rad) + (val / rad) + (rem / rad) + 1;
			 * *--b = d[rem % rad];
			 */
			tmp1 = kludge / rad;
			tmp2 = val / rad;
			rem = (kludge - (tmp1 * rad)) + (val - (tmp2 * rad));
			val = ++tmp1 + tmp2;
			tmp1 = rem / rad;
			val += tmp1;
			*--b = d[rem - (tmp1 * rad)];
		}
	}

	while (val) {
		/*
		 * This is really what's being done ...
		 * *--b = d[val % rad];
		 * val /= rad;
		 */
		tmp1 = val / rad;
		*--b = d[val - (tmp1 * rad)];
		val = tmp1;
	}

done:
	if (sign)
		*--b = '-';

	tmp1 = ndigits - (&_icv_buf[MAXINTLENGTH] - b);
	tmp2 = issigned? ' ':'0';
	while (tmp1 > 0) {
		*--b = tmp2;
		tmp1--;
	}

	return (b);
}

/*
 * NAME: get_addr
 *
 * FUNCTION: return the address to jump to (-j option)
 */
off_t
get_addr(char *s)
{
	off_t a;
	char *ptr;
	int base = 10;

	/*
	 *  Parse type_string to determine input base
	 */
	if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
		s += 2;
		base = 16;
	} else if (*s == '0' && strlen(s) > 1) {
		++s;
		base = 8;
	}


	/*
	 *  Parse input base
	 */
	errno = 0;
	a = (off_t)strtoll(s, &ptr, base);

	if ((a < 0) || errno || (s == ptr)) {
		(void) fprintf(stderr, gettext("Invalid offset\n"));
		usage("od");
	}

	s = ptr;

	/*
	 *  Offset may be followed by a multiplier
	 */
	switch (*s) {
	case 'b':
		a *= BSIZE;
		break;
	case 'k':
		a *= KSIZE;
		break;
	case 'm':
		a *= MSIZE;
		break;
	}

	return (a);
}


/*
 * NAME: offset
 *
 * FUNCTION:  seek to the appropriate starting place.
 *
 * RETURN:  1 -> offset found
 *	    0 -> reached EOF (set bytes_read)
 */

int
offset(off_t s_offset, off_t *bytes_read)
{
	char	buf[BUFSIZ];
	int	n;
	int	nr;
	int fd;
	struct stat st;

	*bytes_read = 0L;
	fd = fileno(stdin);
	if (fstat(fd, &st) < 0) {
		(void) fprintf(stderr, gettext("od: fstat failed: "));
		perror("");
		exit(1);
	}
	if (S_ISREG(st.st_mode)) {
		/*
		 * We can skip this file if the offset is bigger than
		 * the size of the file.
		 */
		if (st.st_size < s_offset) {
			*bytes_read += st.st_size;
			return (0);
		} else if (fseeko(stdin, s_offset, SEEK_SET) < 0) {
			/*
			 * fseeko() will only error if there is a serious
			 * error which would cause fread() to error as well.
			 */
			(void) fprintf(stderr, gettext("od: fseeko failed: "));
			perror("");
			exit(1);
		}
		return (1);
	}

	while (s_offset > 0) {
		nr = (s_offset > BUFSIZ) ? BUFSIZ : (int)s_offset;
		if ((n = fread((void *)buf, (size_t)1, (size_t)nr,
								stdin)) != nr) {
			*bytes_read += n;
			return (0);
		}
		*bytes_read += n;
		s_offset -= n;
	}
	return (1);
}


void
usage(char *progname)
{
	(void) fprintf(stderr, gettext(
"usage: %s [-bcCdDfFoOsSvxX] [-] [file] [offset_string]\n"), progname);

	(void) fprintf(stderr, gettext("       \
%s [-bcCdDfFoOsSvxX] [-t type_string]... [-A address_base] [-j skip] \
[-N count] [-] [file...]\n"), progname);

	exit(1);
}

/*
 * NAME: odopen
 *
 * FUNCTION:  Open next file in argument list.
 *
 * RETURN VALUE: Set exitstat and increment argcnt and argvec to try opening
 *               next file if error occurs when opening the file.
 */
void
odopen(int *argcnt, char **argvec, int *exitstat, int fd, off_t max_bytes)
{
	int	try_open;

	try_open = 1;
	while (*argcnt > 0 && try_open) {
#ifndef XPG4
		if (argvec[0][0] == '-' && argvec[0][1] == '\0') {
			if (dup2(fd, 0) == -1) {
				last_addr();
				(void) fprintf(stderr, gettext(
				"od: dup2() failed: "));
				perror("");
				exit(1);
			}
			rewind(stdin);
			try_open = 0;
		} else {
#endif
			if (freopen(*argvec, "r", stdin) == NULL) {
				if (*argcnt == 1) {
					if (bytes > 0 && bytes < dbuf_size) {
						if (Nflag && max_bytes < bytes)
							bytes = max_bytes;
						/*
						 * od has read some
						 * data, but has
						 * not printed the
						 * partial line yet
						 */
						line(bytes);
						if (addrbytes > 0)
							addr += addrbytes;
						else
							addr += bytes;
					}
					last_addr();
					(void) fprintf(stderr, gettext(
					"od: cannot open %s: "), *argvec);
					perror("");
					exit(2);
				} else {
					(void) fprintf(stderr, gettext(
					"od: cannot open %s: "), *argvec);
					perror("");
					*exitstat = 2;
					/* freopen() has closed stdin */
					if (fdopen(0, "r") == NULL) {
						last_addr();
						(void) fprintf(stderr,
						gettext(
				"od: fdopen(): unable to open stdin: "));
						perror("");
						exit(1);
					}
				}
				argvec++;
				/*
				 * If the following statement is changed,
				 * make sure "od -c noread - noexist <file"
				 * still works
				 */
				(*argcnt)--;
			} else
				try_open = 0;
#ifndef XPG4
		}
#endif
	}
}

/*
 * NAME: last_addr
 *
 * FUNCTION:  Put address on last line.
 *
 */
void
last_addr()
{
	if (addr > 0) {
		if (Aflag == 2)
			(void) fputc('\n', stdout);
		else
			put_addr(addr, '\n');
	}
}

/*
 * NAME: get_offset
 *
 * FUNCTION:  Interpret old style offset entered.
 *
 * RETURN VALUE: offset address
 */
off_t
get_offset(char *offset)
{
	char *p, *s;
	off_t a;
	int d;

	s = offset;
	if (*s == '+')
		s++;
	if (*s == 'x') {
		s++;
		addr_base = 16;
	} else if (*s == '0' && s[1] == 'x') {
		s += 2;
		addr_base = 16;
	}
	p = s;
	/* determine if addr base 10 and do error checking */
	while (*p) {
		if (*p == '.') {
			if ((p == s && *(p+1) != '\0') || addr_base == 16 ||
				(p != s && *(p+1) != 'b' && *(p+1) != 'B' &&
				*(p+1) != '\0')) {
				/*
				 * Ensure mutually exclusive usage
				 * except that decimal overrides
				 * octal when '0' is used with '.'.
				 * Differences between Solaris usage and
				 * XPG4 usage are taken care of in main()
				 * when get_offset() is called.
				 * Solaris:
				 * [+][0]offset[.][b|B]
				 * [+][0][offset][.]
				 * XPG4:
				 * [+][0]offset[.][b|B]
				 * +[offset][.]
				 */
				(void) fprintf(stderr, gettext(
				"od: invalid offset %s\n"), offset);
				usage("od");
			}
			addr_base = 10;
		}
		++p;
	}
	for (a = 0; *s; s++) {
		d = *s;
		if ((d >= '0' && d <= '7' && addr_base == 8) ||
				(d >= '0' && d <= '9' &&
				(addr_base == 10 || addr_base == 16)))
			a = a*addr_base + d - '0';
		else if (d >= 'a' && d <= 'f' && addr_base == 16)
			a = a*addr_base + d + 10 - 'a';
		else
			break;
	}
	if (*s == '.')
		s++;
	if (*s == 'b' || *s == 'B') {
		if (a == 0) {
			/* need offset value when specifying 'b' or 'B' */
			(void) fprintf(stderr, gettext(
			"od: invalid offset %s\n"), offset);
			usage("od");
		}
		++s;
		a *= BSIZE;
	}
	if (*s != '\0') {
		/*
		 * Don't disregard invalid trailing characters.
		 * Differences between Solaris usage and XPG4 usage are
		 * taken care of in main() when get_offset() is called.
		 * Solaris:
		 * [+][0]offset[.][b|B]
		 * [+][0][offset][.]
		 * [+][0x|x][offset]
		 * [+][0x|x]offset[B]
		 * XPG4:
		 * [+][0]offset[.][b|B]
		 * +[offset][.]
		 * [+][0x][offset]
		 * [+][0x]offset[B]
		 * +x[offset]
		 * +xoffset[B]
		 */
		(void) fprintf(stderr, gettext(
		"od: invalid offset %s\n"), offset);
		usage("od");
	}
	return (a);
}

/*
 * COMPONENT_NAME: (LIBCCHR) LIBC Character Classification Funcions
 *
 * FUNCTIONS: mbswidth
 *
 * ORIGINS: 27
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 */

/*
 *
 * FUNCTION: mbswidth
 *
 * RETURN VALUE:  number of bytes in multibyte char
 *
 */

int
mbswidth(const char *s, size_t n)
{
	int len;
	wchar_t *wcs;
	int rc;
	char *str_ptr;
	wchar_t wcsbuf[BSIZE/sizeof (wchar_t)+1];
	char strbuf[256];

	if ((s == (char *)NULL) || (*s == '\0'))
		return ((int)NULL);

	/*
	 * Get the space for the process code.  There cannot be more
	 * process codes than characters.
	 */
	if ((n + 1) * sizeof (wchar_t) <= sizeof (wcsbuf))
		wcs = wcsbuf;
	else if ((wcs = (wchar_t *)malloc((n+1) *
					sizeof (wchar_t))) == (wchar_t *)NULL) {
		(void) fprintf(stderr, gettext("od: cannot allocate memory: "));
		perror("");
		return ((int)NULL);
	}

	/* get space for a temp string */
	if ((n + 1) * sizeof (char) <= sizeof (strbuf))
		str_ptr = strbuf;
	else if ((str_ptr = (char *)malloc(n+1)) == (char *)NULL) {
		if (wcs != wcsbuf)
			free(wcs);
		(void) fprintf(stderr, gettext("od: cannot allocate memory: "));
		perror("");
		return ((int)NULL);
	}
	/* copy s into the temp string */
	(void) strncpy(str_ptr, s, n);
	str_ptr[n] = '\0';

	rc = mbstowcs(wcs, str_ptr, n+1);

	/* was there an invalid character found */
	if (rc == -1)
		len = -1;
	else
		len = wcswidth(wcs, (size_t)rc+1);

	/* free up the malloced space */
	if (wcs != wcsbuf)
		free(wcs);
	if (str_ptr != strbuf)
		free(str_ptr);

	return (len);
}
