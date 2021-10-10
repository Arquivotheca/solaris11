/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/dada/adapters/ghd/ghd.h>
#include <sys/dada/adapters/ghd/ghd_debug.h>
#include <sys/note.h>

#if !(defined(GHD_DEBUG) || defined(__lint))
ulong_t	ghd_debug_flags = 0;
#else
ulong_t	ghd_debug_flags = GDBG_FLAG_ERROR
		/*	| GDBG_FLAG_WAITQ	*/
		/*	| GDBG_FLAG_INTR	*/
		/*	| GDBG_FLAG_START	*/
		/*	| GDBG_FLAG_WARN	*/
		/*	| GDBG_FLAG_DMA		*/
		/*	| GDBG_FLAG_PEND_INTR	*/
		/*	| GDBG_FLAG_START	*/
		/*	| GDBG_FLAG_PKT		*/
		/*	| GDBG_FLAG_INIT	*/
			;
#endif

_NOTE(DATA_READABLE_WITHOUT_LOCK(ghd_debug_flags))

/*PRINTFLIKE1*/
void
ghd_err(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vcmn_err(CE_CONT, fmt, ap);
	va_end(ap);
}

#if defined(GHD_DEBUG)

static void
ghd_dump_ccc(ccc_t *P)
{
	ghd_err("nextp 0x%x tmrp 0x%x label 0x%x &mutex 0x%x\n",
		P->ccc_nextp, P->ccc_tmrp, P->ccc_label, &P->ccc_activel_mutex);
	ghd_err("&activel 0x%x dip 0x%x iblock 0x%x\n",
		&P->ccc_activel, P->ccc_hba_dip, P->ccc_iblock);
	ghd_err("softid 0x%x &hba_mutext 0x%x\n poll 0x%x\n",
		P->ccc_soft_id, &P->ccc_hba_mutex);
	ghd_err("&devs 0x%x &waitq_mutex 0x%x &waitq 0x%x\n",
		&P->ccc_devs, &P->ccc_waitq_mutex, &P->ccc_waitq);
	ghd_err("dq softid 0x%x &dq_mutex 0x%x &doneq 0x%x\n",
		P->ccc_doneq_softid, &P->ccc_doneq_mutex, &P->ccc_doneq);
	ghd_err("handle 0x%x &ccballoc 0x%x\n",
		P->ccc_hba_handle, &P->ccc_ccballoc);
}
#endif
