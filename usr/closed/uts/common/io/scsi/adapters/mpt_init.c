/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mpt_init - This file contains all the functions used to initialize
 * MPT based hardware.
 */

#if defined(lint) && !defined(DEBUG)
#define	DEBUG 1
#define	MPT_DEBUG
#endif

/*
 * standard header files
 */
#include <sys/note.h>
#include <sys/scsi/scsi.h>

#include <sys/mpt/mpi.h>
#include <sys/mpt/mpi_cnfg.h>
#include <sys/mpt/mpi_init.h>
#include <sys/mpt/mpi_ioc.h>

/*
 * private header files.
 */
#include <sys/scsi/adapters/mptvar.h>
#include <sys/scsi/adapters/mptreg.h>

static int mpt_ioc_do_get_facts(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);
static int mpt_ioc_do_get_facts_reply(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);
static int mpt_ioc_do_get_port_facts(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);
static int mpt_ioc_do_get_port_facts_reply(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);
static int mpt_ioc_do_enable_port(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);
static int mpt_ioc_do_enable_port_reply(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);
static int mpt_ioc_do_enable_event_notification(mpt_t *mpt, caddr_t memp,
	int var, ddi_acc_handle_t accessp);
static int mpt_ioc_do_enable_event_notification_reply(mpt_t *mpt, caddr_t memp,
	int var, ddi_acc_handle_t accessp);
static int mpt_do_ioc_init(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);
static int mpt_do_ioc_init_reply(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp);

extern uint_t mpt_raid_fw[];
extern uint_t mpt_fw[];

static const char *
mpt_product_type_string(mpt_t *mpt)
{
	switch (mpt->m_productid & MPI_FW_HEADER_PID_PROD_MASK) {

	case MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI:
		return ("I");
	case MPI_FW_HEADER_PID_PROD_TARGET_INITIATOR_SCSI:
		return ("IT");
	case MPI_FW_HEADER_PID_PROD_TARGET_SCSI:
		return ("T");
	case MPI_FW_HEADER_PID_PROD_IM_SCSI:
		return ("IM/IME");
	case MPI_FW_HEADER_PID_PROD_IS_SCSI:
		return ("IS");
	case MPI_FW_HEADER_PID_PROD_CTX_SCSI:
		return ("CTX");
	case MPI_FW_HEADER_PID_PROD_IR_SCSI:
		return ("IR");
	default:
		return ("?");
	}
}

int
mpt_ioc_get_facts(mpt_t *mpt)
{
	/*
	 * Send get facts messages
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_ioc_facts), NULL,
	    mpt_ioc_do_get_facts)) {
		return (DDI_FAILURE);
	}

	/*
	 * Get facts reply messages
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_ioc_facts_reply), NULL,
	    mpt_ioc_do_get_facts_reply)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_get_facts(mpt_t *mpt, caddr_t memp, int var,
		ddi_acc_handle_t accessp)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(var))
#endif
	msg_ioc_facts_t *facts;
	int numbytes;

	bzero(memp, sizeof (*facts));
	facts = (void *)memp;
	ddi_put8(accessp, &facts->Function, MPI_FUNCTION_IOC_FACTS);
	numbytes = sizeof (*facts);

	/*
	 * Post message via handshake
	 */
	if (mpt_send_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_get_facts_reply(mpt_t *mpt, caddr_t memp, int var,
		ddi_acc_handle_t accessp)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(var))
#endif

	msg_ioc_facts_reply_t *factsreply;
	int numbytes;
	char buf[32];

	bzero(memp, sizeof (*factsreply));
	factsreply = (void *)memp;
	numbytes = sizeof (*factsreply);

	/*
	 * get ioc facts reply message
	 */
	if (mpt_get_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	if (mpt_handle_ioc_status(mpt, accessp, &factsreply->IOCStatus,
	    &factsreply->IOCLogInfo, "mpt_ioc_do_get_facts_reply", 1)) {
		return (DDI_FAILURE);
	}

	/*
	 * store key values from reply to mpt structure
	 */
	mpt->m_fwversion = ddi_get32(accessp, &factsreply->FWVersion.Word);
	mpt->m_productid = ddi_get16(accessp, &factsreply->ProductID);

	/* If we can download firmware to the adapter, do so */
	if (mpt_can_download_firmware(mpt)) {

		if (mpt_download_firmware(mpt)) {
			mpt_log(mpt, CE_WARN, "Firmware download failed\n");
			return (DDI_FAILURE);
		}

		/*
		 * Issue get facts again
		 */
		if (mpt_ioc_get_facts(mpt) == DDI_FAILURE) {
			mpt_log(mpt, CE_WARN, "mpt_ioc_get_facts failed");
			return (DDI_FAILURE);
		}
	} else {
		(void) sprintf(buf, "%u.%u.%u.%u",
		    ddi_get8(accessp, &factsreply->FWVersion.Struct.Major),
		    ddi_get8(accessp, &factsreply->FWVersion.Struct.Minor),
		    ddi_get8(accessp, &factsreply->FWVersion.Struct.Unit),
		    ddi_get8(accessp, &factsreply->FWVersion.Struct.Dev));
		mpt_log(mpt, CE_NOTE, "?mpt%d Firmware version v%s (%s)\n",
		    mpt->m_instance, buf, mpt_product_type_string(mpt));
		(void) ddi_prop_update_string(DDI_DEV_T_NONE, mpt->m_dip,
		    "firmware-version", buf);

		mpt->m_num_ports = ddi_get8(accessp,
		    &factsreply->NumberOfPorts);
		mpt->m_req_frame_size = ddi_get16(accessp,
		    &factsreply->RequestFrameSize);

		/*
		 * Don't allocate the full number of replies due
		 * to a bug in the firmware that only allows a maximum
		 * of 255 reply frames to be posted
		 */
		mpt->m_max_reply_depth = (ddi_get16(accessp,
		    &factsreply->ReplyQueueDepth) - 1);
		mpt->m_max_request_depth = ddi_get16(accessp,
		    &factsreply->GlobalCredits);
		mpt->m_ioc_num = ddi_get8(accessp, &factsreply->IOCNumber);
		mpt->m_max_chain_depth = ddi_get8(accessp,
		    &factsreply->MaxChainDepth);

		/*
		 * Check to see if we had to download firmware to this chip
		 */
		if (ddi_get8(accessp, &factsreply->Flags) &
		    MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT) {
			mpt->m_fwupload = 1;
		}
	}

	return (DDI_SUCCESS);
}

int
mpt_ioc_get_port_facts(mpt_t *mpt, int port)
{
	/*
	 * Send get port facts message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_port_facts), port,
	    mpt_ioc_do_get_port_facts)) {
		return (DDI_FAILURE);
	}

	/*
	 * Get port facts reply message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_port_facts_reply), port,
	    mpt_ioc_do_get_port_facts_reply)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_get_port_facts(mpt_t *mpt, caddr_t memp, int var,
			ddi_acc_handle_t accessp)
{
	msg_port_facts_t *facts;
	int numbytes;

	bzero(memp, sizeof (*facts));
	facts = (void *)memp;
	ddi_put8(accessp, &facts->Function, MPI_FUNCTION_PORT_FACTS);
	ddi_put8(accessp, &facts->PortNumber, var);
	numbytes = sizeof (*facts);

	/*
	 * Send port facts message via handshake
	 */
	if (mpt_send_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_get_port_facts_reply(mpt_t *mpt, caddr_t memp, int var,
				ddi_acc_handle_t accessp)
{
	msg_port_facts_reply_t *factsreply;
	int numbytes;

	bzero(memp, sizeof (*factsreply));
	factsreply = (void *)memp;
	numbytes = sizeof (*factsreply);

	/*
	 * Get port facts reply message via handshake
	 */
	if (mpt_get_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	if (mpt_handle_ioc_status(mpt, accessp, &factsreply->IOCStatus,
	    &factsreply->IOCLogInfo, "mpt_ioc_do_get_port_facts_reply", 1)) {
		return (DDI_FAILURE);
	}

	/*
	 * check to make sure port is cabable of being initiateor
	 */
	if ((ddi_get8(accessp, &factsreply->PortNumber) == var) &&
	    (ddi_get16(accessp, &factsreply->ProtocolFlags) &
	    MPI_PORTFACTS_PROTOCOL_INITIATOR)) {
		/*
		 * store port facts data
		 */
		mpt->m_port_type[var] = ddi_get8(accessp,
		    &factsreply->PortType);
		mpt->m_protocol_flags[var] = ddi_get16(accessp,
		    &factsreply->ProtocolFlags);
		mpt->m_maxdevices[var] = ddi_get16(accessp,
		    &factsreply->MaxDevices);
	}

	return (DDI_SUCCESS);
}

int
mpt_ioc_enable_port(mpt_t *mpt, int port)
{
	/*
	 * Send enable port message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_port_enable), port,
	    mpt_ioc_do_enable_port)) {
		return (DDI_FAILURE);
	}

	/*
	 * Get enable port reply message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_port_enable_reply), port,
	    mpt_ioc_do_enable_port_reply)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_enable_port(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp)
{
	msg_port_enable_t *enable;
	int numbytes;

	bzero(memp, sizeof (*enable));
	enable = (void *)memp;
	ddi_put8(accessp, &enable->Function, MPI_FUNCTION_PORT_ENABLE);
	ddi_put8(accessp, &enable->PortNumber, var);
	numbytes = sizeof (*enable);

	/*
	 * Send message via handshake
	 */
	if (mpt_send_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_enable_port_reply(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(var))
#endif

	int numbytes;
	msg_port_enable_reply_t *portreply;

	numbytes = sizeof (msg_port_enable_reply_t);
	bzero(memp, numbytes);
	portreply = (void *)memp;

	/*
	 * Get message via handshake
	 */
	if (mpt_get_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	if (mpt_handle_ioc_status(mpt, accessp, &portreply->IOCStatus,
	    &portreply->IOCLogInfo, "mpt_ioc_do_enable_port_reply", 1)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

int
mpt_ioc_enable_event_notification(mpt_t *mpt)
{
	ASSERT(mutex_owned(&mpt->m_mutex));

	/*
	 * Send enable event notification message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_event_notify), NULL,
	    mpt_ioc_do_enable_event_notification)) {
		return (DDI_FAILURE);
	}

	/*
	 * Get enable event reply message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_event_notify_reply), NULL,
	    mpt_ioc_do_enable_event_notification_reply)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_enable_event_notification(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(var))
#endif

	msg_event_notify_t *event;
	int numbytes;

	bzero(memp, sizeof (*event));
	event = (void *)memp;
	ddi_put8(accessp, &event->Function, MPI_FUNCTION_EVENT_NOTIFICATION);
	ddi_put8(accessp, &event->Switch, MPI_EVENT_NOTIFICATION_SWITCH_ON);
	numbytes = sizeof (*event);

	/*
	 * Send message via handshake
	 */
	if (mpt_send_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_ioc_do_enable_event_notification_reply(mpt_t *mpt, caddr_t memp, int var,
	ddi_acc_handle_t accessp)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(var))
#endif
	int numbytes;
	msg_event_notify_reply_t *eventsreply;

	numbytes = sizeof (msg_event_notify_reply_t);
	bzero(memp, numbytes);
	eventsreply = (void *)memp;

	/*
	 * Get message via handshake
	 */
	if (mpt_get_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	if (mpt_handle_ioc_status(mpt, accessp, &eventsreply->IOCStatus,
	    &eventsreply->IOCLogInfo,
	    "mpt_ioc_do_enable_event_notification_reply", 1)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

int
mpt_ioc_init(mpt_t *mpt)
{
	/*
	 * Send ioc init message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_ioc_init), NULL,
	    mpt_do_ioc_init)) {
		return (DDI_FAILURE);
	}

	/*
	 * Get ioc init reply message
	 */
	if (mpt_do_dma(mpt, sizeof (struct msg_ioc_init_reply), NULL,
	    mpt_do_ioc_init_reply)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_do_ioc_init(mpt_t *mpt, caddr_t memp, int var, ddi_acc_handle_t accessp)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(var))
#endif

	msg_ioc_init_t *init;
	int numbytes;

	bzero(memp, sizeof (*init));
	init = (void *)memp;
	ddi_put8(accessp, &init->Function, MPI_FUNCTION_IOC_INIT);
	ddi_put8(accessp, &init->WhoInit, MPI_WHOINIT_HOST_DRIVER);
	ddi_put8(accessp, &init->MaxDevices, 0);
	ddi_put8(accessp, &init->MaxBuses, 0);
	ddi_put32(accessp, &init->HostMfaHighAddr, 0);
	ddi_put32(accessp, &init->SenseBufferHighAddr, 0);
	ddi_put16(accessp, &init->ReplyFrameSize, MPT_REPLY_FRAME_SIZE);

	numbytes = sizeof (*init);

	/*
	 * If fw was downloaded by the host, tell ioc to throw out firmware
	 * that is sitting in it's memory taking up space
	 */
	if (mpt->m_fwupload) {
		ddi_put8(accessp, &init->Flags,
		    MPI_IOCINIT_FLAGS_DISCARD_FW_IMAGE);
	}

	/*
	 * Post message via handshake
	 */
	if (mpt_send_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
mpt_do_ioc_init_reply(mpt_t *mpt, caddr_t memp, int var,
		ddi_acc_handle_t accessp)
{
#ifndef __lock_lint
	_NOTE(ARGUNUSED(var))
#endif

	msg_ioc_init_reply_t *initreply;
	int numbytes;

	numbytes = sizeof (msg_ioc_init_reply_t);
	bzero(memp, numbytes);
	initreply = (void *)memp;

	/*
	 * Get reply message via handshake
	 */
	if (mpt_get_handshake_msg(mpt, memp, numbytes, accessp)) {
		return (DDI_FAILURE);
	}

	if (mpt_handle_ioc_status(mpt, accessp, &initreply->IOCStatus,
	    &initreply->IOCLogInfo, "mpt_do_ioc_init_reply", 1)) {
		return (DDI_FAILURE);
	}

	if ((ddi_get32(mpt->m_datap, &mpt->m_reg->m_doorbell)) &
	    MPI_IOC_STATE_OPERATIONAL) {
		mpt_log(mpt, CE_NOTE,
		    "?mpt%d: IOC Operational.\n", mpt->m_instance);
	} else {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}
