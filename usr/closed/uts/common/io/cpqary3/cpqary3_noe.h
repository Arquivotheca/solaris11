/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2009, 2011, Hewlett-Packard Development Company, L.P.
 */

/*
 *
 * Module Name:
 *   CPQARY3_NOE.H
 * Abstract:
 *   The generic Header file for CPQARY3 Driver.
 *   This file has all definitions and declarations related to Notification
 *	of Events for the supported HBA.
 */

#ifndef	CPQARY3_NOE_H
#define	CPQARY3_NOE_H

/*
 * Information from CPQARY3 on Notification of Events
 * View Hardware API for more information
 */

#define	CISS_NOE_CDB_LEN 		0x0D
#define	CISS_CANCEL_NOE_CDB_LEN		0x10
#define	BMIC_NOTIFY_ON_EVENT 		0xD0
#define	BMIC_CANCEL_NOTIFY_ON_EVENT 0xD1
#define	NOE_BUFFER_LENGTH		0x200

#define	ENABLE_FW_SERIAL_LOG		0x80

#pragma pack(1)

typedef struct Noe_Buffer {
	uint32_t	relative_controller_time;
	uint16_t	event_class_code;
	uint16_t	event_subclass_code;
	uint16_t	event_detail_code;
	uint8_t   	event_specific_data[64];
	uint8_t    	ascii_message[80];
	uint32_t   	event_tag;
	uint16_t	month_day;
	uint16_t	year;
	uint32_t	hms;
	uint16_t	pre_powerup_time;
	uint8_t		device_address[8];
	uint8_t		reserved[336];
} NoeBuffer;

#pragma pack()

typedef struct cpqary3_noe_buffer {
	NoeBuffer noebuf;
	struct cpqary3_noe_buffer *pNext;
} cpqary3_noe_buffer;

#define	CPQARY3_NOE_INIT	0
#define	CPQARY3_NOE_RESUBMIT 	1
#define	CPQARY3_NOE_FAILED	3

/*
 * This hierarchy is described in the firmware spec.
 * It provides various categories of reports to provide for
 * notification to host regarding asynchronous phenomena.
 */
#define	CLASS_PROTOCOL		0	/* Event Notifier Protocol */
#define	SUB_CLASS_NON_EVENT	0
#define	DETAIL_DISABLED		1
#define	SUB_CLASS_PROTOCOL_ERR	1
#define	DETAIL_EVENT_Q_OVERFLOW	0

#define	CLASS_HOT_PLUG		1	/* All Hot-Plug Operations */
#define	SUB_CLASS_HP_CHANGE	0

#define	CLASS_SERIAL_LOG	11	/* FW Serial output log */
#define	SUB_CLASS_LOG_AVL	0
#define	SUB_CLASS_LOG_NOTAVL	1


/*
 * New events for HP Smart Array controllers -
 * FW revision greater than 5.14 or later
 */

/* Storage Box HotPlug or Cabling Change */
#define	SUB_CLASS_SB_HP_CHANGE		6
/* Storage box Removed */
#define	DETAIL_STORAGEBOX_REMOVED	0
/* Storage Box Added */
#define	DETAIL_STORAGEBOX_ADDED		1
/* Storage Box Redundant I/O Module Removed */
#define	DETAIL_PATH_REMOVED		2
/* Storage Box Redundant I/O Module (or its path) Added */
#define	DETAIL_PATH_ADDED		3
/* Storage Box (or its first path) Repaired (re-added after failure) */
#define	DETIAL_STORAGEBOX_REPAIRED	4
/* Storage Box Redundant I/O Module (or its path) Repaired */
#define	DETAIL_PATH_REPAIRED		5

/* Disk Drive Redundant Path Change */
#define	SUB_CLASS_DD_PATH_CHANGE 7
/* Redundant path to configured disk drive is Hot Removed */
#define	DETAIL_DD_HOT_REMOVED    0

/* Cable, Memory, Fan */
#define	CLASS_HARDWARE		2
/* Redundant Cabling Change */
#define	SUB_CLASS_RC_CHANGE	7
/* Unsupported Configuration Occurred Online */
#define	DETAIL_UNSUPPORTED_CONFIGURATION 0
/* Temperature, Power, Chasis, UPS  */
#define	CLASS_ENVIRONMENT	3

/* Physical drive Changes	*/
#define	CLASS_PHYSICAL_DRIVE    4

/* Logical drive Changes	*/
#define	CLASS_LOGICAL_DRIVE	5
#define	SUB_CLASS_STATUS	0
#define	DETAIL_CHANGE		0
#define	MEDIA_EXCHANGE		1

/* Spare Status States */
#define	SPARE_UNDEFINED   		0x00
#define	SPARES_DESIGNATED 		0x01
#define	SPARE_REBUILDING  		0x02
#define	SPARE_REBUILT			0x04
#define	SPARES_BAD			0x08
#define	SPARE_ACTIVE			0x10
#define	SPARE_AVAILABLE			0x20

#define	MAX_KNOWN_FAILURE_REASON	132

#endif /* CPQARY3_NOE_H */
