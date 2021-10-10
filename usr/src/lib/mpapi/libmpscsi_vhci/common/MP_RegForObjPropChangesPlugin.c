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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "mp_utils.h"


/*
 *	Called by the common layer to request the plugin to call
 *	a client application's callback (pClientFn) when a property change
 *	is detected for the given object type.
 */

MP_STATUS
MP_RegisterForObjectPropertyChangesPlugin(MP_OBJECT_PROPERTY_FN pClientFn,
		MP_OBJECT_TYPE objectType,
		void *pCallerData)
{
	MP_BOOL hasFunc = MP_FALSE;


	log(LOG_INFO, "MP_RegisterForObjectPropertyChangesPlugin()",
	    " - enter");


	/* Validate the object type passes in within range */
	if (objectType > MP_OBJECT_TYPE_MAX) {

		log(LOG_INFO, "MP_RegisterForObjectPropertyChangesPlugin()",
		    " - objectType is invalid");

		log(LOG_INFO, "MP_RegisterForObjectPropertyChangesPlugin()",
		    " - error exit");

		return (MP_STATUS_INVALID_PARAMETER);
	}

	if (objectType < 1) {

		log(LOG_INFO, "MP_RegisterForObjectPropertyChangesPlugin()",
		    " - objectType is invalid");

		log(LOG_INFO, "MP_RegisterForObjectPropertyChangesPlugin()",
		    " - error exit");

		return (MP_STATUS_INVALID_PARAMETER);
	}

	/* Init sysevents if they have not been initialized yet */
	if (g_SysEventHandle == NULL) {
		if (init_sysevents() != MP_STATUS_SUCCESS)
			return (MP_STATUS_FAILED);
	}

	/* Check to see if we are going to be replacing */
	(void) pthread_mutex_lock(&g_prop_mutex);
	if (g_Property_Callback_List[objectType].pClientFn != NULL) {

		hasFunc = MP_TRUE;
	}

	/* Add the registration */
	g_Property_Callback_List[objectType].pClientFn   = pClientFn;
	g_Property_Callback_List[objectType].pCallerData = pCallerData;
	(void) pthread_mutex_unlock(&g_prop_mutex);

	if (hasFunc) {

		log(LOG_INFO, "MP_RegisterForObjectPropertyChangesPlugin()",
		    " - returning MP_STATUS_FN_REPLACED");

		return (MP_STATUS_FN_REPLACED);
	}


	log(LOG_INFO, "MP_RegisterForObjectPropertyChangesPlugin()",
	    " - exit");

	return (MP_STATUS_SUCCESS);
}
