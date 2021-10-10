/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "iconv_int.h"

#define	NMATCH	5

#define	MODULE_PATTERN	ICONV_PATH "(.+)%(.+)\\.so"
#define	GENTBL_PATTERN	GENTBL_PATH "(.+)%(.+)\\.bt"
#define	KBDTBL_PATTERN	ICONV_PATH "(.+)\\.(.+)\\.t"
#define	ICONV_PATTERN	MODULE_PATTERN "|" KBDTBL_PATTERN

#define	HEADER_MSG	\
	"The following are all supported code set names.  All combinations\n"\
	"of those names are not necessarily available for the pair of the\n"\
	"fromcode-tocode.  Some of those code set names have aliases, which\n"\
	"are case-insensitive and described in parentheses following the\n"\
	"canonical name:\n"

static conv_list_t	*convlist = NULL;

static conv_list_t *
search_entry(char *codename)
{
	conv_list_t	*p;

	p = convlist;
	while (p) {
		int	r;
		r = strcmp(p->name, codename);
		if (r == 0) {
			return (p);
		} else if (r > 0) {
			/* not found */
			return (NULL);
		} else {
			p = p->next;
		}
	}
	return (NULL);
}

static void
add_entry(char *codename)
{
	conv_list_t	*p, *q, *t;
	size_t	len;

	p = convlist;
	q = NULL;
	while (p) {
		int	r;
		r = strcmp(p->name, codename);
		if (r > 0) {
			break;
		} else if (r < 0) {
			q = p;
			p = p->next;
		} else {
			/* already exists */
			free(codename);
			return;
		}
	}
	len = strlen(codename);
	t = MALLOC(conv_list_t, 1);
	t->name = codename;
	t->clen = len;
	t->next = p;
	if (q) {
		q->next = t;
	} else {
		convlist = t;
	}
}

static void
matching_iconv_module(const char *fname, regex_t *pregp)
{
	char	*fromcode, *tocode;
	int	ret, idx;
	regmatch_t	pmatch[NMATCH];
	size_t	fromlen, tolen;

	ret = regexec(pregp, fname, NMATCH, pmatch, 0);
	if (ret != 0) {
		/* match failed */
		return;
	}
	/*
	 * Needs to determine which subexpression matched
	 */
	if (pmatch[1].rm_so != (regoff_t)-1 &&
	    pmatch[2].rm_so != (regoff_t)-1) {
		/* MODULE_PATTERN or GENTBL_PATTERN */
		idx = 1;
	} else {
		/* KBDTBL_PATTERN */
		idx = 3;
	}
	/* from */
	fromlen = pmatch[idx].rm_eo - pmatch[idx].rm_so;
	fromcode = MALLOC(char, fromlen + 1);
	(void) memcpy(fromcode, fname + pmatch[idx].rm_so, fromlen);
	*(fromcode + fromlen) = '\0';

	add_entry(fromcode);

	/* to */
	tolen = pmatch[idx+1].rm_eo - pmatch[idx+1].rm_so;
	tocode = MALLOC(char, tolen + 1);
	(void) memcpy(tocode, fname + pmatch[idx+1].rm_so, tolen);
	*(tocode + tolen) = '\0';

	add_entry(tocode);
}

static void
add_alias_entry(char *canon, char *variant)
{
	conv_list_t	*p;
	char	*t;
	size_t	var_len, new_len, tlen;

	p = search_entry(canon);
	if (p == NULL) {
		/*
		 * this alias does not make sense
		 */
		free(canon);
		free(variant);
		return;
	}
	free(canon);

	var_len = strlen(variant);
	if (p->alen == 0) {
		/* new alias found */
		p->vnames = variant;
		p->alen = var_len;
		/* "basename" + ' ' + '(' + "variant" + ')' */
		p->clen = p->clen + 1 + 1 + p->alen + 1;
		return;
	}

	/* already other variants exist */
	/*
	 * p->alen = "variants"
	 *
	 * "variants" + ',' + ' ' + "new_variant"
	 */
	new_len = p->alen + var_len + 2;
	t = MALLOC(char, new_len + 1);
	(void) sprintf(t, "%s, %s", p->vnames, variant);
	free(p->vnames);
	free(variant);
	p->vnames = t;
	tlen = p->clen - p->alen;
	p->alen = new_len;
	p->clen = tlen + p->alen;
}

static int
make_list(const char *dirname, const char *mpattern)
{
	int	ret;
	DIR	*dirp;
	struct dirent	*direntp;
	size_t	flen, buflen;
	char	filename[PATH_MAX];
	char	*p;
	regex_t	preg;

	(void) strlcpy(filename, dirname, sizeof (filename));

	if ((dirp = opendir(filename)) == NULL) {
		(void) fprintf(stderr,
		    gettext("Failed to open the directory %s.\n"),
		    dirname);
		return (1);
	}

	ret = regcomp(&preg, mpattern, REG_EXTENDED);
	if (ret != 0) {
		(void) fprintf(stderr,
		    "Internal error: regcomp failed.\n");
		return (1);
	}

	flen = strlen(filename);
	buflen = sizeof (filename) - flen;
	p = filename + flen;
	while ((direntp = readdir(dirp)) != NULL) {
		struct stat	statbuf;

		if (strcmp(direntp->d_name, ".") == 0 ||
		    strcmp(direntp->d_name, "..") == 0) {
			continue;
		}
		if (strlcpy(p, direntp->d_name, buflen) >= buflen) {
			/* too long file name */
			continue;
		}
		if (stat(filename, &statbuf) == -1) {
			continue;
		}
		if (!S_ISREG(statbuf.st_mode)) {
			continue;
		}
		matching_iconv_module(filename, &preg);
	}
	(void) closedir(dirp);
	regfree(&preg);

	return (0);
}

static int
show_list(void)
{
	int	curcol;
	int	num_cols = 80;
	conv_list_t	*p;
	char	*clptr;
	struct winsize	win;

	if (!isatty(fileno(stdout))) {
		num_cols = 1;
	} else {
		clptr = getenv("COLUMNS");
		if (clptr) {
			num_cols = atoi(clptr);
		} else {
			if (ioctl(1, TIOCGWINSZ, &win) != -1) {
				num_cols = (win.ws_col == 0) ? 80 : win.ws_col;
			}
		}
		if (num_cols < 20 || num_cols > 1000) {
			num_cols = 80;
		}
		num_cols -= 4;
	}


	(void) printf("%s", gettext(HEADER_MSG));
	curcol = 0;
	p = convlist;
	while (p) {
		if (curcol == 0) {
			(void) printf("   ");
		} else {
			(void) printf(",");
			curcol += 2;
		}
		if (curcol + p->clen >= num_cols) {
			(void) printf("\n   ");
			curcol = 0;
		}
		if (p->vnames == NULL)
			(void) printf(" %s", p->name);
		else
			(void) printf(" %s (%s)", p->name, p->vnames);
		curcol += p->clen;
		p = p->next;
	}
	(void) fputc('\n', stdout);
	if (ferror(stdout)) {
		/* error */
		return (1);
	} else {
		return (0);
	}
}

static int
make_alias_list(const char *aliaspath)
{
	FILE	*fp;
	char	linebuf[LINE_MAX];
	int	ret = 0;

	fp = fopen(aliaspath, "r");
	if (fp == NULL) {
		int	serrno = errno;
		if (serrno == ENOENT) {
			/* no alias file */
			return (0);
		}
		(void) fprintf(stderr,
		    gettext("Failed to open the conversion alias file: %s\n"),
		    aliaspath);
		(void) fprintf(stderr, "%s\n", strerror(serrno));
		exit(2);
	}
	while (fgets(linebuf, sizeof (linebuf), fp) != NULL) {
		char	*ptr, *variant, *canon;
		char	*var_s, *var_e, *can_s, *can_e;
		size_t	var_len, can_len;

		ptr = linebuf;
		if (*ptr == '#') {
			/* comment line */
			continue;
		}
		while (*ptr == ' ' || *ptr == '\t') {
			/* skip leading spaces */
			ptr++;
		}
		/* variant entry */
		var_s = ptr;
		while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
			ptr++;
		}
		if (*ptr == '\0' || *ptr == '\n') {
			/* invalid entry; skipping this line */
			continue;
		}
		var_e = ptr;
		var_len = var_e - var_s;
		variant = MALLOC(char, var_len + 1);
		(void) memcpy(variant, var_s, var_len);
		*(variant + var_len) = '\0';

		while (*ptr == ' ' || *ptr == '\t') {
			/* skip leading spaces */
			ptr++;
		}

		/* canonical name */
		can_s = ptr;
		while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
			ptr++;
		}
		can_e = ptr;
		can_len = can_e - can_s;
		if (can_len == 0) {
			/* invalid entry; skipping this line */
			free(variant);
			continue;
		}
		canon = MALLOC(char, can_len + 1);
		(void) memcpy(canon, can_s, can_len);
		*(canon + can_len) = '\0';

		add_alias_entry(canon, variant);
	}
	if (ferror(fp) != 0) {
		ret = 1;
	}
	(void) fclose(fp);
	return (ret);
}

int
list_all_conversion(void)
{
	if (make_list(ICONV_PATH, ICONV_PATTERN) != 0) {
		return (1);
	}
	if (make_list(GENTBL_PATH, GENTBL_PATTERN) != 0) {
		return (1);
	}

	if (make_alias_list(ICONV_ALIAS_PATH) != 0) {
		return (1);
	}

	if (show_list() != 0) {
		/* error */
		return (1);
	}
	return (0);
}
