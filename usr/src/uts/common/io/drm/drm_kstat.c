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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "drmP.h"
#include <sys/kstat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>

static char *drmkstat_name[] = {
	"opens",
	"closes",
	"IOCTLs",
	"locks",
	"unlocks",
	NULL
};

static int
drm_kstat_update(kstat_t *ksp, int flag)
{
	struct drm_device *sc;
	kstat_named_t *knp;
	int tmp;

	if (flag != KSTAT_READ)
		return (EACCES);

	sc = ksp->ks_private;
	knp = ksp->ks_data;

	for (tmp = 1; tmp < 6; tmp++) {
		(knp++)->value.ui32 = sc->counts[tmp];
	}

	return (0);
}

int
drm_init_kstats(struct drm_device *sc)
{
	int instance;
	kstat_t *ksp;
	kstat_named_t *knp;
	char *np;
	char **aknp;

	instance = ddi_get_instance(sc->devinfo);
	aknp = drmkstat_name;
	ksp = kstat_create("drm", instance, "drminfo", "drm",
	    KSTAT_TYPE_NAMED, sizeof (drmkstat_name)/sizeof (char *) - 1,
	    KSTAT_FLAG_PERSISTENT);
	if (ksp == NULL)
		return (NULL);

	ksp->ks_private = sc;
	ksp->ks_update = drm_kstat_update;
	for (knp = ksp->ks_data; (np = (*aknp)) != NULL; knp++, aknp++) {
		kstat_named_init(knp, np, KSTAT_DATA_UINT32);
	}
	kstat_install(ksp);

	sc->asoft_ksp = ksp;

	return (0);
}

void
drm_fini_kstats(struct drm_device *sc)
{
	if (sc->asoft_ksp)
		kstat_delete(sc->asoft_ksp);
	else
		cmn_err(CE_WARN, "attempt to delete null kstat");
}
