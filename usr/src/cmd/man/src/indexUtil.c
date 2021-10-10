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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <locale.h>

#include "indexUtil.h"

static const char *SECTIONS[] = {
	"ATTRIBUTES", "DESCRIPTION", "EXIT STATUS", "NOTES",
	"NAME", "OPTIONS", "OPERANDS", "OUTPUT", "SEE ALSO",
	"SYNOPSIS", "WARNINGS", "ENVIRONMENT VARIABLES",
	"FILES", "USAGE", "LIST OF COMMANDS", "SECURITY",
	"EXAMPLES", NULL
};

static void print_sections() {
	int i = 0;
	(void) printf(gettext("Available Sections' Name:\n"));
	while (SECTIONS[i]) {
		(void) printf("%s\n", SECTIONS[i]);
		i++;
	}
}

int compare_str(const char *s, const char *d) {
	int lens;
	int lend;

	int res;

	lens = strlen(s);
	lend = strlen(d);

	res = strncmp(s, d, (lens < lend) ? lens : lend);
	if (res == 0)
	{
		if (lens > lend)
			res = 1;
		else if (lens < lend)
			res = -1;
	}
	return (res);
}

static int normalize_cb(char *term, const char *pattern) {
	int len_term = 0;
	int len_pattern = 0;
	int i = 0;

	len_term = strlen(term);
	len_pattern = strlen(pattern);
	if (len_term > len_pattern + 2) {
		for (i = 0; i < len_pattern; i++) {
			if (term[len_term - len_pattern + i] != pattern[i]) {
				return (0);
			}
		}
		term[len_term - len_pattern] = '\0';
	}
	return (0);
}

int normalize(char *term) {
	char *p;
	if (term == NULL) {
		return (0);
	}
	p = term;
	while (*p) {
		*p = tolower(*p);
		p++;
	}

	(void) normalize_cb(term, "ed");
	(void) normalize_cb(term, "es");
	(void) normalize_cb(term, "ing");
	(void) normalize_cb(term, "ly");
	(void) normalize_cb(term, "s");

	(void) replace_str(term, ".", "");
	(void) replace_str(term, ";", "");
	(void) replace_str(term, "?", "");
	(void) replace_str(term, ",", "");
	(void) replace_str(term, ":", "");
	(void) replace_str(term, "%", "");

	(void) replace_str(term, "\\fb", "");
	(void) replace_str(term, "\\fr", "");
	(void) replace_str(term, "\\fp", "");
	(void) replace_str(term, "\\fi", "");
	(void) replace_str(term, "\n", "");
	(void) replace_str(term, "\\", "");
	(void) replace_str(term, "/", "");

	return (0);
}

int replace_str(char *s_str, const char *s_pattern, const char *s_replace) {
	int  str_len;
	char tmp_str[MAXLINESIZE];

	char *find_pos = strstr(s_str, s_pattern);
	if ((!find_pos) || (!s_pattern))
		return (-1);

	while (find_pos) {
		(void) memset(tmp_str, 0, sizeof (tmp_str));
		str_len = find_pos - s_str;
		(void) strncpy(tmp_str, s_str, str_len);
		(void) strlcat(tmp_str, s_replace, MAXLINESIZE);
		(void) strlcat(tmp_str, find_pos + strlen(s_pattern),
		    MAXLINESIZE);
		(void) strlcpy(s_str, tmp_str, MAXLINESIZE);

		find_pos = strstr(s_str, s_pattern);
	}
	return (0);
}

int find_section(const char *sect) {
	int i = 0;
	while (SECTIONS[i]) {
		if (strcasecmp(sect, SECTIONS[i]) == 0) {
			return (i);
		}
		i++;
	}
	return (NOTINSECTION);
}

int free_keyword(Keyword *key) {
	int i;

	if (key == NULL)
		return (0);

	for (i = 0; i < key->size; i++) {
		if (key->word[i] != NULL)
			free(key->word[i]);
	}
	if (key->word != NULL)
		free(key->word);
	if (key != NULL)
		free(key);

	return (0);
}

/*
 * separate words from keywords string.
 * if norm == 0 then
 *    do normalize
 */
int get_words(const char *words, Keyword **key, const char *msc, int norm) {
	char s[MAXQUERYSIZE];
	char w[MAXQUERYSIZE];
	Keyword *k;
	char *pch;
	short int i = 0;

	if ((k = (Keyword *)malloc(sizeof (Keyword))) == NULL) {
		malloc_error();
	}
	k->sid = NOSECTION;
	k->size = 0;
	(void) strlcpy(w, words, MAXQUERYSIZE);

	if (msc != NULL) {
		(void) strlcpy(k->msc, msc, MAXSEC);
	} else {
		k->msc[0] =  '\0';
	}
	/*
	 * separate section name from the keywords string.
	 * i.e., "name:zone"
	 */
	(void) strlcpy(s, words, MAXQUERYSIZE);
	if (strchr(s, ':') != NULL) {
		pch = strtok(s, ":");
		(void) strlcpy(k->sec, pch, MAXSECTIONSIZE);
		if ((k->sid = find_section(k->sec)) == NOTINSECTION) {
			(void) printf(gettext(
			    "\"%s\" is not a supported section name\n"),
			    k->sec);
			print_sections();
			exit(1);
		}
		pch = strtok(NULL, ":");
		if (pch != NULL) {
			(void) strlcpy(w, pch, MAXQUERYSIZE);
		}
	}

	(void) strlcpy(s, w, MAXQUERYSIZE);
	pch = strtok(s, " ");
	while (pch != NULL) {
		i++;
		pch = strtok(NULL, " ");
	}
	k->size = i;
	k->word = (char **)malloc(i * sizeof (char *));
	if (k->word == NULL) {
		malloc_error();
	}
	i = 0;
	(void) strlcpy(s, w, MAXQUERYSIZE);
	pch = strtok(s, " ");
	while (pch != NULL) {
		k->word[i] = (char *)malloc(MAXTERMSIZE);
		if (k->word[i] == NULL) {
			malloc_error();
		}
		(void) strlcpy(k->word[i], pch, MAXTERMSIZE);
		k->word[i][MAXTERMSIZE-1] = '\0';
		if (norm == 0) {
			(void) normalize(k->word[i]);
		}
		i++;
		pch = strtok(NULL, " ");
	}
	*key = k;

	return (i);
}

void malloc_error(void) {
	(void) fprintf(stderr, gettext("Memory allocation failed.\n"));
	exit(1);
}
