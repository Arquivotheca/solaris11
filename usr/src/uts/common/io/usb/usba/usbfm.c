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

#include <sys/types.h>
#include <sys/sunndi.h>
#include <sys/sysmacros.h>
#include <sys/ddifm_impl.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>
#include <sys/fm/io/usb.h>
#include <sys/fm/io/ddi.h>


static void
usba_error_report(dev_info_t *dip, char *err)
{
	char buf[FM_MAX_CLASS];
	uint64_t ena;
	/*
	 * Log generic USB errors.
	 *
	 * Generate an ereport for this error bit.
	 */
	ena = fm_ena_generate(0, FM_ENA_FMT1);
	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", USB_ERROR_SUBCLASS, err);
	ddi_fm_ereport_post(dip, buf, ena, DDI_NOSLEEP, FM_VERSION,
	    DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);

}

void
usb_ereport_post(dev_info_t *dip, char *err)
{
	/*
	 * USB client drivers should have been set up with minimal FM
	 * capability(DDI_FM_EREPORT_CAPABLE) in usb_client_attach()
	 * or scsi_hba_attach_setup(scsa2usb only).
	 */
	if (!DDI_FM_EREPORT_CAP(ddi_fm_capable(dip))) {

		return;
	}

	usba_error_report(dip, err);
}
