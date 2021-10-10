/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/* All Rights Reserved					*/

/* THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/* The copyright notice above does not evidence any	*/
/* actual or intended publication of such source code.	*/

/* Parts of this product may be derived from OSF/1 1.0 and */
/* Berkeley 4.3 BSD systems licensed from */
/* OPEN SOFTWARE FOUNDATION, INC. and the University of California. */

/* Parts of this product may be derived from the systems licensed */
/* from International Business Machines Corp. and */
/* Bell Telephone Laboratories, Inc. */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * XCU4 compliant tail command
 *  NEW STYLE
 *     tail [-fr] [-c number | -n number] [file]
 *  option 'c' means tail number bytes
 *  option 'n' means tail number lines
 *  The number must be a decimal integer whose sign affects the location in
 *  the file, measured in bytes/lines, to begin the copying:
 *          Sign        Copying Starts
 *          +           Relative to the beginning of the file.
 *          -           Relative to the end of the file.
 *          none        Relative to the end of the file.
 *  The origin for couting is 1; that is, -c +1 represents the first byte
 *  of the file, -n -1 the last line.
 *  option 'f' means loop endlessly trying to read more bytes after the
 *  end of file, on the assumption that the file is growing.
 *  option 'r' means inlines in reverse order from end
 *    (for -r, default is entire buffer)
 *  OLD STYLE
 *     tail -[number][b|c|l|r][f] [file]
 *     tail +[number][b|c|l|r][f] [file]
 *  - means n lines before end
 *  + means nth line from beginning
 *  type 'b' means tail n blocks, not lines
 *  type 'c' means tail n characters(bytes?)
 *  type 'l' means tail n lines
 *  type 'r' means in lines in reverse order from end
 *    (for -r, default is entire buffer)
 *  option 'f' means loop endlessly trying to read more bytes after the
 *  end of file, on the assumption that the file is growing.
 *
 * Sun-traditional tail command
 *     tail where [file]
 *     where is [+|-]n[type]
 *     - means n lines before end
 *     + means nth line from beginning
 *     type 'b' means tail n blocks, not lines
 *     type 'c' means tail n bytes
 *     type 'r' means in lines in reverse order from end
 *      (for -r, default is entire buffer)
 *     option 'f' means loop endlessly trying to read more
 *             characters after the end of file, on the assumption
 *             that the file is growing
 *
 * Comments regarding possible (future) cleanup:
 *
 * XXX:	In some places, the input file is accessed through the decsriptor
 *	variable "fd"; in others, it's accessed through the constant "0".  The
 *	way the descriptor is named should be made uniform throughout.
 *
 * XXX:	Too many global variables!
 */
#include	<stdio.h>
#include	<ctype.h>
#include	<locale.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<limits.h>			/* for :INE_MAX and PATH_MAX */
#include	<string.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<sys/signal.h>
#include	<sys/types.h>
#include	<sys/sysmacros.h>
#include	<sys/stat.h>
#include	<sys/statvfs.h>
#include	<sys/param.h>	/* for DEV_BSHIFT and DEV_BSIZE */
#include	<sys/mman.h>
#include	<sys/uio.h>

/*
 * Amount of file buffered when it's not possible to mmap it or determine its
 * size in advance; the value reflects a somewhat arbitrary tradeoff between
 * memory consumption and providing a big enough window into the file to
 * accommodate the command line arguments without truncation (it could
 * reasonably be doubled or quadrupled).
 */
#ifdef XPG4
#define	LBIN    (LINE_MAX * 10)
#else /* !XPG4 */
#define	LBIN	65537		/* 64K + 1 for trailing NUL */
#endif /* XPG4 */

#ifdef XPG4
#define	NO_FLAG	0
#define	FLAG_C	1
#define	FLAG_N	2
#endif /* XPG4 */

char			staticbin[LBIN];	/* just in case malloc fails */
struct stat		statb;
struct statvfs	vstatb;
off_t				fcount;		/* size of the input file */
char			*mapp;		/* mmap'ped pointer to the input file */

/* amount to read from file at a crack */
ulong_t		blocksize = DEV_BSIZE;	/* fallback value */

int		follow;		/* -f flag */
int		piped;
int		isfifo = 0;	/* is input file a FIFO (named pipe)? */
int		bkwds;		/* -r flag  */
int		istty;		/* flag set for terminal device, e.g., stdin */
				/* avoid lseeking on terminal devices, */
				/* bugid 4204623 */

int		fromend, frombegin;
int		bylines;
off_t		num;
static int	exitval = 0;

static char		*readbackline(char *, char *);
static void		readfromstart(int, off_t);
static void		readfromend(int, off_t);
static void		fexit(void);
static void		docopy(void);
static void		usage(void);
static void		tailfromend(void);
static void		tailfrombegin(void);
static int		reallocatebuffer(off_t *, char **);
static ssize_t		unlimitedwrite(int, const char *, off_t);

#ifdef XPG4
static void		setnum(char *, int);
#endif /* XPG4 */


int
main(int argc, char **argv)
{
	int	filefound;
	char	*arg;
	char	filename[PATH_MAX];
#ifdef XPG4
	int	cflag, nflag, bflag;
	char	c;
#endif /* XPG4 */

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	num = -1;
	bylines = -1;
	follow = 0;
	bkwds = 0;
	istty = 0;
	fromend = 1;
	frombegin = 0;
	filefound = 0;

#ifdef XPG4
	if (argc == 1) {			/* just 'tail' */
		num = 10;
		bylines = 1;
		filefound = 0;
		goto file;
	}

	arg = *(argv + 1);
	if (*arg != '-' && *arg != '+') { /* 'tail filename' */
		if (argc > 2) {
			usage();
		} else {
			num = 10;
			bylines = 1;
			if (strlcpy(filename, arg, sizeof (filename)) >=
			    sizeof (filename)) {
				(void) fprintf(stderr,
				    gettext("tail: filename too long.\n"));
				exit(2);
			}
			filefound = 1;
			goto file;
		}
	}

	cflag = 0;
	nflag = 0;
	bflag = 0;

	while (--argc > 0) {
		arg = *++argv;
		switch (*arg) {
		case '+':
			fromend = 0;
			frombegin = 1;
			if (nflag == 1) {
				setnum(arg, FLAG_N);
				nflag = 2;
			} else if (cflag == 1) {
				setnum(arg, FLAG_C);
				cflag = 2;
			} else {
				num = 10;
				setnum(arg, NO_FLAG);
			}
			break;
		case '-':
nextarg:
			c = *(arg + 1);
			if (c == 'f') {
				if (bkwds == 1) {
					usage();
				}
				follow = 1;
				if (cflag == 1) { /* OLD STYLE: 'tail -cf' */
					bylines = 0;
					num = 10;
					cflag = 2;
				}
			} else if (c == 'r') {
				if (follow == 1) {
					usage();
				}
				if (!fromend && num != -1)
					num++;
				if (bylines == 0) {
					usage();
				}
				if (cflag == 1) { /* 'tail -cr' */
					usage();
				}
				bkwds = 1;
				fromend = 1;
				bylines = 1;
			} else if (c == 'c') {
				if (!cflag && !nflag && !bflag) {
					cflag = 1;
				} else {
					usage();
				}
			} else if (c == 'n') {
				if (!cflag && !nflag && !bflag) {
					nflag = 1;
				} else {
					usage();
				}
			} else if (c == 'b') {
				if (!cflag && !nflag && !bflag) {
					bflag = 1;
				} else {
					usage();
				}
			} else if ((c == '-') && (*(arg + 2) == '\0')) {
				arg = *++argv;
				argc--;
				goto fileset;
			} else {
				frombegin = 0;
				fromend = 1;
				if (nflag == 1) {
					setnum(arg, FLAG_N);
					nflag = 2;
				} else if (cflag == 1) {
					setnum(arg, FLAG_C);
					cflag = 2;
				} else {
					setnum(arg, NO_FLAG);
				}
				break;
			}

			if (cflag || bflag) {
				if (bkwds) {
					usage();
				}
			}
			if (*(arg + 2) != '\0') {
				*(arg + 1) = '-';
				arg++;
				goto nextarg;
			}
			break;
		default:
fileset:
			if (nflag == 1) {
				fromend = 1;
				frombegin = 0;
				setnum(arg, FLAG_N);
				nflag = 2;
			} else if (cflag == 1) {
				fromend = 1;
				frombegin = 0;
				setnum(arg, FLAG_C);
				cflag = 2;
			} else {
				if (argc == 0) {
					filefound = 0;
				} else if (argc > 1) {
					usage();
				} else {
					if (strlcpy(filename, arg,
					    sizeof (filename)) >=
					    sizeof (filename)) {
(void) fprintf(stderr, gettext("tail: filename too long.\n"));
					}
					filefound = 1;
				}
			}
			break;
		}
	}
#else /* !XPG4 */
	arg = *(argv + 1);
	if (argc <= 1 || *arg != '-' && *arg != '+') {
		arg = "-10l";
		argc++;
		argv--;
	}
	fromend = (*arg == '-');
	frombegin = (*arg == '+');
	arg++;
	if (isdigit((int)*arg)) {
		num = 0;
		while (isdigit((int)*arg)) {
			num = num * 10 + *arg++ - '0';
		}
	} else if (frombegin) {		/* option was '+' without an integer */
		num = 10;
	} else {
		num = -1;
	}
	if (argc > 2) {
		if (strlcpy(filename, *(argv + 2), sizeof (filename)) >=
		    sizeof (filename)) {
			(void) fprintf(stderr,
			    gettext("tail: filename too long.\n"));
		}
		filefound = 1;
	} else {
		filefound = 0;
	}
	bylines = -1;
	bkwds = 0;
	follow = 0;
	while (*arg) {
		switch (*arg++) {
		case 'b':
			if (num == -1) {
				num = 10;
			}
			num <<= DEV_BSHIFT;
			if (bylines != -1 || bkwds == 1) {
				usage();
			}
			bylines = 0;
			break;
		case 'c':
			if (bylines != -1 || bkwds == 1) {
				usage();
			}
			bylines = 0;
			break;
		case 'f':
			if (bkwds == 1) {
				usage();
			}
			follow = 1;
			break;
		case 'r':
			if (follow == 1) {
				usage();
			}
			if (!fromend && num != -1)
				num++;
			if (bylines == 0) {
				usage();
			}
			bkwds = 1;
			fromend = 1;
			bylines = 1;
			break;
		case 'l':
			if (bylines != -1 && bylines == 1) {
				usage();
			}
			bylines = 1;
			break;
		default:
			usage();
			break;
		}
	}

#endif /* XPG4 */

file:
	if (filefound) {
		int		fd;

		(void) close(0);	/* fd 0 will be used for next open */
		if ((fd = open(filename, 0)) == -1) { /* fd must be 0 */
			(void) fprintf(stderr,
			    gettext("tail: cannot open input\n"));
			exit(2);
		}
		if (fstat(fd, &statb) == -1) {
			(void) fprintf(stderr, gettext(
			    "tail: cannot determine length of %s\n"), filename);
			exit(2);
		}
		fcount = statb.st_size;
		isfifo = ((statb.st_mode & S_IFMT) == S_IFIFO);
		/* Get optimal read chunk size. */
		if (fstatvfs(fd, &vstatb) == 0 && vstatb.f_bsize != 0) {
			blocksize = vstatb.f_bsize;
		}
		if (!follow && fcount <= SIZE_MAX) {
			if ((mapp = (char *)mmap((caddr_t)NULL,
			    (size_t)fcount, PROT_READ,
			    MAP_PRIVATE, fd, (off_t)0)) == MAP_FAILED) {
				/*
				 * Fall back to use read instead of mmap.
				 */
				mapp = NULL;
			}
		}
	}

	(void) lseek(0, (off_t)0, SEEK_CUR);
	piped = (errno == ESPIPE);
	istty = isatty(0);

	if (!fromend && num > 0) {
		num--;
	}
	/* Set num to default values */
	if (!bkwds && num == -1) {
		num = 10;
	}
	if (bylines == -1) {
		bylines = 1;
	}
	if (bkwds) {
		follow = 0;
	}
	if (mapp) {
		/* +infinity, print everything */
		if (bkwds && num == -1)
			num = (off_t)SIZE_MAX;
		if (fromend)
			readfromend(bylines, num);
		else
			readfromstart(bylines, num);
	} else {
		if (fromend)
			tailfromend();
		else
			tailfrombegin();
	}
	/* NOTREACHED */
	return (0);
}

/*
 * Not used when input file is mmap'ped.
 */
static void
fexit()
{
	int    n;
	/*
	 * keep reading and writing if following
	 * something other than a pipe; also continue if
	 * following a FIFO(named pipe).
	 */
	if (!follow || (piped && !isfifo)) {
		exit(exitval);
	}
	for (;;) {
		(void) sleep(1);
		while ((n = read(0, staticbin, sizeof (staticbin))) > 0) {
			(void) unlimitedwrite(1, staticbin, n);
		}
	}
}

/*
 * Not used when input file is mmap'ped.
 */
static void
docopy()
{
	int bytes;

	while ((bytes = read(0, staticbin, sizeof (staticbin))) > 0) {
		(void) unlimitedwrite(1, staticbin, bytes);
	}
	fexit();
	/* NOT REACHED */
}

/*
 * Used only when the input file is mmap'ped.
 *
 * Skips over nunits (lines or characters depending on "bylines")
 * of the input file from the beginning and writes out
 * the remainder.
 */
static void
readfromstart(int bylines, off_t nunits)
{
	if (bylines) {
		for (; nunits > 0 && fcount > 0; fcount--) {
			if (*mapp++ == '\n') {
				nunits--;
			}
		}
	} else {
		/*
		 * skip nunits characters
		 */
		mapp += nunits;
		fcount -= nunits;
	}
	if (fcount > 0) {
		(void) unlimitedwrite(1, mapp, fcount);
	}
	exit(exitval);
}

/*
 * Used only when the input file is mmap'ped.
 *
 * Writes out nunits (depending on bylines) of the input file counting
 * from the end.
 */
static void
readfromend(int bylines, off_t nunits)
{
	off_t	cnt;
	off_t	end, start, mark;

	if (fcount <= 0 || nunits <= 0) {
		exit(0);
	}

	start = (off_t)(uintptr_t)mapp;	/* save start of file */
	mapp += fcount;		/* move mapp to the end */
	end = (off_t)(uintptr_t)mapp;	/* mark the end */

	if (bylines) {
		while (nunits-- > 0) {		/* for each line */
			/* set mark & move back one */
			mark = (off_t)(uintptr_t)mapp--;
			mapp = readbackline(mapp, (char *)(uintptr_t)start);
			cnt = mark - (off_t)(uintptr_t)mapp;
			if (cnt == 0) {		/* done? */
				break;
			}
			if (bkwds) {
				(void) unlimitedwrite(1, mapp, cnt);
				end = (off_t)(uintptr_t)mapp;
				continue;
			}
		}
	} else {
		/*
		 * bump pointer up to end - nunits.
		 */
		if ((end - start) < nunits)
			mapp = (char *)(uintptr_t)start;
		else
			mapp = (char *)(uintptr_t)(end - nunits);
	}

	cnt = end - (off_t)(uintptr_t)mapp;
	if (cnt > 0) {
		(void) unlimitedwrite(1, mapp, cnt);
	}
	exit(0);
}

/*
 * Reads a line backward (or up to limit) and returns the pointer to the
 * beginning of the line.
 */
static char *
readbackline(char *mapp, char *limit)
{
	if (mapp <= limit) {
		return (limit);
	}

	while (*--mapp != '\n') {
		if (mapp == limit) {
			return (limit);
		}
	}

	return (mapp + 1);
}

static void
usage()
{
#ifdef XPG4
	(void) fprintf(stderr,
	    gettext("usage: tail [-f|-r] [-c number | -n number] [file]\n"
		    "       tail [+/-[number][lbc][f]] [file]\n"
		    "       tail [+/-[number][l][r|f]] [file]\n"));
#else /* !XPG4 */
	(void) fprintf(stderr, gettext("usage: tail [+/-[n][lbc][f]] [file]\n"
				"       tail [+/-[n][l][r|f]] [file]\n"));
#endif /* XPG4 */
	exit(2);
}

#ifdef XPG4
static void
setnum(char *arg, int flags)
{

	if ((*arg == '-') || (*arg == '+')) {
		arg++;
	}
	if (flags != NO_FLAG) {		/* '-c num' or '-n num' */

		num = 0;
		while (*arg) {
			if (!isdigit((int)*arg)) {
				/* 'num' must be a decimal integer */
				usage();
			} else {
				num = num * 10 + *arg++ - '0';
			}
		}
		if (flags == FLAG_N) {
			bylines = 1;
		} else {
			bylines = 0;
		}
	} else {
		/* '-[num][bcfrl]' or '+[num][bcfrl]' */
		if (isdigit((int)*arg)) {
			num = 0;
			while (isdigit((int)*arg)) {
				num = num * 10 + *arg++ - '0';
			}
		}
		bylines = -1;
		while (*arg) {
			switch (*arg++) {
			case 'l':
				if (bylines != -1) {
					usage();
				}
				bylines = 1;
				break;
			case 'b':
				if (bylines != -1) {
					usage();
				}
				bylines = 0;
				if (num == -1) {
					num = 10;
				}
				num <<= DEV_BSHIFT;
				break;
			case 'c':
				if (bylines != -1) {
					usage();
				}
				bylines = 0;
				break;
			case 'f':
				if (bkwds == 1) {
					usage();
				}
				follow = 1;
				break;
			case 'r':
				if (follow == 1) {
					usage();
				}
				if (num != -1 && !fromend) {
					num++;
				}
				if (bylines == 0) {
					usage();
				}
				bkwds = 1;
				fromend = 1;
				bylines = 1;
				break;
			default:
				usage();
				break;
			}
		}
	}
}
#endif /* XPG4 */
/*
 * This function is called when it has skip lines from the start
 */
static void
tailfrombegin(void)
{
	off_t	i;
	char *p;
	/*
	 * seek from beginning
	 */

	if (bylines) {
		/*
		 * Read and discard num lines from the beginning of the file,
		 * and then transcribe the residual amount read.
		 */
		i = 0;
		while (num-- > 0) {
			do {
				if (i-- <= 0) {
					p = staticbin;
					i = read(0, p, sizeof (staticbin));
					if (i-- <= 0) {
						fexit();
					}
				}
			} while (*p++ != '\n');
		}
		(void) unlimitedwrite(1, p, i);
	} else if (num > 0) {
		/*
		 * See whether it's possible to seek directly to the desired
		 * position; if not, read up to that point (discarding the
		 * data).
		 */
		if (!piped) {
			(void) fstat(0, &statb);
		}
		if (piped || (statb.st_mode & S_IFMT) == S_IFCHR) {
			while (num > 0) {
				i = MIN(num, sizeof (staticbin));
				i = read(0, staticbin, i);
				if (i <= 0) {
					fexit();
				}
				num -= i;
			}
		} else {
			(void) lseek(0, (off_t)num, SEEK_SET);
		}
	}
	docopy();
	/* NOTREACHED */
}
#define	MAX(a, b)	((a) < (b) ? (b) : (a))
static void
tailfromend(void)
{
	off_t	buffersize;
	off_t 	i, j, k;
	int 	isreallocneeded;
	int 	partial, lastnl;
	char	*bin;

	if (!bkwds && num <= 0) {
		(void) lseek(0, (off_t)0, SEEK_END);
		fexit();
	}
	/* Calculate buffer requirement */
	if (num > 0)
		/* We need to scan atleast LBIN bytes */
		buffersize = bylines ? MAX(num*LINE_MAX, LBIN) : num+1;
	else
		buffersize = (off_t)LBIN;

	if (bkwds && num == -1) {
		num = (off_t)SSIZE_MAX;
	}

	/* If it is -c option, we dont need to reallocate */
	if (bylines)
		isreallocneeded = 1;
	else
		isreallocneeded = 0;

	if (!piped && !istty) {
		/*
		 * Go to offset from end of file
		 * Also check if buffersize is overestimated
		 */
		(void) fstat(0, &statb);
		if (bylines)
			i = (off_t)num*LBIN;
		else
			i = num + 1;

		if (i < (off_t)statb.st_size)
			(void) lseek(0, (off_t)-i, SEEK_END);
		else {
			(void) lseek(0, (off_t)0, SEEK_SET);
		}
		if (buffersize > statb.st_size)
			buffersize = statb.st_size + 1;
	}

	bin = (char *)malloc(buffersize);
	if (bin == NULL) {
		(void) fprintf(stderr,
			gettext("Insufficient memory, output"
			" may be shortened\n"));
		exitval = 1;
		isreallocneeded = 0;
		bin = staticbin;
		buffersize = sizeof (staticbin);
	}

	/*
	 * Suck up data until there's no more available, wrapping circularly
	 * through the buffer.  Set partial to indicate whether or not it was
	 * possible to fill the buffer.
	 */
	partial = 1;
	i = 0;
	for (;;) {
		/*  See comments of function unlimitedwrite() */
		j = read(0, &bin[i], (buffersize - i) > blocksize ? blocksize
		    :(buffersize - i));
		if (j <= 0) {
			break;
		}
		i += j;
		if (i == buffersize) {
			if (isreallocneeded) {
				isreallocneeded = reallocatebuffer(&buffersize,
				    &bin);
				if (isreallocneeded) {
					if (isreallocneeded == 2) {
						i = 0;
						partial = 0;
					} else {
						partial = 1;
					}
				}
			} else {
				partial = 0;
				i = 0;
				continue;
			}
		}
	}
	/*
	 * i now is the offset of the "oldest" data in the (circular) buffer.
	 *
	 * Set k to the offset of the beginning of the tail data within the
	 * circular buffer.
	 */
	if (!bylines) {
		k =
			num <= i ? i - num :
			partial ? 0 :
			num >= buffersize ? i + 1 :
			i - num + buffersize;
		k--;
	} else {
		/*
		 * Scan the buffer for the indicated number of line breaks; j
		 * counts the number found so far.
		 */
		if (bkwds && bin[i == 0 ? buffersize - 1 : i - 1] != '\n') {
			/*
			 * Force a trailing newline, resetting the boundary
			 * between the two halves of the buffer if necessary.
			 */
			bin[i] = '\n';
			if (++i >= buffersize) {
				i = 0;
				partial = 0;
			}
		}
		k = i;
		j = 0;
		do {
			lastnl = k;
			do {
				if (--k < 0) {
					if (partial) {
						if (bkwds) {
							(void) unlimitedwrite(1,
							    bin, lastnl + 1);
						}
						goto brkb;
					}
					k = buffersize - 1;
				}
			} while (bin[k] != '\n' && k != i);
			if (bkwds && j > 0) {
				if (k < lastnl) {
					(void) unlimitedwrite(1, &bin[k + 1],
					    lastnl - k);
				} else {
					(void) unlimitedwrite(1, &bin[k + 1],
					    buffersize - k - 1);
					(void) unlimitedwrite(1, bin,
					    lastnl + 1);
				}
			}
		} while (j++ < num && k != i);
brkb:
		if (bkwds)
			exit(exitval);
		if (k == i) {
			do {
				if (++k >= buffersize)
					k = 0;
			} while (bin[k] != '\n' && k != i);
		}
	}
	if (k < i) {
		(void) unlimitedwrite(1, &bin[k + 1], i - k - 1);
	} else {
		(void) unlimitedwrite(1, &bin[k + 1], buffersize - k - 1);
		(void) unlimitedwrite(1, bin, i);
	}
	fexit();
}
/*
 * In case, the stdin is piped or tty, then function will check
 * if reallocate is actually required. The criteria for this is
 * if the required number of lines are present in the last buffersize - LBIN
 * bytes, then there is no need for reallocation.
 * If reallocate is not required then function will return 2.
 * Returns 1 if realloc was successfull else 0
 * Updates buffersize and bin.
 */
static int
reallocatebuffer(off_t *buffersize, char ** bin)
{
	char *tmppointer;
	off_t i;
	off_t count;

	if (piped || istty) {
		i = *buffersize - 1;
		count = num;
		tmppointer = *bin;

		if (tmppointer[i] == '\n')
			count++;
		/*
		 * There is no need to look for newlines in the first LBIN bytes
		 * of buffer, since if we dont find all the required lines in
		 * the rest of buffer, then we anyway reallocate.
		 */
		while (i >= (off_t)LBIN) {
			if (tmppointer[i--] == '\n') {
				count--;
				if (!count)
					break;
			}
		}
		if (!count && (i >= (off_t)LBIN)) {
			/*
			 * Required number of lines are already present in the
			 * buffer and we have got enough space. So, there is no
			 * need to reallocate, and buffer can be wrapped back.
			 */
			return (2);
		}
	}
	tmppointer = *bin;
	*bin = (char *)realloc(*bin, *buffersize + (off_t)LBIN);
	if (*bin == NULL) {
		*bin = tmppointer;
		(void) fprintf(stderr,
		    gettext("Insufficient memory, output"
		    " may be shortened\n"));
		exitval = 1;
		return (0);
	} else
		*buffersize += (off_t)LBIN;
	return (1);
}
/*
 * This function over comes the limitation of write(2)
 * if nbytes > SSIZE_MAX then the result is implementation dependent
 *
 * This function writes in chunks of blocksize. For larger writes the process
 * becomes unresponsive; i.e even kill -9 will not kill untill write is
 * complete. This will definitely degrade the performance in case of large
 * large files. But on the other hand some people may not like miniscule tail
 * command holding up lots of resources. And when they try to kill it, if it
 * does not die then they may panic.
 *
 * Hence this function makes sure that nbyte passed to write is
 * is always < blocksize
 */
static ssize_t
unlimitedwrite(int fd, const char *buf, off_t nbyte)
{
	size_t bytes_towrite;
	ssize_t bytes_written;
	size_t total_bytes_written = 0;
	while (nbyte > 0) {
		bytes_towrite = nbyte > blocksize ? blocksize : nbyte;
		bytes_written = write(fd, buf, bytes_towrite);
		if (bytes_written == -1) {
			break;
		}
		buf += bytes_written;
		total_bytes_written += bytes_written;
		nbyte -= bytes_written;
	}
	return (total_bytes_written);
}
