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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Interfaces to audit_class(5)  (/etc/security/audit_class)
 */

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <string.h>
#include <synch.h>

/*
 *	Macros to produce a quoted string containing the value of a
 *	preprocessor macro. For example, if SIZE is defined to be 256,
 *	VAL2STR(SIZE) is "256". This is used to construct format
 *	strings for scanf-family functions below.
 */
#define	QUOTE(x)	#x
#define	VAL2STR(x)	QUOTE(x)

static char	au_class_fname[PATH_MAX] = AUDITCLASSFILE;
static FILE	*au_class_file = NULL;
static mutex_t	mutex_classfile = DEFAULTMUTEX;
static mutex_t	mutex_classcache = DEFAULTMUTEX;

#ifdef DEBUG2
void
printclass(au_class_ent_t *p_c)
{
	(void) printf("%llx:%s:%s\n", p_c->ac_class, p_c->ac_name,
	    p_c->ac_desc);
	(void) fflush(stdout);
}
#endif

void
setauclass()
{
	(void) mutex_lock(&mutex_classfile);
	if (au_class_file != NULL) {
		(void) fseek(au_class_file, 0L, 0);
	}
	(void) mutex_unlock(&mutex_classfile);
}


void
endauclass()
{
	(void) mutex_lock(&mutex_classfile);
	if (au_class_file != NULL) {
		(void) fclose(au_class_file);
		au_class_file = NULL;
	}
	(void) mutex_unlock(&mutex_classfile);
}

/*
 * getauclassent():
 *	This is not MT-safe because of the static variables.
 */
au_class_ent_t *
getauclassent()
{
	static au_class_ent_t e;
	static char	cname[AU_CLASS_NAME_MAX];
	static char	cdesc[AU_CLASS_DESC_MAX];

	e.ac_name = cname;
	e.ac_desc = cdesc;

	return (getauclassent_r(&e));
}

/*
 * getauclassent_r
 *	This is MT-safe if each thread passes in its own pointer
 *	to the space where the class entry is returned.  Be careful
 *	to also allocate space from the cname and cdesc pointers
 *	in the au_class_ent structure.
 */
au_class_ent_t *
getauclassent_r(au_class_ent_t *au_class_entry)
{
	int		i;
	int		c_int;
	char		*s, input[AU_CLASS_LINE_MAX+1];
	char		trim_buf[AU_CLASS_NAME_MAX+1];
	au_class_t	v;
	boolean_t	found = B_FALSE;

	/* open audit class file if it isn't already */
	(void) mutex_lock(&mutex_classfile);
	if (au_class_file == NULL) {
		if ((au_class_file = fopen(au_class_fname, "rF")) == NULL) {
			(void) mutex_unlock(&mutex_classfile);
			return (NULL);
		}
	}

	for (;;) {

		i = 0;
		while ((c_int = fgetc(au_class_file)) != EOF && c_int != '\n') {
			if (i < AU_CLASS_LINE_MAX) {
				input[i++] = c_int;
			}
		}
		input[i] = '\0';

		s = input + strspn(input, " \t\r\n");
		if ((*s != '\0') && (*s != '#')) {
			found = B_TRUE;
			s = input;

			/* parse bitfield */
			i = strcspn(s, ":");
			s[i] = '\0';
			if (strncmp(s, "0x", 2) == 0) {
				(void) sscanf(&s[2], "%" SCNx64, &v);
			} else {
				(void) sscanf(s, "%" SCNu64, &v);
			}
			au_class_entry->ac_class = v;
			s = &s[i+1];

			/* parse class name */
			i = strcspn(s, ":");
			s[i] = '\0';
			(void) sscanf(s, "%" VAL2STR(AU_CLASS_NAME_MAX) "s",
			    trim_buf);
			(void) strncpy(au_class_entry->ac_name, trim_buf,
			    AU_CLASS_NAME_MAX);
			s = &s[i+1];

			/* parse class description */
			i = strcspn(s, "\n\0");
			s[i] = '\0';
			(void) strncpy(au_class_entry->ac_desc, s,
			    AU_CLASS_DESC_MAX);

			break;
		}

		if (c_int == EOF) {
			break;
		}
	}

	(void) mutex_unlock(&mutex_classfile);

	return (found ? au_class_entry : NULL);
}


au_class_ent_t *
getauclassnam(char *name)
{
	static au_class_ent_t e;
	static char	cname[AU_CLASS_NAME_MAX];
	static char	cdesc[AU_CLASS_DESC_MAX];

	e.ac_name = cname;
	e.ac_desc = cdesc;

	return (getauclassnam_r(&e, name));
}

au_class_ent_t *
getauclassnam_r(au_class_ent_t *e, char *name)
{
	while (getauclassent_r(e) != NULL) {
		if (strncmp(e->ac_name, name, AU_CLASS_NAME_MAX) == 0) {
			return (e);
		}
	}
	return (NULL);
}


/*
 * xcacheauclass:
 *	Read the entire audit_class file into memory.
 *	Return a pointer to the requested entry in the cache
 *	or a pointer to an invalid entry if the the class
 *	requested is not known.
 *
 *	Return < 0, do not set result pointer, if error.
 *	Return   0, set result pointer to invalid entry, if class not in cache.
 *	Return   1, set result pointer to a valid entry, if class is in cache.
 */
static int
xcacheauclass(au_class_ent_t **result, char *class_name, au_class_t class_no,
    int flags)
{
	static int	invalid;
	static au_class_ent_t **class_tbl;
	static int	called_once;
	static int	lines = 0;

	char		line[AU_CLASS_LINE_MAX+1];
	FILE		*fp;
	au_class_ent_t	*p_class;
	int		i;
	int		c_int;
	int		hit = 0;
	char		*s;

	(void) mutex_lock(&mutex_classcache);
	if (called_once == 0) {

		/* Count number of lines in the class file */
		if ((fp = fopen(au_class_fname, "rF")) == NULL) {
			(void) mutex_unlock(&mutex_classcache);
			return (-1);
		}
		for (;;) {

			i = 0;
			while ((c_int = fgetc(fp)) != EOF && (c_int != '\n')) {
				if (i < AU_CLASS_LINE_MAX) {
					line[i++] = c_int;
				}
			}
			line[i] = '\0';

			s = line + strspn(line, " \t\r\n");
			if ((*s != '\0') && (*s != '#')) {
				lines++;
			}

			if (c_int == EOF) {
				break;
			}
		}
		(void) fclose(fp);
		class_tbl = (au_class_ent_t **)calloc((size_t)lines + 1,
		    sizeof (class_tbl));
		if (class_tbl == NULL) {
			(void) mutex_unlock(&mutex_classcache);
			return (-2);
		}

		lines = 0;
		setauclass();
		/*
		 * This call to getauclassent is protected by
		 * mutex_classcache, so we don't need to use the thread-
		 * safe version (getauclassent_r).
		 */
		while ((p_class = getauclassent()) != NULL) {
			class_tbl[lines] = (au_class_ent_t *)
			    malloc(sizeof (au_class_ent_t));
			if (class_tbl[lines] == NULL) {
				(void) mutex_unlock(&mutex_classcache);
				return (-3);
			}
			class_tbl[lines]->ac_name = strdup(p_class->ac_name);
			class_tbl[lines]->ac_class = p_class->ac_class;
			class_tbl[lines]->ac_desc = strdup(p_class->ac_desc);
#ifdef DEBUG2
			printclass(class_tbl[lines]);
#endif
			lines++;
		}
		endauclass();
		invalid = lines;
		class_tbl[invalid] = (au_class_ent_t *)
		    malloc(sizeof (au_class_ent_t));
		if (class_tbl[invalid] == NULL) {
			(void) mutex_unlock(&mutex_classcache);
			return (-4);
		}
		class_tbl[invalid]->ac_name = "invalid class";
		class_tbl[invalid]->ac_class = AU_MASK_NONE;
		class_tbl[invalid]->ac_desc = class_tbl[invalid]->ac_name;

		called_once = 1;

#ifdef DEBUG2
		for (i = 0; i <= lines; i++) {
			printclass(class_tbl[i]);
		}
#endif

	} /* END if called_once */
	*result = class_tbl[invalid];
	if (flags & AU_CACHE_NAME) {
		for (i = 0; i < lines; i++) {
			if (strncmp(class_name, class_tbl[i]->ac_name,
			    AU_CLASS_NAME_MAX) == 0) {
				*result = class_tbl[i];
				hit = 1;
				break;
			}
		}
	} else if (flags & AU_CACHE_NUMBER) {
		for (i = 0; i < lines; i++) {
			if (class_no == class_tbl[i]->ac_class) {
				*result = class_tbl[i];
				hit = 1;
				break;
			}
		}
	}
	(void) mutex_unlock(&mutex_classcache);
	return (hit);
}

int
cacheauclass(au_class_ent_t **result, au_class_t class_no)
{
	return (xcacheauclass(result, "", class_no, AU_CACHE_NUMBER));
}

int
cacheauclassnam(au_class_ent_t **result, char *class_name)
{
	return (xcacheauclass(result, class_name, (au_class_t)AU_MASK_NONE,
	    AU_CACHE_NAME));
}
