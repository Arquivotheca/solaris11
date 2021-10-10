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



#include "Trace.h"
#include "Exceptions.h"
#include "sun_fc.h"
#ifdef	__cplusplus
extern "C" {
#endif

/**
 * @memo	    Retrieves the attributes for a specific discovered port
 *		    by WWN
 * @return	    HBA_STATUS_OK if the attributes are filled in
 * @param	    handle The HBA to operate on
 * @param	    pwwn The wwn of the desired target
 * @param	    attributes The user-allocated attributes buffer
 * 
 */
HBA_STATUS Sun_fcGetPortAttributesByWWN(HBA_HANDLE handle, HBA_WWN pwwn,
	    PHBA_PORTATTRIBUTES attributes) {
	Trace log("Sun_fcGetPortAttributesByWWN");

	if (attributes == NULL) {
	    log.userError("NULL attributes pointer");
	    return (HBA_STATUS_ERROR_ARG);
	}

	try {
	    Handle *myHandle = Handle::findHandle(handle);
	    *attributes = myHandle->getPortAttributes(wwnConversion(pwwn.wwn));
	    return (HBA_STATUS_OK);
	} catch (HBAException &e) {
	    return (e.getErrorCode());
	} catch (...) {
	    log.internalError("Uncaught exception");
	    return (HBA_STATUS_ERROR);
	}
}
#ifdef	__cplusplus
}
#endif
