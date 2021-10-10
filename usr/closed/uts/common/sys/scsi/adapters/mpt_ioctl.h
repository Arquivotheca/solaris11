/*
 * Copyright (c) 2006, 2008, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *  Copyright 1998 LSI Logic Corporation.  All rights reserved.
 *
 *  This file is confidential and a trade secret of LSI Logic.  The
 *  receipt of or possession of this file does not convey any rights to
 *  reproduce or disclose its contents or to manufacture, use, or sell
 *  anything it may describe, in whole, or in part, without the specific
 *  written consent of LSI Logic Corporation.
 */

#ifndef _SYS_SCSI_ADAPTERS_MPT_IOCTL_H
#define	_SYS_SCSI_ADAPTERS_MPT_IOCTL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#define	MPTIOCTL		('H' << 8)
#define	MPTIOCTL_GET_DMI_DATA	(MPTIOCTL | 1)
#define	MPTIOCTL_GET_TIMERS	(MPTIOCTL | 2)
#define	MPTIOCTL_UPDATE_FLASH	(MPTIOCTL | 3)
#define	MPTIOCTL_RESET_ADAPTER	(MPTIOCTL | 4)
#define	MPTIOCTL_GET_PROPERTY	(MPTIOCTL | 5)
#define	MPTIOCTL_PASS_THRU	(MPTIOCTL | 6)
#define	MPTIOCTL_REG_ACCESS	(MPTIOCTL | 7)
#define	MPTIOCTL_EVENT_QUERY	(MPTIOCTL | 8)
#define	MPTIOCTL_EVENT_ENABLE	(MPTIOCTL | 9)
#define	MPTIOCTL_EVENT_REPORT	(MPTIOCTL | 10)

/*
 *  The following are our ioctl() return status values.  If everything went
 *  well, we return good status.  If the buffer length sent to us is too short
 *  we return a status to tell the user.
 */
#define	MPTIOCTL_STATUS_GOOD	0
#define	MPTIOCTL_STATUS_LEN_TOO_SHORT	1

/*
 *  The data structures defined in this file are marked with a data structure
 *  length and data structure version.  The length value is the first value in
 *  the structure and is used to make sure the buffer is large enough for
 *  communication between the driver and the application.  The version number is
 *  a major version number.  If the data structure changes and only has a new
 *  element appended, then the version number will remain the same but the
 *  length will increase.  If the data structure changes in any other way, such
 *  as re-arranging all of its elements, then the version number will increase.
 *
 *  The following macro defines the current version number of the data
 *  structure.
 */
#define	MPT_DMI_DATA_VERSION	1

/*
 *  The following is the MPTIOCTL_GET_DMI_DATA data structure.  This data
 *  structure is setup so that we hopefully are properly aligned for both 32-bit
 *  and 64-bit mode applications.
 *
 *  StructureLength - This value is the amount of data the caller has allocated
 *  for the structure when they first call us.  After we have filled in the
 *  structure, this indicates the length our data structure is.
 *
 *  MajorVersion - This value is used by the driver to tell the application what
 *  version of the data structure is being provided.  It only changes if the
 *  data ordering of the data below is changed.
 *
 *  MinSyncPeriodNs - This is the minimum period in nano-seconds (ns) that we
 *  will negotiate for on this adapter.  The smaller value the faster
 *  synchronous speed except if this value is zero then asynchronous transfers
 *  is all we support.
 *
 *  MaxWidth - This value indicates the maximum width this bus can be used as.
 *  If the PciDeviceId indicates a width capability of 16 devices and this is
 *  set to 8 devices then the PCI Subsystem ID value has limited our use of this
 *  adapter to 8 devices.  This value also indicates the number of valid
 *  elements in the DevSpeed[] and DevWidth[] array's.
 *
 *  HostScsiId - This is the host adapter SCSI ID being used by this adapter.
 *
 *  PciBusNumber - The number of the PCI bus this adapter is on.  If for some
 *  reason the driver is unable to determine the bus number, device number, or
 *  function number, these values will be set to 0xFF.
 *
 *  PciDeviceNumber - The PCI device number for this device.
 *
 *  PciFunctionNumber - The PCI function number for this device.
 *
 *  PciDeviceId - This is the PCI device ID from PCI configuration space for
 *  this adapter.
 *
 *  PciRevision - This is the PCI revision value from PCI configuration space
 *  for this adapter.
 *
 *  HwBusMode - This value indicates the mode the bus is currently in.  See the
 *  MPT_HW_BUS_MODE_xx macros.
 *
 *  DevSpeed - This array is indexed by the target ID and indicates the
 *  currently negotiated synchronous speed in nano-seconds (ns).  A value of
 *  zero ns indicates asynchronous mode.
 *
 *  DevWidth - This array is indexed by the target ID and indicates the
 *  currently negotiated width in bits.  A value of 8 indicates narrow mode, a
 *  value of 16 indicates wide.
 *
 *  DriverVersion - This is an ascii null-terminated string indicating the
 *  version of this driver.
 *
 *  DevFlags - This array is indexed by the target ID and indicates the
 *  currently negotiated options such as DT and async protection capabilities.
 */

typedef struct mpt_dmi_data
{
	uint32_t	StructureLength; /* 0x00..0x03 */
	uint32_t	Reserved1; /* 0x04..0x07 */
	uint32_t	MajorVersion; /* 0x08..0x0B */
	uint16_t	MinSyncPeriodNs; /* 0x0C..0x0D */
	uint8_t		MaxWidth; /* 0x0E */
	uint8_t		HostScsiId; /* 0x0F */
	uint8_t		PciBusNumber; /* 0x10 */
	uint8_t		PciDeviceNumber; /* 0x11 */
	uint8_t		PciFunctionNumber; /* 0x12 */
	uint8_t		Reserved2; /* 0x13 */
	uint16_t	PciDeviceId; /* 0x14..0x15 */
	uint8_t		PciRevision; /* 0x16 */
	uint8_t		HwBusMode; /* 0x17 */
	uint8_t		Reserved3[8];  /* 0x18..0x1F */
	uint16_t	DevSpeed[256]; /* 0x20..0x21F */
	uint8_t		DevWidth[256]; /* 0x220..0x31F */
	uint32_t	DevFlags[256]; /* 0x320..0x71F */
	char		DriverVersion[80]; /* 0x720..0x76F */
} mpt_dmi_data_t;

typedef struct mpt_get_property
{
	uint64_t	PtrName;
	uint64_t	PtrBuffer;
	uint32_t	NameLen;
	uint32_t	BufferLen;
	uint32_t	PropertyLen;
} mpt_get_property_t;

#define	MPT_PASS_THRU_NONE	0
#define	MPT_PASS_THRU_READ	1
#define	MPT_PASS_THRU_WRITE	2
#define	MPT_PASS_THRU_BOTH	3

typedef struct mpt_pass_thru
{
	uint64_t	PtrRequest;
	uint64_t	PtrReply;
	uint64_t	PtrData;
	uint32_t	RequestSize;
	uint32_t	ReplySize;
	uint32_t	DataSize;
	uint32_t	DataDirection;
	uint64_t	PtrDataOut;
	uint32_t	DataOutSize;
} mpt_pass_thru_t;

typedef struct mpt_event_query
{
	uint32_t	Entries;
	uint32_t	Types;
} mpt_event_query_t;

typedef struct mpt_event_enable
{
	uint32_t	Types;
} mpt_event_enable_t;

/*
 * Event record entry for ioctl.
 */
typedef struct mpt_event_entry
{
	uint32_t	Type;
	uint32_t	Number;
	uint32_t	Data[2];
} mpt_event_entry_t;

typedef struct mpt_event_report
{
	uint32_t		Size;
	mpt_event_entry_t	Events[1];
} mpt_event_report_t;

/*
 * The number of entries in event queue
 */
#define	HW_NUM_EVENT_ENTRIES	(50)

/*
 * Only event types from 0 - 31 should be recorded.
 */
#define	HW_NUM_EVENT_TYPES	(32)

#define	REG_IO_READ	1
#define	REG_IO_WRITE	2
#define	REG_MEM_READ	3
#define	REG_MEM_WRITE	4

typedef struct mpt_reg_access
{
	uint32_t	Command;
	uint32_t	RegOffset;
	uint32_t	RegData;
} mpt_reg_access_t;

#define	MPT_FLAGS_DT		0x0001
#define	MPT_FLAGS_ASYNC_PROT	0x0002
#define	MPT_FLAGS_AIP		MPT_FLAGS_ASYNC_PROT
#define	MPT_FLAGS_PPR		0x0004
#define	MPT_FLAGS_IU		0x0008
#define	MPT_FLAGS_IDP		0x0010
#define	MPT_PASSTHRU_TIMEOUT(timeout)	(ddi_get_lbolt() + \
    drv_usectohz(timeout * MICROSEC))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_MPT_IOCTL_H */
