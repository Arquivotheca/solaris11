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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LIBSTMF_H
#define	_LIBSTMF_H

#include <time.h>
#include <sys/param.h>
#include <libnvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Constants and Types */

/* LU and Local Port states */
#define	STMF_LOGICAL_UNIT_OFFLINE	0
#define	STMF_LOGICAL_UNIT_OFFLINING	1
#define	STMF_LOGICAL_UNIT_ONLINE	2
#define	STMF_LOGICAL_UNIT_ONLINING	3
#define	STMF_LOGICAL_UNIT_UNREGISTERED	4
#define	STMF_TARGET_PORT_OFFLINE	5
#define	STMF_TARGET_PORT_OFFLINING	6
#define	STMF_TARGET_PORT_ONLINE		7
#define	STMF_TARGET_PORT_ONLINING	8
#define	STMF_SERVICE_STATE_ONLINE	9
#define	STMF_SERVICE_STATE_OFFLINE	10
#define	STMF_SERVICE_STATE_ONLINING	11
#define	STMF_SERVICE_STATE_OFFLINING	12
#define	STMF_SERVICE_STATE_UNKNOWN	13
#define	STMF_CONFIG_STATE_NONE		14
#define	STMF_CONFIG_STATE_INIT		15
#define	STMF_CONFIG_STATE_INIT_DONE	16
#define	STMF_CONFIG_STATE_UNKNOWN	17
#define	STMF_DEFAULT_LU_STATE		18
#define	STMF_DEFAULT_TARGET_PORT_STATE	19

#define	STMF_IDENT_LENGTH   255

/* API status return values */
#define	STMF_STATUS_SUCCESS	    0x0000
#define	STMF_STATUS_ERROR	    0x8000
#define	STMF_ERROR_BUSY			(STMF_STATUS_ERROR | 0x01)
#define	STMF_ERROR_NOT_FOUND		(STMF_STATUS_ERROR | 0x02)
#define	STMF_ERROR_MEMBER_NOT_FOUND	(STMF_STATUS_ERROR | 0x03)
#define	STMF_ERROR_GROUP_NOT_FOUND	(STMF_STATUS_ERROR | 0x04)
#define	STMF_ERROR_PERM			(STMF_STATUS_ERROR | 0x05)
#define	STMF_ERROR_NOMEM		(STMF_STATUS_ERROR | 0x06)
#define	STMF_ERROR_INVALID_ARG		(STMF_STATUS_ERROR | 0x07)
#define	STMF_ERROR_EXISTS		(STMF_STATUS_ERROR | 0x08)
#define	STMF_ERROR_SERVICE_NOT_FOUND	(STMF_STATUS_ERROR | 0x09)
#define	STMF_ERROR_SERVICE_ONLINE	(STMF_STATUS_ERROR | 0x0a)
#define	STMF_ERROR_SERVICE_OFFLINE	(STMF_STATUS_ERROR | 0x0b)
#define	STMF_ERROR_GROUP_IN_USE		(STMF_STATUS_ERROR | 0x0c)
#define	STMF_ERROR_LUN_IN_USE		(STMF_STATUS_ERROR | 0x0d)
#define	STMF_ERROR_VE_CONFLICT		(STMF_STATUS_ERROR | 0x0e)
#define	STMF_ERROR_CONFIG_NONE		(STMF_STATUS_ERROR | 0x0f)
#define	STMF_ERROR_SERVICE_DATA_VERSION (STMF_STATUS_ERROR | 0x10)
#define	STMF_ERROR_INVALID_HG		(STMF_STATUS_ERROR | 0x11)
#define	STMF_ERROR_INVALID_TG		(STMF_STATUS_ERROR | 0x12)
#define	STMF_ERROR_PROV_DATA_STALE	(STMF_STATUS_ERROR | 0x13)
#define	STMF_ERROR_NO_PROP		(STMF_STATUS_ERROR | 0x14)
#define	STMF_ERROR_NO_PROP_VAL		(STMF_STATUS_ERROR | 0x15)
#define	STMF_ERROR_MISSING_PROP_VAL	(STMF_STATUS_ERROR | 0x16)
#define	STMF_ERROR_INVALID_BLOCKSIZE	(STMF_STATUS_ERROR | 0x17)
#define	STMF_ERROR_FILE_ALREADY		(STMF_STATUS_ERROR | 0x18)
#define	STMF_ERROR_INVALID_PROPSIZE	(STMF_STATUS_ERROR | 0x19)
#define	STMF_ERROR_INVALID_PROP		(STMF_STATUS_ERROR | 0x20)
#define	STMF_ERROR_PERSIST_TYPE		(STMF_STATUS_ERROR | 0x21)
#define	STMF_ERROR_TG_ONLINE		(STMF_STATUS_ERROR | 0x22)
#define	STMF_ERROR_ACCESS_STATE_SET	(STMF_STATUS_ERROR | 0x23)
#define	STMF_ERROR_NO_PROP_STANDBY	(STMF_STATUS_ERROR | 0x24)
#define	STMF_ERROR_POST_MSG_FAILED	(STMF_STATUS_ERROR | 0x25)
#define	STMF_ERROR_DOOR_INSTALLED	(STMF_STATUS_ERROR | 0x26)

/* Failures for stmfCreateLu */
#define	STMF_ERROR_FILE_IN_USE		(STMF_STATUS_ERROR | 0x100)
#define	STMF_ERROR_INVALID_BLKSIZE	(STMF_STATUS_ERROR | 0x101)
#define	STMF_ERROR_GUID_IN_USE		(STMF_STATUS_ERROR | 0x102)
#define	STMF_ERROR_META_FILE_NAME	(STMF_STATUS_ERROR | 0x103)
#define	STMF_ERROR_DATA_FILE_NAME	(STMF_STATUS_ERROR | 0x104)
#define	STMF_ERROR_SIZE_OUT_OF_RANGE	(STMF_STATUS_ERROR | 0x105)
#define	STMF_ERROR_LU_BUSY		(STMF_STATUS_ERROR | 0x106)
#define	STMF_ERROR_META_CREATION	(STMF_STATUS_ERROR | 0x107)
#define	STMF_ERROR_FILE_SIZE_INVALID	(STMF_STATUS_ERROR | 0x108)
#define	STMF_ERROR_WRITE_CACHE_SET	(STMF_STATUS_ERROR | 0x109)
#define	STMF_ERROR_WRITE_PROTECT_SET	(STMF_STATUS_ERROR | 0x10a)

/* Initiator Name Types */
#define	STMF_FC_PORT_WWN	    1
#define	STMF_ISCSI_NAME		    2


/* provider types */
#define	STMF_LU_PROVIDER_TYPE	1
#define	STMF_PORT_PROVIDER_TYPE	2

/* LU Resource types */
#define	STMF_DISK   0

/* Persistence methods */
#define	STMF_PERSIST_SMF	1
#define	STMF_PERSIST_NONE	2

/* Logical unit access states */
#define	STMF_ACCESS_ACTIVE		"0"
#define	STMF_ACCESS_ACTIVE_TO_STANDBY   "1"
#define	STMF_ACCESS_STANDBY		"2"
#define	STMF_ACCESS_STANDBY_TO_ACTIVE	"3"

/*
 * LU Disk Properties
 */

enum {
	STMF_LU_PROP_ALIAS = 1,
	STMF_LU_PROP_BLOCK_SIZE,
	STMF_LU_PROP_COMPANY_ID,
	STMF_LU_PROP_FILENAME,
	STMF_LU_PROP_GUID,
	STMF_LU_PROP_META_FILENAME,
	STMF_LU_PROP_MGMT_URL,
	STMF_LU_PROP_NEW,
	STMF_LU_PROP_SIZE,
	STMF_LU_PROP_WRITE_PROTECT,
	STMF_LU_PROP_WRITE_CACHE_DISABLE,
	STMF_LU_PROP_VID,
	STMF_LU_PROP_PID,
	STMF_LU_PROP_SERIAL_NUM,
	STMF_LU_PROP_ACCESS_STATE,
	STMF_LU_PROP_HOST_ID
};


/* devid code set and name types */
#define	EUI_64_TYPE	2
#define	NAA_TYPE	3
#define	SCSI_NAME_TYPE	8

#define	BINARY_CODE_SET	1
#define	ASCII_CODE_SET	2
#define	UTF_8_CODE_SET	3

typedef enum _stmfProtocol
{
	STMF_PROTOCOL_FIBRE_CHANNEL =	0,
	STMF_PROTOCOL_SCSI =		1,
	STMF_PROTOCOL_SSA =		2,
	STMF_PROTOCOL_IEEE_1394 =	3,
	STMF_PROTOCOL_SRP =		4,
	STMF_PROTOCOL_ISCSI =		5,
	STMF_PROTOCOL_SAS =		6
} stmfProtocol;


typedef struct _stmfGuid
{
	uchar_t	guid[16];
} stmfGuid;

typedef struct _stmfGuidList
{
	uint32_t cnt;
	stmfGuid guid[1];
} stmfGuidList;

typedef struct _stmfState
{
	int operationalState;
	int configState;
} stmfState;

typedef struct _stmfDevid
{
	uint8_t identLength;	/* length of ident */
	uint8_t	ident[STMF_IDENT_LENGTH]; /* SCSI name string ident */
} stmfDevid;

typedef struct _stmfDevidList
{
	uint32_t cnt;
	stmfDevid devid[1];
} stmfDevidList;

typedef char stmfGroupName[256];
typedef char stmfProviderName[256];

typedef struct _stmfGroupList
{
	uint32_t cnt;
	stmfGroupName name[1];
} stmfGroupList;

typedef struct _stmfProvider
{
	int providerType;
	stmfProviderName name;
} stmfProvider;

typedef struct _stmfProviderList
{
	uint32_t cnt;
	stmfProvider provider[1];
} stmfProviderList;

typedef struct _stmfSession
{
	stmfDevid initiator;
	char alias[256];
	time_t creationTime;
} stmfSession;

typedef struct _stmfSessionList
{
	uint32_t cnt;
	stmfSession session[1];
} stmfSessionList;


typedef struct _stmfViewEntry
{
	boolean_t	veIndexValid;	/* if B_TRUE, veIndex is valid value */
	uint32_t	veIndex;	/* View Entry index */
	boolean_t	allHosts;	/* all initiator ports */
	stmfGroupName   hostGroup;	/* Host Group Name */
	boolean_t	allTargets;	/* B_TRUE = targetGroup is invalid */
	stmfGroupName	targetGroup;	/* Target Group Name */
	boolean_t	luNbrValid;	/* if B_TRUE, luNbr is a valid value */
	uchar_t		luNbr[8];	/* LU number for this view entry */
} stmfViewEntry;

typedef struct _stmfViewEntryList
{
	uint32_t cnt;
	stmfViewEntry ve[1];
} stmfViewEntryList;

typedef struct _stmfViewEntryProperties
{
	stmfGuid	associatedLogicalUnitGuid;
	stmfViewEntry	viewEntry;
} stmfViewEntryProperties;

typedef struct _stmfGroupProperties
{
	uint32_t	cnt;
	stmfDevid	name[1];
} stmfGroupProperties;

typedef struct _stmfTargetProperties
{
	stmfProviderName providerName;
	char		 alias[256];
	uint16_t	 status;
	stmfProtocol	 protocol;
	stmfDevid	 devid;
} stmfTargetProperties;

typedef struct _stmfLogicalUnitProperties
{
	char	    alias[256];
	uchar_t	    vendor[8];
	uchar_t	    product[16];
	uchar_t	    revision[4];
	uint32_t    status;
	char	    providerName[256];
	stmfGuid    luid;
} stmfLogicalUnitProperties;

typedef void * luResource;

typedef struct _stmfLogicalUnitProviderProperties
{
	char	    providerName[MAXPATHLEN];
	uint32_t    instance;
	uint32_t    status;
	uchar_t	    rsvd[64];
} stmfLogicalUnitProviderProperties;

typedef struct _stmfLocalPortProviderProperties
{
	char	    providerName[MAXPATHLEN];
	uint32_t    instance;
	uint32_t    status;
	uchar_t	    rsvd[64];
} stmfLocalPortProviderProperties;

/* API prototypes */
int stmfAddToHostGroup(stmfGroupName *hostGroupName, stmfDevid *name);
int stmfAddToTargetGroup(stmfGroupName *targetGroupName, stmfDevid *targetName);
int stmfAddViewEntry(stmfGuid *lu, stmfViewEntry *viewEntry);
int stmfClearProviderData(char *providerName, int providerType);
int stmfCreateHostGroup(stmfGroupName *hostGroupName);
int stmfCreateLu(luResource hdl, stmfGuid *luGuid);
int stmfCreateLuResource(uint16_t dType, luResource *hdl);
int stmfCreateTargetGroup(stmfGroupName *targetGroupName);
int stmfDeleteHostGroup(stmfGroupName *hostGroupName);
int stmfDeleteLu(stmfGuid *luGuid);
int stmfDeleteTargetGroup(stmfGroupName *targetGroupName);
void stmfDestroyProxyDoor(int hdl);
int stmfDevidFromIscsiName(char *iscsiName, stmfDevid *devid);
int stmfDevidFromWwn(uchar_t wwn[8], stmfDevid *devid);
int stmfFreeLuResource(luResource hdl);
void stmfFreeMemory(void *);
int stmfGetAluaState(boolean_t *enabled, uint32_t *node);
int stmfGetGlobalLuProp(uint16_t dType, uint32_t prop, char *propVal,
    size_t *propLen);
int stmfGetHostGroupList(stmfGroupList **initiatorGroupList);
int stmfGetHostGroupMembers(stmfGroupName *hostGroupName,
    stmfGroupProperties **groupProperties);
int stmfGetLocalPortProviderList(stmfProviderList **localPortProviderList);
int stmfGetLocalPortProviderProperties(stmfProviderName *providerName,
    stmfLocalPortProviderProperties *providerProperties);
int stmfGetLogicalUnitList(stmfGuidList **logicalUnitList);
int stmfGetLogicalUnitProperties(stmfGuid *logicalUnit,
    stmfLogicalUnitProperties *logicalUnitProps);
int stmfGetLogicalUnitProviderList(stmfProviderList **logicalUnitProviderList);
int stmfGetLogicalUnitProviderProperties(stmfProviderName *providerName,
    stmfLogicalUnitProviderProperties *providerProperties);
int stmfGetLuProp(luResource hdl, uint32_t propType, char *prop,
    size_t *propLen);
int stmfGetLuResource(stmfGuid *luGuid, luResource *hdl);
int stmfGetPersistMethod(uint8_t *persistType, boolean_t serviceState);
int stmfGetProviderData(char *providerName, nvlist_t **nvl, int providerType);
int stmfGetProviderDataProt(char *providerName, nvlist_t **nvl,
    int providerType, uint64_t *setToken);
int stmfGetSessionList(stmfDevid *target, stmfSessionList **sessionList);
int stmfGetState(stmfState *);
int stmfGetTargetGroupList(stmfGroupList **targetGroupList);
int stmfGetTargetGroupMembers(stmfGroupName *targetGroupName,
    stmfGroupProperties **groupProperties);
int stmfGetTargetList(stmfDevidList **targetList);
int stmfGetTargetProperties(stmfDevid *target,
    stmfTargetProperties *targetProps);
int stmfGetViewEntryList(stmfGuid *lu, stmfViewEntryList **viewEntryList);
int stmfImportLu(uint16_t dType, char *fname, stmfGuid *luGuid);
int stmfInitProxyDoor(int *hdl, int fd);
int stmfLoadConfig(void);
int stmfLuStandby(stmfGuid *luGuid);
int stmfModifyLu(stmfGuid *luGuid, uint32_t prop, const char *propVal);
int stmfModifyLuByFname(uint16_t dType, const char *fname, uint32_t prop,
    const char *propVal);
int stmfOffline(void);
int stmfOfflineTarget(stmfDevid *devid);
int stmfOfflineLogicalUnit(stmfGuid *logicalUnit);
int stmfOnline(void);
int stmfOnlineTarget(stmfDevid *devid);
int stmfOnlineLogicalUnit(stmfGuid *logicalUnit);
int stmfPostProxyMsg(int hdl, void *buf, uint32_t buflen);
int stmfRemoveFromHostGroup(stmfGroupName *hostGroupName,
    stmfDevid *initiatorName);
int stmfRemoveFromTargetGroup(stmfGroupName *targetGroupName,
    stmfDevid *targetName);
int stmfRemoveViewEntry(stmfGuid *lu, uint32_t viewEntryIndex);
int stmfSetAluaState(boolean_t enabled, uint32_t node);
int stmfSetGlobalLuProp(uint16_t dType, uint32_t propType, const char *propVal);
int stmfSetLuProp(luResource hdl, uint32_t propType, const char *propVal);
int stmfSetPersistMethod(uint8_t persistType, boolean_t serviceSet);
int stmfSetProviderData(char *providerName, nvlist_t *nvl, int providerType);
int stmfSetProviderDataProt(char *providerName, nvlist_t *nvl,
    int providerType, uint64_t *setToken);
int stmfValidateView(stmfViewEntry *viewEntry);
int stmfSetStmfProp(uint8_t propType, char *propVal);
int stmfGetStmfProp(uint8_t propType, char *propVal, size_t *propLen);
int stmfLoadStmfProps(void);
int stmfCheckTargetGroupInUse(stmfGroupName *groupName, boolean_t *inUse);
int stmfCheckHostGroupInUse(stmfGroupName *groupName, boolean_t *inUse);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBSTMF_H */
