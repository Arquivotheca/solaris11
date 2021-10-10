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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <strings.h>
#include <fmdump.h>

struct topo_hdl *fmd_fmri_topo_hold(int version);

char *
fmdump_nvl2str(nvlist_t *nvl)
{
	int err;
	topo_hdl_t *thp;
	char *buf, *str;

	if ((thp = fmd_fmri_topo_hold(TOPO_VERSION)) == NULL)
		return (NULL);
	if (topo_fmri_nvl2str(thp, nvl, &str, &err) != 0)
		return (NULL);
	if ((buf = malloc(strlen(str) + 1)) != NULL)
		(void) strcpy(buf, str);
	topo_hdl_strfree(thp, str);
	return (buf);
}

struct topo_hdl *
fmd_fmri_topo_hold(int version)
{
	int err;

	if (version != TOPO_VERSION)
		return (NULL);

	if (g_thp == NULL) {
		if ((g_thp = topo_open(TOPO_VERSION, "/", &err)) == NULL) {
			(void) fprintf(stderr, "topo_open failed: %s\n",
			    topo_strerror(err));
			exit(1);
		}
	}

	return (g_thp);
}
