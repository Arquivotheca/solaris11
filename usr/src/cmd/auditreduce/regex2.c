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

#include <stdlib.h>
#include <string.h>
#include <libgen.h>

struct exp {
	char *s;	/* the regular expression */
	int not;	/* exclude if matched? */
	char *comp;	/* the compiled regular expression */
};

static int	compile = 1;		/* compile the expression */

static char	*fexp = NULL;	/* full list of regular expressions */
static int	nexp = 1;	/* number of regular expressions in fexp */
static struct exp *p_exp = NULL; /* list of individual expressions */

char *
re_comp2(char *s)
{
	char *p;
	char *rval;
	int i;
	static char *er_regcmp = "regcmp: error";
	static char *er_mem = "memory allocation error";

	compile = 1;
	if (p_exp != NULL) {
		for (i = 0; i < nexp; i++) {
			free(p_exp[i].comp);
		}
		free(p_exp);
	}
	free(fexp);
	if ((fexp = strdup(s)) == NULL) {
		return (er_mem);
	}
	for (p = fexp, nexp = 1; *p != '\0'; p++) {
		if (*p == ',') {
			nexp++;
		}
	}
	if ((p_exp = malloc(nexp * sizeof (*p_exp))) == NULL) {
		free(fexp);
		return (er_mem);
	}
	for (i = 0, p = fexp; *p != '\0'; i++) {
		p_exp[i].comp = NULL;
		if (*p == '~') {
			p++;
			p_exp[i].not = 1;
		} else {
			p_exp[i].not = 0;
		}
		p_exp[i].s = p;
		while (*p != ',' && *p != '\0') {
			p++;
		}
		if (*p == ',') {
			*p = '\0';
			p++;
		}
		if ((rval = regcmp(p_exp[i].s, NULL)) == NULL) {
			free(p_exp);
			free(fexp);
			return (er_regcmp);
		}
		free(rval);
	}
	return (NULL);
}

int
re_exec2(char *s)
{
	int i;

	if (compile) {
		for (i = 0; i < nexp; i++) {
			p_exp[i].comp = regcmp(p_exp[i].s, NULL);
			if (p_exp[i].comp == NULL) {
				return (-1);
			}
		}
		compile = 0;
	}
	for (i = 0; i < nexp; i++) {
		if (regex(p_exp[i].comp, s) != NULL) {
			return (!p_exp[i].not);
		}
	}

	/* no match and no more to check */
	return (0);
}
