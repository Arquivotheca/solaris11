/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * catopen.c
 *
 */

#pragma weak _catopen = catopen
#pragma weak _catclose = catclose

#include "lint.h"
#include "libc.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <nl_types.h>
#include <locale.h>
#include <limits.h>
#include <errno.h>
#include "../i18n/_loc_path.h"
#include "nlspath_checks.h"

#define	SAFE_F			1
#define	UNSAFE_F		0

#define	_DFLT_LOC_PATH_LEN	(sizeof (_DFLT_LOC_PATH) - 1)
#define	_LC_MSG_STR		"/LC_MESSAGES/"
#define	_LC_MSG_STR_LEN		(sizeof (_LC_MSG_STR))

static char *replace_nls_option(char *, char *, char *, char *, char *,
    char *, char *, int *);
static nl_catd file_open(const char *, int);
static nl_catd process_nls_path(char *, int);

nl_catd
catopen(const char *name, int oflag)
{
	nl_catd p;

	if (!name) {				/* Null pointer */
		errno = EFAULT;
		return ((nl_catd)-1);
	} else if (!*name) {		/* Empty string */
		errno = ENOENT;
		return ((nl_catd)-1);
	} else if (strchr(name, '/') != NULL) {
		/* If name contains '/', then it is complete file name */
		p = file_open(name, SAFE_F);
	} else {				/* Normal case */
		p = process_nls_path((char *)name, oflag);
	}

	if (p == NULL) {  /* Opening catalog file failed */
		return ((nl_catd)-1);
	} else {
		return (p);
	}
}


static nl_catd
find_catalog_using_nlspath_locale(char *name, char *locale, char *nlspath,
    int *ltc_defined)
{
	char *l;
	char *t;
	char *c;
	char *s;
	nl_catd	p;
	char path[PATH_MAX + 1];

	l = t = c = NULL;

	/*
	 * Extract language, territory, and codeset codes from a locale
	 * name of the following naming convention:
	 *
	 *	language[_territory][.codeset][@modifier]
	 */
	if (locale) {
		l = s = libc_strdup(locale);
		if (s == NULL)
			return (NULL);

		while (*s != '\0') {
			if (*s == '_') {
				*s++ = '\0';
				t = s;
				continue;
			} else if (*s == '.') {
				*s++ = '\0';
				c = s;
				continue;
			} else if (*s == '@') {
				*s = '\0';
				break;
			}

			s++;
		}
	}

	/*
	 * Process templates in NLSPATH.
	 * Unqualified pathnames are unsafe for file_open().
	 */
	s = nlspath;
	while (*s) {
		if (*s == ':') {
			p = file_open(name, UNSAFE_F);
			if (p != NULL) {
				if (l)
					libc_free(l);
				return (p);
			}

			s++;

			continue;
		}

		s = replace_nls_option(s, name, path, locale, l, t, c,
		    ltc_defined);

		p = file_open(path, UNSAFE_F);
		if (p != NULL) {
			if (l)
				libc_free(l);
			return (p);
		}

		if (*s == '\0')
			break;

		s++;
	}

	if (l)
		libc_free(l);

	return (NULL);
}

static nl_catd
find_catalog_from_system_default_location(char *pathname, char *locale,
    char *name, size_t len_name, char *saved, size_t saved_total_len)
{
	size_t len_locale;

	len_locale = strlen(locale);

	if (saved_total_len + len_locale > PATH_MAX)
		return (NULL);

	(void) memcpy(saved, locale, len_locale);
	saved += len_locale;

	(void) memcpy(saved, _LC_MSG_STR, _LC_MSG_STR_LEN - 1);
	saved += _LC_MSG_STR_LEN - 1;

	(void) memcpy(saved, name, len_name);
	saved += len_name;
	*saved = '\0';

	return (file_open(pathname, SAFE_F));
}

/*
 * This routine will try to find the catalog file by processing NLSPATH
 * environment variable and then from the system default location. If
 * the current locale name will not yield a catalog file, it will also try to
 * obtain alternative locales names, i.e., obsoleted Solaris locale names and
 * canonical locale name for the current locale name, if any, and then
 * search again the catalog file with the alternative locale names.
 *
 * It will return a catd id if it finds a valid catalog file; otherwise, NULL.
 */
static nl_catd
process_nls_path(char *name, int oflag)
{
	char *nlspath;
	char *locale;
	char *canonical;
	char *saved;
	char pathname[PATH_MAX + 1];
	nl_catd p;
	int ltc_defined;
	int start;
	int end;
	size_t len_name;
	size_t saved_total_len;

	/*
	 * locale=language_territory.codeset
	 * XPG4 uses LC_MESSAGES.
	 * XPG3 uses LANG.
	 * From the following two lines, choose one depending on XPG3 or 4.
	 *
	 * Chose XPG4. If oflag == NL_CAT_LOCALE, use LC_MESSAGES.
	 */
	if (oflag == NL_CAT_LOCALE)
		locale = setlocale(LC_MESSAGES, NULL);
	else
		locale = getenv("LANG");

	nlspath = getenv("NLSPATH");

	ltc_defined = 0;

	if (nlspath) {
		p = find_catalog_using_nlspath_locale(name, locale, nlspath,
		    &ltc_defined);
		if (p != NULL)
			return (p);
	}

	/*
	 * Implementation dependent default location of XPG3.
	 * We use /usr/lib/locale/<locale>/LC_MESSAGES/%N.
	 * If C locale, do not translate message.
	 */
	if (locale == NULL) {
		return (NULL);
	} else if (locale[0] == 'C' && locale[1] == '\0') {
		p = libc_malloc(sizeof (struct _nl_catd_struct));
		if (p == NULL) {
			/* malloc failed */
			return (NULL);
		}
		p->__content = NULL;
		p->__size = 0;
		p->__trust = 1;
		return (p);
	}

	(void) memcpy(pathname, _DFLT_LOC_PATH, _DFLT_LOC_PATH_LEN);
	saved = pathname + _DFLT_LOC_PATH_LEN;

	len_name = strlen(name);

	saved_total_len = _DFLT_LOC_PATH_LEN + _LC_MSG_STR_LEN + len_name;

	p = find_catalog_from_system_default_location(pathname, locale, name,
	    len_name, saved, saved_total_len);
	if (p != NULL)
		return (p);

	/*
	 * As the last resort, search again with obsoleted Solaris locale
	 * names and possibly also the canonical locale name for the current
	 * locale name as specified in PSARC/2009/594.
	 */
	alternative_locales(locale, &canonical, &start, &end, 1);

	if (start != -1) {
		for (; start <= end; start++) {
			if (ltc_defined == 1) {
				p = find_catalog_using_nlspath_locale(name,
				    (char *)__lc_obs_msg_lc_list[start],
				    nlspath, &ltc_defined);
				if (p != NULL)
					return (p);
			}

			p = find_catalog_from_system_default_location(pathname,
			    (char *)__lc_obs_msg_lc_list[start], name,
			    len_name, saved, saved_total_len);
			if (p != NULL)
				return (p);
		}
	}

	if (canonical != NULL) {
		if (ltc_defined == 1) {
			p = find_catalog_using_nlspath_locale(name, canonical,
			    nlspath, &ltc_defined);
			if (p != NULL)
				return (p);
		}

		p = find_catalog_from_system_default_location(pathname,
		    canonical, name, len_name, saved, saved_total_len);
		if (p != NULL)
			return (p);
	}

	return (NULL);
}


/*
 * This routine will replace substitution parameters in NLSPATH
 * with appropriate values. Returns expanded path.
 */
static char *
replace_nls_option(char *s, char *name, char *path, char *locale,
    char *lang, char *territory, char *codeset, int *ltc_defined)
{
	char	*t, *u;
	char	*limit;

	t = path;
	limit = path + PATH_MAX;

	while (*s && *s != ':') {
		if (t < limit) {
			/*
			 * %% is considered a single % character (XPG).
			 * %L : LC_MESSAGES (XPG4) LANG(XPG3)
			 * %l : The language element from the current locale.
			 *	(XPG3, XPG4)
			 */
			if (*s != '%')
				*t++ = *s;
			else if (*++s == 'N') {
				u = name;
				while (*u && t < limit)
					*t++ = *u++;
			} else if (*s == 'L') {
				if (locale) {
					u = locale;
					while (*u && t < limit)
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else if (*s == 'l') {
				if (lang) {
					u = lang;
					while (*u && *u != '_' && t < limit)
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else if (*s == 't') {
				if (territory) {
					u = territory;
					while (*u && *u != '.' && t < limit)
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else if (*s == 'c') {
				if (codeset) {
					u = codeset;
					while (*u && t < limit)
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else {
				if (t < limit)
					*t++ = *s;
			}
		}
		++s;
	}
	*t = '\0';
	return (s);
}

/*
 * This routine will open file, mmap it, and return catd id.
 */
static nl_catd
file_open(const char *name, int safe)
{
	int		fd;
	struct stat64	statbuf;
	void		*addr;
	struct _cat_hdr	*tmp;
	nl_catd		tmp_catd;
	int		trust;

	fd = nls_safe_open(name, &statbuf, &trust, safe);

	if (fd == -1) {
		return (NULL);
	}

	addr = mmap(0, (size_t)statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	(void) close(fd);

	if (addr == MAP_FAILED) {
		return (NULL);
	}

	/* check MAGIC number of catalogue file */
	tmp = (struct _cat_hdr *)addr;
	if (tmp->__hdr_magic != _CAT_MAGIC) {
		(void) munmap(addr, (size_t)statbuf.st_size);
		return (NULL);
	}

	tmp_catd = libc_malloc(sizeof (struct _nl_catd_struct));
	if (tmp_catd == NULL) {
		/* malloc failed */
		(void) munmap(addr, statbuf.st_size);
		return (NULL);
	}
	tmp_catd->__content = addr;
	tmp_catd->__size = (int)statbuf.st_size;
	tmp_catd->__trust = trust;

	return (tmp_catd);
}

int
catclose(nl_catd catd)
{
	if (catd &&
	    catd != (nl_catd)-1) {
		if (catd->__content) {
			(void) munmap(catd->__content, catd->__size);
			catd->__content = NULL;
		}
		catd->__size = 0;
		catd->__trust = 0;
		libc_free(catd);
	}
	return (0);
}
