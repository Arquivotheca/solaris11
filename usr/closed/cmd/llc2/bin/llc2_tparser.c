/*
 * Copyright (c) 1998-1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * tparser.c
 *
 * Copyright (c) 1998 NCR Corporation, Dayton, Ohio USA
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <stropts.h>
#include <libintl.h>
#include <locale.h>
#include <llc2.h>
#include <ild.h>

static char opts[] = "hi:o:";

struct func_table {
	char *funcname;
	unsigned short lineno;
};

static struct tcaphdr {
	off_t offset;
	time_t starttime;
	clock_t lbolttime;
} thdr;

/*
 * The following header files are automatically generated during the
 * make process using the appropriate source files.
 */
#include "ild_ftbl.h"
#include "dlpi_ftbl.h"
#include "llc2_ftbl.h"
#include "SAM_ftbl.h"

/* Initialize the module table using the above header files. */
#define	ftbl_size sizeof (struct func_table)

static struct mod_table {
	unsigned int modid;
	char modname[14];
	struct func_table *ftbl;
	int tbl_size;
} mod_tab[] = {
	{ MID_ILD, "ild", ild_ftbl, sizeof (ild_ftbl) / ftbl_size },
	{ MID_DLPI, "dlpi", dlpi_ftbl, sizeof (dlpi_ftbl) / ftbl_size },
	{ MID_LLC2, "llc2", llc2_ftbl, sizeof (llc2_ftbl) / ftbl_size },
	{ MID_SAM, "SAM", SAM_ftbl, sizeof (SAM_ftbl) / ftbl_size },
	{ 0, "unknown", NULL, 0 }
};

static char *ifile;
static char *ofile;
static char *self;

static int ifd;
static FILE *ofp;

static int error = 0;
static int usage_opt = 0;
static clock_t first_time = 0;

static ildTraceEntry_t inbuf;

extern char *optarg;
extern int optind;
static char funcline[160];

static time_t cur_time = 0;
static time_t prev_time = 0;
static time_t delta_time = 0;
static time_t starttime;
static char time_str[15];
static int seconds;

static char *
ild_time(time_t clock)
{
	struct tm *timeptr;
	char *s;

	s = time_str;

	if (prev_time) {
		if ((delta_time = clock - prev_time) < 0) {
			delta_time = 0;
		}
	} else {
		prev_time = clock;
		delta_time = 0;
	}

	seconds = delta_time / HZ;
	delta_time = delta_time % HZ;
	cur_time = starttime + seconds;
	timeptr = localtime(&cur_time);

	(void) sprintf(s, "%02d:%02d:%02d.%02ld",
	    timeptr->tm_hour,
	    timeptr->tm_min,
	    timeptr->tm_sec,
	    delta_time);
	return (s);
}

static char *
findmod(uint_t mod, ushort_t line)
{
	int i, j;
	int maxmods;
	struct func_table *entry;

	maxmods = sizeof (mod_tab) / sizeof (struct mod_table);
	for (i = 0; i < maxmods; i++) {
		if (mod_tab[i].modid == mod) {
			if ((entry = mod_tab[i].ftbl) == NULL) {
				(void) sprintf(funcline, "%12s %5d",
				    mod_tab[i].modname, line);
				return (funcline);
			} else {
				for (j = 0; j < mod_tab[i].tbl_size; j++) {
					if (entry[j].lineno < line) {
						(void) sprintf(funcline,
						    "%12s %5d (%s+%d)",
						    mod_tab[i].modname, line,
						    entry[j].funcname,
						    (line - entry[j].lineno));
						return (funcline);
					}
				}
			}
		}
	}
	return (gettext("unknown"));
}

static void
dspElement(ildTraceEntry_t *t)
{
	ushort_t mod;
	ushort_t line;
	float relative_time;

	if (first_time) {
		relative_time = (((float)t->time - (float)first_time) / 100.0);
	} else {
		relative_time = 0.0;
		first_time = t->time;
	}
	(void) fprintf(ofp, "%s ", ild_time(t->time));
	(void) fprintf(ofp, "%2d  ", (t->cpu_mod_line & 0xff000000) >> 24);
	line = t->cpu_mod_line & 0xffff;
	mod = ((t->cpu_mod_line & 0xff0000) >> 16);
	(void) fprintf(ofp, "%-45s", findmod(mod, line));
	if (t->parm2 == 98) {
		(void) fprintf(ofp, "%8x  LOCK\n", t->parm1);
	} else if (t->parm2 == 99) {
		(void) fprintf(ofp, "%8x  UNLOCK\n", t->parm1);
	} else {
		(void) fprintf(ofp, "%8x  %8x\n", t->parm1, t->parm2);
	}
}

int
main(int argc, char **argv)
{
	char ch;
	off_t sOffset;
	off_t cOffset;
	struct stat sbuf;
	char *usage = gettext("Usage: %s [-h] -i infile [-o outfile]\n");

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	self = argv[0];
	/*
	 * parse command line
	 */
	if (argc < 2) {
		(void) fprintf(stderr, usage, self);
		exit(1);
	}
	while ((ch = getopt(argc, argv, opts)) != EOF) {
		switch (ch) {
		case 'i':
			ifile = optarg;
			break;
		case 'o':
			ofile = optarg;
			break;
		case 'h':
			usage_opt = 1;
			break;
		case '?':
		default:
			error = 1;
		}
	}
	if ((usage_opt != 0) || (error != 0)) {
		(void) fprintf(stderr, usage, self);
		if (error) {
			exit(1);
		} else {
			exit(0);
		}
	}
	if (ifile == NULL) {
		(void) fprintf(stderr, gettext("%s: must supply '-i infile'"
		    " option\n"), self);
		exit(1);
	} else {
		if ((ifd = open(ifile, O_RDONLY)) < 0) {
			perror(ifile);
			exit(1);
		}
		(void) fstat(ifd, &sbuf);
		starttime = sbuf.st_mtime;
	}
	if (ofile == NULL) {
		ofp = stdout;
	} else {
		if ((ofp = fopen(ofile, "w")) == NULL) {
			perror(ofile);
			exit(1);
		}
	}
	if (read(ifd, &thdr, sizeof (struct tcaphdr)) == 0) {
		perror(ifile);
		exit(1);
	}
	sOffset = thdr.offset;
	starttime = thdr.starttime;
	seconds = 0; delta_time = thdr.lbolttime % HZ;
	prev_time = 0;
	cur_time = starttime;


	if (lseek(ifd, sOffset, 0) < 0) {
		perror(ifile);
		exit(1);
	}

	while (read(ifd, &inbuf, sizeof (ildTraceEntry_t)) != 0) {
		dspElement(&inbuf);
	}
	if (sOffset != sizeof (struct tcaphdr)) {
		cOffset = sizeof (struct tcaphdr);
		if (lseek(ifd, cOffset, 0) < 0) {
			perror(ifile);
			exit(1);
		}
		while (read(ifd, &inbuf, sizeof (ildTraceEntry_t)) != 0) {
			dspElement(&inbuf);
			cOffset += sizeof (ildTraceEntry_t);
			if (cOffset == sOffset) {
				break;
			}
		}
	}
	return (0);
}
