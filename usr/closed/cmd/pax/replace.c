/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

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
 * Copyright (c) 1989 Mark H. Colburn.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice is duplicated in all such
 * forms and that any documentation, advertising materials, and other
 * materials related to such distribution and use acknowledge that the
 * software was developed * by Mark H. Colburn and sponsored by The
 * USENIX Association.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * replace.c - regular expression pattern replacement functions
 *
 * Author: Mark H. Colburn, NAPS International
 * Sponsored by The USENIX Association for public distribution.
 *
 * DESCRIPTION
 *
 *	These routines provide for regular expression file name replacement
 *	as required by pax.
 */

/* Headers */

#include <wchar.h>
#include <wctype.h>
#include <widec.h>
#include "pax.h"

/* Function Prototypes */

static long do_regsub(regmatch_t pmatch[], char **src, size_t *srcsize,
    long offst, char *replace);

/*
 * sepsrch - search a wide char string for a wide char
 *
 * DESCRIPTION
 *
 *	Search the input string for the specified delimiter
 *	If the char is found then return a pointer into the
 *  input string where the character was found, otherwise
 *  return a NULL string.
 *
 * PARAMETERS
 *
 *	wcstr - the string to be searched
 *	sep	  - the char to search for
 */

static wchar_t *
sepsrch(const wchar_t *wcstr, wchar_t sep)
{
	wchar_t *wc;

	wc = (wchar_t *)wcstr;
	if (*wc == (wchar_t)NULL)
		return ((wchar_t *)NULL);
	do {
		if ((*wc == sep) && (*(wc-1) != L'\\'))
			return (wc);
	} while (*(++wc));
	return ((wchar_t *)NULL);
}


/*
 * chk_back_ref - check the back references in a replacement string
 *
 * DESCRIPTION
 *	Check for valid back references in the replacement string.  An
 *  invalid back reference will cause chk_back_ref to return -1.
 *  No back references will return 0, otherwise the highest back
 *  reference found will be returned.
 *
 * PARAMETERS
 *
 *	wchar_t *ptr - a wide char pointer into the replacement string.
 */

static int
chk_back_ref(const wchar_t *ptr)
{
	int		bkref;
	int		retval;

	retval = 0;
	while (*ptr != L'\0') {
		if (*ptr++ == L'\\') {
			/* get out quick if an invalid back reference */
			if (*ptr == L'0')
				return (-1);
			if ((*ptr >= L'1') && (*ptr <= L'9')) {
				bkref = *ptr - '0';
				retval = (retval < bkref) ? bkref : retval;
			}
			ptr++;
		}
	}
	return (retval);
}


/*
 * add_replstr - add a replacement string to the replacement string list
 *
 * DESCRIPTION
 *
 *	Add_replstr adds a replacement string to the replacement string
 *	list which is applied each time a file is about to be processed.
 *
 * PARAMETERS
 *
 *	char	*pattern	- A regular expression which is to be parsed
 */


void
add_replstr(char *pattern)
{
	Replstr		*rptr;
	wchar_t		wc_pattern[PATH_MAX+1];
	wchar_t 	*wcs;
	wchar_t 	*ptr;
	wchar_t 	*wptr;
	wchar_t		sep;
	char		old[PATH_MAX+1];
	char		*new;
	size_t		retval;
	int		highbkref;

	if ((rptr = (Replstr *) calloc(1, sizeof (Replstr))) ==
	    (Replstr *)NULL) {
		warn(gettext("Replacement string not added"),
		    gettext("No space"));
		return;
	}

	if ((new = (char *)malloc(strlen(pattern)+1)) == (char *)NULL) {
		warn(gettext("Replacement string not added"),
		    gettext("No space"));
		free(rptr);
		return;
	}
	retval = mbstowcs(wc_pattern, pattern, PATH_MAX + 1);
	if (retval == (size_t)-1) {
		/* ERROR-Invalid mb char */
		warn(gettext("Replacement string not added"),
		    gettext("Invalid multi-byte character"));
		goto quit;
	} else if (retval == (PATH_MAX + 1)) {
		/* ERROR-Name too large no terminating null */
		warn(gettext("Replacement string not added"),
		    gettext("Pathname too long"));
		goto quit;
	}

	/* get the delimiter - the first character in wcpattern */
	/* make wcs now point to the first character in old */
	sep = wc_pattern[0];
	wcs = wc_pattern+1;
	/* set wptr to point to old */
	wptr = wcs;
	retval = 0;

	/* find the second separator */
	if ((ptr = sepsrch(wcs, sep)) == (wchar_t *)NULL) {
		warn(gettext("Replacement string not added"),
		    gettext("Bad delimiters"));
		goto quit;
	}

	/* old will point to the search string and be null terminated */
	wcs = ptr;
	*ptr = (wchar_t)L'\0';
	if (wcstombs(old, wptr, PATH_MAX + 1) == (size_t)-1)
		warn(gettext("Could not convert from wcs to mbs"), pattern);

	/* set wptr to point to new */
	wptr = ++wcs;

	/* find the third and last separator */
	if ((ptr = sepsrch(wcs, sep)) == (wchar_t *)NULL) {
		warn(gettext("Replacement string not added"),
		    gettext("Bad delimiters"));
		goto quit;
	}
	/* new will point to the replacement string and be null terminated */
	wcs = ptr;
	*ptr = (wchar_t)L'\0';
	if (wcstombs(new, wptr, PATH_MAX + 1) == (size_t)-1)
		warn(gettext("Could not convert from wcs to mbs"), pattern);

	/* check for trailing g or p options */
	while (*++wcs) {
		if (*wcs == (wchar_t)L'g')
			rptr->global = 1;
		else if (*wcs == (wchar_t)L'p')
			rptr->print = 1;
		else {
			warn(gettext("Replacement string not added"),
			    gettext("Invalid trailing RE option"));
			goto quit;
		}
	}

	if ((highbkref = chk_back_ref(wptr)) == -1) {
		/* ERROR-Invalid back references */
		warn(gettext("Replacement string not added"),
		    gettext("Invalid RE backreference(s)"));
		goto quit;
	}

	/*
	 * Now old points to 'old' and new points to 'new' and both
	 * are '\0' terminated
	 */
	if ((retval = regcomp(&rptr->comp, old, REG_NEWLINE)) != 0) {
		(void) regerror(retval, &rptr->comp, old, sizeof (old));
		warn(gettext("Replacement string not added"), old);
		goto quit;
	}

	if (rptr->comp.re_nsub < highbkref) {
		warn(gettext("Replacement string not added"),
		    gettext("Invalid RE backreference(s)"));
		goto quit;
	}

	rptr->replace = new;
	rptr->next = NULL;
	if (rplhead == (Replstr *)NULL) {
		rplhead = rptr;
		rpltail = rptr;
	} else {
		rpltail->next = rptr;
		rpltail = rptr;
	}
		return;
quit:
	free(rptr);
	free(new);
}

/*
 * rpl_name - possibly replace a name with a regular expression
 *
 * DESCRIPTION
 *
 *	The string name is searched for in the list of regular expression
 *	substituions.  Whenever the string matches one of the regular
 *	expressions, the string is modified as specified by the user.
 *
 * PARAMETERS
 *
 *	char	**name	- name to search for and possibly modify
 *			  should be a buffer of at least PATH_MAX
 *	size_t	*namesz - the size of the 'name' buffer
 */

void
rpl_name(char **name, size_t *namesz)
{
	long			offst = 0;
	int			found;
	static size_t		maxsize = PATH_MAX + 1;
	static size_t		currbufsz = 0;
	Replstr			*rptr;
	static char		*buff = NULL;

	static regmatch_t	pmatch[10];
	size_t			nmatch = (sizeof (pmatch) / sizeof (pmatch[0]));


	if (*namesz > maxsize) {
		maxsize = *namesz;
	}
	if (buff == NULL) {
		currbufsz = maxsize;
		if ((buff = malloc(currbufsz)) == NULL) {
			fatal(gettext("out of memory"));
		}
	} else if (maxsize >= currbufsz) {
		currbufsz = maxsize;
		if ((buff = realloc(buff, currbufsz)) == NULL) {
			fatal(gettext("out of memory"));
		}
	}

	(void) memset(buff, '\0', currbufsz);
	(void) strncpy(buff, *name, *namesz);

	for (rptr = rplhead, found = 0; !found && rptr; rptr = rptr->next) {
		size_t	buffsz;
		if (regexec(&rptr->comp, buff, nmatch, pmatch, 0) == 0) {
			found = 1;
			offst = do_regsub(pmatch, &buff, &currbufsz, 0,
			    rptr->replace);

			while (rptr->global && offst >= 0 &&
			    regexec(&rptr->comp, buff + offst, nmatch,
			    pmatch, REG_NOTBOL) == 0) {
				offst = do_regsub(pmatch, &buff, &currbufsz,
				    offst, rptr->replace);
			}
		}

		if (found && rptr->print)
			(void) fprintf(stderr, "%s >> %s\n", *name, buff);
		if ((buffsz = strlen(buff) + 1) > *namesz) {
			*namesz = buffsz;
			if ((*name = realloc(*name, *namesz)) == NULL) {
				fatal(gettext("out of memory"));
			}
		}
		(void) strncpy(*name, buff, buffsz);
	}
}

/*
 *  do_regsub - Substitute the replacement string for the matching
 *		portion of the original, expanding backreferences, etc,
 *		along the way.
 *
 *		Modifies the src parameter, and returns the offset (with
 *		respect to the beginning of the resulting value of src)
 *		of the point where the caller should continue searching.
 *
 *		Or returns -1 if there is a problem.
 */

static long
do_regsub(regmatch_t pmatch[], char **src, size_t *srcsize, long offst,
    char *replace)
{
	static char	*dest;
	char		*dstptr;
	char		*repptr;
	char		*repend;
	char		*tptr;
	int		no;
	static int	currdestsz = 0;
	static int	maxsize = 0;
	long		len;
	wchar_t		c[2] = {0};

	if (*srcsize > maxsize) {
		maxsize = *srcsize;
	}
	if (dest == NULL) {
		currdestsz = maxsize;
		if ((dest = malloc(currdestsz)) == NULL) {
			fatal(gettext("out of memory"));
		}
	} else if (maxsize >= currdestsz) {
		currdestsz = maxsize;
		if ((dest = realloc(dest, currdestsz)) == NULL) {
			fatal(gettext("out of memory"));
		}
	}

	(void) memset(dest, '\0', currdestsz);
	(void) strncpy(dest, *src, pmatch[0].rm_so  + offst);

	for (dstptr = dest + pmatch[0].rm_so + offst, repptr = repend = replace,
	    repend += strlen(repptr); repptr < repend; /* empty */) {
		if ((len = mbtowc(&c[0], repptr, MB_CUR_MAX)) < 0) {
			(void) fprintf(stderr, gettext("%s: bad multibyte "
			    "character in replacement.\n"), myname);
			return (-1);
		}

		repptr += len;
		no = -1;

		if (c[0] == (wchar_t)'&')
			no = 0;
		else if (c[0] == (wchar_t)'\\') {
			if ((len = mbtowc(&c[0], repptr, MB_CUR_MAX)) < 0) {
				(void) fprintf(stderr, gettext(
				    "%s: bad multibyte character in "
				    "replacement.\n"), myname);
				return (-1);
			}

			repptr += len;

			if (iswdigit(c[0]))
				no = watoi(c);
		}

		if (no < 0) {
			char	*tchar;
			if ((tchar = malloc(MB_CUR_MAX)) == NULL) {
				fatal(gettext("out of memory"));
			}
			if ((len = wctomb(tchar, c[0])) < 0) {
				(void) fprintf(stderr, gettext("%s: bad wide "
				    "character in replacement.\n"), myname);
				return (-1);
			}
			/* current str len + replace str len + null byte */
			if ((dstptr - dest + len + 1) > currdestsz) {
				int	prevsz = currdestsz;
				currdestsz = dstptr - dest + len + 1;
				if ((dest = realloc(dest,
				    currdestsz)) == NULL) {
					fatal(gettext("out of memory"));
				}
				(void) memset(dest + prevsz, '\0',
				    currdestsz - prevsz);
			}
			(void) strncpy(dstptr, tchar, len);
			free(tchar);

			dstptr += len;
		} else if (pmatch[no].rm_so != -1 && pmatch[no].rm_so != -1) {
			len = pmatch[no].rm_eo - pmatch[no].rm_so;
			/* current str len + replace str len + null byte */
			if ((dstptr - dest + len + 1) > currdestsz) {
				int	prevsz = currdestsz;
				currdestsz = dstptr - dest + len + 1;
				if ((dest = realloc(dest,
				    currdestsz)) == NULL) {
					fatal(gettext("out of memory"));
				}
				(void) memset(dest + prevsz, '\0',
				    currdestsz - prevsz);
			}
			(void) strncpy(dstptr, *src + offst + pmatch[no].rm_so,
			    len);
			dstptr += len;

			if (len != 0 && *(dstptr - 1) == '\0') {
				warn(gettext("regexp"),
				    gettext("damaged match string"));
				return (-1);
			}
		}
	}

	if ((tptr = *src + offst + pmatch[0].rm_eo) != NULL) {
		if ((dstptr - dest + strlen(tptr) + 1) > currdestsz) {
			currdestsz = dstptr - dest + strlen(tptr) + 1;
			if ((dest = realloc(dest, currdestsz)) == NULL) {
				fatal(gettext("out of memory"));
			}
		}
	}
	(void) strcpy(dstptr, *src + offst + pmatch[0].rm_eo);

	if ((strlen(dest) + 1) > *srcsize) {
		*srcsize = strlen(dest) + 1;
		if ((*src = realloc(*src, *srcsize)) == NULL) {
			fatal(gettext("out of memory"));
		}
	}
	(void) strcpy(*src, dest);

	offst = dstptr - dest;
	return (offst);
}

/*
 * get_disposition - get a file disposition
 *
 * DESCRIPTION
 *
 *	Get a file disposition from the user.  If the user enters
 *	the locale's equivalent of yes the the file is processed;
 * 	anything else and the file is ignored.
 *
 * 	The functionality (-y option) is not in 1003.2 as of DRAFT 11
 * 	but we leave it in just in case.
 *
 *	If the user enters EOF, then pax exits with a non-zero return
 *	status.
 *
 * PARAMETERS
 *
 *	int	mode	- string signifying the action to be taken on file
 *	char	*name	- the name of the file
 *	size_t	namesz	- the size of the name buffer
 *
 * RETURNS
 *
 *	Returns 1 if the file should be processed, 0 if it should not.
 */


int
get_disposition(int mode, char *name, size_t namesz)
{
	size_t		bufsz = namesz + 11;
	size_t		ansz = 10;
	char		*ans;
	char		*buf;

	if (f_disposition) {
		if ((buf = calloc(bufsz, sizeof (char))) == NULL) {
			fatal(gettext("out of memory"));
		}
		if (mode == ADD) {
			(void) snprintf(buf, bufsz,
			    gettext("add %s? "), name);
		} else if (mode == EXTRACT) {
			(void) snprintf(buf, bufsz,
			    gettext("extract %s? "), name);
		} else {	/* pass */
			(void) snprintf(buf, bufsz,
			    gettext("pass %s? "), name);
		}

		if ((ans = calloc(ansz, sizeof (char))) == NULL) {
			fatal(gettext("out of memory"));
		}
		if (nextask(buf, &ans, &ansz) == -1)
			exit(1);
		if (strlen(ans) == 0 || isyesno(ans, 1) != 1) {
			free(buf);
			free(ans);
			return (1);
		}
		free(buf);
		free(ans);
	}
	return (0);
}

/*
 * get_newname - prompt the user for a new filename
 *
 * DESCRIPTION
 *
 *	The user is prompted with the name of the file which is currently
 *	being processed.  The user may choose to rename the file by
 *	entering the new file name after the prompt; the user may press
 *	carriage-return/newline, which will skip the file or the user may
 *	type an 'EOF' character, which will cause the program to stop.
 * 	The user can enter a single period ('.') and the name of the
 * 	file will remain unchanged.
 *
 * PARAMETERS
 *
 *	char	**name		- filename, possibly modified by user
 *	size_t	*size		- buffer size of allowable new filename
 *	Stat	*asb		- Stat block for the file
 *
 * RETURNS
 *
 *	Returns 0 if successfull, or -1 if an error occurred.
 *
 */


int
get_newname(char **name, size_t *size, Stat *asb)
{
	char		*buf;
	char		*newname;
	static char	*lastnewname = NULL;	/* Last rename name */
	static char	*lastnewattr = NULL;	/* Last rename attr name */
	size_t		nsz;
	size_t		bufsize;
	size_t		newnamesize = *size;

	if (f_interactive || rename_interact || (lastnewname != NULL) ||
	    (lastnewattr != NULL)) {
		/*
		 * Determine the size of the buffer needed to contain the
		 * question asking the user if the file should be renamed.
		 * For a file that is not an attribute, the buffer will
		 * contain:
		 * 	"Rename " + <file name> + "? " + NULL
		 * and for a file that is an attribute:
		 * 	"Rename " + <base file name> + " Attribute " +
		 *	    <attribute file name> + "? " + NULL
		 */
		if (asb->xattr_info.xattraname == NULL) {
			/* Clear save file and attribute name info. */
			if (lastnewname != NULL) {
				(void) free(lastnewname);
				lastnewname = NULL;
			}

			/*
			 * A new base file name is being processed.  Clear
			 * out saved attribute info.
			 */
			if (lastnewattr != NULL) {
				(void) free(lastnewattr);
				lastnewattr = NULL;
			}

			/*
			 * If rename_interact was previously set and the
			 * file was renamed at that time, but isn't set now,
			 * clear just return.
			 */
			if (!f_interactive && !rename_interact) {
				return (0);
			}

			/* Add 10 for "Rename ", "? ", and NULL */
			bufsize = *size + 10;
		} else {
			/*
			 * If we're processing an attribute or system
			 * attribute, ensure we update the attribute's base
			 * file name info and/or the system attribute's
			 * attribute name with saved info.  Note:
			 * if the saved info is the '\0' character (the
			 * file this attribute or system attribute is
			 * associated with was skipped), then don't restore
			 * the saved info.
			 */
			if (lastnewname != NULL) {
				if (strlen(lastnewname) != 0) {
					(void) free(asb->xattr_info.xattrfname);
					(void) free(*name);
					STRDUP(asb->xattr_info.xattrfname,
					    lastnewname);
					STRDUP(*name, lastnewname);
				}
			}

			/*
			 * If processing a new attribute that is not
			 * anything under the attribute directory of an
			 * an attribute, then clear the saved attribute name,
			 * otherwise, update the attribute's parent name
			 * as well as the full path to the attribute from the
			 * base parent file with the saved name of the
			 * attribute.
			 */
			if (lastnewattr != NULL) {
				if (asb->xattr_info.xattraparent == NULL) {
					(void) free(lastnewattr);
					lastnewattr = NULL;
				} else if (strlen(lastnewattr) != 0) {
					char **tptr =
					    &(asb->xattr_info.xattraparent);
					size_t	apathsz = strlen(
					    lastnewattr) + 2 +
					    strlen(asb->xattr_info.xattraname);

					/* update the attribute's parent */
					if (*tptr != NULL) {
						(void) free(*tptr);
						STRDUP(*tptr, lastnewattr);
					}

					/* update the atttribute's path */
					(void) free(asb->xattr_info.xattrapath);
					if ((asb->xattr_info.xattrapath =
					    malloc(apathsz)) == NULL) {
						fatal("out of memory");
					}
					(void) snprintf(
					    asb->xattr_info.xattrapath,
					    apathsz, "%s%s%s",
					    (lastnewattr == NULL) ?
					    "" : lastnewattr,
					    (lastnewattr == NULL) ? "" :
					    "/", asb->xattr_info.xattraname);
				}
			}

			/*
			 * If the file being processed is a read-write
			 * system attribute or a hidden attribute
			 * directory ("."), then just return as we
			 * don't allow users to rename these files;
			 * they follow the files they are associated
			 * with.
			 */
			if (asb->xattr_info.xattr_rw_sysattr ||
			    Hiddendir) {
				/*
				 * File this attribute or system
				 * attribute was associated with was
				 * skipped, so skip this one also.
				 */
				if (((asb->xattr_info.xattraparent
				    == NULL) && (lastnewname != NULL) &&
				    (*lastnewname == '\0')) ||
				    ((asb->xattr_info.xattraparent
				    != NULL) && (lastnewattr != NULL) &&
				    (*lastnewattr == '\0'))) {
					return (1);
				}
				return (0);
			}

			/*
			 * Add 21 for "Rename ", " Attribute ", "? ",
			 * and NULL
			 */
			bufsize = strlen(*name) + 21 +
			    strlen(asb->xattr_info.xattraname);
		}
		if ((buf = calloc(bufsize, sizeof (char))) == NULL) {
			fatal(gettext("out of memory"));
		}
		if ((newname = calloc(*size, sizeof (char))) == NULL) {
			fatal(gettext("out of memory"));
		}

		(void) snprintf(buf, bufsize,
		    gettext("rename %s%s%s? "),
		    (asb->xattr_info.xattraname == NULL) ? *name :
		    asb->xattr_info.xattrfname,
		    (asb->xattr_info.xattraname == NULL) ? "" :
		    gettext(" Attribute "),
		    (asb->xattr_info.xattraname == NULL) ? "" :
		    asb->xattr_info.xattraname);

		if (nextask(buf, &newname, &newnamesize) == -1) {
			exit(1);	/* EOF */
		}
		free(buf);

		if (rename_interact) {
			rename_interact = 0;
		}

		if ((nsz = strlen(newname)) == 0) {
			if (asb->xattr_info.xattraname == NULL) {
				STRDUP(lastnewname, newname);
			} else {
				STRDUP(lastnewattr, newname);
			}
			return (1);
		}
		nsz++;


		/*
		 * If "." was entered, leave the name the same.
		 * otherwise update the file's info.
		 */
		if (!((newname[0] == '.') && (newname[1] == '\0'))) {
			/*
			 * Only update lastnewname if we are NOT dealing
			 * with an extended attribute.
			 */
			if (asb->xattr_info.xattraname == NULL) {
				(void) free(*name);
				*size = nsz;
				STRDUP(lastnewname, newname);
				STRDUP(*name, newname);
			} else {
				(void) free(asb->xattr_info.xattraname);
				(void) free(asb->xattr_info.xattrapath);
				STRDUP(asb->xattr_info.xattraname, newname);
				STRDUP(asb->xattr_info.xattrapath, newname);
				STRDUP(lastnewattr, newname);
			}
		}
		(void) free(newname);
	}

	return (0);
}


/*
 * mem_rpl_name - possibly replace a name with a regular expression
 *
 * DESCRIPTION
 *
 *	The string name is searched for in the list of regular expression
 *	substituions.  If the string matches any of the regular expressions
 *	then a new buffer is returned with the new string.
 *
 * PARAMETERS
 *
 *	char	*name	- name to search for and possibly modify
 *
 * RETURNS
 * 	an allocated buffer with the new name in it
 */

char *
mem_rpl_name(char *name)
{
	size_t	nbufsz;
	char	*namebuf;

	if (name == NULL) {
		return (NULL);
	}

	if (rplhead == (Replstr *)NULL)
		return (mem_str(name));

	nbufsz = strlen(name) + 1;

	if ((namebuf = calloc(nbufsz, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}
	if (strlcpy(namebuf, name, nbufsz) > nbufsz) {
		fatal(gettext("buffer overflow"));
	}
	rpl_name(&namebuf, &nbufsz);	/* writes new string in namebuf */
	return (namebuf);
}
