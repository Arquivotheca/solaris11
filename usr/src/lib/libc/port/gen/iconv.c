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
 * Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "lint.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <alloca.h>
#include <langinfo.h>
#include "iconv.h"
#include "iconvP.h"
#include "../i18n/_loc_path.h"

static iconv_t	iconv_open_real(const char *, const char *, int);
static iconv_p	iconv_open_all(char *, char *, char *, int, int);
static iconv_p	iconv_open_private(const char *, const char *, int, int);
static iconv_p	iconv_search_alias(char **, char **, const char *,
	const char *, char *, int, int);
static size_t	passthru_icv_iconv(iconv_t, const char **, size_t *, char **,
	size_t *);
static size_t	passthru_icv_iconvstr(char *, size_t *, char *, size_t *, int);
static int	passthru_icv_iconvctl(iconv_t, int, void *);
static void	passthru_icv_close(iconv_t);
static char	*process_conv_modifier_and_special_names(const char *, int *);
static void	free_names(char *, const char *, char *, const char *);


/*
 * These functions are mainly implemented by using a shared object and
 * the dlopen() functions. The actual conversion algorithm for a particular
 * conversion is implemented via a shared object as a loadable conversion
 * module which is linked dynamically at run time.
 *
 * The loadable conversion module resides as either:
 *
 *	/usr/lib/iconv/geniconvtbl.so
 *
 * if the conversion is supported through a geniconvtbl code conversion
 * binary table or as a module that directly specifies the conversion at:
 *
 *	/usr/lib/iconv/fromcode%tocode.so
 *
 * where fromcode is the source encoding and tocode is the target encoding.
 * The modules have three must-have entries, _icv_open(), _icv_iconv(), and
 * _icv_close(), and three optional entries, _icv_open_attr(), _icv_iconvctl(),
 * and _icv_iconvstr().
 *
 * If there is no code conversion supported and if the fromcode and the tocode
 * are specifying the same codeset, then, the byte-by-byte, pass-through code
 * conversion that is embedded in the libc is used instead.
 *
 * The following are the related PSARC cases:
 *
 *	PSARC/1993/153 iconv/iconv_open/iconv_close
 *	PSARC/1999/292 Addition of geniconvtbl(1)
 *	PSARC/2001/072 GNU gettext support
 *	PSARC/2009/561 Pass-through iconv code conversion
 *	PSARC/2010/160 Libc iconv enhancement
 *
 * The PSARC/2001/072 includes the /usr/lib/iconv/alias interface.
 */

iconv_t
iconv_open(const char *tocode, const char *fromcode)
{
	return (iconv_open_real(tocode, fromcode, 0));
}

static iconv_t
iconv_open_real(const char *tocode, const char *fromcode, int string_based)
{
	iconv_t	cd;
	char	*ipath;
	char	*from;
	char	*from_canonical;
	char	*to;
	char	*to_canonical;
	int	flag;

	flag = 0;

	from = process_conv_modifier_and_special_names(fromcode, &flag);
	if (from == NULL) {
		errno = ENOMEM;
		return ((iconv_t)-1);
	}

	to = process_conv_modifier_and_special_names(tocode, &flag);
	if (to == NULL) {
		if (from != fromcode)
			free(from);
		errno = ENOMEM;
		return ((iconv_t)-1);
	}

	if ((cd = malloc(sizeof (struct _iconv_info))) == NULL) {
		free_names(to, tocode, from, fromcode);
		errno = ENOMEM;
		return ((iconv_t)-1);
	}

	/*
	 * Memory for ipath is allocated/released in this function.
	 */
	ipath = malloc(MAXPATHLEN);
	if (ipath == NULL) {
		free_names(to, tocode, from, fromcode);
		free(cd);
		errno = ENOMEM;
		return ((iconv_t)-1);
	}

	cd->_conv = iconv_open_all(to, from, ipath, flag, string_based);
	if (cd->_conv != (iconv_p)-1) {
		/* found a valid module for this conversion */
		free_names(to, tocode, from, fromcode);
		free(ipath);
		return (cd);
	}

	/*
	 * Now, try using the encoding name aliasing table
	 */
	cd->_conv = iconv_search_alias(&to_canonical, &from_canonical,
	    to, from, ipath, flag, string_based);
	free(ipath);
	if (cd->_conv == (iconv_p)-1) {
		/*
		 * As the last resort, check if the tocode and the fromcode
		 * are referring to the same codeset name or not. If so,
		 * assign the embedded pass-through code conversion.
		 */
		if (strcasecmp(to_canonical, from_canonical) != 0) {
			/*
			 * No valid conversion available. Do failure retrun
			 * with the errno set by iconv_search_alias().
			 */
			free_names(to_canonical, to, from_canonical, from);
			free_names(to, tocode, from, fromcode);
			free(cd);
			return ((iconv_t)-1);
		}

		/*
		 * For a pass-through byte-by-byte code conversion, allocate
		 * an internal conversion descriptor and initialize the data
		 * fields appropriately and we are done.
		 */
		cd->_conv = malloc(sizeof (struct _iconv_fields));
		if (cd->_conv == NULL) {
			free_names(to_canonical, to, from_canonical, from);
			free_names(to, tocode, from, fromcode);
			free(cd);
			errno = ENOMEM;
			return ((iconv_t)-1);
		}

		cd->_conv->_icv_state = (void *)malloc(sizeof (int));
		if (cd->_conv->_icv_state == NULL) {
			free_names(to_canonical, to, from_canonical, from);
			free_names(to, tocode, from, fromcode);
			free(cd->_conv);
			free(cd);
			errno = ENOMEM;
			return ((iconv_t)-1);
		}
		cd->_conv->_icv_handle = NULL;
		cd->_conv->_icv_iconv = passthru_icv_iconv;
		cd->_conv->_icv_iconvctl = passthru_icv_iconvctl;
		cd->_conv->_icv_iconvstr = passthru_icv_iconvstr;
		cd->_conv->_icv_close = passthru_icv_close;
		*((int *)(cd->_conv->_icv_state)) = flag;
	}

	/* found a valid module for this conversion */
	free_names(to_canonical, to, from_canonical, from);
	free_names(to, tocode, from, fromcode);
	return (cd);
}

static size_t
search_alias(char **paddr, size_t size, const char *variant)
{
	char	*addr = *paddr;
	char 	*p, *sp, *q;
	size_t	var_len, can_len;

	var_len = strlen(variant);
	p = addr;
	q = addr + size;
	while (q > p) {
		if (*p == '#') {
			/*
			 * Line beginning with '#' is a comment
			 */
			p++;
			while ((q > p) && (*p++ != '\n'))
				;
			continue;
		}
		/* skip leading spaces */
		while ((q > p) &&
		    ((*p == ' ') || (*p == '\t')))
			p++;
		if (q <= p)
			break;
		sp = p;
		while ((q > p) && (*p != ' ') &&
		    (*p != '\t') && (*p != '\n'))
			p++;
		if (q <= p) {
			/* invalid entry */
			break;
		}
		if (*p == '\n') {
			/* invalid entry */
			p++;
			continue;
		}

		if (((p - sp) != var_len) ||
		    ((strncmp(sp, variant, var_len) != 0) &&
		    (strncasecmp(sp, variant, var_len) != 0))) {
			/*
			 * didn't match
			 */

			/* skip remaining chars in this line */
			p++;
			while ((q > p) && (*p++ != '\n'))
				;
			continue;
		}

		/* matching entry found */

		/* skip spaces */
		while ((q > p) &&
		    ((*p == ' ') || (*p == '\t')))
			p++;
		if (q <= p)
			break;
		sp = p;
		while ((q > p) && (*p != ' ') &&
		    (*p != '\t') && (*p != '\n'))
			p++;
		can_len = p - sp;
		if (can_len == 0) {
			while ((q > p) && (*p++ != '\n'))
				;
			continue;
		}
		*paddr = sp;
		return (can_len);
		/* NOTREACHED */
	}
	return (0);
}

static iconv_p
iconv_open_all(char *to, char *from, char *ipath, int flag, int string_based)
{
	iconv_p	cv;
	int	len;

	/*
	 * First, try using the geniconvtbl conversion, which is
	 * performed by /usr/lib/iconv/geniconvtbl.so with
	 * the conversion table file:
	 * /usr/lib/iconv/geniconvtbl/binarytables/fromcode%tocode.bt
	 *
	 * If the geniconvtbl conversion cannot be done,
	 * try the conversion by the individual shared object.
	 */

	len = snprintf(ipath, MAXPATHLEN, _GENICONVTBL_PATH, from, to);
	if ((len <= MAXPATHLEN) && (access(ipath, R_OK) == 0)) {
		/*
		 * from%to.bt exists in the table dir
		 */
		cv = iconv_open_private(_GENICONVTBL_INT_PATH, ipath,
		    flag, string_based);
		if (cv != (iconv_p)-1) {
			/* found a valid module for this conversion */
			return (cv);
		}
	}

	/* Next, try /usr/lib/iconv/from%to.so */
	len = snprintf(ipath, MAXPATHLEN, _ICONV_PATH, from, to);
	if ((len <= MAXPATHLEN) && (access(ipath, R_OK) == 0)) {
		/*
		 * /usr/lib/iconv/from%to.so exists
		 * errno will be set by iconv_open_private on error
		 */
		return (iconv_open_private(ipath, NULL, flag, string_based));
	}
	/* no valid module for this conversion found */
	errno = EINVAL;
	return ((iconv_p)-1);
}

static iconv_p
iconv_search_alias(char **to_canonical, char **from_canonical,
	const char *tocode, const char *fromcode, char *ipath,
	int flag, int string_based)
{
	char	*p;
	size_t	tolen, fromlen;
	iconv_p	cv;
	int	fd;
	struct stat64	statbuf;
	caddr_t	addr;
	size_t	buflen;

	*to_canonical = (char *)tocode;
	*from_canonical = (char *)fromcode;

	fd = open(_ENCODING_ALIAS_PATH, O_RDONLY);
	if (fd == -1) {
		/*
		 * if no alias file found,
		 * errno will be set to EINVAL.
		 */
		errno = EINVAL;
		return ((iconv_p)-1);
	}
	if (fstat64(fd, &statbuf) == -1) {
		(void) close(fd);
		/* use errno set by fstat64 */
		return ((iconv_p)-1);
	}
	buflen = (size_t)statbuf.st_size;
	addr = mmap(NULL, buflen, PROT_READ, MAP_SHARED, fd, 0);
	(void) close(fd);
	if (addr == MAP_FAILED) {
		/* use errno set by mmap */
		return ((iconv_p)-1);
	}
	p = (char *)addr;
	tolen = search_alias(&p, buflen, tocode);
	if (tolen) {
		*to_canonical = malloc(tolen + 1);
		if (*to_canonical == NULL) {
			(void) munmap(addr, buflen);
			*to_canonical = (char *)tocode;
			errno = ENOMEM;
			return ((iconv_p)-1);
		}
		(void) memcpy(*to_canonical, p, tolen);
		(*to_canonical)[tolen] = '\0';
	}
	p = (char *)addr;
	fromlen = search_alias(&p, buflen, fromcode);
	if (fromlen) {
		*from_canonical = malloc(fromlen + 1);
		if (*from_canonical == NULL) {
			(void) munmap(addr, buflen);
			*from_canonical = (char *)fromcode;
			errno = ENOMEM;
			return ((iconv_p)-1);
		}
		(void) memcpy(*from_canonical, p, fromlen);
		(*from_canonical)[fromlen] = '\0';
	}
	(void) munmap(addr, buflen);
	if (tolen == 0 && fromlen == 0) {
		errno = EINVAL;
		return ((iconv_p)-1);
	}

	cv = iconv_open_all(*to_canonical, *from_canonical, ipath,
	    flag, string_based);

	/* errno set by iconv_open_all on error */
	return (cv);
}

static iconv_p
iconv_open_private(const char *lib, const char *tbl, int flag, int string_based)
{
	iconv_t (*fptr)(const char *);
	iconv_t (*fptr_attr)(int, void *);
	iconv_p cdpath;

	if ((cdpath = malloc(sizeof (struct _iconv_fields))) == NULL) {
		errno = ENOMEM;
		return ((iconv_p)-1);
	}

	if ((cdpath->_icv_handle = dlopen(lib, RTLD_LAZY)) == 0)
		goto ICONV_OPEN_ERR_TWO;

	/*
	 * If this is called from iconvstr(), get the address of
	 * _icv_iconvstr and return since that's all we need.
	 */
	if (string_based) {
		if ((cdpath->_icv_iconvstr =
		    (size_t (*)(char *, size_t *, char *, size_t *, int))
		    dlsym(cdpath->_icv_handle, "_icv_iconvstr")) == NULL)
			goto ICONV_OPEN_ERR_ONE;

		return (cdpath);
	}

	/*
	 * Get address of _icv_open or _icv_open_attr depending on whether
	 * we have at least a value defined in the flag.
	 */
	if (flag == 0) {
		if ((fptr = (iconv_t (*)(const char *))dlsym(
		    cdpath->_icv_handle, "_icv_open")) == NULL)
			goto ICONV_OPEN_ERR_ONE;
	} else if ((fptr_attr = (iconv_t (*)(int, void *))dlsym(
	    cdpath->_icv_handle, "_icv_open_attr")) == NULL) {
		goto ICONV_OPEN_ERR_ONE;
	}

	/*
	 * gets address of _icv_iconv in the loadable conversion module
	 * and stores it in cdpath->_icv_iconv
	 */
	if ((cdpath->_icv_iconv = (size_t(*)(iconv_t, const char **,
	    size_t *, char **, size_t *))dlsym(cdpath->_icv_handle,
	    "_icv_iconv")) == NULL)
		goto ICONV_OPEN_ERR_ONE;

	/*
	 * gets address of _icv_close in the loadable conversion module
	 * and stores it in cd->_icv_close
	 */
	if ((cdpath->_icv_close = (void(*)(iconv_t))dlsym(cdpath->_icv_handle,
	    "_icv_close")) == NULL)
		goto ICONV_OPEN_ERR_ONE;

	/*
	 * Get the address of _icv_iconvctl() from the module.
	 * Saving NULL via dlsym() is normal and, in that case, simply,
	 * the module doesn't support the iconvctl().
	 */
	cdpath->_icv_iconvctl = (int (*)(iconv_t, int, void *))dlsym(
	    cdpath->_icv_handle, "_icv_iconvctl");

	/*
	 * Initialize the state of the _icv_iconv conversion routine by
	 * calling _icv_open() or _icv_open_attr().
	 *
	 * For all regular iconv modules, NULL will be passed for the tbl
	 * argument although the iconv_open() of the module won't use that.
	 */
	if (flag == 0) {
		cdpath->_icv_state = (void *)(*fptr)(tbl);
	} else {
		cdpath->_icv_state = (void *)(*fptr_attr)(flag, (void *)tbl);
	}

	if (cdpath->_icv_state != (struct _icv_state *)-1)
		return (cdpath);

ICONV_OPEN_ERR_ONE:
	(void) dlclose(cdpath->_icv_handle);
ICONV_OPEN_ERR_TWO:
	free(cdpath);
	errno = EINVAL;

	return ((iconv_p)-1);
}

int
iconv_close(iconv_t cd)
{
	if (cd == NULL) {
		errno = EBADF;
		return (-1);
	}
	(*(cd->_conv)->_icv_close)(cd->_conv->_icv_state);
	if (cd->_conv->_icv_handle != NULL)
		(void) dlclose(cd->_conv->_icv_handle);
	free(cd->_conv);
	free(cd);
	return (0);
}

static void
passthru_icv_close(iconv_t cd)
{
	free((void *)cd);
}

#pragma weak __xpg5_iconv = iconv
size_t
iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
	char **outbuf, size_t *outbytesleft)
{
	/* check if cd is valid */
	if (cd == NULL) {
		errno = EBADF;
		return ((size_t)-1);
	}

	/* direct conversion */
	return ((*(cd->_conv)->_icv_iconv)(cd->_conv->_icv_state,
	    (const char **)inbuf, inbytesleft, outbuf, outbytesleft));
}

static size_t
/*LINTED E_FUNC_ARG_UNUSED*/
passthru_icv_iconv(iconv_t cd, const char **inbuf, size_t *inbufleft,
	char **outbuf, size_t *outbufleft)
{
	size_t ibl;
	size_t obl;
	size_t len;
	size_t ret_val;

	/* For any state reset request, return success. */
	if (inbuf == NULL || *inbuf == NULL)
		return (0);

	/*
	 * Initialize internally used variables for a better performance
	 * and prepare for a couple of the return values before the actual
	 * copying of the bytes.
	 */
	ibl = *inbufleft;
	obl = *outbufleft;

	if (ibl > obl) {
		len = obl;
		errno = E2BIG;
		ret_val = (size_t)-1;
	} else {
		len = ibl;
		ret_val = 0;
	}

	/*
	 * Do the copy using memmove(). There are no EILSEQ or EINVAL
	 * checkings since this is a simple copying.
	 */
	(void) memmove((void *)*outbuf, (const void *)*inbuf, len);

	/* Update the return values related to the buffers then do return. */
	*inbuf = *inbuf + len;
	*outbuf = *outbuf + len;
	*inbufleft = ibl - len;
	*outbufleft = obl - len;

	return (ret_val);
}

int
iconvctl(iconv_t cd, int req, void *arg)
{
	int flag;

	if (cd == NULL) {
		errno = EBADF;
		return (-1);
	}

	if (req < ICONV_GET_CONVERSION_BEHAVIOR || req > ICONV_TRIVIALP) {
		errno = EINVAL;
		return (-1);
	}

	if (cd->_conv->_icv_iconvctl == NULL) {
		errno = ENOTSUP;
		return (-1);
	}

	if (req == ICONV_SET_CONVERSION_BEHAVIOR) {
		flag = *((int *)arg);

		if ((flag & ICONV_CONV_ILLEGAL_DISCARD) != 0)
			flag &= ~(ICONV_CONV_ILLEGAL_REPLACE_HEX);
		if ((flag & ICONV_CONV_NON_IDENTICAL_DISCARD) != 0)
			flag &= ~(ICONV_CONV_NON_IDENTICAL_REPLACE_HEX |
			    ICONV_CONV_NON_IDENTICAL_TRANSLITERATE);
		if ((flag & ICONV_CONV_NON_IDENTICAL_REPLACE_HEX) != 0)
			flag &= ~(ICONV_CONV_NON_IDENTICAL_TRANSLITERATE);

		*((int *)arg) = flag;
	}

	return ((*cd->_conv->_icv_iconvctl)(cd->_conv->_icv_state, req, arg));
}

static int
passthru_icv_iconvctl(iconv_t cd, int req, void *arg)
{
	int a;
	long f;

	a = *((int *)arg);
	f = *((int *)cd);

	switch (req) {
	case ICONV_GET_CONVERSION_BEHAVIOR:
		a = f;
		break;
	case ICONV_GET_DISCARD_ILSEQ:
		if ((f & ICONV_CONV_ILLEGAL_DISCARD) != 0 &&
		    (f & ICONV_CONV_NON_IDENTICAL_DISCARD) != 0)
			a = 1;
		else
			a = 0;
		break;
	case ICONV_GET_TRANSLITERATE:
		if ((f & ICONV_CONV_NON_IDENTICAL_TRANSLITERATE) != 0)
			a = 1;
		else
			a = 0;
		break;
	case ICONV_SET_CONVERSION_BEHAVIOR:
		f = a;
		break;
	case ICONV_SET_DISCARD_ILSEQ:
		if (a == 0)
			f &= ~(ICONV_CONV_ILLEGAL_DISCARD |
			    ICONV_CONV_NON_IDENTICAL_DISCARD);
		else
			f |= ICONV_CONV_ILLEGAL_DISCARD |
			    ICONV_CONV_NON_IDENTICAL_DISCARD;
		break;
	case ICONV_SET_TRANSLITERATE:
		if (a == 0)
			f &= ~(ICONV_CONV_NON_IDENTICAL_TRANSLITERATE);
		else
			f |= ICONV_CONV_NON_IDENTICAL_TRANSLITERATE;
		break;
	case ICONV_TRIVIALP:
		a = 1;
		break;
	}

	*((int *)cd) = f;
	*((int *)arg) = a;

	return (0);
}

size_t
iconvstr(const char *tocode, const char *fromcode, char *inarray,
	size_t *inlen, char *outarray, size_t *outlen, int flag)
{
	iconv_t cd;
	size_t r;

	if ((cd = iconv_open_real(tocode, fromcode, 1)) == (iconv_t)-1) {
		errno = EBADF;
		return ((size_t)-1);
	}

	r = (*cd->_conv->_icv_iconvstr)(inarray, inlen, outarray, outlen, flag);

	(void) dlclose(cd->_conv->_icv_handle);
	free(cd->_conv);
	free(cd);

	return (r);
}

static size_t
passthru_icv_iconvstr(char *inarray, size_t *inlen, char *outarray,
	size_t *outlen, int flag)
{
	char *np;
	size_t len;
	size_t ret_val;

	if ((flag & ICONV_IGNORE_NULL) == 0 &&
	    (np = (char *)memchr((const void *)inarray, 0, *inlen)) != NULL)
		len = np - inarray;
	else
		len = *inlen;

	if (len > *outlen) {
		len = *outlen;
		errno = E2BIG;
		ret_val = (size_t)-1;
	} else {
		ret_val = 0;
	}

	(void) memmove((void *)outarray, (const void *)inarray, len);

	*inlen -= len;
	*outlen -= len;

	return (ret_val);
}

/*
 * The following function maps "", "char", and "wchar_t" into some
 * uniquely identifiable names as specified in the iconv-l10n-guide.txt at
 * the materials directory of PSARC/2010/160.
 *
 * For any other names, if requested, it duplicates the name into
 * a new memory block and returns.
 */
static char *
process_special_names(char *name, size_t len, int no_malloc)
{
	char *s;

	if (len == 0 || (len == 4 && strncasecmp(name, "char", 4) == 0)) {
		return (strdup(nl_langinfo(CODESET)));
	} else if (len == 7 && strncasecmp(name, "wchar_t", 7) == 0) {
		s = nl_langinfo(CODESET);

		if ((name = malloc(strlen(s) + 9)) == NULL)
			return (NULL);

		(void) strcpy(name, "wchar_t-");
		return (strcat(name, s));
	}

	if (no_malloc)
		return (name);

	if ((s = malloc(len + 1)) == NULL)
		return (NULL);

	(void) memcpy(s, name, len);
	s[len] = '\0';

	return (s);
}

/*
 * The min and max lengths of all indicators at this point are 6 of "IGNORE"
 * and 27 of "NON_IDENTICAL_TRANSLITERATE", respectively.
 */
#define	ICONV_MIN_INDICATOR_LEN			6
#define	ICONV_MAX_INDICATOR_LEN			27

#define	SAME_LEN(len, t)	((len) == (sizeof (t) - 1))
#define	SAME_STR(s, t)		(strncasecmp((s), (t), sizeof (t) - 1) == 0)
#define	SAME_LEN_STR(s, t, len)	(SAME_LEN((len), (t)) && SAME_STR((s), (t)))

/*
 * The following function clears any prior flag values that are
 * conflicting with the new value asked in "s" and then sets the new
 * one at the flag.
 */
static int
check_flag_values(char *s, size_t len, int flag)
{
	if (len < ICONV_MIN_INDICATOR_LEN || len > ICONV_MAX_INDICATOR_LEN)
		return (flag);

	if (SAME_LEN_STR(s, "ILLEGAL_DISCARD", len)) {

		flag = flag & ~(ICONV_CONV_ILLEGAL_REPLACE_HEX) |
		    ICONV_CONV_ILLEGAL_DISCARD;

	} else if (SAME_LEN(len, "ILLEGAL_REPLACE_HEX")) {

		if (SAME_STR(s, "ILLEGAL_REPLACE_HEX"))
			flag = flag & ~(ICONV_CONV_ILLEGAL_DISCARD) |
			    ICONV_CONV_ILLEGAL_REPLACE_HEX;
		else if (SAME_STR(s, "ILLEGAL_RESTORE_HEX"))
			flag |= ICONV_CONV_ILLEGAL_RESTORE_HEX;

	} else if (SAME_LEN_STR(s, "NON_IDENTICAL_DISCARD", len)) {

		flag = flag & ~(ICONV_CONV_NON_IDENTICAL_REPLACE_HEX |
		    ICONV_CONV_NON_IDENTICAL_TRANSLITERATE) |
		    ICONV_CONV_NON_IDENTICAL_DISCARD;

	} else if (SAME_LEN(len, "NON_IDENTICAL_REPLACE_HEX")) {

		if (SAME_STR(s, "NON_IDENTICAL_REPLACE_HEX"))
			flag = flag & ~(ICONV_CONV_NON_IDENTICAL_DISCARD |
			    ICONV_CONV_NON_IDENTICAL_TRANSLITERATE) |
			    ICONV_CONV_NON_IDENTICAL_REPLACE_HEX;
		else if (SAME_STR(s, "NON_IDENTICAL_RESTORE_HEX"))
			flag |= ICONV_CONV_NON_IDENTICAL_RESTORE_HEX;

	} else if (SAME_LEN_STR(s, "NON_IDENTICAL_TRANSLITERATE", len) ||
	    SAME_LEN_STR(s, "TRANSLIT", len)) {

		flag = flag & ~(ICONV_CONV_NON_IDENTICAL_DISCARD |
		    ICONV_CONV_NON_IDENTICAL_REPLACE_HEX) |
		    ICONV_CONV_NON_IDENTICAL_TRANSLITERATE;

	} else if (SAME_LEN_STR(s, "IGNORE", len)) {

		flag = flag & ~(ICONV_CONV_ILLEGAL_REPLACE_HEX |
		    ICONV_CONV_NON_IDENTICAL_REPLACE_HEX |
		    ICONV_CONV_NON_IDENTICAL_TRANSLITERATE) |
		    ICONV_CONV_ILLEGAL_DISCARD |
		    ICONV_CONV_NON_IDENTICAL_DISCARD;

	} else if (SAME_LEN(len, "REPLACE_HEX")) {

		if (SAME_STR(s, "REPLACE_HEX"))
			flag = flag & ~(ICONV_CONV_ILLEGAL_DISCARD |
			    ICONV_CONV_NON_IDENTICAL_DISCARD |
			    ICONV_CONV_NON_IDENTICAL_TRANSLITERATE) |
			    ICONV_CONV_ILLEGAL_REPLACE_HEX |
			    ICONV_CONV_NON_IDENTICAL_REPLACE_HEX;
		else if (SAME_STR(s, "RESTORE_HEX"))
			flag |= ICONV_CONV_ILLEGAL_RESTORE_HEX |
			    ICONV_CONV_NON_IDENTICAL_RESTORE_HEX;

	}

	return (flag);
}

/*
 * The following function accepts a tocode/fromcode name, separates iconv
 * code conversion behavior modification indicators if any and the actual
 * codeset name from the name and sets the flag. It also processes special
 * tocode/fromcode names, "", "char", and "wchar_t" by using
 * process_special_names().
 */
static char *
process_conv_modifier_and_special_names(const char *name, int *flag)
{
	char *start;
	char *prev_start;
	char *end;
	size_t len;

	start = (char *)name;
	while ((start = strchr(start, '/')) != NULL) {
		if (*(start + 1) == '/')
			break;
		start++;
	}

	if (start == NULL)
		return (process_special_names((char *)name, strlen(name), 1));

	len = start - name;

	prev_start = start += 2;

	while ((end = strchr(start, '/')) != NULL) {
		if (*(end + 1) != '/') {
			start = end + 1;
			continue;
		}

		*flag = check_flag_values(prev_start, end - prev_start, *flag);

		prev_start = start = end + 2;
	}

	end = start + strlen(start);
	*flag = check_flag_values(prev_start, end - prev_start, *flag);

	return (process_special_names((char *)name, len, 0));
}

static void
free_names(char *newto, const char *orgto, char *newfrom, const char *orgfrom)
{
	if (newto != orgto)
		free((void *)newto);

	if (newfrom != orgfrom)
		free((void *)newfrom);
}
