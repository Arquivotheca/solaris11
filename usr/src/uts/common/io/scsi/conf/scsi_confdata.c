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

#ifdef	_KERNEL

#include <sys/scsi/scsi_types.h>

/*
 * Autoconfiguration Dependent Data
 */
/*
 * Many defines in this file have built in parallel bus assumption
 * which might need to change as other interconnect evolve.
 */

/*
 * SCSI options word- defines are kept in <scsi/conf/autoconf.h>
 *
 * All this options word does is to enable such capabilities. Each
 * implementation may disable this word, or ignore it entirely.
 * Changing this word after system autoconfiguration is not guaranteed
 * to cause any change in the operation of the system.
 */

int scsi_options =
	SCSI_OPTIONS_DR			|
	SCSI_OPTIONS_LINK		|
	SCSI_OPTIONS_SYNC		|
	SCSI_OPTIONS_PARITY		|
	SCSI_OPTIONS_TAG		|
	SCSI_OPTIONS_FAST		|
	SCSI_OPTIONS_WIDE		|
	SCSI_OPTIONS_FAST20		|
	SCSI_OPTIONS_FAST40		|
	SCSI_OPTIONS_FAST80		|
	SCSI_OPTIONS_FAST160		|
	SCSI_OPTIONS_FAST320		|
	SCSI_OPTIONS_NLUNS_DEFAULT	|
	SCSI_OPTIONS_QAS		|
	0;

/*
 * Scsi bus or device reset recovery time in milliseconds.
 */
unsigned int	scsi_reset_delay = SCSI_DEFAULT_RESET_DELAY;

/*
 * SCSI selection timeout in milliseconds.
 */
int	scsi_selection_timeout = SCSI_DEFAULT_SELECTION_TIMEOUT;

/*
 * Default scsi host id.  Note, this variable is only used if the
 * "scsi-initiator-id" cannot be retrieved from openproms.  This is only
 * a problem with older platforms which don't have openproms and usage
 * of the sport-8 with openproms 1.x.
 */
int	scsi_host_id = 7;

/*
 * Maximum tag age limit.
 * Note exceeding tag age limit of 2 is fairly common;
 * refer to 1164758
 */
int	scsi_tag_age_limit = 2;

/*
 * scsi watchdog tick (secs)
 * Note: with tagged queueing, timeouts are highly inaccurate and therefore
 *	 it doesn't make sense to monitor every second.
 */
int	scsi_watchdog_tick = 10;

/*
 * default scsi target driver "fm-capable" property value
 */
int	scsi_fm_capable = DDI_FM_EREPORT_CAPABLE;

/*
 * SCSI enumeration options defines are kept in <scsi/conf/autoconf.h>.
 * When scsi_enumeration is enabled, driver.conf enumeration is unnecessary.
 *
 * The global variable "scsi_enumeration" is used as the default value of the
 * "scsi-enumeration" property. In addition to enabline/disabling enumeration
 * (bit 0), target and lun threading can be specified.
 *
 *	0	driver.conf enumeration
 *	1	dynamic enumeration with target/lun multi-threading.
 *	3	dynamic enumeration with lun multi-threading disabled.
 *	5	dynamic enumeration with target multi-threading disabled;
 *	7	dynamic enumeration with target/lun multi-threading disabled.
 *
 * Default is currently driver.conf enumeration (0).
 */
int	scsi_enumeration = 0;

#endif	/* _KERNEL */
