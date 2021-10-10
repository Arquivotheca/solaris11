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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/fm/protocol.h>
#include <sys/cpu_module_impl.h>
#include "nhmex_log.h"

static const cmi_mc_ops_t nhmex_mc_ops = {
	nhmex_patounum,
	nhmex_unumtopa,
	nhmex_error_trap  /* cmi_mc_logout */
};
/*ARGSUSED*/
int
inhmex_mc_register(cmi_hdl_t hdl, void *arg1, void *arg2, void *arg3)
{
	cmi_mc_register(hdl, &nhmex_mc_ops, NULL);
	return (CMI_HDL_WALK_NEXT);
}
