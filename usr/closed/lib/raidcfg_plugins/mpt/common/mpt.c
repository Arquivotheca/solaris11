/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stropts.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <fcntl.h>
#include <libintl.h>
#include <libdevinfo.h>
#include <devid.h>
#include <sys/libdevid.h>
#include <config_admin.h>
#include <sys/byteorder.h>
#include <sys/pci.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>
#include <sys/errno.h>
#include <sys/mnttab.h>
#include <sys/scsi/impl/uscsi.h>
#include <sys/scsi/generic/sense.h>
#include <sys/scsi/generic/inquiry.h>
#include <sys/scsi/impl/sense.h>
#include <sys/scsi/generic/commands.h>

#include <raidcfg.h>
#include <raidcfg_spi.h>
#include <sys/raidioctl.h>

#pragma pack(1)
#include <sys/scsi/adapters/mpt_ioctl.h>
#include <sys/mpt/mpi.h>
#include <sys/mpt/mpi_ioc.h>
#include <sys/mpt/mpi_cnfg.h>
#include <sys/mpt/mpi_init.h>
#include <sys/mpt/mpi_raid.h>
#include <sys/scsi/adapters/mptreg.h>
#pragma pack()

#ifndef TEXT_DOMAIN
#define	TEXT_DOMAIN	"SYS_TEST"
#endif

/*
 * define raid status flags
 */
#define	MPT_RAID_FLAG_ENABLED	0x01
#define	MPT_RAID_FLAG_QUIESCED	0x02
#define	MPT_RAID_FLAG_RESYNCING	0x04
#define	MPT_RAID_STATE_OPTIMAL	0x00
#define	MPT_RAID_STATE_DEGRADED	0x01
#define	MPT_RAID_STATE_FAILED	0x02
#define	MPT_RAID_STATE_MISSING	0x03

/*
 * define disk status flags
 */
#define	MPT_RAID_DISKSTATUS_GOOD	0x0
#define	MPT_RAID_DISKSTATUS_FAILED	0x1
#define	MPT_RAID_DISKSTATUS_MISSING	0x2

/*
 * define raid level status flags
 */
#define	MPT_RAID_VOL_TYPE_IS	0x01
#define	MPT_RAID_VOL_TYPE_IME	0x02
#define	MPT_RAID_VOL_TYPE_IM	0x04

/*
 * define raid volume size shift bits
 */
#define	MPT_MAX_VOLUME_SIZE_SHIFT_BIT	11

/*
 * define sas device address
 */
#define	MPT_SAS_ADDR(x)	((MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID \
			<< MPI_SAS_DEVICE_PGAD_FORM_SHIFT) \
			| (x << MPI_SAS_DEVICE_PGAD_BT_TID_SHIFT))

/*
 * RAID Volume 64 bit addressing support
 */
#define	MPT_RAID_CAP_LBA64	(1 << 28)

/*
 * Values for the SAS DeviceInfo field used in SAS Device Status Change Event
 * data and SAS IO Unit Configuration pages.
 */
#define	MPI_SAS_DEVICE_INFO_PRODUCT_SPECIFIC	(0xF0000000)
#define	MPI_SAS_DEVICE_INFO_SEP			(0x00004000)
#define	MPI_SAS_DEVICE_INFO_ATAPI_DEVICE	(0x00002000)
#define	MPI_SAS_DEVICE_INFO_LSI_DEVICE		(0x00001000)
#define	MPI_SAS_DEVICE_INFO_DIRECT_ATTACH	(0x00000800)
#define	MPI_SAS_DEVICE_INFO_SSP_TARGET		(0x00000400)
#define	MPI_SAS_DEVICE_INFO_STP_TARGET		(0x00000200)
#define	MPI_SAS_DEVICE_INFO_SMP_TARGET		(0x00000100)
#define	MPI_SAS_DEVICE_INFO_SATA_DEVICE		(0x00000080)
#define	MPI_SAS_DEVICE_INFO_SSP_INITIATOR	(0x00000040)
#define	MPI_SAS_DEVICE_INFO_STP_INITIATOR	(0x00000020)
#define	MPI_SAS_DEVICE_INFO_SMP_INITIATOR	(0x00000010)
#define	MPI_SAS_DEVICE_INFO_SATA_HOST		(0x00000008)

#define	MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE	(0x00000007)
#define	MPI_SAS_DEVICE_INFO_NO_DEVICE		(0x00000000)
#define	MPI_SAS_DEVICE_INFO_END_DEVICE		(0x00000001)
#define	MPI_SAS_DEVICE_INFO_EDGE_EXPANDER	(0x00000002)
#define	MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER	(0x00000003)

#define	MPT_MPXIO_ENABLE	1
#define	MPT_MPXIO_DISABLE	0

#define	SATA_DISK		1
#define	NOT_SATA_DISK		0
#define	SSD_DISK		1
#define	NOT_SSD_DISK		0

/*
 * These values are based on values we program the IOC.
 */
#define		BUS_SHIFT		16
#define		LUN_SHIFT		8
#define		DISK_MASK		((1 << LUN_SHIFT) - 1)

#define		DISK_ID(b, t, l)	((b << BUS_SHIFT) \
					| (l) << LUN_SHIFT | t)
#define		BUS(disk_id)		((disk_id) >> BUS_SHIFT)
#define		TARGET(disk_id)		((disk_id) & DISK_MASK)
#define		LUN(disk_id)		(((disk_id) >> LUN_SHIFT) & DISK_MASK)

#define		ARRAY_SHIFT		16
#define		ARRAY_MASK		((1 << ARRAY_SHIFT) - 1)

#define		ARRAY_ID(id, l)		((l) << ARRAY_SHIFT | (id))
#define		ARRAY_TARGET(array_id)	((array_id) & ARRAY_MASK)
#define		ARRAY_LUN(array_id)	((array_id) >> ARRAY_SHIFT)

/*
 * FW Update Stuff
 */
#pragma pack(1)
typedef struct mpt_pcir {
	uint8_t		signature[4];
	uint16_t	vendorId;
	uint16_t	deviceId;
	uint8_t		reserved1[2];
	uint16_t	pcirLength;
	uint8_t		pcirRevision;
	uint8_t		classCode[3];
	uint16_t	imageLength;
	uint16_t	imageRevision;
	uint8_t		type;
	uint8_t		indicator;
	uint8_t		reserved2[2];
} pcir_t;
#pragma pack()

/* signature and initial offset for PCI expansion rom images */
#define	PCIROM_SIG	0xaa55	/* offset 0h, length 2 bytes */
#define	PCIR_OFF	0x18	/* Pointer to PCI Data Structure */

/* offsets in PCI data structure header */
#define	PCIR_DEVID	0x6	/* PCI device id */
#define	PCIR_CODETYPE   0x14	/* type of code (intel/fcode) */
#define	PCIR_INDICATOR  0x15	/* "last image" indicator */

/* flags for image types */
#define	BIOS_IMAGE	0x1
#define	FCODE_IMAGE	0x2
#define	UNKNOWN_IMAGE	0x3
#define	LAST_IMAGE	0x80
#define	NOT_LAST_IMAGE	0
#define	PCI_IMAGE_UNIT_SIZE	512

/* ID's and offsets for MPT Firmware images */
#define	FW_ROM_ID			0x5aea	/* bytes 4 & 5 of file */
#define	FW_ROM_OFFSET_CHIP_TYPE		0x22	/* (U16) */
#define	FW_ROM_OFFSET_VERSION		0x24	/* (U16) */
#define	FW_ROM_OFFSET_VERSION_NAME	0x44	/* (32 U8) */

/* Key to search for when looking for fcode version */
#define	FCODE_VERS_KEY1		0x12
#define	FCODE_VERS_KEY2		0x7
#define	BIOS_STR		"LSI SCSI Host Adapter BIOS Driver: "

/* get a word from a buffer (works with non-word aligned offsets) */
#define	gw(x) (((x)[0]) + (((x)[1]) << 8))

#define	MPT_DIRECT_MAPPING	MPI_SAS_IOUNIT2_FLAGS_DIRECT_ATTACH_PHYS_MAP
#define	MPT_SLOT_MAPPING	MPI_SAS_IOUNIT2_FLAGS_ENCLOSURE_SLOT_PHYS_MAP

#define	DEVICESDIR	"/devices/"

#define	DSKDIR	"/dev/rdsk"
#define	MPT_MAX_DISKS_IN_RAID	8
#define	MPT_MAX_RAIDVOLS	2
#define	MPT_MAX_SEG_PER_DISK	2

#define	DISK_BLK_SIZE		512

#define	INVALID_GUID		"\0"

#define	get2bytes(x, y)	(((x[y] << 8) + x[y+1]) & 0xffff)
#define	get4bytes(x, y)	(((x[y] << 24) + (x[y+1] << 16) + \
			(x[y+2] << 8) + x[y+3]) & 0xffffffff)

typedef struct mpt_disklist {
	int m_ndisks;
	uint32_t m_diskid[MPT_MAX_DISKS_IN_RAID];
} disklist_t;

typedef struct mpt_raidvols {
	ushort_t	m_israid;
	uint8_t		m_raidtarg;
	uint8_t		m_state;
	uint8_t		m_flags;
	uint32_t	m_diskid[MPT_MAX_DISKS_IN_RAID];
	uint8_t		m_disknum[MPT_MAX_DISKS_IN_RAID];
	ushort_t	m_diskstatus[MPT_MAX_DISKS_IN_RAID];
	uint32_t	m_pathid[MPT_MAX_DISKS_IN_RAID];
	ushort_t	m_raidbuilding;
	uint64_t	m_raidsize;
	int		m_raidlevel;
	int		m_ndisks;
	int		m_settings;
	int		m_stripesize;
} raidvol_t;

static int mpt_do_command(uint32_t, void *, int, void *, int,
	void *, int, void *, int);
static int mpt_get_iocfacts(uint32_t, msg_ioc_facts_reply_t *);
static int mpt_do_scsi_io(uint32_t, void *, int, msg_scsi_io_reply_t *,
	int, void *, int, void *, int);
static int mpt_do_inquiry(uint32_t, int, int, int, unsigned char *, int);
static int mpt_do_inquiry_VpdPage(uint32_t, int, int, int, int,
	unsigned char *, int);
static int mpt_get_configpage_header(uint32_t, uint8_t, uint8_t,
	uint32_t, msg_config_reply_t *);
static int mpt_get_configpage_length(uint32_t, uint8_t, uint8_t,
	uint32_t, int *);
static int mpt_get_configpage(uint32_t, uint8_t, uint8_t,
	uint32_t, void *, int);
static int mpt_get_compinfo(uint32_t, uint32_t, uint32_t,
	uint32_t, uint32_t *, void *);
static int mpt_get_attr(uint32_t, uint32_t,
	uint32_t, raid_obj_type_id_t, void *);
static int mpt_get_raidinfo(uint32_t);
static int mpt_get_deviceid(uint32_t, uint16_t *, uint16_t *, uint16_t *);
static int mpt_get_max_volume_num(uint32_t, int *);
static int mpt_create_physdisk(uint32_t, uint16_t, int *);
static int mpt_create_volume(uint32_t, disklist_t *, uint8_t, int, uint32_t);
static int mpt_raid_dev_config(cfga_cmd_t, uint32_t, uint32_t, uint8_t);
static int mpt_do_image_fixing(uint32_t, unsigned char *, int, int);
static int mpt_controller_id_to_path(uint32_t, char *);
static int mpt_disk_attr(uint32_t, disk_attr_t *);
static int mpt_disk_hsp_attr(uint32_t, disk_attr_t *);
static int mpt_disk_prop_attr(uint32_t, property_attr_t *);
static int mpt_hsp_attr(uint32_t, uint32_t, hsp_attr_t *);
static int mpt_disk_info_guid(uint32_t, uint32_t, char *);
static int mpt_get_disk_ids(uint32_t, int, uint32_t *);
static int mpt_get_disk_info(uint32_t, disk_attr_t *);
static uint64_t mpt_get_comp_capacity(uint32_t, uint32_t);
static int mpt_array_disk_info(uint32_t, uint32_t, disk_attr_t *);
static int mpt_raid_dev_unmounted(uint32_t, uint32_t);

int rdcfg_open_controller(uint32_t, char **);
int rdcfg_close_controller(uint32_t, char **);
int rdcfg_compnum(uint32_t, uint32_t, raid_obj_type_id_t,
	raid_obj_type_id_t);
int rdcfg_complist(uint32_t, uint32_t, raid_obj_type_id_t,
	raid_obj_type_id_t, int, void *);
int rdcfg_get_attr(uint32_t, uint32_t, uint32_t,
	raid_obj_type_id_t, void *);
int rdcfg_set_attr(uint32_t, uint32_t, uint32_t, uint32_t *, char **);
int rdcfg_array_create(uint32_t, array_attr_t *, int,
	arraypart_attr_t *, char **);
int rdcfg_array_delete(uint32_t, uint32_t, char **);
int rdcfg_hsp_bind(uint32_t, hsp_relation_t *, char **);
int rdcfg_hsp_unbind(uint32_t, hsp_relation_t *, char **);
int rdcfg_flash_fw(uint32_t, char *, uint32_t, char **);

static int mpt_controller_id_to_fd(uint32_t);
static int mpt_disks_compinfo(uint32_t, int, uint32_t *);
static int mpt_props_compinfo(uint32_t, uint32_t, uint32_t *);
static int mpt_hsp_compinfo(uint32_t, uint32_t, uint32_t *);
static int mpt_disks_in_hotspare(uint32_t, uint32_t *);
static int mpt_hsp_in_volume(uint32_t, uint8_t, uint8_t *);
static int mpt_disks_in_volume(uint32_t, int, uint32_t *);
static int mpt_disks_find_array(uint32_t, uint32_t, uint32_t *);
static int mpt_check_disk_valid(char *);
static int mpt_get_phys_disk_num(uint32_t, uint32_t, uint32_t, uint8_t *);
static int mpt_get_phys_path(uint32_t, uint32_t, int *, void *);
static int mpt_check_mpxio_enable(uint32_t);
static int mpt_check_sata_disk(uint32_t, int, int);
static int mpt_check_ssd_disk(uint32_t, int, int);

extern int errno;
uint32_t rdcfg_version = RDCFG_PLUGIN_V1;
raidvol_t raidvolume[MPT_MAX_RAIDVOLS];

static struct devh_t {
	struct devh_t	*next;
	uint32_t	controller_id;
	int		fd;
} *devh_sys = NULL;

static int
mpt_do_command(uint32_t controller_id, void *raid_action, int raid_action_size,
    void *raid_action_reply, int raid_action_reply_size,
    void *Data, int DataSize, void *DataOut, int DataOutSize)
{
	int fd, ret;
	mpt_pass_thru_t mpt_pass_thru;

	fd = mpt_controller_id_to_fd(controller_id);
	if (fd < 0) {
		return (fd);
	}

	(void) memset(&mpt_pass_thru, 0, sizeof (mpt_pass_thru));

	mpt_pass_thru.PtrRequest = (uint64_t)(uintptr_t)raid_action;
	mpt_pass_thru.RequestSize = raid_action_size;
	mpt_pass_thru.PtrReply = (uint64_t)(uintptr_t)raid_action_reply;
	mpt_pass_thru.ReplySize = raid_action_reply_size;

	if (DataSize != 0 && DataOutSize != 0) {
		mpt_pass_thru.PtrData   = (uint64_t)(uintptr_t)Data;
		mpt_pass_thru.DataSize = DataSize;
		mpt_pass_thru.PtrDataOut = (uint64_t)(uintptr_t)DataOut;
		mpt_pass_thru.DataOutSize = DataOutSize;
		mpt_pass_thru.DataDirection = MPT_PASS_THRU_BOTH;
	} else if (DataSize != 0) {
		mpt_pass_thru.PtrData = (uint64_t)(uintptr_t)Data;
		mpt_pass_thru.DataSize  = DataSize;
		mpt_pass_thru.DataDirection = MPT_PASS_THRU_READ;
	} else if (DataOutSize != 0) {
		mpt_pass_thru.PtrData = (uint64_t)(uintptr_t)DataOut;
		mpt_pass_thru.DataSize = DataOutSize;
		mpt_pass_thru.DataDirection = MPT_PASS_THRU_WRITE;
	} else {
		mpt_pass_thru.PtrData = NULL;
		mpt_pass_thru.DataSize = 0;
		mpt_pass_thru.PtrDataOut = NULL;
		mpt_pass_thru.DataOutSize = 0;
		mpt_pass_thru.DataDirection = MPT_PASS_THRU_NONE;
	}

	ret = ioctl(fd, MPTIOCTL_PASS_THRU, &mpt_pass_thru);

	if (ret >= 0)
		return (SUCCESS);
	else
		return (ERR_OP_FAILED);
}

static int
mpt_do_scsi_io(uint32_t controller_id, void *scsi_request, int request_size,
    msg_scsi_io_reply_t *scsi_reply, int reply_size,
    void *Data, int DataSize, void *DataOut, int DataOutSize)
{
	int ioc_status;

	(void) memset(scsi_reply, 0, reply_size);

	if (mpt_do_command(controller_id, scsi_request, request_size,
	    scsi_reply, reply_size, Data, DataSize,
	    DataOut, DataOutSize) != SUCCESS)
		return (ERR_OP_FAILED);

	ioc_status = LE_16(scsi_reply->IOCStatus) & MPI_IOCSTATUS_MASK;

	if (ioc_status == MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE)
		return (ERR_OP_FAILED);

	if (ioc_status == MPI_IOCSTATUS_BUSY ||
	    ioc_status == MPI_IOCSTATUS_SCSI_IOC_TERMINATED ||
	    ioc_status == MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH ||
	    scsi_reply->SCSIStatus == MPI_SCSI_STATUS_CHECK_CONDITION ||
	    scsi_reply->SCSIStatus == MPI_SCSI_STATUS_BUSY ||
	    scsi_reply->SCSIStatus == MPI_SCSI_STATUS_TASK_SET_FULL) {
		(void) memset(scsi_reply, 0, reply_size);

		if (mpt_do_command(controller_id, scsi_request, request_size,
		    scsi_reply, reply_size, Data, DataSize,
		    DataOut, DataOutSize) != SUCCESS)
			return (ERR_OP_FAILED);

		ioc_status = LE_16(scsi_reply->IOCStatus) & MPI_IOCSTATUS_MASK;
	}

	if (ioc_status == MPI_IOCSTATUS_SCSI_DATA_UNDERRUN ||
	    ioc_status == MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH) {
		if (scsi_reply->TransferCount == 0)
			return (ERR_OP_FAILED);
		else
			return (SUCCESS);
	}

	if (ioc_status != MPI_IOCSTATUS_SUCCESS)
		return (ERR_OP_FAILED);

	return (SUCCESS);
}

static int
mpt_get_iocfacts(uint32_t controller_id, msg_ioc_facts_reply_t *ioc_reply)
{
	msg_ioc_facts_t ioc_request;
	int ret;

	(void) memset(&ioc_request, 0, sizeof (ioc_request));
	(void) memset(ioc_reply, 0, sizeof (*ioc_reply));

	ioc_request.Function = LE_8(MPI_FUNCTION_IOC_FACTS);

	ret = mpt_do_command(controller_id, &ioc_request, sizeof (ioc_request),
	    ioc_reply, sizeof (*ioc_reply), NULL, 0, NULL, 0);

	return (ret);
}

static int
mpt_do_inquiry(uint32_t controller_id, int bus, int target, int lun,
    unsigned char *buf, int len)
{
	int ret;
	msg_scsi_io_request_t scsi_io_request;
	msg_scsi_io_reply_t scsi_io_reply;

	(void) memset(&scsi_io_request, 0, sizeof (scsi_io_request));
	(void) memset(&scsi_io_reply, 0, sizeof (scsi_io_reply));

	scsi_io_request.Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	scsi_io_request.Bus = bus;
	scsi_io_request.TargetID = target;
	scsi_io_request.CDBLength = 6;
	scsi_io_request.LUN[1] = lun;
	scsi_io_request.Control = LE_32(MPI_SCSIIO_CONTROL_READ |
	    MPI_SCSIIO_CONTROL_SIMPLEQ);
	scsi_io_request.CDB[0] = 0x12;
	scsi_io_request.CDB[1] = 0;
	scsi_io_request.CDB[2] = 0;
	scsi_io_request.CDB[3] = 0;
	scsi_io_request.CDB[4] = len;
	scsi_io_request.CDB[5] = 0;
	scsi_io_request.DataLength = LE_32(len);

	ret = mpt_do_scsi_io(controller_id, &scsi_io_request,
	    sizeof (scsi_io_request) - sizeof (scsi_io_request.SGL),
	    &scsi_io_reply, sizeof (scsi_io_reply), buf, len, NULL, 0);

	return (ret);
}

static int
mpt_do_inquiry_VpdPage(uint32_t controller_id, int bus, int target, int lun,
    int page, unsigned char *buf, int len)
{
	int ret;
	msg_scsi_io_request_t scsi_io_request;
	msg_scsi_io_reply_t scsi_io_reply;

	(void) memset(&scsi_io_request, 0, sizeof (scsi_io_request));
	(void) memset(&scsi_io_reply, 0, sizeof (scsi_io_reply));

	scsi_io_request.Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	scsi_io_request.Bus = bus;
	scsi_io_request.TargetID = target;
	scsi_io_request.CDBLength = 6;
	scsi_io_request.LUN[1] = lun;
	scsi_io_request.Control = LE_32(MPI_SCSIIO_CONTROL_READ |
	    MPI_SCSIIO_CONTROL_SIMPLEQ);
	scsi_io_request.CDB[0] = 0x12;
	scsi_io_request.CDB[1] = 1;
	scsi_io_request.CDB[2] = page;
	scsi_io_request.CDB[3] = 0;
	scsi_io_request.CDB[4] = len;
	scsi_io_request.CDB[5] = 0;
	scsi_io_request.DataLength = LE_32(len);

	ret = mpt_do_scsi_io(controller_id, &scsi_io_request,
	    sizeof (scsi_io_request) - sizeof (scsi_io_request.SGL),
	    &scsi_io_reply, sizeof (scsi_io_reply), buf, len, NULL, 0);

	return (ret);
}

static int
mpt_get_configpage_header(uint32_t controller_id, uint8_t type, uint8_t number,
    uint32_t address, msg_config_reply_t *reply)
{
	int ret;
	msg_config_t	config;
	msg_config_reply_t	config_reply;

	(void) memset(&config, 0, sizeof (config));
	(void) memset(&config_reply, 0, sizeof (config_reply));

	config.Function	= LE_8(MPI_FUNCTION_CONFIG);
	config.Action	= LE_8(MPI_CONFIG_ACTION_PAGE_HEADER);
	if (type > MPI_CONFIG_PAGETYPE_EXTENDED) {
		config.Header.PageType	= LE_8(MPI_CONFIG_PAGETYPE_EXTENDED);
		config.ExtPageType	= LE_8(type);
	} else
		config.Header.PageType	= LE_8(type);

	config.Header.PageNumber	= LE_8(number);
	config.PageAddress		= LE_32(address);

	ret = mpt_do_command(controller_id, &config,
	    sizeof (config) - sizeof (config.PageBufferSGE),
	    &config_reply, sizeof (config_reply), NULL, 0, NULL, 0);
	if (ret != SUCCESS)
		return (ret);

	if (config_reply.IOCStatus != LE_16(MPI_IOCSTATUS_SUCCESS))
		return (ERR_DEVICE_INVALID);

	if (reply != NULL)
		(void) memcpy(reply, &config_reply, sizeof (config_reply));

	return (SUCCESS);
}

static int
mpt_get_configpage_length(uint32_t controller_id, uint8_t type, uint8_t number,
	uint32_t address, int *length)
{
	int ret;
	msg_config_t	config;
	msg_config_reply_t	config_reply;

	(void) memset(&config, 0, sizeof (config));
	(void) memset(&config_reply, 0, sizeof (config_reply));

	config.Function	= LE_8(MPI_FUNCTION_CONFIG);
	config.Action = LE_8(MPI_CONFIG_ACTION_PAGE_HEADER);

	if (type > MPI_CONFIG_PAGETYPE_EXTENDED) {
		config.Header.PageType	= LE_8(MPI_CONFIG_PAGETYPE_EXTENDED);
		config.ExtPageType	= LE_8(type);
	} else
		config.Header.PageType	= LE_8(type);

	config.Header.PageNumber = LE_8(number);
	config.PageAddress = LE_32(address);

	ret = mpt_do_command(controller_id, &config,
	    sizeof (config) - sizeof (config.PageBufferSGE),
	    &config_reply, sizeof (config_reply), NULL, 0, NULL, 0);
	if (ret != SUCCESS)
		return (ret);

	if (config_reply.IOCStatus != LE_16(MPI_IOCSTATUS_SUCCESS))
		return (ERR_DEVICE_INVALID);

	if (type > MPI_CONFIG_PAGETYPE_EXTENDED)
		*length = config_reply.ExtPageLength * 4;
	else
		*length = config_reply.Header.PageLength * 4;

	return (SUCCESS);
}

static int
mpt_get_configpage(uint32_t controller_id, uint8_t type, uint8_t number,
	uint32_t address, void *page, int pagesize)
{
	int ret;
	msg_config_t	config;
	msg_config_reply_t	config_reply;

	(void) memset(&config, 0, sizeof (config));
	(void) memset(&config_reply, 0, sizeof (config_reply));

	ret = mpt_get_configpage_header(controller_id,
	    type, number, address, &config_reply);
	if (ret != SUCCESS)
		return (ret);

	config.Function	= LE_8(MPI_FUNCTION_CONFIG);
	config.Action = LE_8(MPI_CONFIG_ACTION_PAGE_READ_CURRENT);
	config.ExtPageType = config_reply.ExtPageType;
	config.ExtPageLength = config_reply.ExtPageLength;
	config.Header = config_reply.Header;
	config.PageAddress = LE_32(address);

	ret = mpt_do_command(controller_id, &config,
	    sizeof (config) - sizeof (config.PageBufferSGE),
	    &config_reply, sizeof (config_reply), page, pagesize, NULL, 0);
	if (ret != SUCCESS)
		return (ret);

	if (config_reply.IOCStatus != LE_16(MPI_IOCSTATUS_SUCCESS))
		return (ERR_DEVICE_INVALID);

	return (SUCCESS);
}

static int
mpt_get_deviceid(uint32_t controller_id, uint16_t *deviceid,
    uint16_t *subsystem, uint16_t *subvendorid)
{
	int ret;
	config_page_ioc_0_t iocpage0;

	(void) memset(&iocpage0, 0, sizeof (iocpage0));
	ret = mpt_get_configpage(controller_id, MPI_CONFIG_PAGETYPE_IOC, 0,
	    0, &iocpage0, sizeof (iocpage0));
	if (ret != SUCCESS)
		return (ret);

	*deviceid = LE_16(iocpage0.DeviceID);
	*subsystem = LE_16(iocpage0.SubsystemID);
	*subvendorid = LE_16(iocpage0.SubsystemVendorID);
	return (SUCCESS);
}


static int
mpt_get_raidinfo(uint32_t controller_id)
{
	int i, j, k, l, length, ret, flag, mode, max_tgt;
	uint8_t numvolumes, volid, bus;
	uint8_t physdisknum, physdiskstate;
	uint16_t deviceid, physdiskid, diskid, subsys, subven;
	uint32_t addr;

	config_page_ioc_2_t *iocpage2;
	config_page_raid_vol_0_t *raidpage0;
	config_page_raid_phys_disk_0_t *diskpage0;
	config_page_raid_phys_disk_1_t *diskpage1;
	config_page_sas_io_unit_2_t *sasiounitpage2;
	config_page_sas_device_0_t sasdevpage0;

	(void) memset(raidvolume, 0, MPT_MAX_RAIDVOLS * sizeof (raidvol_t));

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	ret = mpt_get_configpage_length(controller_id, MPI_CONFIG_PAGETYPE_IOC,
	    2, 0, &length);
	if (ret != SUCCESS)
		return (ret);

	iocpage2 = malloc(length);
	(void) memset(iocpage2, 0, length);
	ret = mpt_get_configpage(controller_id, MPI_CONFIG_PAGETYPE_IOC, 2, 0,
	    iocpage2, length);
	if (ret != SUCCESS) {
		free(iocpage2);
		return (ret);
	}

	numvolumes = LE_8(iocpage2->NumActiveVolumes);

	for (i = 0; i < numvolumes; i++) {
		volid = LE_8(iocpage2->RaidVolume[i].VolumeID);
		raidvolume[i].m_israid = 1;
		raidvolume[i].m_raidtarg = volid;

		/*
		 * Get the settings for the raid volume
		 * this includes the real target id's
		 * for the disks making up the raid
		 */
		addr = (iocpage2->RaidVolume[i].VolumeBus << 8) +
		    iocpage2->RaidVolume[i].VolumeID;

		ret = mpt_get_configpage_length(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_VOLUME,
		    0, addr, &length);
		if (ret != SUCCESS) {
			free(iocpage2);
			return (ret);
		}

		raidpage0 = malloc(length);
		(void) memset(raidpage0, 0, length);
		ret = mpt_get_configpage(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_VOLUME, 0, addr,
		    raidpage0, length);
		if (ret != SUCCESS) {
			free(iocpage2);
			free(raidpage0);
			return (ret);
		}

		raidvolume[i].m_state = LE_8(raidpage0->VolumeStatus.State);
		raidvolume[i].m_flags = LE_8(raidpage0->VolumeStatus.Flags);
		raidvolume[i].m_raidlevel = LE_8(raidpage0->VolumeType);
		raidvolume[i].m_ndisks = LE_8(raidpage0->NumPhysDisks);
		raidvolume[i].m_settings =
		    LE_16(raidpage0->VolumeSettings.Settings);
		/*
		 * Convert original 512 data block numbers into bytes
		 */
		raidvolume[i].m_stripesize =
		    LE_32(raidpage0->StripeSize) << 9;

		if (LE_32(iocpage2->CapabilitiesFlags) & MPT_RAID_CAP_LBA64) {
			raidvolume[i].m_raidsize =
			    (LE_32(raidpage0->MaxLBA) | ((uint64_t)
			    LE_32(raidpage0->MaxLBAHigh) << 32)) << 9;
		} else {
			raidvolume[i].m_raidsize = (uint64_t)
			    (LE_32(raidpage0->MaxLBA)) << 9;
		}

		for (j = 0; j < raidvolume[i].m_ndisks; j++) {
			physdisknum = LE_8(raidpage0->PhysDisk[j].PhysDiskNum);
			raidvolume[i].m_disknum[j] = physdisknum;

			ret = mpt_get_configpage_length(controller_id,
			    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
			    0, physdisknum, &length);
			if (ret != SUCCESS) {
				free(iocpage2);
				free(raidpage0);
				return (ret);
			}

			diskpage0 = malloc(length);
			(void) memset(diskpage0, 0, length);
			ret = mpt_get_configpage(controller_id,
			    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
			    0, physdisknum, diskpage0, length);
			if (ret != SUCCESS) {
				free(iocpage2);
				free(raidpage0);
				free(diskpage0);
				return (ret);
			}

			/*
			 * get real target id's for physical disk
			 */
			if (deviceid == MPT_1030) {
				/* scsi HBA, just read the PhysDiskID */
				diskid = DISK_ID(diskpage0->PhysDiskBus,
				    diskpage0->PhysDiskID, 0);
			} else if (deviceid == MPT_1064 ||
			    deviceid == MPT_1064E ||
			    deviceid == MPT_1068 ||
			    deviceid == MPT_1068E) {
				physdiskid =
				    DISK_ID(diskpage0->PhysDiskBus,
				    diskpage0->PhysDiskID, 0);
				physdiskstate =
				    LE_8(diskpage0->PhysDiskStatus.State);

				/*
				 * SAS IOUnit Page 2 contains the mapping
				 * mode of the specific device
				 */
				ret = mpt_get_configpage_length(controller_id,
				    MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
				    2, 0, &length);
				if (ret != SUCCESS) {
					free(iocpage2);
					free(raidpage0);
					free(diskpage0);
					return (ret);
				}

				sasiounitpage2 = malloc(length);
				(void) memset(sasiounitpage2, 0, length);
				ret = mpt_get_configpage(controller_id,
				    MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 2,
				    0, sasiounitpage2, length);
				if (ret != SUCCESS) {
					free(iocpage2);
					free(raidpage0);
					free(diskpage0);
					free(sasiounitpage2);
					return (ret);
				}

				flag = sasiounitpage2->Flags;
				free(sasiounitpage2);

				mode = (flag &
				    MPI_SAS_IOUNIT2_FLAGS_MASK_PHYS_MAP_MODE) >>
				    MPI_SAS_IOUNIT2_FLAGS_SHIFT_PHYS_MAP_MODE;

				if (physdiskstate ==
				    MPI_PHYSDISK0_STATUS_ONLINE &&
				    mode ==
				    MPT_DIRECT_MAPPING) {
					addr = MPT_SAS_ADDR(physdiskid);
					(void) memset(&sasdevpage0, 0,
					    sizeof (sasdevpage0));
					ret = mpt_get_configpage(controller_id,
					    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
					    0, addr, &sasdevpage0,
					    sizeof (sasdevpage0));
					if (ret != SUCCESS) {
						free(iocpage2);
						free(raidpage0);
						free(diskpage0);
						return (ret);
					}
					diskid = LE_8(sasdevpage0.PhyNum);
				} else {
					/*
					 * The disk is missing or we're not
					 * using direct mapping mode.
					 */
					diskid = physdiskid;
				}
			}

			/*
			 * Store disk information in the appropriate location
			 */
			for (k = 0; k < MPT_MAX_DISKS_IN_RAID; k++) {
				/* find the correct position in the arrays */
				if (raidvolume[i].m_disknum[k] == physdisknum)
					break;
			}

			raidvolume[i].m_diskid[k] = diskid;

			/*
			 * physical disk page 1 contains
			 * path infomation for dual port disks
			 */
			ret = mpt_get_configpage_length(controller_id,
			    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
			    1, physdisknum, &length);
			if (ret != SUCCESS) {
				free(iocpage2);
				free(raidpage0);
				return (ret);
			}
			diskpage1 = malloc(length);
			(void) memset(diskpage1, 0, length);
			ret = mpt_get_configpage(controller_id,
			    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
			    1, physdisknum, diskpage1, length);
			if (ret != SUCCESS) {
				free(iocpage2);
				free(raidpage0);
				free(diskpage1);
				return (ret);
			}

			if (diskpage1->NumPhysDiskPaths == 2) {
				for (l = 0; l < 2; l++) {
					if ((diskpage0->PhysDiskID ==
					    diskpage1->Path[l].PhysDiskID) &&
					    (diskpage0->PhysDiskBus ==
					    diskpage1->Path[l].PhysDiskBus))
						continue;

					raidvolume[i].m_pathid[k] = DISK_ID(
					    diskpage1->Path[l].PhysDiskBus,
					    diskpage1->Path[l].PhysDiskID, 0);
				}
			}

			switch (LE_8(diskpage0->PhysDiskStatus.State)) {
			case MPI_PHYSDISK0_STATUS_MISSING:
				raidvolume[i].m_diskstatus[k] =
				    MPT_RAID_DISKSTATUS_MISSING;
				break;
			case MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE:
			case MPI_PHYSDISK0_STATUS_FAILED:
			case MPI_PHYSDISK0_STATUS_FAILED_REQUESTED:
				raidvolume[i].m_diskstatus[k] =
				    MPT_RAID_DISKSTATUS_FAILED;
				break;
			case MPI_PHYSDISK0_STATUS_ONLINE:
			default:
				raidvolume[i].m_diskstatus[k] =
				    MPT_RAID_DISKSTATUS_GOOD;
				break;
			}
			free(diskpage0);
			free(diskpage1);
		}
		/*
		 * Correct the target IDs when Enclosure Slot Mapping mode
		 * is being used. We will do this by looping over all of
		 * the physcial disks to determine the disk with largest
		 * target ID this target ID was fabricated by the IOC to
		 * reference the target that gave the volume it's target
		 * number.
		 */
		if (mode == MPT_SLOT_MAPPING) {
			max_tgt = 0;
			for (k = 0; k < raidvolume[i].m_ndisks; k++) {
				if (TARGET(raidvolume[i].m_diskid[k]) >
				    TARGET(raidvolume[i].m_diskid[max_tgt]))
					max_tgt = k;
			}

			bus = BUS(raidvolume[i].m_diskid[max_tgt]);
			raidvolume[i].m_diskid[max_tgt] = DISK_ID(bus,
			    volid, 0);
		}

		free(raidpage0);
	}
	free(iocpage2);
	return (SUCCESS);
}

static int
mpt_create_physdisk(uint32_t controller_id, uint16_t targ, int *physdisk)
{
	int ret;
	config_page_raid_phys_disk_0_t physdiskpage0;
	msg_raid_action_t raid_action;
	msg_raid_action_reply_t raid_action_reply;
	msg_config_reply_t config_reply;
	msg_ioc_facts_reply_t ioc_reply;

	(void) memset(&config_reply, 0, sizeof (config_reply));
	(void) memset(&ioc_reply, 0, sizeof (ioc_reply));
	(void) memset(&physdiskpage0, 0, sizeof (physdiskpage0));

	ret = mpt_get_configpage_header(controller_id,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK, 0, 0, &config_reply);
	if (ret != SUCCESS)
		return (ret);

	ret = mpt_get_iocfacts(controller_id, &ioc_reply);
	if (ret != SUCCESS)
		return (ret);

	physdiskpage0.Header = config_reply.Header;
	physdiskpage0.PhysDiskIOC = ioc_reply.IOCNumber;
	physdiskpage0.PhysDiskBus = BUS(targ);
	physdiskpage0.PhysDiskID = TARGET(targ);

	(void) memset(&raid_action, 0, sizeof (raid_action));
	(void) memset(&raid_action_reply, 0, sizeof (raid_action_reply));

	raid_action.Function = LE_8(MPI_FUNCTION_RAID_ACTION);
	raid_action.Action = LE_8(MPI_RAID_ACTION_CREATE_PHYSDISK);

	ret = mpt_do_command(controller_id, &raid_action,
	    sizeof (raid_action) - sizeof (raid_action.ActionDataSGE),
	    &raid_action_reply, sizeof (raid_action_reply),
	    NULL, 0, &physdiskpage0, sizeof (physdiskpage0));
	if (ret == SUCCESS)
		*physdisk = LE_32(raid_action_reply.ActionData);

	return (ret);
}

static int
mpt_create_volume(uint32_t controller_id, disklist_t *disklist,
	uint8_t volid, int raid_level, uint32_t stripe_size)
{
	int i, flags, length, ndisks, ret, settings = 0;
	int phys_disks[MPT_MAX_DISKS_IN_RAID];
	msg_config_reply_t config_reply;
	msg_ioc_facts_reply_t ioc_reply;
	msg_raid_action_t raid_action;
	msg_raid_action_reply_t raid_action_reply;
	config_page_ioc_2_t iocpage2;
	config_page_raid_vol_0_t *raidpage0;
	config_page_manufacturing_4_t manufacturingpage4;
	uint16_t deviceid, subsys, subven;
	uint32_t size, min_size, metadata_size;
	uint64_t disk_size, volume_size, max_lba, max_volume_size;

	ndisks = disklist->m_ndisks;

	(void) memset(&iocpage2, 0, sizeof (iocpage2));
	ret = mpt_get_configpage(controller_id, MPI_CONFIG_PAGETYPE_IOC, 2,
	    0, &iocpage2, sizeof (iocpage2));
	if (ret != SUCCESS)
		return (ret);

	if (iocpage2.NumActiveVolumes == iocpage2.MaxVolumes)
		return (ERR_ARRAY_AMOUNT);

	flags = LE_32(iocpage2.CapabilitiesFlags);

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	(void) memset(&manufacturingpage4, 0, sizeof (manufacturingpage4));
	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_PAGETYPE_MANUFACTURING, 4, 0, &manufacturingpage4,
	    sizeof (manufacturingpage4));
	if (ret != SUCCESS)
		return (ret);

	(void) memset(&ioc_reply, 0, sizeof (ioc_reply));
	ret = mpt_get_iocfacts(controller_id, &ioc_reply);
	if (ret != SUCCESS)
		return (ret);

	/*
	 * Create physdisk page for each disk
	 */
	min_size = (uint32_t)OBJ_ATTR_NONE;
	for (i = 0; i < ndisks; i++) {
		disk_size = mpt_get_comp_capacity(controller_id,
		    disklist->m_diskid[i]);

		switch (deviceid) {
		case MPT_1030:
			/*
			 * For LSI 1030, metadata size is 33 blocks
			 */
			metadata_size = 33 * DISK_BLK_SIZE;
			size = (disk_size - metadata_size) / (1024 * 1024);
			break;
		case MPT_1064:
		case MPT_1064E:
		case MPT_1068:
		case MPT_1068E:
			if (LE_8(raid_level) == MPI_RAID_VOL_TYPE_IM)
				settings =
				    LE_32(manufacturingpage4.IMVolumeSettings);
			else if (LE_8(raid_level) == MPI_RAID_VOL_TYPE_IS)
				settings =
				    LE_32(manufacturingpage4.ISVolumeSettings);
			else if (LE_8(raid_level) == MPI_RAID_VOL_TYPE_IME)
				settings =
				    LE_32(manufacturingpage4.IMEVolumeSettings);

			switch (settings &
			    MPI_RAIDVOL0_SETTING_MASK_METADATA_SIZE) {
			case MPI_RAIDVOL0_SETTING_512MB_METADATA_SIZE:
				metadata_size = 512;	/* 512M */
				break;
			case MPI_RAIDVOL0_SETTING_64MB_METADATA_SIZE:
				metadata_size = 64;	/* 64M */
				break;
			default:
				break;
			}

			size = disk_size / (1024 * 1024) - metadata_size;
			break;
		default:
			break;
		}

		if (size < min_size)
			min_size = size;

		ret = mpt_create_physdisk(controller_id,
		    disklist->m_diskid[i], &phys_disks[i]);
		if (ret != SUCCESS)
			return (ret);
	}

	(void) memset(&config_reply, 0, sizeof (config_reply));
	ret = mpt_get_configpage_header(controller_id,
	    MPI_CONFIG_PAGETYPE_RAID_VOLUME, 0, 0, &config_reply);
	if (ret != SUCCESS)
		return (ret);

	length = sizeof (*raidpage0) +
	    (ndisks - 1) * sizeof (raidpage0->PhysDisk);

	if (length < LE_8(config_reply.Header.PageLength) * 4)
		length = LE_8(config_reply.Header.PageLength) * 4;
	raidpage0 = malloc(length);

	(void) memset(raidpage0, 0, length);

	raidpage0->Header.PageType = config_reply.Header.PageType;
	raidpage0->Header.PageNumber = config_reply.Header.PageNumber;
	raidpage0->Header.PageLength = LE_8(length / 4);
	raidpage0->Header.PageVersion = config_reply.Header.PageVersion;
	raidpage0->VolumeIOC = ioc_reply.IOCNumber;
	raidpage0->VolumeBus = LE_8(0);
	raidpage0->VolumeID = LE_8(volid);
	raidpage0->NumPhysDisks = LE_8(ndisks);
	raidpage0->VolumeType = LE_8(raid_level);

	/*
	 * The max volume size is calculated in MB
	 */
	if (flags & MPI_IOCPAGE2_CAP_FLAGS_RAID_64_BIT_ADDRESSING)
		max_volume_size = ((uint64_t)1 <<
		    (64 - MPT_MAX_VOLUME_SIZE_SHIFT_BIT));
	else
		max_volume_size = ((uint64_t)1 <<
		    (32 - MPT_MAX_VOLUME_SIZE_SHIFT_BIT));

	max_volume_size -= metadata_size;

	volume_size = (uint64_t)(min_size * ndisks);

	if (raidpage0->VolumeType != MPI_RAID_VOL_TYPE_IS)
		volume_size /= 2;

	/*
	 * Maximum volume size is exceeded
	 * Use Maximum size to create volume
	 */
	if (volume_size > max_volume_size)
		volume_size = max_volume_size;

	max_lba = (uint64_t)(volume_size * 2048 - 1);
	if (LE_32(iocpage2.CapabilitiesFlags) & MPT_RAID_CAP_LBA64) {
		raidpage0->MaxLBA = LE_32((uint32_t)max_lba);
		raidpage0->MaxLBAHigh = LE_32((uint32_t)(max_lba >> 32));
	} else {
		raidpage0->MaxLBA = LE_32((uint32_t)max_lba);
	}

	if (LE_8(raidpage0->VolumeType) == MPI_RAID_VOL_TYPE_IM) {
		settings = LE_32(manufacturingpage4.IMVolumeSettings);
	} else if (LE_8(raidpage0->VolumeType) == MPI_RAID_VOL_TYPE_IS) {
		if (stripe_size == (uint32_t)OBJ_ATTR_NONE)
			raidpage0->StripeSize = LE_32((uint32_t)128);
		else
			raidpage0->StripeSize = LE_32(stripe_size >> 9);

		settings = LE_32(manufacturingpage4.ISVolumeSettings);
	} else if (LE_8(raidpage0->VolumeType) == MPI_RAID_VOL_TYPE_IME) {
		if (stripe_size == (uint32_t)OBJ_ATTR_NONE)
			raidpage0->StripeSize = LE_32((uint32_t)128);
		else
			raidpage0->StripeSize = LE_32(stripe_size >> 9);

		settings = LE_32(manufacturingpage4.IMEVolumeSettings);
	}

	raidpage0->VolumeSettings.Settings =
	    LE_16(settings & ~MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE);

	for (i = 0; i < ndisks; i++) {
		raidpage0->PhysDisk[i].PhysDiskNum = LE_8(phys_disks[i]);
		raidpage0->PhysDisk[i].PhysDiskMap = LE_8(i);
	}

	if (raidpage0->VolumeType == MPI_RAID_VOL_TYPE_IM) {
		raidpage0->PhysDisk[0].PhysDiskMap =
		    LE_8(MPI_RAIDVOL0_PHYSDISK_PRIMARY);
		raidpage0->PhysDisk[1].PhysDiskMap =
		    LE_8(MPI_RAIDVOL0_PHYSDISK_SECONDARY);
	}

	(void) memset(&raid_action, 0, sizeof (raid_action));
	(void) memset(&raid_action_reply, 0, sizeof (raid_action_reply));

	raid_action.Function = LE_8(MPI_FUNCTION_RAID_ACTION);
	raid_action.Action = LE_8(MPI_RAID_ACTION_CREATE_VOLUME);
	raid_action.VolumeBus = LE_8(0);
	raid_action.VolumeID = LE_8(volid);

	ret = mpt_do_command(controller_id, &raid_action,
	    sizeof (raid_action) - sizeof (raid_action.ActionDataSGE),
	    &raid_action_reply, sizeof (raid_action_reply),
	    NULL, 0, raidpage0, length);
	if (ret != SUCCESS) {
		free(raidpage0);
		return (ret);
	}

	free(raidpage0);
	return (SUCCESS);
}

/*ARGSUSED*/
static int
mpt_get_phys_disk_num(uint32_t controller_id, uint32_t array_id,
	uint32_t disk_id, uint8_t *phys_disk_num)
{
	int i, j;

	/*
	 * Loop over all of the RAID volumes in the system until
	 * we locate the volume we're looking for. Then look for
	 * the specific disk_id so we can obtain the correct unique
	 * disk number assigend by the IOC.
	 */
	for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
		if (raidvolume[i].m_israid == 0)
			continue;
		if (raidvolume[i].m_raidtarg == array_id)
			break;
	}

	for (j = 0; j < raidvolume[i].m_ndisks; j++) {
		if (raidvolume[i].m_diskid[j] == disk_id) {
			*phys_disk_num = raidvolume[i].m_disknum[j];
			return (SUCCESS);
		}
	}

	return (ERR_DEVICE_NOENT);
}

/*ARGSUSED*/
static int
mpt_get_phys_path(uint32_t controller_id, uint32_t array_id,
	int *path_num, void *ids)
{
	int i, j, data_size, num = 0;
	uint32_t *data = NULL;

	data_size = *path_num;
	data_size *= sizeof (uint32_t);
	if (data_size)
		data = (uint32_t *)ids;

	for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
		if (raidvolume[i].m_israid == 0)
			continue;
		if (raidvolume[i].m_raidtarg == array_id)
			break;
	}

	for (j = 0; j < raidvolume[i].m_ndisks; j++) {
		if (raidvolume[i].m_pathid[j] != 0) {
			if (data != NULL)
				data[num] = raidvolume[i].m_pathid[j];
			++num;
		}
	}

	*path_num = num;

	return (SUCCESS);
}

static int
mpt_get_compinfo(uint32_t controller_id, uint32_t container_id,
	uint32_t container_type, uint32_t component_type,
	uint32_t *component_num, void *ids)
{
	int num = 0;
	int data_size, i, j, nvols, ret;
	uint32_t *data = NULL;

	ret = mpt_get_max_volume_num(controller_id, &nvols);

	if (ret != SUCCESS)
		return (ret);

	data_size = *component_num;
	data_size *= sizeof (uint32_t);
	if (data_size)
		data = (uint32_t *)ids;

	switch (container_type) {
	case OBJ_TYPE_CONTROLLER:
		switch (component_type) {
		case OBJ_TYPE_ARRAY:
			for (i = 0; i < nvols; ++i) {
				if (raidvolume[i].m_israid == 0)
					continue;
				if (data != NULL) {
					if (num >= *component_num)
						break;
					data[num] = raidvolume[i].m_raidtarg;
				}
				++num;
			}
			*component_num = num;
			break;
		default:
			*component_num = 0;
			break;
		}
		break;
	case OBJ_TYPE_ARRAY:
		switch (component_type) {
		case OBJ_TYPE_ARRAY_PART:
			for (i = 0; i < nvols; ++i) {
				if (raidvolume[i].m_israid == 0)
					continue;
				if (raidvolume[i].m_raidtarg ==
				    container_id)
					break;
			}

			if (i == nvols) {
				*component_num = 0;
				break;
			}

			num = raidvolume[i].m_ndisks;
			if (data != NULL)
				for (j = 0; j < num; ++j)
					data[j] = raidvolume[i].m_diskid[j];
			*component_num = num;
			break;
		case OBJ_TYPE_TASK:
			for (i = 0; i < nvols; ++i) {
				if (raidvolume[i].m_israid == 0)
					continue;
				if (raidvolume[i].m_raidtarg ==
				    container_id)
					break;
			}

			if (i == nvols) {
				*component_num = 0;
				break;
			}

			if (raidvolume[i].m_flags &
			    MPT_RAID_FLAG_RESYNCING &&
			    raidvolume[i].m_state ==
			    MPT_RAID_STATE_DEGRADED) {
				*component_num = 1;
				if (data != NULL)
					data[0] = container_id;
			} else
				*component_num = 0;
			break;
		default:
			*component_num = 0;
			break;
		}
		break;
	case OBJ_TYPE_DISK:
		switch (component_type) {
		case OBJ_TYPE_DISK_SEG:
			for (i = 0; i < nvols; ++i) {
				if (raidvolume[i].m_israid == 0)
					continue;

			for (j = 0; j <
			    raidvolume[i].m_ndisks; ++j) {
				if (container_id ==
				    raidvolume[i].m_diskid[j]) {
					num = 1;
					break;
				}
			}

			if (num)
				break;
			}
			*component_num = num;
			if (data != NULL)
				data[0] = 0;
			break;
		default:
			*component_num = 0;
			break;
		}
		break;
	default:
		*component_num = 0;
		break;
	}
	return (SUCCESS);
}

static int
mpt_get_attr(uint32_t controller_id, uint32_t device_id,
	uint32_t index_id, raid_obj_type_id_t type, void *attr)
{
	int i, j, nvols, ret, size = 0;
	uint16_t deviceid, subsys, subven;
	controller_attr_t controller;
	array_attr_t array;
	arraypart_attr_t arraypart;
	diskseg_attr_t diskseg;
	task_attr_t task;
	config_page_ioc_2_t iocpage2;
	msg_ioc_facts_reply_t ioc_reply;

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	ret = mpt_get_max_volume_num(controller_id, &nvols);
	if (ret != SUCCESS)
		return (ret);

	switch (type) {
	case OBJ_TYPE_CONTROLLER:
		size = sizeof (controller);
		(void) memcpy((caddr_t)&controller, attr, size);
		switch (deviceid) {
		case MPT_1030:
			controller.connection_type = TYPE_SCSI;
			(void) snprintf(controller.controller_type,
			    CONTROLLER_TYPE_LEN, "LSI_1030");
			break;
		case MPT_1064:
		case MPT_1064E:
			controller.connection_type = TYPE_SAS;
			if (subven == MPT_DELL_SAS_VID) {
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "DELL_SAS5%s",
				    (subsys == MPT_DELL_SAS5E_PLAIN) ?
				    "E" : "iR");
			} else
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "LSI_1064%s",
				    (deviceid == MPT_1064E) ? "E": "");
			break;
		case MPT_1068:
		case MPT_1068E:
			controller.connection_type = TYPE_SAS;
			if (subven == MPT_DELL_SAS_VID) {
				switch (subsys) {
				case MPT_DELL_SAS6IR_PLAIN:
				case MPT_DELL_SAS6IR_INTBLADES:
				case MPT_DELL_SAS6IR_INTPLAIN:
				case MPT_DELL_SAS6IR_INTWS:
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "DELL_SAS6iR");
				break;
				case MPT_DELL_SAS5E_PLAIN:
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "DELL_SAS5E");
				break;
				case MPT_DELL_SAS5I_PLAIN:
				case MPT_DELL_SAS5I_INT:
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "DELL_SAS5i");
				break;
				case MPT_DELL_SAS5IR_INTRAID1:
				case MPT_DELL_SAS5IR_INTRAID2:
				case MPT_DELL_SAS5IR_ADAPTERRAID:
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "DELL_SAS5iR");
				break;
				default:
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "Unknown Dell device");
				break;
				}
			} else {
				(void) snprintf(controller.controller_type,
				    CONTROLLER_TYPE_LEN, "LSI_1068%s",
				    (deviceid == MPT_1068E) ? "E" : "");
			}
			break;
		default:
			controller.connection_type = TYPE_UNKNOWN;
			break;
		}
		(void) memset(&iocpage2, 0, sizeof (iocpage2));
		ret = mpt_get_configpage(controller_id, MPI_CONFIG_PAGETYPE_IOC,
		    2, 0, &iocpage2, sizeof (iocpage2));
		if (ret != SUCCESS)
			return (ret);

		controller.max_seg_per_disk = MPT_MAX_SEG_PER_DISK;
		controller.capability =
		    RAID_CAP_DISK_TRANS | RAID_CAP_FULL_DISK_ONLY;
		if (LE_32(iocpage2.CapabilitiesFlags) & MPT_RAID_VOL_TYPE_IS)
			controller.capability |= RAID_CAP_RAID0;
		if (LE_32(iocpage2.CapabilitiesFlags) & MPT_RAID_VOL_TYPE_IM)
			controller.capability |= RAID_CAP_RAID1;
		if (LE_32(iocpage2.CapabilitiesFlags) & MPT_RAID_VOL_TYPE_IME)
			controller.capability |= RAID_CAP_RAID1E;
		controller.max_array_num = nvols;

		(void) memset(&ioc_reply, 0, sizeof (ioc_reply));
		ret = mpt_get_iocfacts(controller_id, &ioc_reply);
		if (ret != SUCCESS)
			return (ret);

		/*
		 * The LSI convention for displaying firmware versions
		 * has been changed to use decimal for the newer
		 * controllers.
		 */
		if (deviceid == MPT_1030) {
			(void) snprintf(controller.fw_version,
			    CONTROLLER_FW_LEN, "%x.%02x.%02x.%02x",
			    LE_8(ioc_reply.FWVersion.Struct.Major),
			    LE_8(ioc_reply.FWVersion.Struct.Minor),
			    LE_8(ioc_reply.FWVersion.Struct.Unit),
			    LE_8(ioc_reply.FWVersion.Struct.Dev));
		} else {
			(void) snprintf(controller.fw_version,
			    CONTROLLER_FW_LEN, "%d.%02d.%02d.%02d",
			    LE_8(ioc_reply.FWVersion.Struct.Major),
			    LE_8(ioc_reply.FWVersion.Struct.Minor),
			    LE_8(ioc_reply.FWVersion.Struct.Unit),
			    LE_8(ioc_reply.FWVersion.Struct.Dev));
		}

		/*
		 * LSI SAS controllers support local HSP
		 */
		if (deviceid == MPT_1064 || deviceid == MPT_1064E ||
		    deviceid == MPT_1068 || deviceid == MPT_1068E)
			controller.capability |= RAID_CAP_L_HSP;

		(void) memcpy(attr, (caddr_t)&controller, size);
		break;

	case OBJ_TYPE_ARRAY:
		size = sizeof (array);
		(void) memcpy((caddr_t)&array, attr, size);

		for (i = 0; i < nvols; ++i) {
			if (raidvolume[i].m_israid == 0)
				continue;
			if (raidvolume[i].m_raidtarg ==
			    device_id)
				break;
		}

		if (i == nvols)
			break;

		array.array_id = device_id;
		array.tag.idl.target_id = ARRAY_TARGET(array.array_id);
		array.tag.idl.lun = ARRAY_LUN(array.array_id);
		switch (raidvolume[i].m_state) {
		case MPT_RAID_STATE_OPTIMAL:
			array.state = ARRAY_STATE_OPTIMAL;
			break;
		case MPT_RAID_STATE_DEGRADED:
			array.state = ARRAY_STATE_DEGRADED;
			break;
		case MPT_RAID_STATE_FAILED:
			array.state = ARRAY_STATE_FAILED;
			break;
		case MPT_RAID_STATE_MISSING:
			array.state = ARRAY_STATE_MISSING;
			break;
		default:
			array.state = (uint32_t)OBJ_ATTR_NONE;
			break;
		}

		if (raidvolume[i].m_flags & MPI_IOCPAGE2_FLAG_VOLUME_INACTIVE)
			array.state |= ARRAY_STATE_INACTIVATE;

		array.capacity = raidvolume[i].m_raidsize;
		if (raidvolume[i].m_raidlevel == MPI_RAID_VOL_TYPE_IM) {
			array.raid_level = RAID_LEVEL_1;
			array.stripe_size = (uint32_t)OBJ_ATTR_NONE;
		} else if (raidvolume[i].m_raidlevel == MPI_RAID_VOL_TYPE_IS) {
			array.raid_level = RAID_LEVEL_0;
			array.stripe_size = raidvolume[i].m_stripesize;
		} else if (raidvolume[i].m_raidlevel == MPI_RAID_VOL_TYPE_IME) {
			array.raid_level = RAID_LEVEL_1E;
			array.stripe_size = raidvolume[i].m_stripesize;
		}

		if (raidvolume[i].m_settings &
		    MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE)
			array.write_policy = (uint32_t)CACHE_WR_ON;
		else
			array.write_policy = (uint32_t)CACHE_WR_OFF;
		array.read_policy = (uint32_t)OBJ_ATTR_NONE;
		(void) memcpy(attr, (caddr_t)&array, size);
		break;

	case OBJ_TYPE_ARRAY_PART:
		size = sizeof (arraypart);
		(void) memcpy((caddr_t)&arraypart, attr, size);

		for (i = 0; i < nvols; ++i) {
			if (raidvolume[i].m_israid == 0)
				continue;
			if (raidvolume[i].m_raidtarg ==
			    device_id)
				break;
		}
		if (i == nvols)
			return (ERR_DEVICE_INVALID);

		for (j = 0; j < raidvolume[i].m_ndisks; ++j)
			if (raidvolume[i].m_diskid[j] ==
			    index_id)
				break;
		if (j == raidvolume[i].m_ndisks)
			return (ERR_DEVICE_INVALID);

		arraypart.disk_id = index_id;
		switch (raidvolume[i].m_raidlevel) {
		case MPI_RAID_VOL_TYPE_IS:
			arraypart.size = raidvolume[i].m_raidsize /
			    raidvolume[i].m_ndisks;
			break;
		case MPI_RAID_VOL_TYPE_IM:
			arraypart.size = raidvolume[i].m_raidsize;
			break;
		case MPI_RAID_VOL_TYPE_IME:
			arraypart.size =
			    raidvolume[i].m_raidsize * 2 /
			    raidvolume[i].m_ndisks;
			break;
		default:
			arraypart.size = (uint64_t)OBJ_ATTR_NONE;
		}

		switch (raidvolume[i].m_diskstatus[j]) {
		case MPT_RAID_DISKSTATUS_MISSING:
		case MPT_RAID_DISKSTATUS_FAILED:
			arraypart.state = ARRAYPART_STATE_MISSED;
			break;
		default:
			arraypart.state = ARRAYPART_STATE_GOOD;
		}
		(void) memcpy(attr, (caddr_t)&arraypart, size);
		break;

	case OBJ_TYPE_DISK_SEG:
		size = sizeof (diskseg);
		(void) memcpy((caddr_t)&diskseg, attr, size);

		for (i = 0; i < nvols; ++i) {
			if (raidvolume[i].m_israid == 0)
				continue;

			for (j = 0; j < raidvolume[i].m_ndisks;
			    ++j) {
				if (device_id ==
				    raidvolume[i].m_diskid[j])
					break;
			}
			if (j < raidvolume[i].m_ndisks)
				break;
		}

		if (j < raidvolume[i].m_ndisks) {
			diskseg.offset = 0;
			diskseg.size = raidvolume[i].m_raidsize;
			if (raidvolume[i].m_raidlevel ==
			    MPI_RAID_VOL_TYPE_IS)
				diskseg.size /= raidvolume[i].m_ndisks;
			if (raidvolume[i].m_diskstatus[j] !=
			    MPT_RAID_DISKSTATUS_MISSING &&
			    raidvolume[i].m_diskstatus[j] !=
			    MPT_RAID_DISKSTATUS_FAILED)
				diskseg.state = DISKSEG_STATE_NORMAL;
		}
		(void) memcpy(attr, (caddr_t)&diskseg, size);
		break;

	case OBJ_TYPE_TASK:
		size = sizeof (task);
		(void) memcpy((caddr_t)&task, attr, size);

		for (i = 0; i < nvols; ++i) {
			if (raidvolume[i].m_israid == 0)
				continue;
			if (raidvolume[i].m_raidtarg ==
			    device_id)
				break;
		}
		if (i == nvols)
			return (ERR_DEVICE_INVALID);

		task.task_id = device_id;
		task.progress = (uint32_t)OBJ_ATTR_NONE;
		task.task_func = TASK_FUNC_BUILD;
		if (raidvolume[i].m_flags &
		    MPT_RAID_FLAG_RESYNCING &&
		    raidvolume[i].m_state ==
		    MPT_RAID_STATE_DEGRADED)
			task.task_state = TASK_STATE_RUNNING;
		else
			task.task_state = TASK_STATE_DONE;

		(void) memcpy(attr, (caddr_t)&task, size);
		break;

	default:
		break;
	}
	return (SUCCESS);
}

static int
mpt_raid_dev_unmounted(uint32_t controller_id, uint32_t target_id)
{
	struct mnttab mt;
	FILE *f;
	char path[MAX_PATH_LEN];

	(void) snprintf(path, MAX_PATH_LEN, "c%dt%dd0",
	    controller_id, target_id);

	f = fopen(MNTTAB, "r");

	while (getmntent(f, &mt) != EOF)
		if (strstr(mt.mnt_special, path) != NULL) {
			(void) fclose(f);
			return (ERR_ARRAY_IN_USE);
		}

	(void) fclose(f);
	return (SUCCESS);
}

static int
mpt_do_image_fixing(uint32_t controller_id, unsigned char *buf,
	int size, int lastimage)
{
	int i, n, ret, type = 0;
	uint8_t checksum = 0;
	uint16_t deviceid, subven, subsys;
	pcir_t	*pcir;

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	/*
	 * Patch the PCIR structure to change the PCI Device ID
	 * to match the PCI Device ID of the chip
	 */
	n = (buf[0x19] << 8) + buf[0x18];

	if (n + sizeof (pcir_t) < size) {
		pcir = (pcir_t *)(buf + n);

		if (pcir->signature[0] == 'P' &&
		    pcir->signature[1] == 'C' &&
		    pcir->signature[2] == 'I' &&
		    pcir->signature[3] == 'R') {
			type = pcir->type;

			if (type != 255)
				pcir->deviceId = LE_16(deviceid);
			if (lastimage)
				pcir->indicator |= LAST_IMAGE;
			else
				pcir->indicator &= ~LAST_IMAGE;
		}
		n = LE_16(pcir->imageLength) * 512;
	} else {
		n = size;
	}

	for (i = 0; i < size; i++)
		if (buf[i] == '@' && buf[i+1] == '(' &&
		    buf[i+2] == '#' && buf[i+3] == ')')
			break;

	if (type != 1 || i < size) {
		for (i = 0; i < n - 1; i++)
			checksum += buf[i];

		buf[i] = -checksum;
	}

	return (SUCCESS);
}

static int
mpt_get_max_volume_num(uint32_t controller_id, int *num)
{
	int length, ret;
	config_page_ioc_2_t *iocpage2;

	ret = mpt_get_configpage_length(controller_id, MPI_CONFIG_PAGETYPE_IOC,
	    2, 0, &length);
	if (ret != SUCCESS)
		return (ret);

	iocpage2 = malloc(length);
	(void) memset(iocpage2, 0, length);
	ret = mpt_get_configpage(controller_id, MPI_CONFIG_PAGETYPE_IOC, 2, 0,
	    iocpage2, length);
	if (ret != SUCCESS) {
		free(iocpage2);
		return (ret);
	}

	*num = LE_8(iocpage2->MaxVolumes);

	free(iocpage2);
	return (SUCCESS);
}

static void
getimagetype(uint8_t *rombuf, int *imagetype)
{
	uint8_t type = rombuf[gw(&rombuf[PCIR_OFF]) + PCIR_CODETYPE];

	if (type == 0)
		*imagetype = BIOS_IMAGE;
	else if (type == 1)
		*imagetype = FCODE_IMAGE;
	else
		*imagetype = UNKNOWN_IMAGE;
}

static int
getfcodever(uint8_t *rombuf, uint32_t nbytes, char **fcodeversion)
{
	int x, y, size;
	int found_1 = 0, found_2 = 0;
	int image_length = 0;
	int num_of_images = 0;
	uint8_t *rombuf_1 = NULL;
	uint16_t image_units = 0;

	/*
	 * Single Image - Open firmware image
	 */
	if (rombuf[gw(&rombuf[PCIR_OFF]) + PCIR_CODETYPE] == 1) {
		rombuf_1 = rombuf + gw(rombuf + PCIR_OFF) + PCI_PDS_INDICATOR;
		num_of_images = 1;
		goto process_image;
	}

	/*
	 * Combined Image - First Image - x86/PC-AT Bios image
	 */
	if (rombuf[gw(&rombuf[PCIR_OFF]) + PCIR_CODETYPE] != 0) {
		/* This is neither open image nor Bios/Fcode combined image */
		return (ERR_OP_FAILED);
	}

	/*
	 * Seek to 2nd Image
	 */
	rombuf_1 = rombuf + gw(rombuf + PCI_ROM_PCI_DATA_STRUCT_PTR);
	image_units = gw(rombuf_1 + PCI_PDS_IMAGE_LENGTH);
	image_length = image_units * PCI_IMAGE_UNIT_SIZE;
	rombuf_1 += image_length;

	/*
	 * Combined Image - Second Image - Open Firmware image
	 */
	if (rombuf_1[PCI_PDS_CODE_TYPE] != 1) {
		/* This is neither open image nor Bios/Fcode combined image */
		return (ERR_OP_FAILED);
	}
	rombuf_1 += PCI_PDS_INDICATOR;
	num_of_images = 2;

process_image:
	/*
	 * This should be the last image
	 */
	if (*rombuf_1 != LAST_IMAGE) {
		/* This is not a valid Bios/Fcode image file */
		return (ERR_OP_FAILED);
	}

	/*
	 * Scan through the bios/fcode file to get the fcode version
	 * 0x12 and 0x7 indicate the start of the fcode version string
	 */
	for (x = 0; x < (nbytes - 8); x++) {
		if ((rombuf[x] == FCODE_VERS_KEY1) &&
		    (rombuf[x+1] == FCODE_VERS_KEY2) &&
		    (rombuf[x+2] == 'v') && (rombuf[x+3] == 'e') &&
		    (rombuf[x+4] == 'r') && (rombuf[x+5] == 's') &&
		    (rombuf[x+6] == 'i') && (rombuf[x+7] == 'o') &&
		    (rombuf[x+8] == 'n')) {
			found_1 = 1;
			break;
		}
	}

	/*
	 * Store the version string if we have found the beginning of it
	 */
	if (found_1) {
		while (x > 0) {
			if (rombuf[--x] == FCODE_VERS_KEY1) {
				if (rombuf[x-1] != FCODE_VERS_KEY1) {
					x++;
				}
				break;
			}
		}
		if (x > 0) {
			*fcodeversion = (char *)malloc(rombuf[x] + 1);
			for (y = 0; y < rombuf[x]; y++) {
				(*fcodeversion)[y] = rombuf[x+y+1];
			}
			(*fcodeversion)[y] = '\0';
		} else {
			found_1 = 0;
		}
	}

	/*
	 * Scan through the bios/fcode file to get the Bios version
	 * "@(#)" string indicates the start of the Bios version string
	 * Append this version string, after already existing fcode version.
	 */
	if (num_of_images == 2) {
		for (x = 0; x < (nbytes - 4); x++) {
			if ((rombuf[x] == '@') && (rombuf[x+1] == '(') &&
			    (rombuf[x+2] == '#') && (rombuf[x+3] == ')')) {
				found_2 = 1;
				break;
			}
		}

		if (found_2) {
			x += 4;
			(*fcodeversion)[y] = '\n';
			size = y + strlen((char *)(rombuf + x)) +
			    strlen(BIOS_STR) + 2;
			*fcodeversion = (char *)realloc((*fcodeversion), size);
			y++;
			(*fcodeversion)[y] = '\0';
			(void) strlcat(*fcodeversion, BIOS_STR, size);
			(void) strlcat(*fcodeversion, (char *)(rombuf + x),
			    size);
		}
	}

	if (found_1 || found_2)
		return (SUCCESS);

	return (ERR_OP_FAILED);
}

static void
getfwver(uint8_t *rombuf, char *fwversion)
{
	(void) snprintf(fwversion, 8, "%d.%.2d.%.2d.%.2d",
	    rombuf[FW_ROM_OFFSET_VERSION + 3],
	    rombuf[FW_ROM_OFFSET_VERSION + 2],
	    rombuf[FW_ROM_OFFSET_VERSION + 1],
	    rombuf[FW_ROM_OFFSET_VERSION + 0]);
}

static int
getbioscodever(uint8_t *rombuf, uint32_t nbytes, char **biosversion)
{
	int x, size;
	int found = 0;

	for (x = 0; x < (nbytes - 4); x++) {
		if ((rombuf[x] == '@') && (rombuf[x+1] == '(') &&
		    (rombuf[x+2] == '#') && (rombuf[x+3] == ')')) {
			found = 1;
			break;
		}
	}

	if (found) {
		x += 4;
		size = strlen((char *)(rombuf + x)) + strlen(BIOS_STR) + 1;
		*biosversion = (char *)realloc((*biosversion), size);
		bcopy((char *)(rombuf + x), *biosversion, size - 1);
		(*biosversion)[size - 1] = '\0';
		return (SUCCESS);
	}

	return (ERR_OP_FAILED);
}

static int
checkfile(uint8_t *rombuf, uint32_t nbytes, int *imagetype)
{
	char *imageversion = NULL;
	char *biosversion = NULL;
	char fwversion[8];

	if (gw(&rombuf[0]) == PCIROM_SIG) {

		*imagetype = UNKNOWN_IMAGE;
		getimagetype(rombuf, imagetype);

		if (*imagetype == FCODE_IMAGE) {
			if (getfcodever(rombuf, nbytes, &imageversion) ==
			    SUCCESS && imageversion != NULL) {
				(void) printf(gettext("Image file contains "
				    "fcode version \t%s\n"), imageversion);
				free(imageversion);
			}
		} else if (*imagetype == BIOS_IMAGE) {
			if (getbioscodever(rombuf, nbytes, &biosversion) ==
			    SUCCESS && biosversion != NULL) {
				(void) printf(gettext("Image file contains "
				    "BIOS version \t%s\n"), biosversion);
				free(biosversion);
			}
		} else {
			/* When imagetype equals to UNKNOWN_IMAGE */
			return (ERR_OP_FAILED);
		}

	} else if (gw(&rombuf[3]) == FW_ROM_ID) {
			getfwver(rombuf, fwversion);

			if ((gw(&rombuf[FW_ROM_OFFSET_CHIP_TYPE]) &
			    MPI_FW_HEADER_PID_PROD_MASK) ==
			    MPI_FW_HEADER_PID_PROD_IM_SCSI) {
				(void) printf(gettext("ROM image contains "
				    "MPT firmware " "version %s "
				    "(w/Integrated Mirroring)\n"),
				    fwversion);
			} else {
				(void) printf(gettext("ROM image contains "
				    "MPT firmware " "version %s\n"),
				    fwversion);
			}
	} else {
		return (ERR_OP_FAILED);
	}

	return (SUCCESS);
}

static int
mpt_check_mpxio_enable(uint32_t controller_id)
{
	int n, value, ret;
	char *type = NULL;
	char path[MAX_PATH_LEN], physpath[MAX_PATH_LEN];
	char *abspath, *minor;
	di_node_t mpt_node;

	(void) snprintf(path, MAX_PATH_LEN, "%s/c%d", CFGDIR, controller_id);
	n = readlink(path, physpath, MAX_PATH_LEN);
	if (n <= 0)
		return (ERR_DEVICE_NOENT);

	physpath[n] = '\0';
	abspath = strstr(physpath, DEVICESDIR);
	abspath += sizeof (DEVICESDIR) - 2;

	if ((minor = strrchr(abspath, ':')) != NULL)
		*minor = '\0';

	if ((mpt_node = di_init(abspath, DINFOCPYALL)) == DI_NODE_NIL) {
		return (ERR_DEVICE_NOENT);
	}

	value = di_prop_lookup_strings(DDI_DEV_T_ANY, mpt_node,
	    "mpxio-disable", &type);


	if (value >= 0) {
		if (strcmp(type, "yes") == SUCCESS)
			ret = MPT_MPXIO_DISABLE;
		else if (strcmp(type, "no") == SUCCESS)
			ret = MPT_MPXIO_ENABLE;
		else
			ret = ERR_OP_FAILED;
	} else {
		ret = ERR_DEVICE_INVALID;
	}

	di_fini(mpt_node);
	return (ret);
}

int
rdcfg_open_controller(uint32_t controller_id, char **err_ptr)
{
	int fd, ret;
	char path[MAX_PATH_LEN];
	struct devh_t *devh;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	ret = mpt_controller_id_to_path(controller_id, path);

	if (ret < SUCCESS)
		return (ret);

	fd = open(path, O_RDWR | O_NDELAY);
	if (fd < 0)
		return (ERR_DRIVER_OPEN);

	devh = malloc(sizeof (struct devh_t));
	if (devh == NULL) {
		(void) close(fd);
		return (ERR_NOMEM);
	}

	devh->controller_id = controller_id;
	devh->fd = fd;
	devh->next = devh_sys;
	devh_sys = devh;

	(void) mpt_get_raidinfo(controller_id);

	return (SUCCESS);

}

int
rdcfg_close_controller(uint32_t controller_id, char **err_ptr)
{
	struct devh_t *devh, *tmp;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	devh = devh_sys;
	tmp = devh;
	while (devh != NULL && devh->controller_id != controller_id) {
		tmp = devh;
		devh = devh->next;
	}

	if (devh == NULL)
		return (SUCCESS);

	(void) close(devh->fd);

	if (tmp == devh_sys)
		devh_sys = devh_sys->next;
	else
		tmp->next = devh->next;

	free(devh);

	(void) mpt_get_raidinfo(controller_id);

	return (SUCCESS);
}

int
rdcfg_compnum(uint32_t controller_id, uint32_t container_id,
	raid_obj_type_id_t container_type, raid_obj_type_id_t component_type)
{
	int num = 0, ret;

	if (container_type == OBJ_TYPE_CONTROLLER &&
	    component_type == OBJ_TYPE_DISK)
		return (mpt_disks_compinfo(controller_id, 0, NULL));

	if (container_type == OBJ_TYPE_DISK &&
	    component_type == OBJ_TYPE_PROP)
		return (mpt_props_compinfo(controller_id, container_id, NULL));

	if (container_type == OBJ_TYPE_DISK &&
	    component_type == OBJ_TYPE_HSP)
		return (mpt_hsp_compinfo(controller_id, container_id, NULL));

	ret = mpt_get_compinfo(controller_id, container_id, container_type,
	    component_type, (uint32_t *)&num, NULL);

	if (ret == SUCCESS)
		return (num);
	return (ret);
}

int
rdcfg_complist(uint32_t controller_id, uint32_t container_id,
	raid_obj_type_id_t container_type, raid_obj_type_id_t component_type,
	int comp_num, void *ids)
{
	int ret;

	if (container_type == OBJ_TYPE_CONTROLLER &&
	    component_type == OBJ_TYPE_DISK)
		return (mpt_disks_compinfo(controller_id,
		    comp_num, ids));

	if (container_type == OBJ_TYPE_DISK &&
	    component_type == OBJ_TYPE_PROP)
		return (mpt_props_compinfo(controller_id, container_id, ids));

	if (container_type == OBJ_TYPE_DISK &&
	    component_type == OBJ_TYPE_HSP)
		return (mpt_hsp_compinfo(controller_id, container_id, ids));

	ret = mpt_get_compinfo(controller_id, container_id, container_type,
	    component_type, (uint32_t *)&comp_num, ids);

	return (ret);
}

int
rdcfg_get_attr(uint32_t controller_id, uint32_t device_id, uint32_t index_id,
	raid_obj_type_id_t type, void *attr)
{
	if (type == OBJ_TYPE_DISK)
		return (mpt_disk_attr(controller_id, attr));
	else if (type == OBJ_TYPE_PROP)
		return (mpt_disk_prop_attr(controller_id, attr));
	else if (type == OBJ_TYPE_HSP)
		return (mpt_hsp_attr(controller_id, device_id, attr));
	else
		return (mpt_get_attr(controller_id, device_id, index_id,
		    type, attr));
}

int
rdcfg_set_attr(uint32_t controller_id, uint32_t device_id, uint32_t cmd,
	uint32_t *value, char **err_ptr)
{
	int length, ret;
	uint8_t volume_bus, volume_id;
	uint16_t attr;
	uint32_t addr;
	msg_raid_action_t raid_action;
	msg_raid_action_reply_t raid_action_reply;
	config_page_raid_vol_0_t *raidpage0;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	volume_bus = 0;
	volume_id = (uint8_t)device_id;

	(void) memset(&raid_action, 0, sizeof (raid_action));
	(void) memset(&raid_action_reply, 0, sizeof (raid_action_reply));

	raid_action.Function = MPI_FUNCTION_RAID_ACTION;
	raid_action.VolumeBus = LE_8(volume_bus);
	raid_action.VolumeID = LE_8(volume_id);

	switch (cmd) {
	case SET_CACHE_WR_PLY:
		addr = (volume_bus << 8) + volume_id;

		ret = mpt_get_configpage_length(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_VOLUME,
		    0, addr, &length);
		if (ret != SUCCESS)
			return (ret);

		raidpage0 = malloc(length);
		(void) memset(raidpage0, 0, length);
		ret = mpt_get_configpage(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_VOLUME, 0, addr,
		    raidpage0, length);
		if (ret != SUCCESS) {
			free(raidpage0);
			return (ret);
		}

		attr = LE_16(raidpage0->VolumeSettings.Settings);

		if (*value == CACHE_WR_ON)
			attr |= MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE;
		else
			attr &= ~MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE;

		raidpage0->VolumeSettings.Settings = LE_16(attr);
		(void) memcpy(&raid_action.ActionDataWord,
		    &raidpage0->VolumeSettings,
		    sizeof (raid_action.ActionDataWord));

		raid_action.Action = MPI_RAID_ACTION_CHANGE_VOLUME_SETTINGS;

		free(raidpage0);
		break;

	case SET_ACTIVATION_PLY:
		if (*value == ARRAY_ACT_ACTIVATE)
			raid_action.Action = MPI_RAID_ACTION_ACTIVATE_VOLUME;
		break;

	default:
		break;
	}

	ret = mpt_do_command(controller_id, &raid_action, sizeof (raid_action),
	    &raid_action_reply, sizeof (raid_action_reply), NULL, 0, NULL, 0);

	return (ret);
}

int
rdcfg_array_create(uint32_t controller_id, array_attr_t *array, int num,
	arraypart_attr_t *array_part, char **err_ptr)
{
	int i, j, ret, disk_num, dual_path_num = 0;
	int is_ssd_1, is_ssd_2, is_sata_1, is_sata_2;
	disklist_t disklist;
	uint8_t targetid, rlevel;
	uint16_t deviceid, subsys, subven;
	char guid1[MAX_PATH_LEN], guid2[MAX_PATH_LEN];
	uint32_t *disk_ids = NULL, *disk_path_ids = NULL;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	if (array->capacity == OBJ_ATTR_NONE)
		return (ERR_OP_NO_IMPL);

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
		if (ret != SUCCESS)
			return (ret);

	/*
	 * At least 2 disks required to build an RAID array;
	 * and we should count in the separators
	 */
	if (num < 4)
		return (ERR_ARRAY_DISKNUM);

	/*
	 * Common Operations for both SCSI and SAS controllers
	 */
	if (array->raid_level == RAID_LEVEL_0)
		rlevel = MPI_RAID_VOL_TYPE_IS;
	else if (array->raid_level == RAID_LEVEL_1)
		rlevel = MPI_RAID_VOL_TYPE_IM;
	else if (array->raid_level == RAID_LEVEL_1E)
		rlevel = MPI_RAID_VOL_TYPE_IME;
	else
		return (ERR_ARRAY_LEVEL);

	disklist.m_ndisks = num - 2;
	targetid = array_part[1].disk_id;
	disk_path_ids = (uint32_t *)malloc(disklist.m_ndisks *
	    sizeof (uint32_t));

	for (i = 0; i < disklist.m_ndisks; i++) {
		disklist.m_diskid[i] = array_part[i + 1].disk_id;
		if (targetid > disklist.m_diskid[i])
			return (ERR_ARRAY_LAYOUT);
	}

	switch (deviceid) {
	case MPT_1030:
		for (i = 1; i < disklist.m_ndisks; i++) {
			if ((ret = mpt_raid_dev_unmounted(controller_id,
			    disklist.m_diskid[i])) != SUCCESS)
				return (ret);
		}
		break;

	case MPT_1064:
	case MPT_1064E:
	case MPT_1068:
	case MPT_1068E:
		/*
		 * For LSI 1064(E)/1068(E) controllers,
		 * Mounted disks can not be used for volumes;
		 * GUID should be checked for dual port disks.
		 */
		for (i = 0; i < disklist.m_ndisks; i++) {
			if ((ret = mpt_raid_dev_unmounted(controller_id,
			    disklist.m_diskid[i])) != SUCCESS)
				return (ret);

			if ((ret = mpt_disk_info_guid(controller_id,
			    disklist.m_diskid[i], guid1)) != SUCCESS)
				continue;

			for (j = i + 1; j < disklist.m_ndisks; j++) {
				if ((ret =  mpt_disk_info_guid(controller_id,
				    disklist.m_diskid[j], guid2)) != SUCCESS)
					continue;

				/*
				 * devices without a valid GUID
				 * can NOT have dual ports
				 */
				if ((strcmp(guid1, INVALID_GUID) != SUCCESS) &&
				    (strcmp(guid2, INVALID_GUID) != SUCCESS) &&
				    (strcmp(guid1, guid2) == SUCCESS))
					return (ERR_ARRAY_LAYOUT);
			}
		}

		/*
		 * Check and save the additional path for dual port disks
		 */

		disk_num = mpt_disks_compinfo(controller_id, 0, NULL);
		disk_ids = (uint32_t *)malloc(disk_num * sizeof (uint32_t));
		(void) mpt_disks_compinfo(controller_id, disk_num, disk_ids);

		for (i = 0; i < disk_num; i++) {
			if ((ret = mpt_disk_info_guid(controller_id,
			    disk_ids[i], guid1)) != SUCCESS)
				continue;

			for (j = 0; j < disklist.m_ndisks; j++) {
				if (disk_ids[i] == disklist.m_diskid[j])
					continue;

				if ((ret = mpt_disk_info_guid(controller_id,
				    disklist.m_diskid[j], guid2)) != SUCCESS)
					continue;

				/*
				 * devices without a valid GUID
				 * can NOT have dual ports
				 */
				if ((strcmp(guid1, INVALID_GUID) != SUCCESS) &&
				    (strcmp(guid2, INVALID_GUID) != SUCCESS) &&
				    (strcmp(guid1, guid2) == SUCCESS)) {
					/*
					 * Save the additional path and
					 * Configure down after creation
					 */
					disk_path_ids[dual_path_num] =
					    disk_ids[i];
					dual_path_num++;
				}
			}
		}

		if (disk_ids)
			free(disk_ids);


		/*
		 * For LSI 1064(E)/1068(E) controllers,
		 * NO mixing of SAS and SATA disk in
		 * a single RAID volume is allowed
		 */
		is_sata_1 = mpt_check_sata_disk(controller_id, 0,
		    disklist.m_diskid[0]);

		for (i = 1; i < disklist.m_ndisks; i++) {
			is_sata_2 = mpt_check_sata_disk(controller_id, 0,
			    disklist.m_diskid[i]);

			if (is_sata_1 != is_sata_2)
				return (ERR_ARRAY_LAYOUT);
		}

		/*
		 * For LSI 1064(E)/1068(E) controllers,
		 * NO mixing of SSD and Mechanical in
		 * a single RAID volume is allowed
		 */
		is_ssd_1 = mpt_check_ssd_disk(controller_id, 0,
		    disklist.m_diskid[0]);

		for (i = 1; i < disklist.m_ndisks; i++) {
			is_ssd_2 = mpt_check_ssd_disk(controller_id, 0,
			    disklist.m_diskid[i]);

			if (is_ssd_1 != is_ssd_2)
				return (ERR_ARRAY_LAYOUT);

		}

		break;

	default:
		break;
	}

	ret = mpt_create_volume(controller_id, &disklist, targetid, rlevel,
	    array->stripe_size);
	if (ret != SUCCESS)
		return (ret);

	array->array_id = (uint32_t)targetid;

	/*
	 * In case the controller is LSI 1030,
	 * or LSI 1064(E)/1068(E) in mpxio disable mode
	 * Configure down all volume disks
	 * and configure up array target id;
	 * In case the controller is LSI 1064(E)/1068(E) in mpxio enable mode,
	 * Configuration work is done by driver.
	 */
	switch (deviceid) {
	case MPT_1030:
		for (i = 0; i < disklist.m_ndisks; i++) {
			ret = mpt_raid_dev_config(CFGA_CMD_UNCONFIGURE,
			    controller_id, disklist.m_diskid[i], 0);
			if (ret == ERR_ARRAY_CONFIG)
				ret = SUCCESS;
			else if (ret < SUCCESS)
				return (ret);
		}

		ret = mpt_raid_dev_config(CFGA_CMD_CONFIGURE, controller_id,
		    targetid, 0);
		if (ret == ERR_ARRAY_CONFIG)
			ret = SUCCESS;
		else if (ret < SUCCESS)
			return (ret);

		break;

	case MPT_1064:
	case MPT_1064E:
	case MPT_1068:
	case MPT_1068E:
		if (mpt_check_mpxio_enable(controller_id) ==
		    MPT_MPXIO_DISABLE) {
			for (i = 0; i < disklist.m_ndisks; i++) {
				ret = mpt_raid_dev_config(CFGA_CMD_UNCONFIGURE,
				    controller_id, disklist.m_diskid[i], 0);
				if (ret == ERR_ARRAY_CONFIG)
					ret = SUCCESS;
				else if (ret < SUCCESS)
					return (ret);
			}

			ret = mpt_raid_dev_config(CFGA_CMD_CONFIGURE,
			    controller_id, targetid, 0);
			if (ret == ERR_ARRAY_CONFIG)
				ret = SUCCESS;
			else if (ret < SUCCESS)
				return (ret);

			/*
			 * Configure down additional path of dual port disks
			 */
			for (i = 0; i < dual_path_num; i++) {
				ret = mpt_raid_dev_config(CFGA_CMD_UNCONFIGURE,
				    controller_id, disk_path_ids[i], 0);
				if (ret == ERR_ARRAY_CONFIG)
					ret = SUCCESS;
				else if (ret < SUCCESS)
					return (ret);
			}
		}

		if (disk_path_ids)
			free(disk_path_ids);
		break;

	default:
		break;
	}

	(void) mpt_get_raidinfo(controller_id);

	return (SUCCESS);
}

int
rdcfg_array_delete(uint32_t controller_id, uint32_t array_id, char **err_ptr)
{
	int i, disk_num, path_num, ret;
	uint16_t deviceid, subsys, subven;
	uint32_t *disk_ids = NULL, *path_ids = NULL;
	msg_raid_action_t	raid_action;
	msg_raid_action_reply_t	raid_action_reply;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	if ((ret = mpt_raid_dev_unmounted(controller_id, array_id)) != SUCCESS)
		return (ret);

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	ret = mpt_get_compinfo(controller_id, array_id, OBJ_TYPE_ARRAY,
	    OBJ_TYPE_ARRAY_PART, (uint32_t *)&disk_num, NULL);
	if (ret != SUCCESS)
		return (ret);

	disk_ids = (uint32_t *)malloc(disk_num * sizeof (uint32_t));
	ret = mpt_get_compinfo(controller_id, array_id, OBJ_TYPE_ARRAY,
	    OBJ_TYPE_ARRAY_PART, (uint32_t *)&disk_num, disk_ids);
	if (ret != SUCCESS)
		return (ret);
	disk_ids[0] = array_id;

	if (deviceid == MPT_1064 || deviceid == MPT_1064E ||
	    deviceid == MPT_1068 || deviceid == MPT_1068E) {
		ret = mpt_get_phys_path(controller_id, array_id,
		    &path_num, NULL);
		if (ret != SUCCESS)
			return (ret);

		if (path_num != 0) {
			path_ids = (uint32_t *)malloc(path_num *
			    sizeof (uint32_t));
			ret = mpt_get_phys_path(controller_id, array_id,
			    &path_num, path_ids);
			if (ret != SUCCESS)
				return (ret);
		}
	}

	(void) memset(&raid_action, 0, sizeof (raid_action));
	(void) memset(&raid_action_reply, 0, sizeof (raid_action_reply));

	raid_action.Function = LE_8(MPI_FUNCTION_RAID_ACTION);
	raid_action.Action = LE_8(MPI_RAID_ACTION_DELETE_VOLUME);
	raid_action.VolumeBus = LE_8(0);
	raid_action.VolumeID = LE_8(array_id);
	raid_action.ActionDataWord =
	    LE_32(MPI_RAID_ACTION_ADATA_DEL_PHYS_DISKS);

	ret = mpt_do_command(controller_id, &raid_action, sizeof (raid_action),
	    &raid_action_reply, sizeof (raid_action_reply), NULL, 0, NULL, 0);

	/*
	 * In case the controller is LSI 1030,
	 * or LSI 1064(E)/1068(E) in mpxio disable mode
	 * Configure up all disks from volume;
	 * In case the controller is LSI 1064(E)/1068(E) in mpxio enable mode,
	 * Configuration work is done by driver.
	 */
	if (deviceid == MPT_1030) {
		for (i = 0; i < disk_num; i++) {
			ret = mpt_raid_dev_config(CFGA_CMD_CONFIGURE,
			    controller_id, disk_ids[i], 0);
			if (ret == ERR_ARRAY_CONFIG)
				ret = SUCCESS;
			else if (ret < SUCCESS)
				return (ret);
		}
	} else if (deviceid == MPT_1064 || deviceid == MPT_1064E ||
	    deviceid == MPT_1068 || deviceid == MPT_1068E) {
		if (mpt_check_mpxio_enable(controller_id) ==
		    MPT_MPXIO_DISABLE) {
			for (i = 0; i < disk_num; i++) {
				ret = mpt_raid_dev_config(CFGA_CMD_CONFIGURE,
				    controller_id, disk_ids[i], 0);
				if (ret == ERR_ARRAY_CONFIG)
					ret = SUCCESS;
				else if (ret < SUCCESS)
					return (ret);
			}

			/*
			 * Configure up additional path of dual port disks
			 */
			for (i = 0; i < path_num; i++) {
				ret = mpt_raid_dev_config(CFGA_CMD_CONFIGURE,
				    controller_id, path_ids[i], 0);
				if (ret == ERR_ARRAY_CONFIG)
					ret = SUCCESS;
				else if (ret < SUCCESS)
					return (ret);
			}
		}
	}

	(void) mpt_get_raidinfo(controller_id);

	return (ret);
}

int
rdcfg_hsp_bind(uint32_t controller_id,
	hsp_relation_t *hsp_relations, char **err_ptr)
{
	int i, hsp_id, ret, disk_num, dual_path = 0;
	uint32_t path_id, *disk_ids;
	char disk_guid[MAX_PATH_LEN], hsp_guid[MAX_PATH_LEN];
	config_page_raid_phys_disk_0_t physdiskpage0;
	msg_raid_action_t raid_action;
	msg_raid_action_reply_t raid_action_reply;
	msg_config_reply_t config_reply;
	msg_ioc_facts_reply_t ioc_reply;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	if (hsp_relations->array_id == (uint32_t)OBJ_ATTR_NONE)
		return (ERR_OP_ILLEGAL);

	(void) mpt_disk_info_guid(controller_id, hsp_relations->disk_id,
	    hsp_guid);

	disk_num = mpt_get_disk_ids(controller_id, 0, NULL);
	disk_ids = (uint32_t *)malloc(disk_num * sizeof (uint32_t));
	(void) mpt_get_disk_ids(controller_id, disk_num, disk_ids);

	for (i = 0; i < disk_num; i++) {
		if (hsp_relations->disk_id == disk_ids[i])
			continue;

		(void) mpt_disk_info_guid(controller_id, disk_ids[i],
		    disk_guid);

		/*
		 * devices without a valid GUID
		 * can NOT have dual ports
		 */
		if ((strcmp(hsp_guid, INVALID_GUID) != SUCCESS) &&
		    (strcmp(disk_guid, INVALID_GUID) != SUCCESS) &&
		    (strcmp(hsp_guid, disk_guid) == SUCCESS)) {
			dual_path = 1;
			path_id = disk_ids[i];
			break;
		}
	}

	for (i = 0; i < MPT_MAX_RAIDVOLS; ++i) {
		if (hsp_relations->array_id == raidvolume[i].m_raidtarg) {
			hsp_id = i;
			break;
		}
	}

	(void) memset(&config_reply, 0, sizeof (config_reply));
	(void) memset(&ioc_reply, 0, sizeof (ioc_reply));
	(void) memset(&physdiskpage0, 0, sizeof (physdiskpage0));

	ret = mpt_get_configpage_header(controller_id,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK, 0, 0, &config_reply);
	if (ret != SUCCESS)
		return (ret);

	ret = mpt_get_iocfacts(controller_id, &ioc_reply);
	if (ret != SUCCESS)
		return (ret);

	physdiskpage0.Header = config_reply.Header;
	physdiskpage0.PhysDiskIOC = ioc_reply.IOCNumber;
	physdiskpage0.PhysDiskBus = BUS(hsp_relations->disk_id);
	physdiskpage0.PhysDiskID = TARGET(hsp_relations->disk_id);
	physdiskpage0.PhysDiskSettings.HotSparePool =
	    (uint8_t)(1 << hsp_id);

	(void) memset(&raid_action, 0, sizeof (raid_action));
	(void) memset(&raid_action_reply, 0, sizeof (raid_action_reply));

	raid_action.Function = LE_8(MPI_FUNCTION_RAID_ACTION);
	raid_action.Action = LE_8(MPI_RAID_ACTION_CREATE_PHYSDISK);

	ret = mpt_do_command(controller_id, &raid_action,
	    sizeof (raid_action) - sizeof (raid_action.ActionDataSGE),
	    &raid_action_reply, sizeof (raid_action_reply),
	    NULL, 0, &physdiskpage0, sizeof (physdiskpage0));

	if (ret != SUCCESS)
		return (ret);

	/*
	 * For LSI 1064(E)/1068(E) controller,
	 * When mpxio is disabled, configure down the hotspare disk
	 * And also configure down additional path for dual port disks
	 * When mpxio is enabled, configuration work is done by driver
	 */
	if (mpt_check_mpxio_enable(controller_id) == MPT_MPXIO_DISABLE) {
		ret = mpt_raid_dev_config(CFGA_CMD_UNCONFIGURE, controller_id,
		    hsp_relations->disk_id, 0);
		if (ret == ERR_ARRAY_CONFIG)
			ret = SUCCESS;
		else if (ret < SUCCESS)
			return (ret);


		if (dual_path == 1) {
			ret = mpt_raid_dev_config(CFGA_CMD_UNCONFIGURE,
			    controller_id, path_id, 0);
				if (ret == ERR_ARRAY_CONFIG)
					ret = SUCCESS;
				else if (ret < SUCCESS)
					return (ret);
		}
	}

	(void) mpt_get_raidinfo(controller_id);

	return (ret);
}

int
rdcfg_hsp_unbind(uint32_t controller_id,
	hsp_relation_t *hsp_relations, char **err_ptr)
{
	int i, length, physdisk_num, ret;
	uint8_t physdisk, path_num;
	uint16_t deviceid, subsys, subven;
	uint32_t path_id;
	msg_raid_action_t raid_action;
	msg_raid_action_reply_t raid_action_reply;
	config_page_ioc_3_t *iocpage3;
	config_page_raid_phys_disk_0_t *diskpage0;
	config_page_raid_phys_disk_1_t *diskpage1;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	/*
	 * Get hidden disk information in volumes
	 */
	ret = mpt_get_configpage_length(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, &length);
	if (ret != SUCCESS)
		return (ret);

	iocpage3 = malloc(length);
	(void) memset(iocpage3, 0, length);
	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, iocpage3, length);
	if (ret != SUCCESS) {
		free(iocpage3);
		return (ret);
	}

	physdisk_num = iocpage3->NumPhysDisks;
	for (i = 0; i < physdisk_num; ++i) {
		physdisk = iocpage3->PhysDisk[i].PhysDiskNum;

		ret = mpt_get_configpage_length(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
		    0, physdisk, &length);
		if (ret != SUCCESS)
			continue;

		diskpage0 = malloc(length);
		(void) memset(diskpage0, 0, length);
		ret = mpt_get_configpage(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
		    0, physdisk, diskpage0, length);
		if (ret != SUCCESS) {
			free(diskpage0);
			continue;
		}

		if (hsp_relations->disk_id == diskpage0->PhysDiskID)
			break;
	}

	/*
	 * physical disk page 1 contains
	 * path infomation for dual path disks
	 */

	ret = mpt_get_configpage_length(controller_id,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
	    1, physdisk, &length);
	if (ret != SUCCESS)
		return (ret);
	diskpage1 = malloc(length);
	(void) memset(diskpage1, 0, length);
	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
	    1, physdisk, diskpage1, length);
	if (ret != SUCCESS) {
		free(diskpage1);
		return (ret);
	}

	path_num = diskpage1->NumPhysDiskPaths;

	if (path_num == 2) {
		for (i = 0; i < 2; i++) {
			if ((diskpage0->PhysDiskID ==
			    diskpage1->Path[i].PhysDiskID) &&
			    (diskpage0->PhysDiskBus ==
			    diskpage1->Path[i].PhysDiskBus))
				continue;

			path_id = DISK_ID(
			    diskpage1->Path[i].PhysDiskBus,
			    diskpage1->Path[i].PhysDiskID, 0);
		}
	}

	free(diskpage0);
	free(diskpage1);
	free(iocpage3);

	(void) memset(&raid_action, 0, sizeof (raid_action));
	(void) memset(&raid_action_reply, 0, sizeof (raid_action_reply));

	raid_action.Function = MPI_FUNCTION_RAID_ACTION;
	raid_action.Action = MPI_RAID_ACTION_DELETE_PHYSDISK;
	raid_action.PhysDiskNum = physdisk;

	ret = mpt_do_command(controller_id, &raid_action, sizeof (raid_action),
	    &raid_action_reply, sizeof (raid_action_reply),
	    NULL, 0, NULL, 0);

	if (ret != SUCCESS)
		return (ret);

	/*
	 * For LSI 1064(E)/1068(E) controller,
	 * When mpxio is disabled, configure up the hotspare disk
	 * And also configure up additional path for dual port disks
	 * When mpxio is enabled, configuration work is done by driver
	 */
	if (mpt_check_mpxio_enable(controller_id) == MPT_MPXIO_DISABLE) {
		ret = mpt_raid_dev_config(CFGA_CMD_CONFIGURE, controller_id,
		    hsp_relations->disk_id, 0);
		if (ret == ERR_ARRAY_CONFIG)
			ret = SUCCESS;
		else if (ret < SUCCESS)
			return (ret);

		if (path_num == 2) {
			ret = mpt_raid_dev_config(CFGA_CMD_CONFIGURE,
			    controller_id, path_id, 0);
			if (ret == ERR_ARRAY_CONFIG)
				ret = SUCCESS;
			else if (ret < SUCCESS)
				return (ret);
		}
	}

	(void) mpt_get_raidinfo(controller_id);

	return (ret);
}

int
rdcfg_flash_fw(uint32_t controller_id, char *buf, uint32_t size,
	char **err_ptr)
{
	int fd, imagetype, ret;
	uint8_t *rombuf;
	update_flash_t flashdata;

	if (err_ptr != NULL)
		*err_ptr = NULL;

	fd = mpt_controller_id_to_fd(controller_id);
	if (fd < 0)
		return (fd);

	buf = (char *)realloc(buf, size + 1);
	buf[size] = 0;

	rombuf = (uint8_t *)buf;
	if (checkfile(rombuf, size, &imagetype) != SUCCESS) {
		return (ERR_OP_FAILED);
	}

	(void) memset(&flashdata, 0, sizeof (flashdata));
	if ((rombuf[0] == 0x55) && (rombuf[1] == 0xaa)) {
		flashdata.type = FW_TYPE_FCODE;
		(void) mpt_do_image_fixing(controller_id,
		    (unsigned char *)buf, size, NULL);
	} else {
		flashdata.type = FW_TYPE_UCODE;
	}
	flashdata.ptrbuffer = (caddr_t)buf;
	flashdata.size = size;

	ret = ioctl(fd, RAID_UPDATEFW, &flashdata);

	if (ret >= 0)
		return (SUCCESS);

	return (ERR_OP_FAILED);
}

static int
mpt_controller_id_to_path(uint32_t controller_id, char *path)
{
	int fd;
	char buf[MAX_PATH_LEN] = {0}, buf1[MAX_PATH_LEN] = {0}, *colon;

	(void) snprintf(buf, MAX_PATH_LEN, "%s/c%d", CFGDIR, controller_id);
	if (readlink(buf, buf1, sizeof (buf1)) < 0)
		return (ERR_DRIVER_NOT_FOUND);

	if (buf1[0] != '/')
		(void) snprintf(buf, sizeof (buf), "%s/", CFGDIR);
	else
		buf[0] = 0;
	(void) strlcat(buf, buf1, MAX_PATH_LEN);

	colon = strrchr(buf, ':');
	if (colon == NULL)
		return (ERR_DRIVER_NOT_FOUND);
	else
		*colon = 0;

	(void) snprintf(path, MAX_PATH_LEN, "%s:devctl", buf);

	fd = open(buf, O_RDONLY | O_NDELAY);

	if (fd < 0)
		return (ERR_DRIVER_NOT_FOUND);

	(void) close(fd);

	return (SUCCESS);
}

static int
mpt_controller_id_to_fd(uint32_t controller_id)
{
	struct devh_t *devh;

	devh = devh_sys;
	while (devh != NULL && devh->controller_id != controller_id) {
		devh = devh->next;
	}

	if (devh == NULL) {
		return (ERR_DRIVER_OPEN);
	}

	return (devh->fd);
}

static int
mpt_disks_compinfo(uint32_t controller_id, int comp_num, uint32_t *ids)
{
	int disk_num_canonical, disk_num_volume, disk_num_hotspare;
	int num = 0, i, j;
	uint32_t *ids_canonical = NULL;
	uint32_t *ids_volume = NULL;
	uint32_t *ids_hotspare = NULL;

	disk_num_volume = mpt_disks_in_volume(controller_id, 0, NULL);
	if (disk_num_volume != 0) {
		ids_volume = calloc(disk_num_volume, sizeof (uint32_t));
		if (ids_volume == NULL)
			return (ERR_NOMEM);
		(void) mpt_disks_in_volume(controller_id, disk_num_volume,
		    ids_volume);
	}

	disk_num_canonical = mpt_get_disk_ids(controller_id, 0, NULL);
	if (disk_num_canonical != 0) {
		ids_canonical = calloc(disk_num_canonical, sizeof (uint32_t));
		if (ids_canonical == NULL) {
			if (ids_volume)
				free(ids_volume);
			return (ERR_NOMEM);
		}
		(void) mpt_get_disk_ids(controller_id, disk_num_canonical,
		    ids_canonical);
	}

	disk_num_hotspare = mpt_disks_in_hotspare(controller_id, NULL);
	if (disk_num_hotspare != 0) {
		ids_hotspare = calloc(disk_num_hotspare, sizeof (uint32_t));
		if (ids_hotspare == NULL) {
			if (ids_volume)
				free(ids_volume);
			if (ids_canonical)
				free(ids_canonical);
			return (ERR_NOMEM);
		}
		(void) mpt_disks_in_hotspare(controller_id, ids_hotspare);
	}

	for (i = 0; i < disk_num_volume; ++i) {
		if (ids != NULL) {
			if (num >= comp_num) {
				if (ids_volume)
					free(ids_volume);
				if (ids_canonical)
					free(ids_canonical);
				if (ids_hotspare)
					free(ids_hotspare);
				return (num);
			}
			ids[num] = ids_volume[i];
		}
		++ num;
	}

	for (i = 0; i < disk_num_canonical; ++i) {
		for (j = 0; j < disk_num_volume; ++j) {
			if (ids_volume[j] == ids_canonical[i])
				break;
		}

		if (j == disk_num_volume) {
			/* This is a disk */
			if (ids != NULL) {
				if (num >= comp_num) {
					if (ids_volume)
						free(ids_volume);
					if (ids_canonical)
						free(ids_canonical);
					if (ids_hotspare)
						free(ids_hotspare);
					return (num);
				}
				ids[num] = ids_canonical[i];
			}
			++num;
		}
	}

	for (i = 0; i < disk_num_hotspare; ++i) {
		if (ids != NULL) {
			if (num >= comp_num) {
				if (ids_volume)
					free(ids_volume);
				if (ids_canonical)
					free(ids_canonical);
				if (ids_hotspare)
					free(ids_hotspare);
				return (num);
			}
			ids[num] = ids_hotspare[i];
		}
		++ num;
	}

	if (ids_volume)
		free(ids_volume);
	if (ids_canonical)
		free(ids_canonical);
	if (ids_hotspare)
		free(ids_hotspare);
	return (num);
}

static int
mpt_props_compinfo(uint32_t controller_id, uint32_t container_id, uint32_t *ids)
{
	int num, ret;
	uint16_t deviceid, subsys, subven;
	char guid[MAX_PATH_LEN];

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	if (deviceid == MPT_1030) {
		num = 0;
		if (ids != NULL)
			ids = NULL;
	} else if (deviceid == MPT_1064 || deviceid == MPT_1064E ||
	    deviceid == MPT_1068 || deviceid == MPT_1068E) {
		int i, disk_num_volume, disk_num_hotspare;
		uint32_t *ids_volume = NULL;
		uint32_t *ids_hotspare = NULL;

		disk_num_hotspare = mpt_disks_in_hotspare(controller_id, NULL);
		if (disk_num_hotspare != 0) {
			ids_hotspare = calloc(disk_num_hotspare,
			    sizeof (uint32_t));
			(void) mpt_disks_in_hotspare(controller_id,
			    ids_hotspare);
		}
		for (i = 0; i < disk_num_hotspare; ++i) {
			if (ids_hotspare[i] == container_id) {
				if (ids != NULL)
					ids = NULL;
				return (0);
			}
		}

		disk_num_volume = mpt_disks_in_volume(controller_id, 0, NULL);
		if (disk_num_volume != 0) {
			ids_volume = calloc(disk_num_volume, sizeof (uint32_t));
			(void) mpt_disks_in_volume(controller_id,
			    disk_num_volume, ids_volume);
		}
		for (i = 0; i < disk_num_volume; ++i) {
			if (ids_volume[i] == container_id) {
				if (ids != NULL)
					ids = NULL;
				return (0);
			}
		}

		(void) mpt_disk_info_guid(controller_id, container_id, guid);

		/*
		 * SATA or SAS devices without a valid GUID
		 */
		if (strcmp(guid, INVALID_GUID) == SUCCESS)
			return (0);

		num = 1;
		if (ids != NULL)
			ids[0] = container_id;
	}

	return (num);
}

static int
mpt_hsp_compinfo(uint32_t controller_id, uint32_t container_id, uint32_t *ids)
{
	int i, num = 0, disk_num_hotspare;
	uint8_t array_id;
	uint32_t *ids_hotspare = NULL;

	disk_num_hotspare = mpt_disks_in_hotspare(controller_id, NULL);
	if (disk_num_hotspare != 0) {
		ids_hotspare = calloc(disk_num_hotspare, sizeof (uint32_t));
		(void) mpt_disks_in_hotspare(controller_id, ids_hotspare);
	}

	for (i = 0; i < disk_num_hotspare; ++i) {
		if (ids_hotspare[i] == container_id) {
			num = 1;
			if (ids != NULL) {
				(void) mpt_hsp_in_volume(controller_id,
				    container_id, &array_id);
				ids[0] = array_id;
			}
			break;
		}
	}

	return (num);
}

static int
mpt_check_sata_disk(uint32_t controller_id, int bus, int target)
{
	int addr, ret;
	config_page_sas_device_0_t sasdevpage0;

	addr = MPT_SAS_ADDR((bus << 8) + target);
	(void) memset(&sasdevpage0, 0, sizeof (sasdevpage0));

	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE, 0, addr,
	    &sasdevpage0, sizeof (sasdevpage0));

	if (ret == SUCCESS) {
		if (LE_32(sasdevpage0.DeviceInfo) &
		    MPI_SAS_DEVICE_INFO_SATA_DEVICE)
			return (SATA_DISK);
	}

	return (NOT_SATA_DISK);
}

static int
mpt_check_ssd_disk(uint32_t controller_id, int bus, int target)
{
	unsigned char vpd[MAX_PATH_LEN];
	int i, n;

	if (mpt_do_inquiry_VpdPage(controller_id, bus, target,
	    0, 0x00, vpd, sizeof (vpd)) != SUCCESS)
		return (NOT_SSD_DISK);

	if ((vpd[0] & 0x1f) == 0x00) {
		n = vpd[3] + 4;
		for (i = 4; i < n; i++) {
			if (vpd[i] == 0xb1) {
				if (mpt_do_inquiry_VpdPage(
				    controller_id, bus, target,
				    0, 0xb1, vpd, sizeof (vpd)) !=
				    SUCCESS)
					return (NOT_SSD_DISK);

				n = vpd[3] + 4;
				if (n >= 6)
					if (get2bytes(vpd, 4) == 0x0001)
						return (SSD_DISK);
			}
		}
	}
	return (NOT_SSD_DISK);
}

static int
mpt_get_disk_ids(uint32_t controller_id, int comp_num, uint32_t *ids)
{
	DIR *dir;
	struct dirent *dp;
	int num = 0, array_num = 0, physdisk_num = 0, path_num = 0;
	int i, j, length, ret, dev_info;
	uint8_t target, physdisk, bus;
	uint16_t deviceid, subsys, subven;
	uint32_t *array_ids = NULL, *disk_ids = NULL, *path_ids = NULL;
	uint32_t handle = 0xffff;
	struct scsi_inquiry inq;
	config_page_sas_device_0_t sasdevpage0;
	config_page_ioc_3_t *iocpage3;
	config_page_raid_phys_disk_0_t *diskpage0;
	config_page_raid_phys_disk_1_t *diskpage1;

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	/* First check how many arrays available */
	ret = mpt_get_compinfo(controller_id, (uint32_t)OBJ_ATTR_NONE,
	    OBJ_TYPE_CONTROLLER, OBJ_TYPE_ARRAY, (uint32_t *)&array_num, NULL);
	if (ret < SUCCESS)
		return (ret);

	/* Then get all array_ids */
	if (array_num > 0) {
		array_ids = calloc(array_num, sizeof (uint32_t));
		if (array_ids == NULL)
			return (ERR_NOMEM);

		ret = mpt_get_compinfo(controller_id, (uint32_t)OBJ_ATTR_NONE,
		    OBJ_TYPE_CONTROLLER, OBJ_TYPE_ARRAY,
		    (uint32_t *)&array_num, array_ids);
		if (ret < SUCCESS) {
			free(array_ids);
			return (ret);
		}
	}

	switch (deviceid) {
	case MPT_1030:
		if ((dir = opendir(DSKDIR)) == NULL) {
			if (array_ids)
				free(array_ids);
		return (ERR_DRIVER_NOT_FOUND);
		}
		/* Scan the canonical disk entries */
		while ((dp = readdir(dir)) != NULL) {
			uint32_t c_id, t_id, d_id, s_id;

			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;

			if (sscanf(dp->d_name, "c%ut%ud%us%u", &c_id, &t_id,
			    &d_id, &s_id) != 4)
				continue;

			if (controller_id == c_id && s_id == 2) {
				if (mpt_check_disk_valid(dp->d_name) != SUCCESS)
					continue;

				/* Check if the entry is for an array */
				if (array_ids != NULL) {
					int i;

					for (i = 0; i < array_num; ++i) {
						if (array_ids[i] == DISK_ID(0,
						    t_id, d_id))
							break;
					}
					if (i < array_num)
						continue;
				}

				/* Add this disk into list */
				if (ids != NULL) {
					if (num >= comp_num)
						break;
					ids[num] = DISK_ID(0, t_id, d_id);
				}
				++num;
			}
		}
		break;
	case MPT_1064:
	case MPT_1064E:
	case MPT_1068:
	case MPT_1068E:
		/*
		 * Get hidden disk and additional path information in volumes
		 */
		ret = mpt_get_configpage_length(controller_id,
		    MPI_CONFIG_PAGETYPE_IOC, 3, 0, &length);
		if (ret != SUCCESS)
			return (ret);

		iocpage3 = malloc(length);
		(void) memset(iocpage3, 0, length);
		ret = mpt_get_configpage(controller_id,
		    MPI_CONFIG_PAGETYPE_IOC, 3, 0, iocpage3, length);
		if (ret != SUCCESS) {
			free(iocpage3);
			return (ret);
		}

		physdisk_num = iocpage3->NumPhysDisks;

		if (physdisk_num != 0) {
			disk_ids = (uint32_t *)malloc(
			    physdisk_num * sizeof (uint32_t));
			for (i = 0; i < physdisk_num; ++i) {
				physdisk = iocpage3->PhysDisk[i].PhysDiskNum;

				ret = mpt_get_configpage_length(controller_id,
				    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
				    0, physdisk, &length);
				if (ret != SUCCESS)
					continue;

				diskpage0 = malloc(length);
				(void) memset(diskpage0, 0, length);
				ret = mpt_get_configpage(controller_id,
				    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
				    0, physdisk, diskpage0, length);
				if (ret != SUCCESS)
					continue;

				disk_ids[i] = diskpage0->PhysDiskID;

				ret = mpt_get_configpage_length(controller_id,
				    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
				    1, physdisk, &length);
				if (ret != SUCCESS)
					continue;

				diskpage1 = malloc(length);
				(void) memset(diskpage1, 0, length);
				ret = mpt_get_configpage(controller_id,
				    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
				    1, physdisk, diskpage1, length);
				if (ret != SUCCESS)
					continue;

				if (diskpage1->NumPhysDiskPaths == 1)
					continue;

				for (j = 0; j < 2; j++) {
					if ((diskpage0->PhysDiskID ==
					    diskpage1->Path[j].PhysDiskID) &&
					    (diskpage0->PhysDiskBus ==
					    diskpage1->Path[j].PhysDiskBus))
						continue;

					path_ids = realloc(path_ids,
					    (++path_num) * sizeof (uint32_t));
					path_ids[path_num - 1] = DISK_ID(
					    diskpage1->Path[j].PhysDiskBus,
					    diskpage1->Path[j].PhysDiskID, 0);
				}

			free(diskpage0);
			free(diskpage1);
			}
		}

		free(iocpage3);

		(void) memset(&sasdevpage0, 0, sizeof (sasdevpage0));

		/* Scan the canonical disk entries */
		while (mpt_get_configpage(controller_id,
		    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
		    0, handle, &sasdevpage0,
		    sizeof (sasdevpage0)) == SUCCESS) {

			handle = LE_16(sasdevpage0.DevHandle);
			dev_info = LE_32(sasdevpage0.DeviceInfo);
			target = LE_8(sasdevpage0.TargetID);
			bus = LE_8(sasdevpage0.Bus);

			if (dev_info & MPI_SAS_DEVICE_INFO_SSP_INITIATOR)
				continue;

			if (dev_info & MPI_SAS_DEVICE_INFO_SATA_HOST)
				continue;

			if (dev_info & (MPI_SAS_DEVICE_INFO_SATA_DEVICE |
			    MPI_SAS_DEVICE_INFO_ATAPI_DEVICE |
			    MPI_SAS_DEVICE_INFO_SSP_TARGET)) {
				if (path_ids != NULL) {
					for (i = 0; i < path_num; ++i) {
						if (path_ids[i] ==
						    DISK_ID(0, target, 0))
							break;
					}
					if (i < path_num)
						continue;
				}

				if (disk_ids != NULL) {
					for (i = 0; i < physdisk_num; ++i) {
						if (disk_ids[i] ==
						    DISK_ID(0, target, 0))
							break;
					}
					if (i < physdisk_num)
						continue;
				}

				/*
				 * Check to see if we have a CD/DVD drive or
				 * the likes attached to the controller. We
				 * always want to skip removable media.
				 */
				bzero(&inq, sizeof (inq));
				if (mpt_do_inquiry(controller_id, bus, target,
				    0, (unsigned char *)&inq, sizeof (inq)) !=
				    SUCCESS)
					continue;

				if (inq.inq_rmb == 1)
					continue;

				/* Add this disk into list */
				if (ids != NULL) {
					if (num >= comp_num)
						break;
					ids[num] = target;
				}
				++ num;
			}
		}
		break;
	default:
		break;
	}

	if (array_ids)
		free(array_ids);
	if (disk_ids)
		free(disk_ids);
	if (path_ids)
		free(path_ids);

	return (num);
}

static int
mpt_disks_in_volume(uint32_t controller_id, int component_num, uint32_t *ids)
{
	int i, j, nvols, ret, num = 0;
	uint16_t deviceid, subsys, subven;

	ret = mpt_get_deviceid(controller_id, &deviceid, &subsys, &subven);
	if (ret != SUCCESS)
		return (ret);

	ret = mpt_get_max_volume_num(controller_id, &nvols);
	if (ret != SUCCESS)
		return (ret);

	for (i = 0; i < nvols; ++i) {
		if (raidvolume[i].m_israid == 0)
			continue;

		for (j = 0; j < raidvolume[i].m_ndisks; ++j) {
			if (raidvolume[i].m_diskstatus[j] !=
			    MPT_RAID_DISKSTATUS_MISSING) {
				if (ids != NULL) {
					if (num >= component_num)
						break;
					ids[num] =
					    raidvolume[i].m_diskid[j];
				}
				++num;
			}
		}
	}

	return (num);
}

static int
mpt_disks_in_hotspare(uint32_t controller_id, uint32_t *ids)
{
	int i, ret, length;
	int physdisk_num = 0, hotspare_num = 0;
	uint8_t physdisk;
	config_page_ioc_3_t *iocpage3;
	config_page_raid_phys_disk_0_t *diskpage0;

	/*
	 * Get hidden disk information in volumes
	 */
	ret = mpt_get_configpage_length(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, &length);
	if (ret != SUCCESS)
		return (ret);

	iocpage3 = malloc(length);
	(void) memset(iocpage3, 0, length);
	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, iocpage3, length);
	if (ret != SUCCESS) {
		free(iocpage3);
		return (ret);
	}

	physdisk_num = iocpage3->NumPhysDisks;

	if (physdisk_num != 0) {
		for (i = 0; i < physdisk_num; ++i) {
			physdisk = iocpage3->PhysDisk[i].PhysDiskNum;

			ret = mpt_get_configpage_length(controller_id,
			    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
			    0, physdisk, &length);
			if (ret != SUCCESS)
				continue;

			diskpage0 = malloc(length);
			(void) memset(diskpage0, 0, length);
			ret = mpt_get_configpage(controller_id,
			    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
			    0, physdisk, diskpage0, length);
			if (ret != SUCCESS)
				continue;

			if (diskpage0->PhysDiskSettings.HotSparePool != 0) {
				++hotspare_num;
				if (ids != NULL)
					ids[hotspare_num - 1] =
					    diskpage0->PhysDiskID;
			}

			free(diskpage0);
		}
	}
	free(iocpage3);

	return (hotspare_num);
}

static int
mpt_disks_find_array(uint32_t controller_id, uint32_t disk_id,
	uint32_t *array_id)
{
	int ret, i, j, array_num, arraypart_num;
	uint32_t *array_ids, *arraypart_ids;

	*array_id = (uint32_t)OBJ_ATTR_NONE;
	array_num = rdcfg_compnum(controller_id, 0, OBJ_TYPE_CONTROLLER,
	    OBJ_TYPE_ARRAY);
	if (array_num <= 0)
		return (array_num);
	array_ids = calloc(array_num, sizeof (uint32_t));
	if (array_ids == NULL)
		return (ERR_NOMEM);
	ret = rdcfg_complist(controller_id, 0, OBJ_TYPE_CONTROLLER,
	    OBJ_TYPE_ARRAY, array_num, array_ids);
	if (ret < SUCCESS) {
		free(array_ids);
		return (ret);
	}

	for (i = 0; i < array_num; ++i) {
		arraypart_num = rdcfg_compnum(controller_id, array_ids[i],
		    OBJ_TYPE_ARRAY, OBJ_TYPE_ARRAY_PART);
		if (arraypart_num == 0)
			continue;
		arraypart_ids = calloc(arraypart_num, sizeof (uint32_t));
		if (arraypart_ids == NULL) {
			free(array_ids);
			return (ERR_NOMEM);
		}
		ret = rdcfg_complist(controller_id, array_ids[i],
		    OBJ_TYPE_ARRAY, OBJ_TYPE_ARRAY_PART,
		    arraypart_num, arraypart_ids);
		if (ret < SUCCESS) {
			free(array_ids);
			free(arraypart_ids);
			return (ret);
		}

		for (j = 0; j < arraypart_num; ++j) {
			if (arraypart_ids[j] == disk_id)
				*array_id = array_ids[i];
		}

		free(arraypart_ids);
	}

	free(array_ids);
	return (SUCCESS);
}

static int
mpt_array_disk_info(uint32_t controller_id, uint32_t id, disk_attr_t *disk_attr)
{
	int length, ret;
	config_page_raid_phys_disk_0_t diskpage0;

	ret = mpt_get_configpage_length(controller_id,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK, 0, id, &length);

	if (ret != SUCCESS)
		return (ret);

	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK, 0, id, &diskpage0, length);

	if (ret != SUCCESS)
		return (ret);

	(void) memcpy(disk_attr->vendorid,
	    diskpage0.InquiryData.VendorID, DISK_VENDER_LEN);
	(void) memcpy(disk_attr->productid,
	    diskpage0.InquiryData.ProductID, DISK_PRODUCT_LEN);
	(void) memcpy(disk_attr->revision,
	    diskpage0.InquiryData.ProductRevLevel, DISK_REV_LEN);

	return (SUCCESS);
}

static int
mpt_get_disk_info(uint32_t controller_id, disk_attr_t *disk_attr)
{
	int ret;
	uint32_t bus, target, lun;
	unsigned char inq[36];

	bus = BUS(disk_attr->disk_id);
	target = TARGET(disk_attr->disk_id);
	lun = LUN(disk_attr->disk_id);

	if ((ret = mpt_do_inquiry(controller_id, bus, target, lun,
	    inq, sizeof (inq))) != SUCCESS)
		return (ret);

	(void) memcpy(disk_attr->vendorid, &inq[8],
	    DISK_VENDER_LEN);
	(void) memcpy(disk_attr->productid, &inq[16],
	    DISK_PRODUCT_LEN);
	(void) memcpy(disk_attr->revision, &inq[32],
	    DISK_REV_LEN);

	return (SUCCESS);
}

static uint64_t
mpt_get_comp_capacity(uint32_t controller_id, uint32_t disk_id)
{
	int ret;
	unsigned char cap[8];
	uint32_t mode, size;
	uint64_t capacity;
	msg_scsi_io_request_t scsi_io_request;
	msg_scsi_io_reply_t scsi_io_reply;

	(void) memset(&scsi_io_request, 0, sizeof (scsi_io_request));
	(void) memset(&scsi_io_reply, 0, sizeof (scsi_io_reply));

	scsi_io_request.Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	scsi_io_request.Bus = BUS(disk_id);
	scsi_io_request.TargetID = TARGET(disk_id);
	scsi_io_request.CDBLength = 10;
	scsi_io_request.LUN[1] = LUN(disk_id);
	scsi_io_request.Control = LE_32(MPI_SCSIIO_CONTROL_READ |
	    MPI_SCSIIO_CONTROL_SIMPLEQ);
	scsi_io_request.CDB[0] = 0x25;
	scsi_io_request.CDB[1] = 0;
	scsi_io_request.CDB[2] = 0;
	scsi_io_request.CDB[3] = 0;
	scsi_io_request.CDB[4] = 0;
	scsi_io_request.CDB[5] = 0;
	scsi_io_request.CDB[6] = 0;
	scsi_io_request.CDB[7] = 0;
	scsi_io_request.CDB[8] = 0;
	scsi_io_request.CDB[9] = 0;
	scsi_io_request.DataLength = LE_32(sizeof (cap));

	ret = mpt_do_scsi_io(controller_id, &scsi_io_request,
	    sizeof (scsi_io_request) - sizeof (scsi_io_request.SGL),
	    &scsi_io_reply, sizeof (scsi_io_reply),
	    cap, sizeof (cap), NULL, 0);
	if (ret < SUCCESS)
		return (ERR_DISK_STATE);

	mode = get4bytes(cap, 4);
	size = get4bytes(cap, 0);
	capacity = ((uint64_t)mode) * (size + 1);

	return (capacity);
}

static int
mpt_disk_attr(uint32_t controller_id, disk_attr_t *disk_attr)
{
	uint32_t array_id;
	int i, disk_num_hotspare, ret;
	uint32_t *ids_hotspare = NULL;

	disk_num_hotspare = mpt_disks_in_hotspare(controller_id, NULL);
	if (disk_num_hotspare != 0) {
		ids_hotspare = calloc(disk_num_hotspare, sizeof (uint32_t));
		(void) mpt_disks_in_hotspare(controller_id, ids_hotspare);
	}

	for (i = 0; i < disk_num_hotspare; ++i) {
		if (ids_hotspare[i] == disk_attr->disk_id)
			return (mpt_disk_hsp_attr(controller_id, disk_attr));
	}

	ret = mpt_disks_find_array(controller_id,
	    disk_attr->disk_id, &array_id);
	if (ret < SUCCESS)
		return (ret);

	disk_attr->tag.cidl.bus = BUS(disk_attr->disk_id);
	disk_attr->tag.cidl.target_id = TARGET(disk_attr->disk_id);
	disk_attr->tag.cidl.lun = LUN(disk_attr->disk_id);

	if (array_id == (uint32_t)OBJ_ATTR_NONE) {
		(void) mpt_get_disk_info(controller_id, disk_attr);
		disk_attr->capacity = mpt_get_comp_capacity(
		    controller_id, disk_attr->disk_id);
		disk_attr->state = DISK_STATE_GOOD;
	} else {
		uint32_t *arraypart_ids;
		array_attr_t array_attr;
		arraypart_attr_t arraypart_attr;
		int i, num;
		uint8_t phys_disk_num = 0;

		num = rdcfg_compnum(controller_id, array_id, OBJ_TYPE_ARRAY,
		    OBJ_TYPE_ARRAY_PART);

		ret = rdcfg_get_attr(controller_id, array_id, 0,
		    OBJ_TYPE_ARRAY, &array_attr);
		if (ret < SUCCESS)
			return (ret);

		switch (array_attr.raid_level) {
		case RAID_LEVEL_0:
			disk_attr->capacity = array_attr.capacity /
			    (uint64_t)num;
			break;
		case RAID_LEVEL_1:
			disk_attr->capacity = array_attr.capacity;
			break;
		case RAID_LEVEL_1E:
			disk_attr->capacity = array_attr.capacity * 2 /
			    (uint64_t)num;
			break;
		default:
			break;
		}

		arraypart_ids = calloc(num, sizeof (uint32_t));
		ret = rdcfg_complist(controller_id, array_id, OBJ_TYPE_ARRAY,
		    OBJ_TYPE_ARRAY_PART, num, arraypart_ids);
		if (ret < SUCCESS) {
			free(arraypart_ids);
			return (ret);
		}
		for (i = 0; i < num; ++i) {
			(void) rdcfg_get_attr(controller_id, array_id,
			    arraypart_ids[i], OBJ_TYPE_ARRAY_PART,
			    &arraypart_attr);
			if (disk_attr->disk_id == arraypart_attr.disk_id) {
				/*
				 * Locate the proper physical disk number
				 * assigned by the IOC so we can obtain
				 * the correct disk information.
				 */
				ret = mpt_get_phys_disk_num(controller_id,
				    array_id, arraypart_ids[i], &phys_disk_num);
				if (ret < SUCCESS) {
					free(arraypart_ids);
					return (ret);
				}
				(void) mpt_array_disk_info(controller_id,
				    phys_disk_num, disk_attr);
				if (arraypart_attr.state ==
				    ARRAYPART_STATE_GOOD)
					disk_attr->state = DISK_STATE_GOOD;
				else if (arraypart_attr.state ==
				    ARRAYPART_STATE_MISSED)
					disk_attr->state = DISK_STATE_FAILED;
				break;
			}
		}

		free(arraypart_ids);
	}

	return (SUCCESS);
}

static int
mpt_hsp_in_volume(uint32_t controller_id, uint8_t disk_id, uint8_t *array_id)
{
	int i, length, physdisk_num, ret, flag, vol;
	uint8_t physdisk;
	config_page_ioc_3_t *iocpage3;
	config_page_raid_phys_disk_0_t *diskpage0;

	/*
	 * Get hidden disk information in volumes
	 */
	ret = mpt_get_configpage_length(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, &length);
	if (ret != SUCCESS)
		return (ret);

	iocpage3 = malloc(length);
	(void) memset(iocpage3, 0, length);
	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, iocpage3, length);
	if (ret != SUCCESS) {
		free(iocpage3);
		return (ret);
	}

	physdisk_num = iocpage3->NumPhysDisks;
	for (i = 0; i < physdisk_num; ++i) {
		physdisk = iocpage3->PhysDisk[i].PhysDiskNum;

		ret = mpt_get_configpage_length(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
		    0, physdisk, &length);
		if (ret != SUCCESS)
			continue;

		diskpage0 = malloc(length);
		(void) memset(diskpage0, 0, length);
		ret = mpt_get_configpage(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
		    0, physdisk, diskpage0, length);
		if (ret != SUCCESS)
			continue;

		if (disk_id == diskpage0->PhysDiskID)
			break;
	}

	flag = diskpage0->PhysDiskSettings.HotSparePool;

	free(iocpage3);
	free(diskpage0);

	for (vol = 0; vol < MPT_MAX_RAIDVOLS; vol++)
		if (flag & (1 << vol))
			break;

	*array_id = raidvolume[vol].m_raidtarg;

	return (SUCCESS);
}

static int
mpt_disk_hsp_attr(uint32_t controller_id, disk_attr_t *disk_attr)
{
	int i, length, physdisk_num, ret;
	uint8_t physdisk;
	config_page_ioc_3_t *iocpage3;
	config_page_raid_phys_disk_0_t *diskpage0;

	disk_attr->tag.cidl.bus = BUS(disk_attr->disk_id);
	disk_attr->tag.cidl.target_id = TARGET(disk_attr->disk_id);
	disk_attr->tag.cidl.lun = LUN(disk_attr->disk_id);

	/*
	 * Get hidden disk information in volumes
	 */
	ret = mpt_get_configpage_length(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, &length);
	if (ret != SUCCESS)
		return (ret);

	iocpage3 = malloc(length);
	(void) memset(iocpage3, 0, length);
	ret = mpt_get_configpage(controller_id,
	    MPI_CONFIG_PAGETYPE_IOC, 3, 0, iocpage3, length);
	if (ret != SUCCESS) {
		free(iocpage3);
		return (ret);
	}

	physdisk_num = iocpage3->NumPhysDisks;
	for (i = 0; i < physdisk_num; ++i) {
		physdisk = iocpage3->PhysDisk[i].PhysDiskNum;

		ret = mpt_get_configpage_length(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
		    0, physdisk, &length);
		if (ret != SUCCESS)
			continue;

		diskpage0 = malloc(length);
		(void) memset(diskpage0, 0, length);
		ret = mpt_get_configpage(controller_id,
		    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
		    0, physdisk, diskpage0, length);
		if (ret != SUCCESS)
			continue;

		if (disk_attr->disk_id == diskpage0->PhysDiskID)
			break;
	}

	(void) memcpy(disk_attr->vendorid,
	    diskpage0->InquiryData.VendorID, DISK_VENDER_LEN);
	(void) memcpy(disk_attr->productid,
	    diskpage0->InquiryData.ProductID, DISK_PRODUCT_LEN);
	(void) memcpy(disk_attr->revision,
	    diskpage0->InquiryData.ProductRevLevel, DISK_REV_LEN);

	disk_attr->capacity = (LE_32(diskpage0->MaxLBA) + 1) / 2048;
	disk_attr->state = DISK_STATE_GOOD;

	free(diskpage0);
	return (SUCCESS);
}

static int
mpt_disk_prop_attr(uint32_t controller_id, property_attr_t *prop_attr)
{
	prop_attr->prop_type = PROP_GUID;

	if (prop_attr->prop_size == 0) {
		prop_attr->prop_size = MAX_PATH_LEN;
	} else {
		int ret;
		char guid[MAX_PATH_LEN];
		ret = mpt_disk_info_guid(controller_id,
		    prop_attr->prop_id, guid);
		if (ret != SUCCESS)
			return (ret);
		(void) memcpy(prop_attr->prop, guid, prop_attr->prop_size);
	}
	return (SUCCESS);
}

static int
mpt_hsp_attr(uint32_t controller_id, uint32_t device_id, hsp_attr_t *hsp_attr)
{
	uint8_t disk_id, array_id;

	disk_id = (uint8_t)device_id;
	(void) mpt_hsp_in_volume(controller_id, disk_id, &array_id);

	hsp_attr->type =  HSP_TYPE_LOCAL;
	hsp_attr->associated_id = array_id;

	return (SUCCESS);
}

static int
mpt_raid_dev_config(cfga_cmd_t cmd, uint32_t controller_id, uint32_t target_id,
	uint8_t type)
{
	cfga_err_t cfga_err;
	char *ap_id;
	int count = 0;

	ap_id = (char *)malloc(MAX_PATH_LEN);
	if (ap_id == NULL)
		return (ERR_NOMEM);

	if (type == 0) {
		(void) snprintf(ap_id, MAX_PATH_LEN, "c%d::dsk/c%dt%dd0",
		    controller_id, controller_id, target_id);
	} else
		(void) snprintf(ap_id, MAX_PATH_LEN, "c%d", controller_id);

	do {
		(void) sleep(1);
		cfga_err = config_change_state(cmd, 1, &ap_id, NULL,
		    NULL, NULL, NULL, 0);
		count++;
	} while (cfga_err != CFGA_OK && count < 7);

	if (cfga_err != CFGA_OK) {
		free(ap_id);
		return (ERR_ARRAY_CONFIG);
	}

	free(ap_id);
	return (SUCCESS);
}

static int
mpt_disk_info_guid(uint32_t controller_id, uint32_t disk_id, char *guid)
{
	int ret;
	uint32_t bus, target, lun;
	unsigned char inq[MAX_PATH_LEN], inq83[MAX_PATH_LEN];
	ddi_devid_t devid;
	char *i_guid = NULL;

	*guid = '\0';
	bus = BUS(disk_id);
	target = TARGET(disk_id);
	lun = LUN(disk_id);

	if ((ret = mpt_do_inquiry(controller_id, bus, target, lun,
	    inq, sizeof (inq))) != SUCCESS)
		return (ret);

	if ((ret = mpt_do_inquiry_VpdPage(controller_id, bus, target, lun, 0x83,
	    inq83, sizeof (inq83))) != SUCCESS)
		return (ret);

	if ((ret = devid_scsi_encode(DEVID_SCSI_ENCODE_VERSION_LATEST, NULL,
	    inq, sizeof (struct scsi_inquiry), NULL, 0,
	    inq83, (size_t)inq83, &devid)) == SUCCESS) {
		i_guid = devid_to_guid(devid);
		devid_free(devid);
		if (i_guid != NULL) {
			(void) strlcpy(guid, i_guid, MAX_PATH_LEN);
			devid_free_guid(i_guid);
		}
	}

	return (SUCCESS);
}

static int
mpt_check_disk_valid(char *devnamep)
{
	char dev[MAX_PATH_LEN];
	int search_file;
	struct stat stbuf;
	struct dk_cinfo dkinfo;
	struct dk_minfo mediainfo;
	int isremovable, ret;

	(void) strcpy(dev, DSKDIR);
	(void) strcat(dev, "/");
	(void) strlcat(dev, devnamep, MAX_PATH_LEN);

	/* Attemp to open the disk. If it fails, skip it. */
	if ((search_file = open(dev, O_RDWR | O_NDELAY)) < 0)
		return (ERR_DRIVER_OPEN);

	/* Must be a character device */
	if (fstat(search_file, &stbuf) < 0 ||
	    !S_ISCHR(stbuf.st_mode)) {
		(void) close(search_file);
		return (ERR_DRIVER_OPEN);
	}

	/*
	 * Attempt to read the configuration info on the disk.
	 * Again, if it fails, we assume the disk's not there.
	 * Note we must close the file for the disk before we
	 * continue.
	 */
	if (ioctl(search_file, DKIOCINFO, &dkinfo) < 0) {
		(void) close(search_file);
		return (ERR_DEVICE_INVALID);
	}

	/* If it is a removable media, skip it */
	ret = ioctl(search_file, DKIOCREMOVABLE, &isremovable);
	if ((ret >= 0) && (isremovable != 0)) {
		(void) close(search_file);
		return (ERR_DEVICE_TYPE);
	}

	if (dkinfo.dki_ctype == DKC_CDROM) {
		if (ioctl(search_file, DKIOCGMEDIAINFO,
		    &mediainfo) < 0) {
			mediainfo.dki_media_type = DK_UNKNOWN;
		}
	}

	/*
	 * Skip CDROM devices, they are read only.
	 * But not devices like Iomega Rev Drive which
	 * identifies itself as a CDROM, but has a removable
	 * disk.
	 */
	if ((dkinfo.dki_ctype == DKC_CDROM) &&
	    (mediainfo.dki_media_type != DK_REMOVABLE_DISK)) {
		(void) close(search_file);
		return (ERR_DEVICE_TYPE);
	}

	(void) close(search_file);
	return (SUCCESS);
}
