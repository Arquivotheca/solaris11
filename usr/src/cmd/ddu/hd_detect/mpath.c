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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Get multipath devices information
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include "mpath.h"
#include "libddudev.h"

static lu_obj 	*pLuObj = NULL;

/*
 * Match the multipath logical unit
 * According initiator port to match a multipath logical unit
 *
 * iportID: initiator port
 * pObj: list of multipath logical unit to be matched
 *       if pObj is NULL, match the object from logical
 *       unit list head.
 *
 * Return:
 * If matched, return object else return NULL
 */
lu_obj *
getInitiaPortDevices(char *iportID, lu_obj *pObj)
{
	MP_STATUS		status;
	MP_OID_LIST		*pPathOidList;
	MP_PATH_LOGICAL_UNIT_PROPERTIES	pathProps;
	MP_INITIATOR_PORT_PROPERTIES	initProps;
	lu_obj 			*Obj;
	int			i;

	if (pObj == NULL) {
		Obj = pLuObj;
	} else {
		Obj = pObj->next;
	}

	while (Obj != NULL) {
		/* get a list of object IDs */
		status = MP_GetAssociatedPathOidList(Obj->oid, &pPathOidList);

		if (status == MP_STATUS_SUCCESS) {
			for (i = 0; i < pPathOidList->oidCount; i++) {
				(void) memset(&pathProps, 0,
				    sizeof (MP_PATH_LOGICAL_UNIT_PROPERTIES));
				/* get the specified path properties */
				status = MP_GetPathLogicalUnitProperties(
				    pPathOidList->oids[i], &pathProps);

				if (status == MP_STATUS_SUCCESS) {
					/* get initiator port property */
					status =
					    MP_GetInitiatorPortProperties(
					    pathProps.initiatorPortOid,
					    &initProps);

					/* match the initiator property */
					if (status == MP_STATUS_SUCCESS) {
						if (strcmp(iportID,
						    initProps.portID) == 0) {
							return (Obj);
						}
					}
				}
			}
		}

		Obj = Obj->next;
	}

	return (NULL);
}

/*
 * Get multipath logical units properties list
 * Lookup multipath logical units in plugin objects
 * For each multipath logical unit, get its properties
 *
 * Return:
 * Return a list of multipath logical units property,
 * If fail, return NULL
 */
lu_obj *
getLogicalUnit()
{
	MP_STATUS	status;
	MP_OID_LIST 	*pPluginList;
	MP_OID_LIST	*pLUList;
	MP_MULTIPATH_LOGICAL_UNIT_PROPERTIES	luProps;
	lu_obj 		*pObj;
	int		i, j;
	int		lu, nObj;

	/* get a list of the plugin object IDs */
	status = MP_GetPluginOidList(&pPluginList);
	if (status != MP_STATUS_SUCCESS) {
		perror("failed to get plugin List");
		return (NULL);
	}

	if ((NULL == pPluginList) || (pPluginList->oidCount < 1)) {
		FPRINTF(stderr, "No plugin get\n");
		return (NULL);
	}

	nObj = 0;

	/* caculate number of multipath logical units */
	for (i = 0; i < pPluginList->oidCount; i++) {
		status = MP_GetMultipathLus(pPluginList->oids[i], &pLUList);
		if (status == MP_STATUS_SUCCESS) {
			nObj = nObj + pLUList->oidCount;
		}
	}

	if (nObj) {
		pObj = (lu_obj*)malloc(nObj * sizeof (lu_obj));
		if (pObj == NULL) {
			perror("fail to allocate space for logical unit");
			return (NULL);
		}
	} else {
		return (NULL);
	}

	lu = 0;

	/* for each multipath logical unit, get its properties */
	for (i = 0; i < pPluginList->oidCount; i++) {
		status = MP_GetMultipathLus(pPluginList->oids[i], &pLUList);
		if (status == MP_STATUS_SUCCESS) {
			for (j = 0; j < pLUList->oidCount; j++) {
			(void) memset(&luProps, 0,
			    sizeof (MP_MULTIPATH_LOGICAL_UNIT_PROPERTIES));
				/* get logical unit properties */
				status = MP_GetMPLogicalUnitProperties(
				    pLUList->oids[j], &luProps);

				if (status == MP_STATUS_SUCCESS) {
					/* store multipath unit path */
					(void) strcpy(pObj[lu].path,
					    luProps.deviceFileName);
					pObj[lu].oid.objectSequenceNumber =
					    pLUList->oids[j].
					    objectSequenceNumber;
					pObj[lu].oid.objectType =
					    pLUList->oids[j].objectType;
					pObj[lu].oid.ownerId =
					    pLUList->oids[j].ownerId;
					pObj[lu].next = NULL;
					if (lu) {
						pObj[lu - 1].next = &pObj[lu];
					}
					pObj[lu].node = DI_NODE_NIL;
					lu++;
				}
			}
		}
	}

	if (lu == 0) {
		free(pObj);
		pObj = NULL;
	}

	return (pObj);
}

/*
 * Match devinfo devices tree node with multipath logical unit
 * according node device link to match multipath logical unit
 * path property. If matched, record node in Obj->node.
 * This is called indirectly via di_devlink_walk.
 *
 * devlink: node device link
 * arg: devinfo devices tree node
 *
 * Return:
 * DI_WALK_CONTINUE: continue walk
 */
int
check_mpath_link(di_devlink_t devlink, void *arg)
{
	const char *link_path;
	lu_obj 	*Obj;

	link_path = di_devlink_path(devlink);

	Obj = pLuObj;

	while (Obj) {
		if (Obj->node == DI_NODE_NIL) {
			/* Match node device link with multipath unit path */
			if (strcmp(Obj->path, link_path) == 0) {
				Obj->node = (di_node_t)arg;
				break;
			}
		}

		Obj = Obj->next;
	}

	return (DI_WALK_CONTINUE);
}

/*
 * Create multipath logical units list
 */
int
mpath_init()
{
	if (pLuObj) {
		return (1);
	}

	pLuObj = getLogicalUnit();

	if (pLuObj) {
		return (0);
	} else {
		return (1);
	}
}

/*
 * Free multipath logical units list
 */
void
mpath_fini()
{
	if (pLuObj) {
		free(pLuObj);
		pLuObj = NULL;
	}
}
