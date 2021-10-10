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
 *
 */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	EFB_EDID_H
#define	EFB_EDID_H

#include <sys/types.h>

#include "efb_i2c.h"


/*
 * Functions used for extracting EDID information from a monitor.  These
 * functions could easily be modified or made more generic.
 *
 * Caller should provide a vector of function calls which can
 * be called to perform the following low-level functions.  See i2c.h
 * for details.
 *
 *
 * The function efb_read_edid() will return the edid data as retrieved from
 * the monitor.  This function will return I2C_OK on success.
 *
 * Caller should use mutex locking to ensure that two threads do not
 * attempt to simultaneously access the monitor.  The regular device
 * mutex should usually not be used for this purpose if avoidable, as this
 * protocol can take a considerable amount of time to execute.
 *
 * This call will fail on some monitors if sync is not active.  In these
 * cases, the caller should activate sync and try again.
 *
 */

extern	int	efb_read_edid(efb_private_t *efb_priv, int port, uint8_t *data,
		    uint_t *len);


#endif	/* EFB_EDID_H */
