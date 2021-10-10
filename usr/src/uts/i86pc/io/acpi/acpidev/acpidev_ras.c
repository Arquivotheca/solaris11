/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

/*
 * This file implements the interfaces to detect and control system device
 * RAS features based on the ACPI PRCT and RETI tables.
 * The ACPI PRCT table (Platform RAS Capabilities Table) defines OS visible
 * RAS capabilities for system devices, including processor and memory etc.
 * The ACPI RETI table (RAS Event Trigger Interface) provides the interfaces
 * to control those system devices defined by the PRCT table.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/note.h>
#include <sys/param.h>
#include <sys/synch.h>
#include <sys/sysmacros.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>
#include <sys/acpidev.h>
#include <sys/acpidev_rsc.h>
#include <sys/acpidev_dr.h>
#include <sys/acpidev_impl.h>

/* ACPI Platform RAS Capability Table Signature. */
#define	ACPIDEV_PRCT_NAME		"PRCT"

/* ACPI RAS Event Trigger Interface Table Signature. */
#define	ACPIDEV_RETI_NAME		"RETI"

/* RAS capability flags defined by the PRCT table. */
#define	ACPIDEV_PRCT_CAP_OxLINE		0x1
#define	ACPIDEV_PRCT_CAP_HOTPLUG	0x2
#define	ACPIDEV_PRCT_CAP_POWER		0x4
#define	ACPIDEV_PRCT_CAP_MIG_MASK	0x30
#define	ACPIDEV_PRCT_CAP_MIG_NONE	0x00
#define	ACPIDEV_PRCT_CAP_MIG_NONBLOCK	0x10
#define	ACPIDEV_PRCT_CAP_MIG_BLOCK	0x20
#define	ACPIDEV_PRCT_CAP_MIG_CHILD	0x30
#define	ACPIDEV_PRCT_CAP_SPARE		0x40

/* Special device UID to match all devices. */
#define	ACPIDEV_RAS_UID_ALL		0xFF

typedef uint8_t				acpidev_ras_uid_t;

typedef enum acpidev_ras_device_type {
	ACPIDEV_RAS_DEVTYPE_MODULE = 0,
	ACPIDEV_RAS_DEVTYPE_MEMORY = 1,
	ACPIDEV_RAS_DEVTYPE_PROCESSOR = 2,
	ACPIDEV_RAS_DEVTYPE_UNKNOWN = 0xFF
} acpidev_ras_device_type_t;

typedef enum acpidev_reti_cmd_code {
	ACPIDEV_RETI_CMD_ONLINE = 1,
	ACPIDEV_RETI_CMD_OFFLINE = 2
} acpidev_reti_cmd_code_t;

typedef enum acpidev_reti_cmd_result {
	ACPIDEV_RETI_RES_SUCCESS = 0,
	ACPIDEV_RETI_RES_INPROGRESS = 1,
	ACPIDEV_RETI_RES_BUSY = 2,
	ACPIDEV_RETI_RES_INVALID_PARAMETER = 3,
	ACPIDEV_RETI_RES_INVALID_COMMAND = 4,
	ACPIDEV_RETI_RES_INTERNAL_ERROR = 255
} acpidev_reti_cmd_result_t;

#pragma pack(1)

typedef struct acpidev_ras_device_capability {
	uint16_t			cap_type;
	uint32_t			cap_flags;
	acpidev_ras_uid_t		cap_uid;
} acpidev_ras_device_cap_t;

typedef struct acpidev_reti_control {
	ACPI_GENERIC_ADDRESS		reti_cmd_addr;
	uint32_t			reti_cmd_value;
	ACPI_GENERIC_ADDRESS		reti_arg_addr;
} acpidev_reti_ctrl_t;

typedef struct acpidev_reti_cmd_sts {
	uint8_t				reti_cmd;
	uint32_t			reti_status;
} acpidev_reti_cmd_sts_t;

typedef struct acpidev_reti_evt_arg {
	acpidev_reti_cmd_sts_t		evt_cmd_sts;
	uint8_t				evt_dev_type;
	acpidev_ras_uid_t		evt_dev_uid;
} acpidev_reti_evt_arg_t;

#pragma pack()

extern kmutex_t acpidev_dr_lock;

/*
 * Simulate the behavior of hardware triggered online events.  When triggering
 * online events for sockets by pressing the attention buttons, the memory
 * devices connected to those sockets will be onlined together.
 * Set this flag to TRUE to simulate the behavior of hardware triggered online
 * events. And it should be set to TRUE if memory devices will always be onlined
 * together with the socket.
 */
boolean_t acpidev_reti_sim_hardware = B_FALSE;

/*
 * Protect the RETI command structure from concurrent access.
 */
static kmutex_t				acpidev_reti_lock;
static acpidev_reti_ctrl_t		*acpidev_reti_ctrl;
static acpidev_ras_device_cap_t		*acpidev_prct_array;
static uint32_t				acpidev_prct_count;

/*
 * Timeout value in seconds to wait for clients (acpinex driver) to
 * clear the events.
 */
static uint_t				acpidev_ras_event_wait = 120;
static kcondvar_t			acpidev_ras_event_cv;
static ACPI_HANDLE			acpidev_ras_event_hdl;

ACPI_STATUS
acpidev_ras_init(void)
{
	ACPI_TABLE_HEADER *prctp;
	ACPI_TABLE_HEADER *retip;
	int prct_err = 0;
	intptr_t ptr;
	size_t len;

	/* Validate and cache the ACPI PRCT table. */
	if (ACPI_FAILURE(AcpiGetTable(ACPIDEV_PRCT_NAME, 1, &prctp))) {
		return (AE_SUPPORT);
	}
	ASSERT(prctp != NULL);
	len = prctp->Length - sizeof (ACPI_TABLE_HEADER);
	if (len < sizeof (uint32_t)) {
		prct_err = 1;
	} else {
		ptr = (intptr_t)prctp + sizeof (ACPI_TABLE_HEADER);
		acpidev_prct_count = *(uint32_t *)ptr;
		ptr += sizeof (uint32_t);
		len -= sizeof (uint32_t);
		if (acpidev_prct_count * sizeof (acpidev_ras_device_cap_t) !=
		    len) {
			prct_err = 1;
		} else {
			acpidev_prct_array = (acpidev_ras_device_cap_t *)ptr;
		}
	}
	if (prct_err) {
		cmn_err(CE_WARN,
		    "?acpidev: invalid ACPI Platform RAS Capabilities Table.");
		acpidev_prct_array = NULL;
		acpidev_prct_count = 0;
		return (AE_INVALID_TABLE_LENGTH);
	}

	/* Validate and cache the ACPI RETI table. */
	if (ACPI_SUCCESS(AcpiGetTable(ACPIDEV_RETI_NAME, 1, &retip))) {
		if (retip->Length !=
		    sizeof (*retip) + sizeof (acpidev_reti_ctrl_t)) {
			cmn_err(CE_WARN, "?acpidev: invalid ACPI "
			    "RAS Event Trigger Interface Table.");
		} else {
			acpidev_reti_ctrl = (acpidev_reti_ctrl_t *)(retip + 1);
		}
	}

	mutex_init(&acpidev_reti_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&acpidev_ras_event_cv, NULL, CV_DEFAULT, NULL);

	return (AE_OK);
}

static uint8_t
acpidev_ras_get_devtype(acpidev_data_handle_t dhdl)
{
	if (dhdl->aod_class_id == ACPIDEV_CLASS_ID_CONTAINER) {
		return (ACPIDEV_RAS_DEVTYPE_MODULE);
	} else if (dhdl->aod_class_id == ACPIDEV_CLASS_ID_MEMORY) {
		return (ACPIDEV_RAS_DEVTYPE_MEMORY);
	} else {
		return (ACPIDEV_RAS_DEVTYPE_UNKNOWN);
	}
}

static acpidev_ras_device_cap_t *
acpidev_ras_search_capability(acpidev_class_id_t clsid, uint32_t uid)
{
	uint32_t idx;
	acpidev_ras_device_cap_t *cp;

	for (cp = acpidev_prct_array, idx = 0;
	    idx < acpidev_prct_count; cp++, idx++) {
		if (clsid == ACPIDEV_CLASS_ID_CONTAINER &&
		    cp->cap_type == ACPIDEV_RAS_DEVTYPE_MODULE &&
		    (cp->cap_uid == ACPIDEV_RAS_UID_ALL ||
		    cp->cap_uid == uid)) {
			return (cp);
		}
		if (clsid == ACPIDEV_CLASS_ID_MEMORY &&
		    cp->cap_type == ACPIDEV_RAS_DEVTYPE_MEMORY &&
		    (cp->cap_uid == ACPIDEV_RAS_UID_ALL ||
		    cp->cap_uid == uid)) {
			return (cp);
		}
	}

	return (NULL);
}

ACPI_STATUS
acpidev_ras_check_capabilities(ACPI_HANDLE hdl, acpidev_data_handle_t dhdl)
{
	int uid;
	acpidev_class_id_t clsid;
	acpidev_ras_device_cap_t *cp;

	ASSERT(hdl != NULL);
	if (acpidev_prct_array == NULL) {
		return (AE_SUPPORT);
	}

	/*
	 * The ACPI Platform RAS Capability Table is available, search the
	 * RAS capability entry for the device.
	 * Currently the RETI table only supports Module Device (including
	 * Physical Preocessor) and Memory Device.
	 */
	clsid = acpidev_dr_device_get_class(hdl);
	if (clsid != ACPIDEV_CLASS_ID_MEMORY &&
	    clsid != ACPIDEV_CLASS_ID_CONTAINER) {
		return (AE_SUPPORT);
	}
	if (ACPI_FAILURE(acpica_eval_int(hdl, METHOD_NAME__UID, &uid))) {
		return (AE_SUPPORT);
	} else if (uid < 0 || uid >= ACPIDEV_RAS_UID_ALL) {
		ACPIDEV_DEBUG(CE_WARN, "!acpidev: invalid _UID (%d).", uid);
		return (AE_LIMIT);
	}
	cp = acpidev_ras_search_capability(clsid, uid);
	if (cp == NULL) {
		return (AE_SUPPORT);
	} else if (dhdl == NULL) {
		return (AE_OK);
	}

	dhdl->aod_bduid = (uint32_t)uid;
	dhdl->aod_class_id = clsid;

	/* Check device RAS capabilities. */
	ACPIDEV_DR_SET_CAPABLE(dhdl);
	if (cp->cap_flags & ACPIDEV_PRCT_CAP_OxLINE) {
		ACPIDEV_DR_SET_OxLINE_CAPABLE(dhdl);
		if (cp->cap_flags & ACPIDEV_PRCT_CAP_POWER) {
			ACPIDEV_DR_SET_POWER_CAPABLE(dhdl);
		}
		if (cp->cap_flags & ACPIDEV_PRCT_CAP_HOTPLUG) {
			ACPIDEV_DR_SET_HOTPLUG_CAPABLE(dhdl);
		}
		if (cp->cap_flags & ACPIDEV_PRCT_CAP_SPARE) {
			ACPIDEV_DR_SET_SPARE_CAPABLE(dhdl);
		}

		switch (cp->cap_flags & ACPIDEV_PRCT_CAP_MIG_MASK) {
		case ACPIDEV_PRCT_CAP_MIG_NONBLOCK:
			ACPIDEV_DR_SET_MIGRATION_CAPABLE(dhdl);
			ACPIDEV_SET_MIGRATION_MODE(dhdl,
			    ACPIDEV_ODF_MIGRATION_NONBLOCK);
			break;
		case ACPIDEV_PRCT_CAP_MIG_BLOCK:
			ACPIDEV_DEBUG(CE_WARN, "!acpidev: unsupported "
			    "HARDWARE_BLOCKING migration mode.");
			break;
		case ACPIDEV_PRCT_CAP_MIG_CHILD:
			ACPIDEV_DEBUG(CE_WARN,
			    "!acpidev: unsupported CHILD migration mode.");
			break;
		case ACPIDEV_PRCT_CAP_MIG_NONE:
		default:
			break;
		}
	}

	return (AE_OK);
}

static ACPI_STATUS
acpidev_reti_read_value(ACPI_GENERIC_ADDRESS *ap, size_t off, size_t sz,
    uint64_t *valp)
{
	ACPI_GENERIC_ADDRESS addr;

	addr = *ap;
	addr.BitWidth = sz * NBBY;
	addr.Address += off;

	return (AcpiRead(valp, &addr));
}

static ACPI_STATUS
acpidev_reti_write_value(ACPI_GENERIC_ADDRESS *ap, size_t off, size_t sz,
    uint64_t val)
{
	ACPI_GENERIC_ADDRESS addr;

	addr = *ap;
	addr.BitWidth = sz * NBBY;
	addr.Address += off;

	return (AcpiWrite(val, &addr));
}

static ACPI_STATUS
acpidev_reti_issue_cmd(acpidev_reti_cmd_code_t cmd)
{
	ACPI_STATUS rc;
	uint64_t status = 0;

	ASSERT(MUTEX_HELD(&acpidev_reti_lock));

	/* Issue the command and wait for completion. */
	rc = acpidev_reti_write_value(&acpidev_reti_ctrl->reti_arg_addr,
	    offsetof(acpidev_reti_cmd_sts_t, reti_cmd),
	    sizeof (uint8_t), (uint64_t)cmd);
	if (ACPI_SUCCESS(rc)) {
		rc = (AcpiWrite(acpidev_reti_ctrl->reti_cmd_value,
		    &acpidev_reti_ctrl->reti_cmd_addr));
	}
	while (ACPI_SUCCESS(rc)) {
		rc = acpidev_reti_read_value(&acpidev_reti_ctrl->reti_arg_addr,
		    offsetof(acpidev_reti_cmd_sts_t, reti_status),
		    sizeof (uint32_t), &status);
		if (ACPI_FAILURE(rc)) {
			/* Failed to read the command status. */
			ACPIDEV_DEBUG(CE_WARN, "!acpidev: failed to read "
			    "status value from the RETI control block.");
			break;
		} else if (status == ACPIDEV_RETI_RES_SUCCESS) {
			/* The command finished. */
			break;
		} else if (status == ACPIDEV_RETI_RES_INPROGRESS) {
			/* The command is still inprogress. */
			delay(1);
		} else {
			/* The command failed. */
			if (status == ACPIDEV_RETI_RES_INVALID_PARAMETER) {
				ACPIDEV_DEBUG(CE_WARN, "!acpidev: RETI "
				    "command returns INVALID PARAMETER.");
			} else if (status == ACPIDEV_RETI_RES_BUSY) {
				ACPIDEV_DEBUG(CE_WARN, "!acpidev: RETI "
				    "command returns DEVICE BUSY.");
			} else if (status == ACPIDEV_RETI_RES_INVALID_COMMAND) {
				ACPIDEV_DEBUG(CE_WARN, "!acpidev: RETI "
				    "command returns INVALID COMMAND.");
			} else {
				ACPIDEV_DEBUG(CE_WARN, "!acpidev: RETI command "
				    "returns error code %d.", (uint32_t)status);
			}
			rc = AE_ERROR;
			break;
		}
	}

	return (rc);
}

static ACPI_STATUS
acpidev_reti_evt_cmd(ACPI_HANDLE hdl, acpidev_reti_cmd_code_t cmd)
{
	ACPI_STATUS rc = AE_OK;
	acpidev_data_handle_t dhdl;
	uint8_t type;

	if (hdl == NULL || (cmd != ACPIDEV_RETI_CMD_ONLINE &&
	    cmd != ACPIDEV_RETI_CMD_OFFLINE)) {
		ACPIDEV_DEBUG(CE_WARN,
		    "!acpidev: invalid parameters to acpidev_reti_evt_cmd().");
		return (AE_BAD_PARAMETER);
	}

	dhdl = acpidev_data_get_handle(hdl);
	if (dhdl == NULL) {
		ACPIDEV_DEBUG(CE_WARN, "!acpidev: failed to get data handle "
		    "associated with %p.", hdl);
		return (AE_ERROR);
	} else if (!ACPIDEV_DR_IS_OxLINE_CAPABLE(dhdl)) {
		cmn_err(CE_WARN,
		    "!acpidev: device %p isn't ONLINE/OFFLINE capable.", hdl);
		return (AE_SUPPORT);
	}

	type = acpidev_ras_get_devtype(dhdl);
	if (type == ACPIDEV_RAS_DEVTYPE_UNKNOWN) {
		ACPIDEV_DEBUG(CE_WARN, "!acpidev: ONLINE/OFFLINE can't "
		    "support device type %d.", dhdl->aod_class_id);
		return (AE_SUPPORT);
	}

	/*
	 * On Intel platforms, physical processors may be presented as ACPI
	 * module devices in ACPI namespace. Treat it as a physical processor
	 * if the board is a CPU board.
	 */
	if (acpidev_reti_sim_hardware == B_FALSE &&
	    dhdl->aod_bdtype == ACPIDEV_CPU_BOARD &&
	    type == ACPIDEV_RAS_DEVTYPE_MODULE) {
		type = ACPIDEV_RAS_DEVTYPE_PROCESSOR;
	}

	mutex_enter(&acpidev_reti_lock);
	if (acpidev_reti_ctrl == NULL) {
		mutex_exit(&acpidev_reti_lock);
		ACPIDEV_DEBUG(CE_WARN,
		    "!acpidev: the ACPI RETI table is unavailable.");
		return (AE_SUPPORT);
	}
	rc = acpidev_reti_write_value(&acpidev_reti_ctrl->reti_arg_addr,
	    offsetof(acpidev_reti_evt_arg_t, evt_dev_type),
	    sizeof (uint8_t), (uint64_t)type);
	if (ACPI_SUCCESS(rc)) {
		rc = acpidev_reti_write_value(&acpidev_reti_ctrl->reti_arg_addr,
		    offsetof(acpidev_reti_evt_arg_t, evt_dev_uid),
		    sizeof (acpidev_ras_uid_t), (uint64_t)dhdl->aod_bduid);
	}
	if (ACPI_SUCCESS(rc)) {
		rc = acpidev_reti_issue_cmd(cmd);
	}
	mutex_exit(&acpidev_reti_lock);
	if (ACPI_FAILURE(rc)) {
		ACPIDEV_DEBUG(CE_WARN,
		    "!acpidev: failed to issue %s command for the object %p.",
		    (cmd == ACPIDEV_RETI_CMD_ONLINE) ? "ONLINE" : "OFFLINE",
		    hdl);
	}

	return (rc);
}

ACPI_STATUS
acpidev_ras_trigger_offline_event(ACPI_HANDLE hdl)
{
	return (acpidev_reti_evt_cmd(hdl, ACPIDEV_RETI_CMD_OFFLINE));
}

ACPI_STATUS
acpidev_ras_trigger_online_event(ACPI_HANDLE hdl)
{
	ACPI_STATUS rc;

	ASSERT(MUTEX_HELD(&acpidev_dr_lock));

	if (acpidev_reti_sim_hardware) {
		return (acpidev_reti_evt_cmd(hdl, ACPIDEV_RETI_CMD_ONLINE));
	}

	while (acpidev_ras_event_hdl != NULL) {
		cv_wait(&acpidev_ras_event_cv, &acpidev_dr_lock);
	}
	acpidev_ras_event_hdl = hdl;

	rc = acpidev_reti_evt_cmd(hdl, ACPIDEV_RETI_CMD_ONLINE);
	if (ACPI_SUCCESS(rc)) {
		/* Wait for the client to clear the event. */
		(void) cv_timedwait(&acpidev_ras_event_cv, &acpidev_dr_lock,
		    ddi_get_lbolt() + SEC_TO_TICK(acpidev_ras_event_wait));
		if (acpidev_ras_event_hdl != NULL) {
			cmn_err(CE_WARN, "!acpidev: failed to handle "
			    "the software triggered ONLINE event.");
		}
	}

	acpidev_ras_event_hdl = NULL;
	cv_broadcast(&acpidev_ras_event_cv);

	return (rc);
}

ACPI_STATUS
acpidev_ras_clear_event(ACPI_HANDLE hdl, UINT32 event_type)
{
	ASSERT(MUTEX_HELD(&acpidev_dr_lock));

	if (hdl != acpidev_ras_event_hdl) {
		return (AE_ERROR);
	} else if (event_type != ACPI_NOTIFY_BUS_CHECK &&
	    event_type != ACPI_NOTIFY_DEVICE_CHECK &&
	    event_type != ACPI_NOTIFY_DEVICE_CHECK_LIGHT) {
		return (AE_ERROR);
	}

	/* Clear the event and notify the event producer. */
	acpidev_ras_event_hdl = NULL;
	cv_broadcast(&acpidev_ras_event_cv);

	return (AE_OK);
}
