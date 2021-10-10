/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2009, 2011, Hewlett-Packard Development Company, L.P.
 */

/*
 *
 * Module Name:
 *   CPQARY3_NOE.C
 * Abstract:
 *  This  File  has  Modules  that  handle  the NOE  functionality  for
 *	this driver.
 *	It  builds and  submits  the NOE  command to  the adapter.  It also
 *	processes a completed NOE command.
 *	A study of the FirmWare specifications would be neccessary to relate
 *	coding in this module to the hardware functionality.
 */

#include	"cpqary3.h"

/*
 * All Possible Logical Volume Status
 */

char *log_vol_status[] = {
	"OK",
	"Failed",
	"Not Configured",
	"Regenerating",
	"Needs Rebuild Permission",
	"Rebuilding",
	"Wrong Drive Replaced",
	"Bad Drive Connection",
	"Box Overheating",
	"Box Overheated",
	"Volume Expanding",
	"Not Yet Available",
	"Volume Needs to Expand",
	"Disabled",
	"Ejected",
	"Erasing",
	"Unknown"
};

typedef struct cpqary3_val_str_table_struct {
	int32_t val;
	int8_t *str;
} cpqary3_val_str_table;

static cpqary3_val_str_table ascii_failure_reason[] = {
	{0,  "None"},
	{0x01, "Too Small in Load Config"},
	{0x02, "Error Erasing RIS"},
	{0x03, "Error Saving RIS"},
	{0x04, "Fail Drive Command"},
	{0x05, "Mark Bad Drive Failed"},
	{0x06, "Mark Bad Drive failed in Finish Remap"},
	{0x07, "Timeout"},
	{0x08, "AutoSense Failed"},
	{0x09, "Medium Error 1"},
	{0x0A, "Medium Error 2"},
	{0x0B, "Not Ready - Bad Sense Code"},
	{0x0C, "Not Ready"},
	{0x0D, "Hardware Error"},
	{0x0E, "Command Aborted"},
	{0x0F, "Write Protected"},
	{0x10, "Spin Up Failure during recovery"},
	{0x11, "Rebuild Write Error"},
	{0x12, "Too Small in Hot Plug"},
	{0x13, "Reset Recovery Abort"},
	{0x14, "Removed in Hot Plug"},
	{0x15, "Init Request Sense Failed"},
	{0x16, "Init Start Unit Failed"},
	{0x17, "Inquiry Failed"},
	{0x18, "Non Disk Device"},
	{0x19, "Read Capacity Failed"},
	{0x1A, "Invalid Block Size"},
	{0x1B, "HotPlug Request Sense Failed"},
	{0x1C, "HotPug Start Unit Failed"},
	{0x1D, "Write Error After Remap"},
	{0x1E, "Init Reset Recovery Aborted"},
	{0x1F, "Deferred Write Error"},
	{0x20, "Missing In Save RIS"},
	{0x21, "Wrong Replace"},
	{0x22, "GDP VPD Enquiry Failed"},
	{0x23, "GDP Mode Sense Failed"},
	{0x24, "Drive not in 48 BIT mode"},
	{0x25, "Drive Type Mix in Hot Plug"},
	{0x26, "Drive Type Mix in Load Configuration"},
	{0x27, "Protocol Adapter Failed"},
	{0x28, "Faulty ID - Bay Empty"},
	{0x29, "Faulty ID - Bay Occupied"},
	{0x2A, "Faulty ID In Invalid Bay"},
	{0x2B, "Write Retries Failed"},
	{0x30, "Invalid Target TSH"},
	{0x31, "CHIM - Bus Hung"},
	{0x32, "HotPlug Domain Validation Failed"},
	{0x33, "Init Domain Validation Failed"},
	{0x34, "Init HotPlug Probe Failed"},
	{0x35, "CHIM - Scan Failed"},
	{0x36, "Queue Full On Zero"},
	{0x37, "Smart Error Reported"},
	{0x38, "PHY Reset Failed"},
	{0x40, "Only One Controller Can See drive"},
	{0x41, "KC Volume Failed"},
	{0x42, "Unexpected Replacement"},
	{0x80, "Drive Offline Due to Secure Erase Operation"},
	{0x81, "Drive Offline - replacement Drive too Small"},
	{0x82, "Drive Offline - Mix Type Drives"},
	{0x83, "Drive Offline - Secure Erase Complete, not yet replaced"}
};


/*
 * Local Functions Definitions
 */

uint8_t cpqary3_disable_NOE_command(cpqary3_t *);
static int8_t *cpqary3_table_lookup(cpqary3_val_str_table *TabPtr, uint32_t);

/*
 *	Function        : 	cpqary3_send_NOE_command
 *	Description     : 	This routine builds and submits the NOE Command
 *				to the Controller.
 *	Called By       :   	cpqary3_attach(), cpqary3_NOE_handler()
 *	Parameters      : 	per-controller, per-command,
 *				Flag to signify first time or otherwise
 *	Calls           :   	cpqary3_alloc_phyctgs_mem(),
 *				cpqary3_cmdlist_occupy(),
 *				cpqary3_submit(), cpqary3_add2submitted_cmdq(),
 *				cpqary3_free_phyctgs_mem()
 *	Return Values   : 	SUCCESS / FAILURE
 *				[Shall fail only if memory allocation
 *				issues exist]
 */
uint8_t
cpqary3_send_NOE_command(cpqary3_t *ctlr, cpqary3_cmdpvt_t *memp,
    uint8_t flag)
{
	uint32_t		phys_addr = 0;
	NoeBuffer 		*databuf;
	CommandList_t  		*cmdlist;
	cpqary3_phyctg_t	*phys_handle;

	RETURN_FAILURE_IF_NULL(ctlr);
	/*
	 * NOTE : DO NOT perform this operation
	 * for memp. Shall result in a failure
	 * of submission of the NOE command
	 *  as it shall be NULL for the
	 *  very first time
	 */

	/*
	 * Allocate Memory for Return data
	 * if failure, RETURN.
	 * Allocate Memory for CommandList
	 * If error, RETURN.
	 * get the Request Block from the CommandList
	 * Fill in the Request Packet with the corresponding values
	 * Special Information can be filled in the "bno" field of
	 * the request structure.
	 * Here, the "bno" field is filled for Asynchronous Mode.
	 * Submit the Command.
	 * If Failure, WARN and RETURN.
	 */
	if (CPQARY3_NOE_RESUBMIT == flag) {
		if ((NULL == memp) || (NULL == memp->cmdlist_memaddr)) {
			cmn_err(CE_WARN, " CPQary3 : _send_NOE_command :"
			    " Re-Use Not possible; CommandList NULL \n");
			return (CPQARY3_FAILURE);
		}

		bzero(MEM2DRVPVT(memp)->sg, sizeof (NoeBuffer));
		memp->cmdlist_memaddr->Header.Tag.drvinfo_n_err =
		    CPQARY3_NOECMD_SUCCESS;
	}	/* end of if (CPQARY3_NOE_RESUBMIT...) */
	else if (CPQARY3_NOE_INIT == flag) {
		phys_handle = (cpqary3_phyctg_t *)
		    MEM_ZALLOC(sizeof (cpqary3_phyctg_t));
		if (!phys_handle)
			return (CPQARY3_FAILURE);

		databuf = (NoeBuffer *)cpqary3_alloc_phyctgs_mem(ctlr,
		    sizeof (NoeBuffer), &phys_addr, phys_handle);
		if (!databuf) {
			return (CPQARY3_FAILURE);
		}
		bzero(databuf, sizeof (NoeBuffer));

		if (NULL == (memp = cpqary3_cmdlist_occupy(ctlr))) {
			cpqary3_free_phyctgs_mem(phys_handle,
			    CPQARY3_FREE_PHYCTG_MEM);
			return (CPQARY3_FAILURE);
		}

		memp->driverdata = (cpqary3_private_t *)
		    MEM_ZALLOC(sizeof (cpqary3_private_t));
		if (NULL == memp->driverdata) {
			cpqary3_free_phyctgs_mem(phys_handle,
			    CPQARY3_FREE_PHYCTG_MEM);
			cpqary3_cmdlist_release(memp,
			    CPQARY3_HOLD_SW_MUTEX);
			return (CPQARY3_FAILURE);
		}
		memp->driverdata->sg		= databuf;
		memp->driverdata->phyctgp	= phys_handle;

		cmdlist	= memp->cmdlist_memaddr;
		cmdlist->Header.SGTotal			= 1;
		cmdlist->Header.SGList			= 1;
		cmdlist->Header.Tag.drvinfo_n_err = CPQARY3_NOECMD_SUCCESS;
		cmdlist->Header.LUN.PhysDev.Mode  = PERIPHERIAL_DEV_ADDR;

		cmdlist->Request.CDBLen			= CISS_NOE_CDB_LEN;
		cmdlist->Request.Timeout		= 0;
		cmdlist->Request.Type.Type		= CISS_TYPE_CMD;
		cmdlist->Request.Type.Attribute		= CISS_ATTR_HEADOFQUEUE;
		cmdlist->Request.Type.Direction 	= CISS_XFER_READ;
		cmdlist->Request.CDB[0]			= CISS_NEW_READ;
		cmdlist->Request.CDB[1]			= BMIC_NOTIFY_ON_EVENT;
		cmdlist->Request.CDB[6]			= ENABLE_FW_SERIAL_LOG;
		cmdlist->Request.CDB[10] = (NOE_BUFFER_LENGTH >> 8) & 0xff;
		cmdlist->Request.CDB[11] = NOE_BUFFER_LENGTH & 0xff;

		cmdlist->SG[0].Addr			= phys_addr;
		cmdlist->SG[0].Len			= NOE_BUFFER_LENGTH;

	}	/* end of if(CPQARY3_NOE_INIT...) */

	/* PERF */

	memp->complete = cpqary3_noe_complete;

	mutex_enter(&ctlr->hw_mutex);
	if (EIO == cpqary3_submit(ctlr, memp)) {
		mutex_exit(&ctlr->hw_mutex);
		if (CPQARY3_NOE_INIT == flag) {
			cpqary3_free_phyctgs_mem(phys_handle,
			    CPQARY3_FREE_PHYCTG_MEM);
			cpqary3_cmdlist_release(memp, CPQARY3_HOLD_SW_MUTEX);
		}
		return (CPQARY3_FAILURE);
	}
	mutex_exit(&ctlr->hw_mutex);

	return (CPQARY3_SUCCESS);

}	/* cpqary3_send_NOE_command() */


/*
 *	Function        : 	cpqary3_disable_NOE_command
 *	Description     : 	This routine disables the Event Notifier
 *				for the specified Controller.
 *	Called By       : 	cpqary3_cleanup()
 *	Parameters      : 	Per Controller Structure
 *	Calls           :   	cpqary3_cmdlist_occupy(), cpqary3_submit(),
 *				cpqary3_add2submitted_cmdq()
 *	Return Values   : 	SUCCESS / FAILURE
 *				[Shall fail only if Memory Constraints exist]
 */
uint8_t
cpqary3_disable_NOE_command(cpqary3_t *ctlr)
{
	CommandList_t		*cmdlist;
	cpqary3_cmdpvt_t	*memp;
	int			cv_rval;
	clock_t			cpqary3_lbolt, timeout;

	RETURN_FAILURE_IF_NULL(ctlr);

	/*
	 * Allocate Memory for CommandList
	 * If error, RETURN.
	 * get the Request Block from the CommandList
	 * Fill in the Request Packet with the corresponding values
	 * Submit the Command.
	 * If Failure, WARN and RETURN.
	 */

	if (NULL == (memp = cpqary3_cmdlist_occupy(ctlr))) {
		cmn_err(CE_WARN, "CPQary3 : _disable_NOE_command : Failed \n");
		return (CPQARY3_FAILURE);
	}

	cmdlist = memp->cmdlist_memaddr;
	cmdlist->Header.Tag.drvinfo_n_err = CPQARY3_NOECMD_SUCCESS;
	cmdlist->Header.LUN.PhysDev.Mode = PERIPHERIAL_DEV_ADDR;

	cmdlist->Request.CDBLen = CISS_CANCEL_NOE_CDB_LEN;
	cmdlist->Request.Timeout = 0;
	cmdlist->Request.Type.Type = CISS_TYPE_CMD;
	cmdlist->Request.Type.Attribute = CISS_ATTR_HEADOFQUEUE;
	cmdlist->Request.Type.Direction = CISS_XFER_NONE;
	cmdlist->Request.CDB[0]		= ARRAY_WRITE;	/* 0x27 */
	cmdlist->Request.CDB[6]		= BMIC_CANCEL_NOTIFY_ON_EVENT;

	memp->complete = cpqary3_noe_complete;

	mutex_enter(&ctlr->hw_mutex);
	memp->cmdpvt_flag = CPQARY3_NOECMD_SUBMITTED;

	if (EIO == cpqary3_submit(ctlr, memp)) {
		mutex_exit(&ctlr->hw_mutex);
		cpqary3_cmdlist_release(memp, CPQARY3_HOLD_SW_MUTEX);
		return (CPQARY3_FAILURE);
	}

	cpqary3_lbolt = ddi_get_lbolt();
	timeout = cpqary3_lbolt + drv_usectohz(3000000);
	while (memp->cmdpvt_flag == CPQARY3_NOECMD_SUBMITTED) {

		cv_rval = cv_timedwait_sig(&ctlr->cv_noe_wait,
		    &ctlr->hw_mutex, timeout);
		switch (cv_rval) {
		case -1: /* timed out */
			memp->cmdpvt_flag = CPQARY3_TIMEOUT;
			cmn_err(CE_NOTE, "CPQary3: "
			    "Timeout reached\n"
			    "Resume signal for disable NOE " \
			    "command not received\n");
			mutex_exit(&ctlr->hw_mutex);
			return (CPQARY3_FAILURE);
		case 0: /* signal received */
			memp->cmdpvt_flag = CPQARY3_EINTR;
			cmn_err(CE_NOTE, "CPQary3: "
			    "system call interrupted "
			    "for disable NOE command\n");
			mutex_exit(&ctlr->hw_mutex);
			return (CPQARY3_FAILURE);
		default:
			break;
		}
	} /* end of while */
	mutex_exit(&ctlr->hw_mutex);
	return (CPQARY3_SUCCESS);

}	/* cpqary3_disable_NOE_command() */

uint8_t
cpqary3_disable_NOE_command_nlock(cpqary3_t *ctlr, clock_t timeoutms)
{
	CommandList_t		*cmdlist;
	cpqary3_cmdpvt_t	*memp;
	uint8_t	rc = 0;
	clock_t tt;

	RETURN_FAILURE_IF_NULL(ctlr);

	/*
	 * Allocate Memory for CommandList
	 * If error, RETURN.
	 * get the Request Block from the CommandList
	 * Fill in the Request Packet with the corresponding values
	 * Submit the Command.
	 * If Failure, WARN and RETURN.
	 */

	memp = ctlr->quiescecmd;
	cmdlist = memp->cmdlist_memaddr;
	cmdlist->Header.Tag.drvinfo_n_err = CPQARY3_NOECMD_SUCCESS;
	cmdlist->Header.LUN.PhysDev.Mode = PERIPHERIAL_DEV_ADDR;

	cmdlist->Request.CDBLen = CISS_CANCEL_NOE_CDB_LEN;
	cmdlist->Request.Timeout = 0;
	cmdlist->Request.Type.Type = CISS_TYPE_CMD;
	cmdlist->Request.Type.Attribute = CISS_ATTR_HEADOFQUEUE;
	cmdlist->Request.Type.Direction = CISS_XFER_NONE;
	cmdlist->Request.CDB[0]		= ARRAY_WRITE;	/* 0x27 */
	cmdlist->Request.CDB[6]		= BMIC_CANCEL_NOTIFY_ON_EVENT;

	/* PERF */

	(void) cpqary3_submit(ctlr, memp);

	/* poll for completion */
	rc = CPQARY3_FAILURE;
	for (tt = 0; (timeoutms == 0) || (tt < timeoutms); tt++) {
		rc = cpqary3_poll_retrieve(ctlr, memp->tag.tag_value, 0);
		if (CPQARY3_SUCCESS == rc) {
			break;
		}
		drv_usecwait(1000);
	}
	if (CPQARY3_SUCCESS != rc) {
		prom_printf("CPQary3 %s : Disable NOE Operation"
		    "Failed, Timeout in quiesce\n", ctlr->hba_name);
		return (rc);
	}

	/* PERF */
	return (CPQARY3_SUCCESS);

}	/* cpqary3_disable_NOE_command_nlock() */



/*
 *	Function        : 	cpqary3_NOE_handler
 *	Description     : 	This routine handles all those NOEs
 *				tabulated at the
 *				begining of this code.
 *	Called By       : 	cpqary3_process_pkt()
 *	Parameters      : 	Pointer to the Command List
 *	Calls           :   	cpqary3_send_NOE_command(),
 *				cpqary3_display_spare_status()
 *				cpqary3_free_phyctgs_mem(),
 *				cpqary3_cmdlist_release()
 *	Return Values   : 	None
 */
void
cpqary3_NOE_handler(cpqary3_cmdpvt_t *memp)
{
	uint16_t		drive = 0;
	NoeBuffer		*evt;
	cpqary3_t		*ctlr;
	cpqary3_phyctg_t	*phys_handle;
	uint8_t			driveId = 0;
	uint8_t			*failure_string;

	DBG("CPQary3 : _NOE_handler : Entering \n");

	/*
	 * This should never happen....
	 * If the pointer passed as argument is NULL, Panic the System.
	 */
	if (!memp) {
		cmn_err(CE_PANIC, "CPQary3 : Notification of Events :"
		    "	NULL pointer argument !!! \n");
	}

	evt = (NoeBuffer*) MEM2DRVPVT(memp)->sg;
	ctlr = (cpqary3_t *)memp->ctlr;
	phys_handle	= (cpqary3_phyctg_t *)MEM2DRVPVT(memp)->phyctgp;

	/* Don't display more than 79 characters */
	evt->ascii_message[79] = 0;

	switch (evt->event_class_code) {
	case CLASS_PROTOCOL:
		/*
		 * the following cases are not handled:
		 * 000 	: This is for Synchronous NOE. CPQary3 follows
		 * 	asynchronous NOE.
		 * 002	: Asynchronous NOE time out. CPQary3 does not implement
		 * 	time outs for NOE. It shall always reside in the HBA.
		 */

		cmn_err(CE_NOTE, " %s", ctlr->hba_name);
		if ((evt->event_subclass_code == SUB_CLASS_NON_EVENT) &&
		    (evt->event_detail_code == DETAIL_DISABLED)) {
			cmn_err(CE_CONT, " %s", ctlr->hba_name);
			cmn_err(CE_CONT, "CPQary3 : Event Notifier"
			    " Disabled \n");
			MEM_SFREE(memp->driverdata, sizeof (cpqary3_private_t));
			cpqary3_free_phyctgs_mem(phys_handle,
			    CPQARY3_FREE_PHYCTG_MEM);
			cpqary3_cmdlist_release(memp, CPQARY3_NO_MUTEX);
			return;
		} else if ((evt->event_subclass_code ==
		    SUB_CLASS_PROTOCOL_ERR) && (evt->event_detail_code ==
		    DETAIL_EVENT_Q_OVERFLOW)) {
			cmn_err(CE_CONT, " %s\n", evt->ascii_message);
		}
		cmn_err(CE_CONT, "\n");
		break;

	case CLASS_HOT_PLUG:
		if (evt->event_subclass_code == SUB_CLASS_HP_CHANGE) {
			cmn_err(CE_NOTE, " %s", ctlr->hba_name);
			cmn_err(CE_CONT, " %s\n", evt->ascii_message);

			/*
			 * Fix for QUIX 1000440284: Display the Physical
			 * Drive Num info only for CISS Controllers
			 */

			if (!(ctlr->bddef->bd_flags & SA_BD_SAS)) {
				driveId = *(uint16_t *)(void *)
				    (&evt->event_specific_data[0]);
				if (*(uint16_t *)(void *)(&evt->
				    event_specific_data[0]) & 0x80) {
					driveId = *(uint16_t *)(void *)
					    (&evt->event_specific_data[0]) -
					    0x80;
					cmn_err(CE_CONT, " Physical Drive Num"
					    " .......SCSI Port %u, Drive Id"
					    " %u\n", (driveId/16) + 1,
					    (driveId%16));
				} else {
					driveId = *(uint16_t *)(void *)
					    (&evt->event_specific_data[0]);
					cmn_err(CE_CONT, " Physical Drive Num"
					    " .......SCSI Port %u,"
					    " Drive Id %u\n", (driveId/16) + 1,
					    (driveId%16));
				}
			}

			cmn_err(CE_CONT, " Configured Drive ? ....... %s\n",
			    evt->event_specific_data[2] ? "YES":"NO");
			if (evt->event_specific_data[3]) {
				cmn_err(CE_CONT, " Spare Drive?"
				    " ............. %s\n",
				    evt->event_specific_data[3] ? "YES":"NO");
			}
		} else if (evt->event_subclass_code == SUB_CLASS_SB_HP_CHANGE) {
			if (evt->event_detail_code == DETAIL_PATH_REMOVED) {
				cmn_err(CE_WARN, " %s", ctlr->hba_name);
				cmn_err(CE_CONT, " Storage Enclosure cable"
				    " or %s\n", evt->ascii_message);
			} else if (evt->event_detail_code ==
			    DETAIL_PATH_REPAIRED) {
				cmn_err(CE_NOTE, " %s", ctlr->hba_name);
				cmn_err(CE_CONT, " Storage Enclosure Cable"
				    " or %s\n", evt->ascii_message);
			} else {
				cmn_err(CE_NOTE, " %s", ctlr->hba_name);
				cmn_err(CE_CONT, " %s\n", evt->ascii_message);
			}
		} else {
			cmn_err(CE_NOTE, " %s", ctlr->hba_name);
			cmn_err(CE_CONT, " %s\n", evt->ascii_message);
		}

		cmn_err(CE_CONT, "\n");
		if (!((evt->event_subclass_code == 0) &&
		    (evt->event_detail_code == 0))) {
			(uint32_t)cpqary3_add2_noeq(ctlr, evt);
		}
		break;

	case CLASS_HARDWARE:
	case CLASS_ENVIRONMENT:
		cmn_err(CE_NOTE, " %s", ctlr->hba_name);
		cmn_err(CE_CONT, " %s\n", evt->ascii_message);
		cmn_err(CE_CONT, "\n");

		/* Add this event to the NOE queue */
		(uint32_t)cpqary3_add2_noeq(ctlr, evt);
		break;

	case CLASS_PHYSICAL_DRIVE:
		cmn_err(CE_WARN, " %s", ctlr->hba_name);
		cmn_err(CE_CONT, " %s\n", evt->ascii_message);

		/*
		 * Fix for QUIX 1000440284: Display the Physical Drive
		 *  Num info only for CISS Controllers
		 */

		if (!(ctlr->bddef->bd_flags & SA_BD_SAS)) {
			driveId = *(uint16_t *)(void *)
			    (&evt->event_specific_data[0]);
			if (*(uint16_t *)(void *)
			    (&evt->event_specific_data[0]) & 0x80) {
				driveId = *(uint16_t *)(void *)
				    (&evt->event_specific_data[0]) - 0x80;
				cmn_err(CE_CONT, " Physical Drive Num ......."
				    " SCSI Port %u, Drive Id %u\n",
				    (driveId/16) + 1, (driveId%16));
			} else {
				driveId = *(uint16_t *)(void *)
				    (&evt->event_specific_data[0]);
				cmn_err(CE_CONT, " Physical Drive Num ......."
				    "SCSI Port %u, Drive Id %u\n",
				    (driveId/16) + 1, (driveId%16));
			}
		}

		if (evt->event_specific_data[2] < MAX_KNOWN_FAILURE_REASON) {
			failure_string = (uint8_t *)
			    cpqary3_table_lookup(ascii_failure_reason,
			    (uint32_t)evt->event_specific_data[2]);
			cmn_err(CE_CONT, " Failure Reason............ %s\n",
			    failure_string);
		} else
			cmn_err(CE_CONT, " Failure Reason............"
			    "UNKNOWN \n");

		cmn_err(CE_CONT, "\n");

		/* Add this event to the NOE queue */
		(uint32_t)cpqary3_add2_noeq(ctlr, evt);
		break;

	case CLASS_LOGICAL_DRIVE:
		cmn_err(CE_NOTE, " %s", ctlr->hba_name);

		/*
		 * Fix for QXCR1000717274 - We are appending the logical
		 * voulme number by one
		 * to be in sync with logical volume details given by HPQacucli
		 */

		if ((evt->event_subclass_code == SUB_CLASS_STATUS) &&
		    (evt->event_detail_code == DETAIL_CHANGE)) {
			cmn_err(CE_CONT, " State change, logical drive %u\n",
			    (*(uint16_t *)(void *)(&evt->
			    event_specific_data[0])+1));
			cmn_err(CE_CONT, " New Logical Drive State... %s\n",
			    log_vol_status[evt->event_specific_data[3]]);

		/*
		 * If the Logical drive has FAILED or it was NOT CONFIGURED,
		 * in the corresponding target structure, set flag as NONE
		 * to suggest that no target exists at this id.
		 */

		if ((evt->event_specific_data[3] == 1) ||
		    (evt->event_specific_data[3] == 2)) {
			drive =	*(uint16_t *)(void *)(&evt->
			    event_specific_data[0]);
			drive = ((drive < CTLR_SCSI_ID) ? drive : drive +
			    CPQARY3_TGT_ALIGNMENT);
			if (ctlr) {
				if (ctlr->cpqary3_tgtp[drive]) {
					ctlr->cpqary3_tgtp[drive]->type =
					    CPQARY3_TARGET_NONE;
				}
			}
		}

		if (evt->event_specific_data[4] & SPARE_REBUILDING) {
			cmn_err(CE_CONT, " Logical Drive %d:"
			    " Data is rebuilding on spare drive\n",
			    (*(uint16_t *)(void *)(&evt->
			    event_specific_data[0]) + 1));
		}

		if (evt->event_specific_data[4] & SPARE_REBUILT) {
			cmn_err(CE_CONT, " Logical Drive %d: Rebuild complete."
			    " Spare is now active\n",
			    (*(uint16_t *)(void *)(&evt->
			    event_specific_data[0])+1));
		}

	}

	else if ((evt->event_subclass_code == SUB_CLASS_STATUS) &&
	    (evt->event_detail_code == MEDIA_EXCHANGE)) {
		cmn_err(CE_CONT, " Media exchange detected, logical drive %u\n",
		    (*(uint16_t *)(void *)(&evt->event_specific_data[0])+1));
	} else {
		cmn_err(CE_CONT, " %s\n", evt->ascii_message);
	}

	cmn_err(CE_CONT, "\n");
	break;

	case CLASS_SERIAL_LOG: {
		if (evt->event_subclass_code == SUB_CLASS_LOG_AVL) {
			/*
			 * Serial output log from firmware available in
			 * path "/var/run/serial_output/slot.x"
			 */

			/* Add this event to the NOE queue */
			(uint32_t)cpqary3_add2_noeq(ctlr, evt);

		} else if (evt->event_subclass_code == SUB_CLASS_LOG_NOTAVL) {
			/*
			 * No more Serial output from firmware
			 */

			cmn_err(CE_CONT, "\n");
		}
		break;
	}
	default:
	cmn_err(CE_NOTE, "%s", ctlr->hba_name);
	cmn_err(CE_CONT, " %s\n", evt->ascii_message);
	cmn_err(CE_CONT, "\n");
	break;

	}	/* End of Switch statement */

	/*
	 * Here, we reuse this command block to resubmit the NOE
	 * command.
	 * Ideally speaking, the resubmit should never fail
	 */
	if (CPQARY3_FAILURE == (cpqary3_send_NOE_command(ctlr, memp,
	    CPQARY3_NOE_RESUBMIT))) {
		cmn_err(CE_WARN, "CPQary3: Failed to ReInitialize"
		    " NOTIFY OF EVENT \n");
		cpqary3_free_phyctgs_mem(MEM2DRVPVT(memp)->phyctgp,
		    CPQARY3_FREE_PHYCTG_MEM);
		cpqary3_cmdlist_release(memp, CPQARY3_NO_MUTEX);
		return;
	}

	DBG("CPQary3 : _NOE_handler : Leaving \n");

	return;

} 	/* End of cpqary3_NOE_handler() */

/*
 *       Function         :    	cpqary3_noe_complete
 *       Description      :    	This routine processes the completed
 *				NOE commands and
 *                             	initiates any callback that is needed.
 *       Called By        :    	cpqary3_send_NOE_command,
 *				cpqary3_disable_NOE_command
 *       Parameters       :    	per-command
 *       Calls            :    	cpqary3_NOE_handler, cpqary3_cmdlist_release
 *       Return Values    :    	None
 */
void
cpqary3_noe_complete(cpqary3_cmdpvt_t *cpqary3_cmdpvtp)
{

	ddi_dma_handle_t	dmahandle;
	ASSERT(cpqary3_cmdpvtp != NULL);

	if (CPQARY3_TIMEOUT == cpqary3_cmdpvtp->cmdpvt_flag) {
		cpqary3_free_phyctgs_mem(cpqary3_cmdpvtp->driverdata->phyctgp,
		    CPQARY3_FREE_PHYCTG_MEM);
		cpqary3_cmdlist_release(cpqary3_cmdpvtp, CPQARY3_NO_MUTEX);
		return;
	}
	if (CPQARY3_EINTR == cpqary3_cmdpvtp->cmdpvt_flag) {
		cpqary3_cmdlist_release(cpqary3_cmdpvtp, CPQARY3_NO_MUTEX);
		return;
	}
	if (cpqary3_cmdpvtp->cmdlist_memaddr->Request.CDB[6] ==
	    BMIC_CANCEL_NOTIFY_ON_EVENT) {
		cpqary3_cmdpvtp->cmdpvt_flag = 0;
		cv_signal(&cpqary3_cmdpvtp->ctlr->cv_noe_wait);
		cpqary3_cmdlist_release(cpqary3_cmdpvtp, CPQARY3_NO_MUTEX);
	} else {
		dmahandle = cpqary3_cmdpvtp->cpqary3_phyctgp->cpqary3_dmahandle;
		(void) ddi_dma_sync(dmahandle, 0, 0, DDI_DMA_SYNC_FORCPU);
		if (cpqary3_cmdpvtp->driverdata != NULL)
			(void) ddi_dma_sync(cpqary3_cmdpvtp->driverdata->
			    phyctgp->cpqary3_dmahandle, 0, 0,
			    DDI_DMA_SYNC_FORCPU);
		cpqary3_NOE_handler(cpqary3_cmdpvtp);
	}
}

/*
 *	Function	:	cpqary3_add2_noeq
 *	Description	:	This routine adds the NOE buffer of the
 *				events generated to the queue. Follows FIFO.
 *	Called By	:	cpqary3_NOE_handler()
 *	Parameters	:	per_controller, NOE buffer
 *	Calls		:	None
 *	Return Values	:	None
 */
int32_t
cpqary3_add2_noeq(cpqary3_t *ctlr, NoeBuffer *evt)
{
	cpqary3_noe_buffer *cpqary3_noe_buf;
	ASSERT(ctlr != NULL);
	ASSERT(evt != NULL);

	if (ctlr->event_logged > 100) {
		return (CPQARY3_FAILURE);
	} else {
		cpqary3_noe_buf = (cpqary3_noe_buffer *)
		    MEM_ZALLOC(sizeof (cpqary3_noe_buffer));

		if (NULL == cpqary3_noe_buf) {
			DBG("cpqary3_NOE_handler: No memory for"
			    " cpqary3_noe_buf\n");
			return (CPQARY3_FAILURE);
		}

		bzero(cpqary3_noe_buf, sizeof (cpqary3_noe_buffer));
		bcopy(evt, &(cpqary3_noe_buf->noebuf), sizeof (NoeBuffer));

		cpqary3_noe_buffer *temp_cpqary3_noe_buf;

		/*
		 * See if the Head is empty.
		 * If empty add noe_buf,
		 * Increment the event logged count.
		 * Return.
		 */

		mutex_enter(&ctlr->noe_mutex);
		if (NULL == ctlr->noe_head) {
			ctlr->noe_head = cpqary3_noe_buf;
			ctlr->noe_tail = cpqary3_noe_buf;
			cpqary3_noe_buf->pNext = NULL;
			ctlr->event_logged++;
			mutex_exit(&ctlr->noe_mutex);
			return (CPQARY3_SUCCESS);
		} else {
			temp_cpqary3_noe_buf = ctlr->noe_tail;
			temp_cpqary3_noe_buf->pNext = cpqary3_noe_buf;
			ctlr->noe_tail = cpqary3_noe_buf;
			ctlr->event_logged++;
			mutex_exit(&ctlr->noe_mutex);
		}
	}
	return (CPQARY3_SUCCESS);
}

/*
 *	Function         :   cpqary3_retrieve_from_noeq
 *	Description      :   This routine removes the NOE buffer from the NOE Q.
 *	Called By        :   cpqary3_noe_pass()
 *	Parameters       :   per_controller, ioctl request,mode
 *	Calls            :   None
 *	Return Values    :   NULL if there are no events
 *                           cpqary3_noe_buf on success.
 */
cpqary3_noe_buffer
*cpqary3_retrieve_from_noeq(cpqary3_t *cpqary3p)
{
	cpqary3_noe_buffer *cpqary3_noe_buf;

	/*
	 * If noe_head is NULL
	 * return NULL, no more events in the queue
	 */

	mutex_enter(&cpqary3p->noe_mutex);
	if (cpqary3p->noe_head == NULL) {
		mutex_exit(&cpqary3p->noe_mutex);
		return (NULL);
	}

	/*
	 * First handle only one element
	 */

	cpqary3_noe_buf = cpqary3p->noe_head;

	if (cpqary3_noe_buf->pNext == NULL) {
		cpqary3p->noe_head = NULL;
		cpqary3p->noe_tail = NULL;
		cpqary3p->event_logged--;
		mutex_exit(&cpqary3p->noe_mutex);
		return (cpqary3_noe_buf);
	} else {
		cpqary3p->noe_head = cpqary3_noe_buf->pNext;
		cpqary3p->event_logged--;
		mutex_exit(&cpqary3p->noe_mutex);
		return (cpqary3_noe_buf);
	}
}

/*
 *      Function         :   cpqary3_table_lookup
 *      Description      :   This routine traverses the cpqary3_val_str_table
 *                                                              and returns the
 *                                                              string
 *                                                              corresponding
 *                                                              to the
 *                                                              value sent.
 *      Called By        :   cpqary3_NOE_handler()
 *      Parameters       :      pointer to cpqary3_val_str_table,
 *                                                              value to be
 *                                                              matched
 *      Calls            :   None
 *      Return Values    :   string matching the value sent by
 *                                                              the calling
 *                                                              functions.
 */
static int8_t
*cpqary3_table_lookup(cpqary3_val_str_table *TabPtr, uint32_t Val)
{
	uint32_t i = 0;
	while (TabPtr[i].val != -1) {
		if (TabPtr[i].val == Val) {
			return (TabPtr[i].str);
		}
		i++;
	}
	return (TabPtr[i].str);
}
