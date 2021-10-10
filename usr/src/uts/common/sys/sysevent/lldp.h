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

#ifndef _SYS_SYSEVENT_LLDP_H
#define	_SYS_SYSEVENT_LLDP_H

/*
 * LLDP sysevent definitions.  Note that all of these definitions are
 * Sun-private and are subject to change at any time.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Event Channel associated with following LLDP events */
#define	LLDP_EVENT_CHAN	"com.oracle:lldp:events"

/*
 * Event type EC_LLDP/ESC_LLDP_APPLN event schema
 *
 *	Event Class	- EC_LLDP
 *	Event Sub-Class	- ESC_LLDP_APPLN
 *	Event Vendor	- com.oracle
 *	Event Publisher	- lldpd
 *
 *	Attribute Name	- LLDP_EVENT_VERSION
 *	Attribute Type	- DATA_TYPE_UINT32
 *	Attribute Value	- <version>
 *
 *	Attribute Name	- LLDP_AGENT_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 *
 *	Attribute Name	- LLDP_OPER_CFG
 *	Attribute Type	- DATA_TYPE_NVLIST
 *	Attribute Value	- <Operating config of a feature>
 */
#define	LLDP_EVENT_VERSION	"lldp_event_version"
#define	LLDP_AGENT_NAME		"lldp_agent_name"
#define	LLDP_OPER_CFG		"lldp_oper_cfg"

/*
 * Event type EC_LLDP/ESC_LLDP_MODE event schema
 *
 *	Event Class	- EC_LLDP
 *	Event Sub-Class	- ESC_LLDP_MODE
 *	Event Vendor	- com.oracle
 *	Event Publisher	- lldpd
 *
 *	Attribute Name	- LLDP_EVENT_VERSION
 *	Attribute Type	- DATA_TYPE_UINT32
 *	Attribute Value	- <version>
 *
 *	Attribute Name	- LLDP_AGENT_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 *
 *	Attribute Name	- LLDP_PREVIOUS_MODE
 *	Attribute Type	- DATA_TYPE_UINT32 (lldp_admin_status_t)
 *	Attribute Value	- <previous mode>
 *
 *	Attribute Name	- LLDP_CURRENT_MODE
 *	Attribute Type	- DATA_TYPE_UINT32 (lldp_admin_status_t)
 *	Attribute Value	- <current mode>
 */
#define	LLDP_MODE_PREVIOUS	"lldp_mode_previous"
#define	LLDP_MODE_CURRENT	"lldp_mode_current"

/*
 * Event type EC_LLDP/ESC_LLDP_REMOTE event schema
 *
 *	Event Class	- EC_LLDP
 *	Event Sub-Class	- ESC_LLDP_REMOTE
 *	Event Vendor	- com.oracle
 *	Event Publisher	- lldpd
 *
 *	Attribute Name	- LLDP_EVENT_VERSION
 *	Attribute Type	- DATA_TYPE_UINT32
 *	Attribute Value	- <version>
 *
 *	Attribute Name	- LLDP_AGENT_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 *
 *	Attribute Name	- LLDP_REMOTE_CHASSISID
 *	Attribute Type	- DATA_TYPE_NVLIST
 *	Attribute Value	- <contains CID of the peer. lldp_nvlist2chassid()
 *			    should be used.>
 *
 *	Attribute Name	- LLDP_REMOTE_PORTID
 *	Attribute Type	- DATA_TYPE_NVLIST
 *	Attribute Value	- <contains PID of the peer. lldp_nvlist2pid()
 *			    should be used.>
 *
 *	Attribute Name	- LLDP_CHANGE_TYPE
 *	Attribute Type	- DATA_TYPE_UINT32
 *	Attribute Value - any one of (LLDP_REMOTE_CHANGED |
 *			  LLDP_REMOTE_SHUTDOWN | LLDP_REMOTE_INFOAGE)
 *
 *	Attribute Name	- LLDP_ADDED_TLVS
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 *
 *	Attribute Name	- LLDP_MODIFIED_TLVS
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 *
 *	Attribute Name	- LLDP_DELETED_TLVS
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 */
#define	LLDP_CHASSISID		"chassisid"
#define	LLDP_PORTID		"portid"
#define	LLDP_CHANGE_TYPE	"change_type"
#define	LLDP_ADDED_TLVS		"added_tlvs"
#define	LLDP_MODIFIED_TLVS	"modified_tlvs"
#define	LLDP_DELETED_TLVS	"deleted_tlvs"

/* various change types */
#define	LLDP_REMOTE_CHANGED	0x01
#define	LLDP_REMOTE_SHUTDOWN	0x02
#define	LLDP_REMOTE_INFOAGE	0x04

/*
 * Event type EC_LLDP/ESC_LLDP_LOCAL event schema
 *
 *	Event Class	- EC_LLDP
 *	Event Sub-Class	- ESC_LLDP_LOCAL
 *	Event Vendor	- com.oracle
 *	Event Publisher	- lldpd
 *
 *	Attribute Name	- LLDP_EVENT_VERSION
 *	Attribute Type	- DATA_TYPE_UINT32
 *	Attribute Value	- <version>
 *
 *	Attribute Name	- LLDP_AGENT_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 */
#define	LLDP_LOCAL_CHANGED	"lldp_local_changed"

/*
 * Event type EC_LLDP/ESC_LLDP_PFC event schema
 *
 *	Event Class	- EC_LLDP
 *	Event Sub-Class	- ESC_LLDP_PFC
 *	Event Vendor	- com.oracle
 *	Event Publisher	- lldpd
 *
 *	Attribute Name	- LLDP_EVENT_VERSION
 *	Attribute Type	- DATA_TYPE_UINT32
 *	Attribute Value	- <version>
 *
 *	Attribute Name	- LLDP_AGENT_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- <Name of the lldp agent>
 *
 *	Attribute Name	- LLDP_OPER_CFG
 *	Attribute Type	- DATA_TYPE_NVLIST
 *	Attribute Value	- <Operating config of a feature>
 */
#define	LLDP_EVENT_CUR_VERSION	1

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SYSEVENT_LLDP_H */
