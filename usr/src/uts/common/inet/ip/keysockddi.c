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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <inet/common.h>
#include <inet/ipsec_impl.h>

#define	INET_NAME	"keysock"
#define	INET_MODSTRTAB	keysockinfo
#define	INET_DEVSTRTAB	keysockinfo
#define	INET_MODDESC	"PF_KEY socket STREAMS module"
#define	INET_DEVDESC	"PF_KEY socket STREAMS driver"
#define	INET_DEVMINOR	0
#define	INET_DEVMTFLAGS	(D_MP|D_MTPERMOD|D_MTPUTSHARED)
#define	INET_MODMTFLAGS	INET_DEVMTFLAGS

#include "../inetddi.c"


struct modlinkage *keysock_modlp = NULL;

int
_init(void)
{
	int	error;

	if (!keysock_ddi_init())
		return (ENOMEM);
	error = mod_install(&modlinkage);
	if (error != 0)
		keysock_ddi_destroy();
	else
		keysock_modlp = &modlinkage;

	return (error);
}

int
_fini(void)
{
	int	error;

	error = mod_remove(&modlinkage);
	if (error != 0)
		return (error);

	keysock_ddi_destroy();
	keysock_modlp = NULL;
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
