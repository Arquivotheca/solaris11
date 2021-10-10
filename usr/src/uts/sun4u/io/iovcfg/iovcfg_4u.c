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

/*
 * IOV Configuration Module for sun4u
 */

#include	<sys/conf.h>
#include	<sys/mkdev.h>
#include	<sys/modctl.h>
#include	<sys/stat.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/iovcfg.h>
#include	<sys/pciconf.h>

#define	IOVCFG_INFO	"IOV Configuration Module"

/*
 * This is a iovcfg module for sun4u platforms.
 * The purpose of this moodule is to provide dummy iovcfg_xxx routines
 * for the pcie framework so that SRIOV enabled OS can be booted
 * successfully on sun4u platforms.
 * SRIOV will not be supported on sun4u platforms.
 */

/*
 * Module linkage information for the kernel.
 */
static	struct modlmisc		iovcfg_modlmisc = {
	&mod_miscops,
	IOVCFG_INFO,
};

static	struct modlinkage	iovcfg_modlinkage = {
	MODREV_1,
	(void *)&iovcfg_modlmisc,
	NULL
};

/*
 * Module gets loaded when the px driver is loaded.
 */
int
_init(void)
{

	return (mod_install(&iovcfg_modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&iovcfg_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&iovcfg_modlinkage, modinfop));
}

/* ARGSUSED */
int
iovcfg_param_get(char *pf_pathname, nvlist_t **nvlp)
{

	if (nvlp == NULL)
		return (EINVAL);
	*nvlp = NULL;
	return (-1);
}

/* ARGSUSED */
int
iovcfg_get_numvfs(char *pf_path, uint_t *num_vf_p)
{

	if (num_vf_p == NULL)
		return (EINVAL);
	*num_vf_p = 0;
	return (0);
}

/* ARGSUSED */
int
iovcfg_is_vf_assigned(char *pf_path, uint_t vf_index, boolean_t *loaned_p)
{

	if (loaned_p == NULL) {
		return (EINVAL);
	}
	*loaned_p = B_FALSE;
	return (0);
}

/* ARGSUSED */
int
iovcfg_configure_pf_class(char *pf_path)
{

	return (0);
}

int
iovcfg_update_pflist(void)
{
	return (0);
}
