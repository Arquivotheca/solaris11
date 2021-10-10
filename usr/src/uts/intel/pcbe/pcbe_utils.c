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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#include <sys/cpuvar.h>
#include <sys/param.h>
#include <sys/cpc_impl.h>
#include <sys/cpc_pcbe.h>
#include <sys/modctl.h>
#include <sys/inttypes.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/x86_archext.h>
#include <sys/sdt.h>
#include <sys/archsystm.h>
#include <sys/privregs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cred.h>
#include <sys/policy.h>
#include "pcbe_utils.h"

/* Generic utility functions for Performance Counter Back-End */

/* Generic CPU information */
static uint_t	pcbe_family;
static uint_t	pcbe_model;

int
pcbe_family_get()
{
	return (pcbe_family);
}

int
pcbe_model_get()
{
	return (pcbe_model);
}

void
pcbe_init()
{
	pcbe_family = cpuid_getfamily(CPU);
	pcbe_model = cpuid_getmodel(CPU);
}



/*
 * This function looks for an "needle" in a comma-separated "haystack".
 * Examples:
 *
 * needle: "test"
 *
 * haystack: "test" -> match
 * haystack: "hello,test,world" -> match
 * haystack: "hello,test_test,world" -> fail
 */
int
pcbe_name_compare(char *haystack, char *needle) {
	size_t length, token_length;
	char *pos, *token;

	token = haystack;
	pos = strstr(haystack, ",");
	length = strlen(needle);
	token_length = (size_t)pos - (size_t)token;

	while (pos) {
		/* Only do a strcmp if the length matches */
		if (length == token_length) {
			if (strncmp(token, needle, length) == 0)
				return (0);
		}

		/* Look for the next token */
		token = ++pos;
		pos = strstr(token, ",");
		token_length = (size_t)pos - (size_t)token;
	}

	return (strcmp(token, needle));
}
