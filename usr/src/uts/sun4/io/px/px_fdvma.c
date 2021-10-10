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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Internal PCI Fast DVMA implementation
 */
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/async.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/dvma.h>
#include "px_obj.h"

/*LINTLIBRARY*/

/*
 * The following routines are used to implement the sun4u fast dvma
 * routines on this bus.
 */

/*ARGSUSED*/
int
px_fdvma_reserve(dev_info_t *dip, dev_info_t *rdip, px_t *px_p,
	ddi_dma_req_t *dmareq, ddi_dma_handle_t *handlep)
{
	return (DDI_DMA_NORESOURCES);
}

/*ARGSUSED*/
int
px_fdvma_release(dev_info_t *dip, px_t *px_p, ddi_dma_impl_t *mp)
{
	return (DDI_SUCCESS);
}
