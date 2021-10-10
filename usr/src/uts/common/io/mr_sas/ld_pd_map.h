/*
 * ld_pd_map.h
 *
 * Copyright (C) LSI Corp, Corporation, 2009-2010
 *
 */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * define command opcodes
 */
#ifndef INCLUDE_LD_PD_MAP
#define	INCLUDE_LD_PD_MAP
#include <sys/scsi/scsi.h>
#include "fusion.h"

/* raid->write_mode; raid->read_ahead; dcmd->state */
/* Write through */
#define	WRITE_THROUGH				0
/* Delayed Write */
#define	WRITE_BACK				1
#define	READ_10					0x28
#define	READ_16					0x88
#define	WRITE_10				0x2a
#define	WRITE_16				0x8a

/* maximum disks per array */
#define	MAX_ROW_SIZE				32

/* maximum spans per logical drive */
#define	MAX_SPAN_DEPTH				8
#define	MEGASAS_LOAD_BALANCE_FLAG		0x1
#define	MR_DEFAULT_IO_TIMEOUT			20

#pragma pack(1)

typedef struct _LD_LOAD_BALANCE_INFO
{
    U8	loadBalanceFlag;
    U8	reserved1;
    U16	raid1DevHandle[2];
    U16	scsi_pending_cmds[2];
    U64	last_accessed_block[2];
} LD_LOAD_BALANCE_INFO, *PLD_LOAD_BALANCE_INFO;

typedef struct _MR_FW_RAID_MAP_ALL {
	MR_FW_RAID_MAP		raidMap;
	MR_LD_SPAN_MAP		ldSpanMap[MAX_LOGICAL_DRIVES - 1];
	LD_LOAD_BALANCE_INFO	load_balance_info[MAX_LOGICAL_DRIVES];
} MR_FW_RAID_MAP_ALL;
/*
 * Raid Context structure which describes MegaRAID specific IO Paramenters
 * This resides at offset 0x60 where the SGL normally starts in MPT IO Frames
 */
typedef struct _MPI2_SCSI_IO_VENDOR_UNIQUE {
	U16	reservedForHw0;		/* 0x00 -0x01  */
	U16	timeoutValue;		/* 0x02 -0x03  */
	U8	regLockFlags;		/* 0x04 */
	U8	reservedForHw1;		/* 0x05 */
	U16	ldTargetId;		/* 0x06 - 0x07 */
	U64	regLockRowLBA;		/* 0x08 - 0x0F */
	U32	regLockLength;		/* 0x10 - 0x13 */
	U16	nextLMId;		/* 0x14 - 0x15 */
	U8	extStatus;		/* 0x16 */
	U8	status;			/* 0x17 status */
	/*
	 * 0x18 resvd[7:6],
	 * ioSubType[5:4],
	 * resvd[3:1],
	 * preferredCpu[0]
	 */
	U8	RAIDFlags;
	/*
	 * 0x19 numSge;
	 * not including
	 * chain entries
	 */
	U8	numSGE;
	U16	configSeqNum;		/* 0x1A -0x1B */
	U8	spanArm;		/* 0x1C span[7:5], arm[4:0] */
	U8	resvd2[3];		/* 0x1D-0x1f */
} MPI2_SCSI_IO_VENDOR_UNIQUE, MPI25_SCSI_IO_VENDOR_UNIQUE;

#define	RAID_CTX_SPANARM_ARM_SHIFT	(0)
#define	RAID_CTX_SPANARM_ARM_MASK	(0x1f)

#define	RAID_CTX_SPANARM_SPAN_SHIFT	(5)
#define	RAID_CTX_SPANARM_SPAN_MASK	(0xE0)


/*
 * RAID SCSI IO Request Message
 * Total SGE count will be one less
 * than  _MPI2_SCSI_IO_REQUEST
 */
typedef struct _MPI2_RAID_SCSI_IO_REQUEST
{
	uint16_t		DevHandle;			/* 0x00 */
	uint8_t			ChainOffset;			/* 0x02 */
	uint8_t			Function;			/* 0x03 */
	uint16_t		Reserved1;			/* 0x04 */
	uint8_t			Reserved2;			/* 0x06 */
	uint8_t			MsgFlags;			/* 0x07 */
	uint8_t			VP_ID;				/* 0x08 */
	uint8_t			VF_ID;				/* 0x09 */
	uint16_t		Reserved3;			/* 0x0A */
	uint32_t		SenseBufferLowAddress;		/* 0x0C */
	uint16_t		SGLFlags;			/* 0x10 */
	uint8_t			SenseBufferLength;		/* 0x12 */
	uint8_t			Reserved4;			/* 0x13 */
	uint8_t			SGLOffset0;			/* 0x14 */
	uint8_t			SGLOffset1;			/* 0x15 */
	uint8_t			SGLOffset2;			/* 0x16 */
	uint8_t			SGLOffset3;			/* 0x17 */
	uint32_t		SkipCount;			/* 0x18 */
	uint32_t		DataLength;			/* 0x1C */
	uint32_t		BidirectionalDataLength;	/* 0x20 */
	uint16_t		IoFlags;			/* 0x24 */
	uint16_t		EEDPFlags;			/* 0x26 */
	uint32_t		EEDPBlockSize;			/* 0x28 */
	uint32_t		SecondaryReferenceTag;		/* 0x2C */
	uint16_t		SecondaryApplicationTag;	/* 0x30 */
	uint16_t		ApplicationTagTranslationMask;	/* 0x32 */
	uint8_t			LUN[8];				/* 0x34 */
	uint32_t		Control;			/* 0x3C */
	Mpi2ScsiIoCdb_t		CDB;				/* 0x40 */
	MPI2_SCSI_IO_VENDOR_UNIQUE	RaidContext;		/* 0x60 */
	Mpi2SGEIOUnion_t	SGL; /* 0x80 */
} MPI2_RAID_SCSI_IO_REQUEST, MPI2_POINTER PTR_MPI2_RAID_SCSI_IO_REQUEST,
Mpi2RaidSCSIIORequest_t, MPI2_POINTER pMpi2RaidSCSIIORequest_t;

/*
 * define region lock types
 */
typedef enum	_REGION_TYPE {
	REGION_TYPE_UNUSED	= 0,	/* lock is currently not active */
	REGION_TYPE_SHARED_READ	= 1,	/* shared lock (for reads) */
	REGION_TYPE_SHARED_WRITE = 2,
	REGION_TYPE_EXCLUSIVE	= 3	/* exclusive lock (for writes) */
} REGION_TYPE;

struct QUAD_ELEMENT {
	uint64_t	log_start;
	uint64_t	log_end;
	uint64_t	offset_in_span;
	uint32_t	diff;
	uint32_t	reserved;	/* for alignment */
};

struct SPAN_INFO {
	uint32_t	no_elements;
	struct		QUAD_ELEMENT quad_t[MAX_SPAN_DEPTH];
};

struct LOG_BLOCK_SPAN_INFO {
	struct	SPAN_INFO span_info[MAX_SPAN_DEPTH];
};

typedef struct LD_SPAN {		/* SPAN structure */
	uint64_t	start_blk;	/* starting block number in array */
	uint64_t	num_blks;	/* number of blocks */
	uint16_t	arrayRef;	/* array reference */
	uint8_t		reserved[6];
} LD_SPAN;

#define	DM_PATH_MAXPATH		2
#define	DM_PATH_FIRSTPATH	0
#define	DM_PATH_SECONDPATH	1

/* declare valid Region locking values */
typedef enum _REGION_LOCK {
	REGION_LOCK_BYPASS		= 0,
	/* for RAID 6 single-drive failure */
	REGION_LOCK_UNCOND_SHARED_READ	= 1,
	REGION_LOCK_UNCOND_SHARED_WRITE	= 2,
	REGION_LOCK_UNCOND_SHARED_OTHER	= 3,
	REGION_LOCK_UNCOND_SHARED_EXCLUSIVE = 0xFF
} REGION_LOCK;

typedef struct _RAID_CTX_PD_INFO {
	MPI2_SCSI_IO_VENDOR_UNIQUE RaidContext;
	uint64_t PhysicalStartBlock;
	uint8_t fp_possible;
} RAID_CTX_PD_INFO;

typedef struct _LD_PD_MAP_INFO {
	uint8_t	span_depth;	/* Total Number of Spans */
	uint8_t	level;		/* RAID level */
	/* shift-count to get stripe size (0=512, 1=1K, 7=64K, etc.) */
	uint8_t	stripe_shift;
	uint8_t	rowSize;	/* number of disks in a row */
	uint8_t	rowDataSize;	/* number of data disks in a row */

	uint8_t	write_mode;	/* WRITE_THROUGH or WRITE_BACK */

	uint8_t	PRL;		/* To differentiate between RAID1 and RAID1E */
	uint8_t	SRL;

	/*
	 * Security bits
	 */
	struct {
		/* FDE or controller encryption (MR_LD_ENCRYPTION_TYPE) */
		uint8_t	encryptionType;
	} security;

	struct {
		uint8_t	pdPiMode:4;	/* Every Pd has to be same. */
		uint8_t	ldPiMode:4;
	} dif;
	/* mod factor for data/parity calculations */
	uint8_t	modFactor;
	/*
	 * arm type info (used in GET_ARM macro)
	 * TO CHECK AGAIN with Architects
	 */
	uint8_t	arms[MAX_ROW_SIZE];

	struct {
		uint16_t	devHandle[DM_PATH_MAXPATH];
		uint8_t		validHandles;
		uint8_t		curDevHdl;
		uint8_t		isSSD:1;
		uint8_t		isSata:1;
	} devInfo[MAX_SPAN_DEPTH][MAX_ROW_SIZE];

	uint64_t  num_rows[MAX_SPAN_DEPTH];	/* number of rows/span */
	LD_SPAN span[MAX_SPAN_DEPTH];		/* 16 + 8*24 = 208 bytes */

	struct  SPAN_INFO span_info[MAX_SPAN_DEPTH];
/*
 * We may not need it. Check with architects
 * list of PDs in this array (PD_INVALID (FF) = missing)
 * TO CHECK AGAIN with Architects
 */
	uint8_t   pd[MAX_SPAN_DEPTH][MAX_ROW_SIZE];
} LD_PD_MAP_INFO;


struct mrsas_init_frame2 {
	uint8_t	cmd;				/* 00h */
	uint8_t reserved_0;			/* 01h */
	uint8_t cmd_status;			/* 02h */

	uint8_t reserved_1;			/* 03h */
	uint32_t reserved_2;			/* 04h */

	uint32_t context;			/* 08h */
	uint32_t pad_0;				/* 0Ch */

	uint16_t flags;				/* 10h */
	uint16_t reserved_3;			/* 12h */
	uint32_t data_xfer_len;			/* 14h */

	uint32_t queue_info_new_phys_addr_lo;	/* 18h */
	uint32_t queue_info_new_phys_addr_hi;	/* 1Ch */
	uint32_t queue_info_old_phys_addr_lo;	/* 20h */
	uint32_t queue_info_old_phys_addr_hi;	/* 24h */
	uint64_t driverversion;
	uint32_t reserved_4[6];			/* 28h */
};


/* TODO find the right definition */
#define	XXX_MFI_CMD_OP_INIT2			0x9

/*
 * Request descriptor types
 */
#define	MPI2_REQ_DESCRIPT_FLAGS_LD_IO		0x7
#define	MPI2_REQ_DESCRIPT_FLAGS_MFA		0x1

#define	MPI2_REQ_DESCRIPT_FLAGS_TYPE_SHIFT	1

/*
 * TODO Following are taken from mpi2.h
 * and it is well beyond uint8_t RequestDescriptorType :3; can hold
 * #define	MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE		0x08
 * #define	MPI2_REQ_DESCRIPT_FLAGS_RAID_ACCELERATOR	0x0A
 */
/*
 * MPT RAID MFA IO Descriptor.
 */
typedef struct _XXX_RAID_MFA_IO_DESCRIPTOR {
	uint32_t	RequestFlags : 8;
	uint32_t	MessageAddress1 : 24;	/* bits 31:8 */
	uint32_t	MessageAddress2;	/* bits 61:32 */
} XXX_RAID_MFA_IO_REQUEST_DESCRIPTOR,
*PXXX_RAID_MFA_IO_REQUEST_DESCRIPTOR;

/* union of Request Descriptors */
typedef union _MRSAS_REQUEST_DESCRIPTOR_UNION
{
	MPI2_DEFAULT_REQUEST_DESCRIPTOR		Default;
	MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR	HighPriority;
	MPI2_SCSI_IO_REQUEST_DESCRIPTOR		SCSIIO;
	MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR	SCSITarget;
	MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR	RAIDAccelerator;
	XXX_RAID_MFA_IO_REQUEST_DESCRIPTOR	MFAIo;
	MRU64					Words;
} MRSAS_REQUEST_DESCRIPTOR_UNION;


enum {
	MRSAS_SCSI_VARIABLE_LENGTH_CMD		= 0x7F,
	MRSAS_SCSI_SERVICE_ACTION_READ32	= 0x9,
	MRSAS_SCSI_SERVICE_ACTION_WRITE32	= 0xB,
	MRSAS_SCSI_ADDL_CDB_LEN			= 0x18,
	MRSAS_RD_WR_PROTECT			= 0x20,
	MRSAS_EEDPBLOCKSIZE			= 512
};
#pragma pack()
#define	IEEE_SGE_FLAGS_ADDR_MASK	(0x03)
#define	IEEE_SGE_FLAGS_SYSTEM_ADDR	(0x00)
#define	IEEE_SGE_FLAGS_IOCDDR_ADDR	(0x01)
#define	IEEE_SGE_FLAGS_IOCPLB_ADDR	(0x02)
#define	IEEE_SGE_FLAGS_IOCPLBNTA_ADDR	(0x03)
#define	IEEE_SGE_FLAGS_CHAIN_ELEMENT	(0x80)
#define	IEEE_SGE_FLAGS_END_OF_LIST	(0x40)

#ifndef KMDB_MODULE
U8	MR_BuildRaidContext(struct IO_REQUEST_INFO *,
	    MPI2_SCSI_IO_VENDOR_UNIQUE *, MR_FW_RAID_MAP_ALL *);
U8	MR_ValidateMapInfo(MR_FW_RAID_MAP_ALL *map,
    PLD_LOAD_BALANCE_INFO lbInfo);
MR_LD_RAID *MR_LdRaidGet(U32 ld, MR_FW_RAID_MAP_ALL *map);
U16 MR_TargetIdToLdGet(U32 ldTgtId, MR_FW_RAID_MAP_ALL *map);
U16 MR_GetLDTgtId(U32 ld, MR_FW_RAID_MAP_ALL *map);
U16 get_updated_dev_handle(PLD_LOAD_BALANCE_INFO lbInfo,
    struct IO_REQUEST_INFO *in_info);
#endif /* KMDB_MODULE */
#endif /* INCLUDE_LD_PD_MAP */
