/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"


#include <fnmatch.h>
#include <unistd.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libnvpair.h>
#include "pax.h"

/* Function Prototypes */
int		is_opt_match(nvlist_t *list, const char *key, const char *val);
int		parseopt(const char *opt);
static int	process_res_opt(const char *key, const char *val);
static int	store_nvpair(nvlist_t **list, const char *key, const char *val);

#define	RESERVEDOPTION(k)	((STREQUAL(k, "delete")) || \
				    (STREQUAL(k, "exthdr.name")) || \
				    (STREQUAL(k, "globexthdr.name")) || \
				    (STREQUAL(k, "invalid")) || \
				    (STREQUAL(k, "linkdata")) || \
				    (STREQUAL(k, "listopt")) || \
				    (STREQUAL(k, "times")))

/*
 * process_res_opt()
 *
 * Processes reserved keywords for the -o options, including
 *	delete
 *	exthdr.name
 *	globexthdr.name
 *	invalid
 *	linkdata
 *	listopt
 *	times
 *
 * Input
 *	key	-o options keyword
 *	val	-o options value from keyword=value pair
 */
static int
process_res_opt(const char *key, const char *val)
{

	if (key != NULL) {
		/*
		 * With the exception of "listopt", all reserved
		 * options are applicable only to the -x pax format.
		 * The other exception to this rule would apply to
		 * the -x xustar format as we allow -o times to be
		 * specified.
		 */
		if (!(STREQUAL(key, "listopt")) && !f_stdpax) {
			if (!(STREQUAL(key, "times") && f_pax)) {
				char *s;
				STRDUP(s, key);
				warn(s, gettext(
				    "Option applicable only to the "
				    "-x pax and -x ustar formats"));
				return (-1);
			}
		}
		if (STREQUAL(key, "delete")) {
			/*
			 * If multiple -o delete=value were specified, then
			 * it's additive; add all specified string patterns
			 * to the deleteopt list.
			 */
			if (*val == '\0') {
				if (deleteopt != NULL) {
					(void) nvlist_remove_all(deleteopt,
					    key);
					/*
					 * It's the only key kept in list
					 * so free it.
					 */
					nvlist_free(deleteopt);
					deleteopt = NULL;
				}

			} else if (deleteopt == NULL) {
				if (nvlist_alloc(&deleteopt, 0, 0) != 0) {
					diag(gettext(
					    "Unable to allocate "
					    "delete list buffer: %s\n"),
					    strerror(errno));
						return (-1);
				}
			}

			/* store the delete=<value> pair */
			if ((*val != '\0') &&
			    (nvlist_add_string(deleteopt, key,
			    val) != 0)) {
				fatal(gettext(
				    "Unable to maintain delete list"));
			}
		} else if (STREQUAL(key, "exthdr.name")) {
			if (exthdrnameopt != NULL) {
				free(exthdrnameopt);
			}

			if (*val != '\0') {
				STRDUP(exthdrnameopt, val);
			} else {	/* ignore attribute */
				exthdrnameopt = NULL;
			}
		} else if (STREQUAL(key, "globexthdr.name")) {
			if (gexthdrnameopt != NULL) {
				free(gexthdrnameopt);
			}

			if (*val != '\0') {
				STRDUP(gexthdrnameopt, val);
			} else {	/* ignore attribute */
				gexthdrnameopt = NULL;
			}
		} else if (STREQUAL(key, "invalid")) {
			if (*val != '\0') {
				/* check for valid actions */
				if (STREQUAL(val, "rename")) {
					invalidopt = INV_RENAME;
				} else if (STREQUAL(val, "UTF-8")) {
					invalidopt = INV_UTF8;
				} else if (STREQUAL(val, "write")) {
					invalidopt = INV_WRITE;
				} else if (STREQUAL(val, "bypass")) {
					invalidopt = INV_BYPASS;
				} else {
					char *s;
					STRDUP(s, val);
					warn(s, gettext(
					    "Invalid action for "
					    "-o invalid, "
					    "using bypass action"));
					invalidopt = INV_BYPASS;
				}
			} else {
				invalidopt = INV_BYPASS;
			}
		} else if (STREQUAL(key, "linkdata")) {
			f_linkdata = 1;
		} else if (STREQUAL(key, "listopt")) {
			if (*val != '\0') {
				size_t dstsize = strlen(val) + 1;
				if (listopt == NULL) {
					if ((listopt = calloc(1,
					    dstsize * sizeof (char)))
					    == NULL) {
						fatal(gettext(
						    "Unable to allocate "
						    "listopt buffer"));
					}
				} else {
					dstsize += strlen(listopt);
					if ((listopt = realloc(listopt,
					    dstsize * sizeof (char)))
					    == NULL) {
						fatal(gettext(
						    "Unable to allocate "
						    "listopt buffer"));
					}
				}
				if (strlcat(listopt, val,
				    dstsize) >= dstsize) {
					fatal(gettext("buffer overrun"));
				}
			} else {	/* ignore attribute */
				if (listopt != NULL) {
					free(listopt);
				}
				listopt = NULL;
			}
		} else if (STREQUAL(key, "times")) {
			f_times = 1;
		}
	}
	return (0);
}


/*
 * is_opt_match()
 *
 * Walk the option list trying to match the key=value pair provided.
 *
 * Input
 * 	list	- list to walk
 *	key	- key to match
 *	val	- value to match
 *
 * Output
 *	PAX_OK		- success, key=value pair matched
 *	PAX_NOT_FOUND	- key=value pair not found in list
 */
int
is_opt_match(nvlist_t *list, const char *key, const char *val)
{
	char *vptr = NULL;
	char *name = NULL;
	nvpair_t *curr = NULL;
	int status = PAX_NOT_FOUND;

	if ((list == NULL) || (key == NULL) || (val == NULL)) {
		return (PAX_NOT_FOUND);
	}

	while (curr = nvlist_next_nvpair(list, curr)) {

		/* match the key */
		name = nvpair_name(curr);
		if (strcmp(key, name) == 0) {

			/* match the value */
			(void) nvpair_value_string(curr, &vptr);
			if (fnmatch(vptr, val, 0) == 0) {
				status = PAX_OK;
				break;
			}
		}
	}

	return (status);
}

/*
 * store_nvpair()
 *
 * Store a key=value pair in 'list'.  Note:  If the value is
 * NULL, then the list should be removed.
 *
 * Input
 *	list	- list to add the key=value pair to
 *	key	- key from key=value pair
 *	val	- value from key=value pair
 *
 * Output
 *	0 on success, otherwise non-zero.
 */
static int
store_nvpair(nvlist_t **list, const char *key, const char *val)
{
	int	err;

	if ((list == NULL) || (key == NULL)) {
		return (-1);
	}

	if (*val == '\0') {
		if (*list != NULL) {
			nvpair_t *nvp = NULL;
			(void) nvlist_remove_all(*list, key);

			/*
			 * Free the list if the last key=value pair
			 * was removed from the list.
			 */
			if (nvlist_next_nvpair(*list, nvp) == NULL) {
				nvlist_free(*list);
				*list = NULL;
			}
		}

	} else if (*list == NULL) {
		if ((err = nvlist_alloc(list, NV_UNIQUE_NAME, 0)) != 0) {
			return (err);
		}
	}

	/* store the key=value pair */
	if ((*val != '\0') && ((err = nvlist_add_string(*list, key,
	    val)) != 0)) {
		return (err);
	}

	return (0);
}

/*
 * parseopt - parse the "-o option" on the command line for key=value pairs.
 *
 * The option arg can consist of one or more comma-separated
 * keywords of the form
 *      keyword[[:]=value][,keyword[[:]=value], ...]
 * Note: a comma can be contained in value if it is preceded by
 * a backslash.
 *
 * The keys of the key=value pairs specified are stored in a linked list, with
 * each key containing a list of values.
 *
 * list ----> |--------|
 *            |  key   |
 *            |--------|      |--------|
 *            | values |----> | value  |
 *            |--------|      |--------|
 *                            | values | ----> . . .
 *                            |--------|
 *
 * Currently, keys may contain multiple distinct values.
 *
 * If a duplicate key is entered, with the exception of the 'delete' key,
 * and there is a conflict, then the later key will be used, and the values
 * associated with the previous key will be deleted.  If multiple
 * -o delete=value were specified, then it's additive; all values will be
 * stored.
 */
int
parseopt(const char *opt)
{
	char		*tmpopt;
	char		*key;	/* key=value pairs from cmd line */
	char		*val;
	int		done = 0;
	int		err;
	int		i = 0;
	int		cnt = 0;

	STRDUP(tmpopt, opt);
	if ((key = calloc(1, strlen(opt) + 1)) == NULL) {
		fatal(strerror(errno));
	}
	if ((val = calloc(1, strlen(opt) + 1)) == NULL) {
		fatal(strerror(errno));
	}

	/* Process each key=value pair */
	while (!done) {

		/* skip leading spaces */
		while ((tmpopt[i] != '\0') && isspace(tmpopt[i])) {
			i++;
		}

		/* Get the keyword */
		while (!done) {
			/*
			 * We're done getting the keyword if we reached the
			 * end of the options string (-o keyword), or we've
			 * reached comma (-o keyword,), or we've reached
			 * an equal sign (-o keyword=).
			 */
			switch (tmpopt[i]) {
			case '\0':	/* -o keyword */
				done = 1;
				break;
			case ',':	/* -o keyword, */
				key[cnt++] = tmpopt[i++];
				done = 1;
				break;
			case '=':	/* -o keyword= */
				i++;
				done = 1;
				break;
			default:
				key[cnt++] = tmpopt[i++];
			}
		}
		key[cnt] = '\0';

		/* Get the value */
		done = 0;
		if ((cnt > 0) && (key[cnt - 1] == ',')) {
			key[cnt - 1] = '\0';
			cnt = 0;
		} else if (STREQUAL(key, "listopt")) {
			/*
			 * The keyword "listopt" is the only
			 * or final key=value pair in
			 * a -o option-argument.  All characters
			 * in the remainder of the option-argument
			 * are considered part of the format string
			 * and will be the value of the key=value
			 * pair.
			 */
			cnt = 0;
			while (tmpopt[i] != '\0') {
				val[cnt++] = tmpopt[i++];
			}
		} else {
			cnt = 0;
			while (!done) {
				switch (tmpopt[i]) {
				case '\0':
					done = 1;
					break;
				case ',':	/* -o keyword=value, */
					i++;	/* skip comma separator */
					done = 1;
					break;
				case '\\':	/* literal comma in value */
					if ((tmpopt[i + 1] != '\0') &&
					    (tmpopt[i + 1] == ',')) {
						val[cnt++] = ',';
						i++;	/* move past '\\' */
						i++;	/* move past ',' */
					} else {
						val[cnt++] = tmpopt[i++];
					}
					break;
				default:
					val[cnt++] = tmpopt[i++];
				}
			}
		}
		val[cnt] = '\0';

		/* Now we need to process the key=value pairs */
		if (key[0] != '\0') {

			/*
			 * If this key is one of the reserved option keys,
			 * then set appropriate flags related to that key.
			 */
			if (RESERVEDOPTION(key)) {
				if (process_res_opt(key, val) != 0) {
					free(tmpopt);
					free(key);
					free(val);
					return (-1);
				}
				if ((gexthdrnameopt != NULL) &&
				    (goptlist == NULL)) {
					if ((err = nvlist_alloc(&goptlist,
					    NV_UNIQUE_NAME, 0)) != 0) {
						free(tmpopt);
						free(key);
						free(val);
						return (err);
					}
				}
				if ((exthdrnameopt != NULL) &&
				    (xoptlist == NULL)) {
					if ((err = nvlist_alloc(&xoptlist,
					    NV_UNIQUE_NAME, 0)) != 0) {
						free(tmpopt);
						free(key);
						free(val);
						return (err);
					}
				}
			} else {

				/*
				 * If the key:=value form was used, we need
				 * to generate a typeflag 'x' extended header.
				 */
				if (key[strlen(key) - 1] == ':') {
					key[strlen(key) - 1] = '\0';

					if ((err = store_nvpair(&xoptlist, key,
					    val)) != 0) {
						free(tmpopt);
						free(key);
						free(val);
						return (err);
					}
				/*
				 * If the key=value form was used, we need
				 * to generate a typeflag 'g' extended header.
				 */
				} else {
					if ((err = store_nvpair(&goptlist, key,
					    val)) != 0) {
						free(tmpopt);
						free(key);
						free(val);
						return (err);
					}
				}
			}
		}
		cnt = 0;
		key[0] = '\0';
		val[0] = '\0';
		/*
		 * If we've reached the end of the options line,
		 * we're finished processing key=value pairs.
		 */
		if (tmpopt[i] == '\0') {
			done = 1;
		} else {
			done = 0;
		}
	}
	free(tmpopt);
	free(key);
	free(val);
	return (0);
}
