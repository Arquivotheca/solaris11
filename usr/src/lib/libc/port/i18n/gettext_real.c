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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "lint.h"
#include "mtlib.h"
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <libintl.h>
#include <thread.h>
#include <synch.h>
#include <limits.h>
#include <unistd.h>
#include "libc.h"
#include "_loc_path.h"
#include "msgfmt.h"
#include "gettext.h"
#include "nlspath_checks.h"

#define	GETTEXT_ALTERNATIVE_LOCALES_NOT_CALLED		(-9)

static int	process_nlspath(const char *, const char *,
    const char *, Nlstmp **, char **, int *, int *);
static char	*replace_nls_option(char *, const char *, char *,
    char *, char *, char *, char *, int *, size_t *);

char *
_real_gettext_u(const char *domain, const char *msgid1, const char *msgid2,
    unsigned long int ln, int category, int plural)
{
	char	msgfile[MAXPATHLEN]; 	/* 1024 */
	char	mydomain[TEXTDOMAINMAX + 1]; /* 256 + 1 */
	Nlstmp	*cur_binding;	/* points to current binding in list */
	char	*cur_locale, *cur_domain, *result, *nlspath;
	char	*msgloc, *cur_domain_binding;
	char	*language;
	unsigned int	n = (unsigned int)ln;	/* we don't need long for n */
	uint32_t	cur_domain_len;
	uint32_t	hash_domain;
	struct msg_pack	*mp, omp;
	char	*canonical;
	int	start;
	int	end;

#ifdef GETTEXT_DEBUG
	gprintf(0, "*************** _real_gettext_u(\"%s\", \"%s\", "
	    "\"%s\", %d, %d, %d)\n",
	    domain ? domain : "NULL", msgid1 ? msgid1 : "NULL",
	    msgid2 ? msgid2 : "NULL", n, category, plural);
	gprintf(0, "***************** global_gt: 0x%p\n", global_gt);
	printgt(global_gt, 1);
#endif

	if (msgid1 == NULL)
		return (NULL);

	mp = memset(&omp, 0, sizeof (omp));	/* msg pack */

	/*
	 * category may be LC_MESSAGES or LC_TIME
	 * locale contains the value of 'category'
	 */
	cur_locale = setlocale(category, NULL);

	language = getenv("LANGUAGE"); /* for GNU */
	if (language) {
		if (!*language || strchr(language, '/') != NULL) {
			/*
			 * LANGUAGE is an empty string or
			 * LANGUAGE contains '/'.
			 * Ignore it.
			 */
			language = NULL;
		}
	}

	/*
	 * Query the current domain if domain argument is NULL pointer
	 */
	mydomain[0] = '\0';
	if (domain == NULL) {
		/*
		 * if NULL is specified for domainname,
		 * use the currently bound domain.
		 */
		cur_domain = _textdomain_u(NULL, mydomain);
	} else if (!*domain) {
		/*
		 * if an empty string is specified
		 */
		cur_domain = DEFAULT_DOMAIN;
	} else {
		cur_domain = (char *)domain;
	}

	hash_domain = get_hashid(cur_domain, &cur_domain_len);
	if (cur_domain_len > TEXTDOMAINMAX) {
		/* domain is invalid, return msg_id */
		DFLTMSG(result, msgid1, msgid2, n, plural);
		return (result);
	}

	start = GETTEXT_ALTERNATIVE_LOCALES_NOT_CALLED;

	nlspath = getenv("NLSPATH"); /* get the content of NLSPATH */
	if (nlspath == NULL || !*nlspath) {
		/* no NLSPATH is defined in the environ */
		if ((*cur_locale == 'C') && (*(cur_locale + 1) == '\0')) {
			/*
			 * If C locale,
			 * return the original msgid immediately.
			 */
			DFLTMSG(result, msgid1, msgid2, n, plural);
			return (result);
		}
		nlspath = NULL;
	} else {
		/* NLSPATH is set */
		int	ret;

		msgloc = setlocale(LC_MESSAGES, NULL);

		ret = process_nlspath(cur_domain, msgloc,
		    (const char *)nlspath, &cur_binding, &canonical,
		    &start, &end);
		if (ret == -1) {
			/* error occurred */
			DFLTMSG(result, msgid1, msgid2, n, plural);
			return (result);
		} else if (ret == 0) {
			nlspath = NULL;
		}
	}

	cur_domain_binding = _real_bindtextdomain_u(cur_domain,
	    NULL, TP_BINDING);
	if (cur_domain_binding == NULL) {
		DFLTMSG(result, msgid1, msgid2, n, plural);
		return (result);
	}

	mp->msgid1 = msgid1;
	mp->msgid2 = msgid2;
	mp->domain = cur_domain;
	mp->binding = cur_domain_binding;
	mp->locale = cur_locale;
	mp->language = language;
	mp->domain_len = cur_domain_len;
	mp->n = n;
	mp->category = category;
	mp->plural = plural;
	mp->hash_domain = hash_domain;

	/*
	 * Spec1170 requires that we use NLSPATH if it's defined, to
	 * override any system default variables.  If NLSPATH is not
	 * defined or if a message catalog is not found in any of the
	 * components (bindings) specified by NLSPATH, dcgettext_u() will
	 * search for the message catalog in either a) the binding path set
	 * by any previous application calls to bindtextdomain() or
	 * b) the default binding path (/usr/lib/locale).  Save the original
	 * binding path so that we can search it if the message catalog
	 * is not found via NLSPATH.  The original binding is restored before
	 * returning from this routine because the gettext routines should
	 * not change the binding set by the application.  This allows
	 * bindtextdomain() to be called once for all gettext() calls in the
	 * application.
	 */

	/*
	 * First, examine NLSPATH
	 */
	if (nlspath) {
		/*
		 * NLSPATH binding has been successfully built
		 */
#ifdef GETTEXT_DEBUG
		gprintf(0, "************************** examining NLSPATH\n");
		gprintf(0, "       cur_binding: \"0x%p\"\n",
		    (void *)cur_binding);
#endif

		mp->nlsp = 1;

		while (cur_binding != NULL) {
			if (cur_binding->len > MAXPATHLEN) {
				DFLTMSG(result, msgid1, msgid2, n, plural);
				return (result);
			}

			mp->msgfile = cur_binding->pathname;

			result = handle_mo(mp);
			if (result != NULL)
				return (result);

			cur_binding = cur_binding->next;
		}
	}

	mp->msgfile = msgfile;
	mp->nlsp = 0;
	mp->binding = cur_domain_binding;
	/*
	 * Next, examine LANGUAGE
	 */
	if (language) {
		char	*ret_msg;
		ret_msg = handle_lang(mp);
		if (ret_msg != NULL) {
			/* valid msg found in GNU MO */
			return (ret_msg);
		}
		/*
		 * handle_lang() may have overridden locale
		 */
		mp->locale = cur_locale;
		mp->status = 0;
	}

	/*
	 * Handle a single binding.
	 */
#ifdef GETTEXT_DEBUG
	*mp->msgfile = '\0';
#endif
	if (mk_msgfile(mp) == NULL) {
		DFLTMSG(result, msgid1, msgid2, n, plural);
		return (result);
	}

	result = handle_mo(mp);
	if (result) {
		return (result);
	}

	/*
	 * Lastly, check with obsoleted and canonical locale names.
	 */
	if (start == GETTEXT_ALTERNATIVE_LOCALES_NOT_CALLED ||
	    category != LC_MESSAGES)
		alternative_locales(cur_locale, &canonical, &start, &end, 1);

	if (start != -1) {
		for (; start <= end; start++) {
			mp->locale = (char *)__lc_obs_msg_lc_list[start];

			if (mk_msgfile(mp) == NULL) {
				DFLTMSG(result, msgid1, msgid2, n, plural);
				return (result);
			}

			result = handle_mo(mp);
			if (result != NULL)
				return (result);
		}
	}

	if (canonical != NULL) {
		mp->locale = canonical;

		if (mk_msgfile(mp) == NULL) {
			DFLTMSG(result, msgid1, msgid2, n, plural);
			return (result);
		}

		result = handle_mo(mp);
		if (result != NULL)
			return (result);
	}

	DFLTMSG(result, msgid1, msgid2, n, plural);
	return (result);
} /* _real_gettext_u */

static void
free_Nlstmp(Nlstmp *nlstmp)
{
	Nlstmp *tp;

	while (nlstmp) {
		tp = nlstmp;
		nlstmp = nlstmp->next;
		if (tp->pathname)
			free(tp->pathname);
		free(tp);
	}
}

/*
 * Populate templates as a linked list, nt, based on NLSPATH, locale, and
 * domain values.
 */
static int
populate_templates(Nlstmp **nt, Nlstmp **tail, const char *domain,
    size_t domain_len, const char *loc, const char *nlspath, int *ltc_defined)
{
	char *l;
	char *t;
	char *c;
	char *s;
	char path[MAXPATHLEN];
	Nlstmp *tnt;
	size_t len;

	l = s = strdup(loc);
	if (l == NULL)
		return (0);

	t = c = NULL;

	/*
	 * Collect language, territory, and codeset from the locale name
	 * based on the following locale naming convention:
	 *
	 *	language[_territory][.codeset][@modifier]
	 */
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

	/*
	 * Process NLSPATH and populate templates.
	 *
	 * The domain_len and the len used at below includes '\0'.
	 */

	s = (char *)nlspath;
	while (*s) {
		if (*s == ':') {
			if (domain_len <= 1) {
				s++;
				continue;
			}

			tnt = malloc(sizeof (Nlstmp));
			if (tnt == NULL) {
				free(l);
				free_Nlstmp(*nt);
				return (0);
			}

			tnt->pathname = malloc(domain_len);
			if (tnt->pathname == NULL) {
				free(l);
				free(tnt);
				free_Nlstmp(*nt);
				return (0);
			}

			(void) memcpy(tnt->pathname, domain, domain_len);
			tnt->len = domain_len;
			tnt->next = NULL;

			if (*tail == NULL) {
				*nt = *tail = tnt;
			} else {
				(*tail)->next = tnt;
				*tail = tnt;
			}

			s++;
			continue;
		}

		s = replace_nls_option(s, domain, path, (char *)loc,
		    l, t, c, ltc_defined, &len);
		if (s == NULL) {
			free(l);
			free_Nlstmp(*nt);
			return (0);
		}

		if (*path != '\0') {
			tnt = malloc(sizeof (Nlstmp));
			if (tnt == NULL) {
				free(l);
				free_Nlstmp(*nt);
				return (0);
			}

			tnt->pathname = malloc(len);
			if (tnt->pathname == NULL) {
				free(l);
				free(tnt);
				free_Nlstmp(*nt);
				return (0);
			}

			(void) memcpy(tnt->pathname, path, len);
			tnt->len = len;
			tnt->next = NULL;

			if (*tail == NULL) {
				*nt = *tail = tnt;
			} else {
				(*tail)->next = tnt;
				*tail = tnt;
			}
		}

		if (*s != '\0')
			s++;
	}

	free(l);

	return (1);
}

/*
 * process_nlspath(): process the NLSPATH environment variable.
 *
 *		This routine looks at NLSPATH in the environment,
 *		and will try to build up the binding list based
 *		on the settings of NLSPATH. It will not only use
 *		the locale name supplied but also, if available,
 *		obsoleted Solaris locale names based on the supplied
 *		locale name and also canonical locale name in case
 *		the supplied is an alias.
 *
 * RETURN:
 * -1:  Error occurred
 *  0:  No error, but no binding list has been built
 *  1:  No error, and a binding list has been built or found
 *
 */
static int
process_nlspath(const char *domain, const char *locale, const char *nlspath,
    Nlstmp **binding, char **canonical, int *start, int *end)
{
	size_t len;
	Nlstmp *nthead;
	Nlstmp *nttail;
	Nls_node *cur_nls;
	Nls_node *nnp;
	int ltc_defined;
	int i;

#ifdef GETTEXT_DEBUG
	gprintf(0, "*************** process_nlspath(%s, %s, "
	    "%s, 0x%p)\n", domain,
	    locale, nlspath, (void *)binding);
#endif

	cur_nls = global_gt->c_n_node;
	if (cur_nls &&
	    (strcmp(cur_nls->domain, domain) == 0 &&
	    strcmp(cur_nls->locale, locale) == 0 &&
	    strcmp(cur_nls->nlspath, nlspath) == 0)) {
		*binding = cur_nls->ppaths;
		return (1);
	}

	nnp = global_gt->n_node;
	while (nnp) {
		if (strcmp(nnp->domain, domain) == 0 &&
		    strcmp(nnp->locale, locale) == 0 &&
		    strcmp(nnp->nlspath, nlspath) == 0) {
			/* found */
			global_gt->c_n_node = nnp;
			*binding = nnp->ppaths;
			return (1);
		}
		nnp = nnp->next;
	}
	/* not found */

	nthead = NULL;
	nttail = NULL;
	ltc_defined = 0;
	len = strlen(domain) + 1;

	if (populate_templates(&nthead, &nttail, domain, len, locale,
	    nlspath, &ltc_defined) == 0)
		return (-1);

	if (ltc_defined != 0) {
		alternative_locales((char *)locale, canonical, start, end, 1);

		if (*start != -1) {
			for (i = *start; i <= *end; i++) {
				if (populate_templates(&nthead, &nttail,
				    domain, len, __lc_obs_msg_lc_list[i],
				    nlspath, &ltc_defined) == 0)
					return (-1);
			}
		}

		if (*canonical != NULL) {
			if (populate_templates(&nthead, &nttail, domain, len,
			    *canonical, nlspath, &ltc_defined) == 0)
				return (-1);
		}
	}

	if (nthead == NULL)
		return (0);

	nnp = malloc(sizeof (Nls_node));
	if (nnp == NULL) {
		free_Nlstmp(nthead);
		return (-1);
	}

	nnp->domain = malloc(len);
	if (nnp->domain == NULL) {
		free_Nlstmp(nthead);
		free(nnp);
		return (-1);
	} else {
		(void) memcpy(nnp->domain, domain, len);
	}

	len = strlen(locale) + 1;
	nnp->locale = malloc(len);
	if (nnp->locale == NULL) {
		free_Nlstmp(nthead);
		free(nnp->domain);
		free(nnp);
		return (-1);
	} else {
		(void) memcpy(nnp->locale, locale, len);
	}

	len = strlen(nlspath) + 1;
	nnp->nlspath = malloc(len);
	if (nnp->nlspath == NULL) {
		free_Nlstmp(nthead);
		free(nnp->domain);
		free(nnp->locale);
		free(nnp);
		return (-1);
	} else {
		(void) memcpy(nnp->nlspath, nlspath, len);
	}
	nnp->ppaths = nthead;

	nnp->next = global_gt->n_node;
	global_gt->n_node = nnp;
	global_gt->c_n_node = nnp;

	*binding = nthead;
	return (1);
}


/*
 * This routine will replace substitution parameters in NLSPATH
 * with appropiate values.
 */
static char *
replace_nls_option(char *s, const char *name, char *pathname, char *locale,
    char *lang, char *territory, char *codeset, int *ltc_defined, size_t *len)
{
	char	*t, *u;
	char	*limit;

	t = pathname;
	limit = pathname + MAXPATHLEN - 1;

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
				if (name) {
					u = (char *)name;
					while (*u && (t < limit))
						*t++ = *u++;
				}
			} else if (*s == 'L') {
				if (locale) {
					u = locale;
					while (*u && (t < limit))
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else if (*s == 'l') {
				if (lang) {
					u = lang;
					while (*u && (*u != '_') &&
					    (t < limit))
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else if (*s == 't') {
				if (territory) {
					u = territory;
					while (*u && (*u != '.') &&
					    (t < limit))
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else if (*s == 'c') {
				if (codeset) {
					u = codeset;
					while (*u && (t < limit))
						*t++ = *u++;
				}
				*ltc_defined = 1;
			} else {
				if (t < limit)
					*t++ = *s;
			}
		} else {
			/* too long pathname */
			return (NULL);
		}
		++s;
	}

	*t = '\0';
	*len = t - pathname + 1;

	return (s);
}


char *
_real_bindtextdomain_u(const char *domain, const char *binding,
    int type)
{
	struct domain_binding	*bind, *prev;
	Gettext_t	*gt = global_gt;
	char	**binding_addr;

#ifdef GETTEXT_DEBUG
	gprintf(0, "*************** _real_bindtextdomain_u(\"%s\", "
	    "\"%s\", \"%s\")\n",
	    (domain ? domain : ""),
	    (binding ? binding : ""),
	    (type == TP_BINDING) ? "TP_BINDING" : "TP_CODESET");
#endif

	/*
	 * If domain is a NULL pointer, no change will occur regardless
	 * of binding value. Just return NULL.
	 */
	if (domain == NULL) {
		return (NULL);
	}

	/*
	 * Global Binding is not supported any more.
	 * Just return NULL if domain is NULL string.
	 */
	if (*domain == '\0') {
		return (NULL);
	}

	/* linear search for binding, rebind if found, add if not */
	bind = FIRSTBIND(gt);
	prev = NULL;	/* Two pointers needed for pointer operations */

	while (bind) {
		if (strcmp(domain, bind->domain) == 0) {
			/*
			 * Domain found.
			 */
			binding_addr = (type == TP_BINDING) ? &(bind->binding) :
			    &(bind->codeset);
			if (binding == NULL) {
				/*
				 * if binding is null, then query
				 */
				return (*binding_addr);
			}
			/* replace existing binding with new binding */
			if (*binding_addr) {
				free(*binding_addr);
			}
			if ((*binding_addr = strdup(binding)) == NULL) {
				return (NULL);
			}
#ifdef GETTEXT_DEBUG
			printlist();
#endif
			return (*binding_addr);
		}
		prev = bind;
		bind = bind->next;
	} /* while (bind) */

	/* domain has not been found in the list at this point */
	if (binding) {
		/*
		 * domain is not found, but binding is not NULL.
		 * Then add a new node to the end of linked list.
		 */

		if ((bind = malloc(sizeof (Dbinding))) == NULL) {
			return (NULL);
		}
		if ((bind->domain = strdup(domain)) == NULL) {
			free(bind);
			return (NULL);
		}
		bind->binding = NULL;
		bind->codeset = NULL;
		binding_addr = (type == TP_BINDING) ? &(bind->binding) :
		    &(bind->codeset);
		if ((*binding_addr = strdup(binding)) == NULL) {
			free(bind->domain);
			free(bind);
			return (NULL);
		}
		bind->next = NULL;

		if (prev) {
			/* reached the end of list */
			prev->next = bind;
		} else {
			/* list was empty */
			FIRSTBIND(gt) = bind;
		}

#ifdef GETTEXT_DEBUG
		printlist();
#endif
		return (*binding_addr);
	} else {
		/*
		 * Query of domain which is not found in the list
		 * for bindtextdomain, returns defaultbind
		 * for bind_textdomain_codeset, returns NULL
		 */
		if (type == TP_BINDING) {
			return ((char *)defaultbind);
		} else {
			return (NULL);
		}
	} /* if (binding) */

	/* Must not reach here */

} /* _real_bindtextdomain_u */


char *
_textdomain_u(const char *domain, char *result)
{
	char	*p;
	size_t	domain_len;
	Gettext_t	*gt = global_gt;

#ifdef GETTEXT_DEBUG
	gprintf(0, "*************** _textdomain_u(\"%s\", 0x%p)\n",
	    (domain ? domain : ""), (void *)result);
#endif

	/* Query is performed for NULL domain pointer */
	if (domain == NULL) {
		(void) strcpy(result, CURRENT_DOMAIN(gt));
		return (result);
	}

	/* check for error. */
	/*
	 * domain is limited to TEXTDOMAINMAX bytes
	 * excluding a null termination.
	 */
	domain_len = strlen(domain);
	if (domain_len > TEXTDOMAINMAX) {
		/* too long */
		return (NULL);
	}

	/*
	 * Calling textdomain() with a null domain string sets
	 * the domain to the default domain.
	 * If non-null string is passwd, current domain is changed
	 * to the new domain.
	 */

	/* actually this if clause should be protected from signals */
	if (*domain == '\0') {
		if (CURRENT_DOMAIN(gt) != default_domain) {
			free(CURRENT_DOMAIN(gt));
			CURRENT_DOMAIN(gt) = (char *)default_domain;
		}
	} else {
		p = malloc(domain_len + 1);
		if (p == NULL)
			return (NULL);
		(void) strcpy(p, domain);
		if (CURRENT_DOMAIN(gt) != default_domain)
			free(CURRENT_DOMAIN(gt));
		CURRENT_DOMAIN(gt) = p;
	}

	(void) strcpy(result, CURRENT_DOMAIN(gt));
	return (result);
} /* _textdomain_u */

/*
 * key_2_text() translates msd_id into target string.
 */
static char *
key_2_text(Msg_s_node *messages, const char *key_string)
{
	int	val;
	char	*msg_id_str;
	unsigned char	kc = *(unsigned char *)key_string;
	struct msg_struct	*check_msg_list;

#ifdef GETTEXT_DEBUG
	gprintf(0, "*************** key_2_text(0x%p, \"%s\")\n",
	    (void *)messages, key_string ? key_string : "(null)");
	printsunmsg(messages, 1);
#endif

	check_msg_list = messages->msg_list +
	    messages->msg_file_info->msg_mid;
	for (;;) {
		msg_id_str = messages->msg_ids +
		    check_msg_list->msgid_offset;
		/*
		 * To maintain the compatibility with Zeus mo file,
		 * msg_id's are stored in descending order.
		 * If the ascending order is desired, change "msgfmt.c"
		 * and switch msg_id_str and key_string in the following
		 * strcmp() statement.
		 */
		val = *(unsigned char *)msg_id_str - kc;
		if ((val == 0) &&
		    (val = strcmp(msg_id_str, key_string)) == 0) {
			return (messages->msg_strs
			    + check_msg_list->msgstr_offset);
		} else if (val < 0) {
			if (check_msg_list->less != LEAFINDICATOR) {
				check_msg_list = messages->msg_list +
				    check_msg_list->less;
				continue;
			}
			return ((char *)key_string);
		} else {
			/* val > 0 */
			if (check_msg_list->more != LEAFINDICATOR) {
				check_msg_list = messages->msg_list +
				    check_msg_list->more;
				continue;
			}
			return ((char *)key_string);
		}
	}
}

/*
 * sun_setmsg
 *
 * INPUT
 *   mnp  - message node
 *   addr - address to the mmapped file
 *   size - size of the file
 *
 * RETURN
 *   0   - either T_SUN_MO or T_ILL_MO has been set
 *   1   - not a valid sun mo file
 *  -1   - failed
 */
static int
sun_setmsg(Msg_node *mnp, char *addr, size_t size)
{
	struct msg_info	*sun_header;
	Msg_s_node	*p;
	uint32_t	first_4bytes;
	int	mid, count;
	int	struct_size, struct_size_old;
	int	msg_struct_size;

	if (size < sizeof (struct msg_info)) {
		/* invalid mo file */
		mnp->type = T_ILL_MO;
#ifdef GETTEXT_DEBUG
		gprintf(0, "********* exiting sun_setmsg\n");
		printmnp(mnp, 1);
#endif
		return (0);
	}

	first_4bytes = *((uint32_t *)(uintptr_t)addr);
	if (first_4bytes > INT_MAX) {
		/*
		 * Not a valid sun mo file
		 */
		return (1);
	}

	/* candidate for sun mo */

	sun_header = (struct msg_info *)(uintptr_t)addr;
	mid = sun_header->msg_mid;
	count = sun_header->msg_count;
	msg_struct_size = sun_header->msg_struct_size;
	struct_size_old = (int)(OLD_MSG_STRUCT_SIZE * count);
	struct_size = (int)(MSG_STRUCT_SIZE * count);

	if ((((count - 1) / 2) != mid) ||
	    ((msg_struct_size != struct_size_old) &&
	    (msg_struct_size != struct_size))) {
		/* invalid mo file */
		mnp->type = T_ILL_MO;
#ifdef GETTEXT_DEBUG
		gprintf(0, "********* exiting sun_setmsg\n");
		printmnp(mnp, 1);
#endif
		return (0);
	}
	/* valid sun mo file */

	p = malloc(sizeof (Msg_s_node));
	if (p == NULL) {
		return (-1);
	}

	p->msg_file_info = sun_header;
	p->msg_list = (struct msg_struct *)(uintptr_t)
	    (addr + sizeof (struct msg_info));
	p->msg_ids = (char *)(addr + sizeof (struct msg_info) +
	    struct_size);
	p->msg_strs = (char *)(addr + sizeof (struct msg_info) +
	    struct_size + sun_header->str_count_msgid);

	mnp->msg.sunmsg = p;
	mnp->type = T_SUN_MO;
#ifdef GETTEXT_DEBUG
	gprintf(0, "******** exiting sun_setmsg\n");
	printmnp(mnp, 1);
#endif
	return (0);
}

/*
 * setmsg
 *
 * INPUT
 *   mnp  - message node
 *   addr - address to the mmapped file
 *   size - size of the file
 *
 * RETURN
 *   0   - succeeded
 *  -1   - failed
 */
static int
setmsg(Msg_node *mnp, char *addr, size_t size)
{
	int	ret;
	if ((ret = sun_setmsg(mnp, addr, size)) <= 0)
		return (ret);

	return (gnu_setmsg(mnp, addr, size));
}

static char *
handle_type_mo(Msg_node *mnp, struct msg_pack *mp)
{
	char	*result;

	switch (mnp->type) {
	case T_ILL_MO:
		/* invalid MO */
		return (NULL);
	case T_SUN_MO:
		/* Sun MO found */
		mp->status |= ST_SUN_MO_FOUND;

		if (mp->plural) {
			/*
			 * *ngettext is called against
			 * Sun MO file
			 */
			int	exp = (mp->n == 1);
			result = (char *)mp->msgid1;
			if (!exp)
				result = (char *)mp->msgid2;
			return (result);
		}
		result = key_2_text(mnp->msg.sunmsg, mp->msgid1);
		if (!mnp->trusted) {
			result = check_format(mp->msgid1, result, 0);
		}
		return (result);
	case T_GNU_MO:
		/* GNU MO found */
		mp->status |= ST_GNU_MO_FOUND;

		result = gnu_key_2_text(mnp->msg.gnumsg,
		    get_codeset(mp->domain), mp);

		if (result == mp->msgid1 || result == mp->msgid2) {
			/* no valid msg found */
			return (result);
		}

		/* valid msg found */
		mp->status |= ST_GNU_MSG_FOUND;

		if (!mnp->trusted) {
			result = check_format(mp->msgid1, result, 0);
			if (result == mp->msgid1) {
				DFLTMSG(result, mp->msgid1, mp->msgid2,
				    mp->n, mp->plural);
			}
		}
		return (result);
	default:
		/* this should never happen */
		DFLTMSG(result, mp->msgid1, mp->msgid2, mp->n, mp->plural);
		return (result);
	}
	/* NOTREACHED */
}

/*
 * handle_mo() returns NULL if invalid MO found.
 */
char *
handle_mo(struct msg_pack *mp)
{
	int	fd;
	char	*result;
	struct stat64	statbuf;
	Msg_node	*mnp;
	Gettext_t	*gt = global_gt;

#define	CONNECT_ENTRY	\
	mnp->next = gt->m_node; \
	gt->m_node = mnp; \
	gt->c_m_node = mnp

#ifdef GETTEXT_DEBUG
	gprintf(0, "*************** handle_mo(0x%p)\n", (void *)mp);
	printmp(mp, 1);
#endif

	mnp = check_cache(mp);

	if (mnp != NULL) {
		/* cache found */
		return (handle_type_mo(mnp, mp));
	}

	/*
	 * Valid entry not found in the cache
	 */
	mnp = calloc(1, sizeof (Msg_node));
	if (mnp == NULL) {
		DFLTMSG(result, mp->msgid1, mp->msgid2, mp->n, mp->plural);
		return (result);
	}
	mnp->hashid = mp->hash_domain;
	mnp->path = strdup(mp->msgfile);
	if (mnp->path == NULL) {
		free(mnp);
		DFLTMSG(result, mp->msgid1, mp->msgid2, mp->n, mp->plural);
		return (result);
	}

	fd = nls_safe_open(mp->msgfile, &statbuf, &mp->trusted, !mp->nlsp);
	if ((fd == -1) || (statbuf.st_size > LONG_MAX)) {
		if (fd != -1)
			(void) close(fd);
		mnp->type = T_ILL_MO;
		CONNECT_ENTRY;
		return (NULL);
	}
	mp->fsz = (size_t)statbuf.st_size;
	mp->addr = mmap(NULL, mp->fsz, PROT_READ, MAP_SHARED, fd, 0);
	(void) close(fd);

	if (mp->addr == MAP_FAILED) {
		free(mnp->path);
		free(mnp);
		DFLTMSG(result, mp->msgid1, mp->msgid2, mp->n, mp->plural);
		return (result);
	}

	if (setmsg(mnp, (char *)mp->addr, mp->fsz) == -1) {
		free(mnp->path);
		free(mnp);
		(void) munmap(mp->addr, mp->fsz);
		DFLTMSG(result, mp->msgid1, mp->msgid2, mp->n, mp->plural);
		return (result);
	}
	mnp->trusted = mp->trusted;
	CONNECT_ENTRY;

	return (handle_type_mo(mnp, mp));
}
