/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2009, 2011, Hewlett-Packard Development Company, L.P.
 */

/*
 * Module Name:
 *   CPQARY3_IOCTL.C
 * Abstract:
 *   This module contains the routines required for IOCTLs.
 * Supported IOCTLs :
 *	CPQARY3_IOCTL_DRIVER_INFO	- to get driver details
 *	CPQARY3_IOCTL_CTLR_INFO		- to get controller details
 *	CPQARY3_IOCTL_BMIC_PASS		- to pass BMIC commands
 *	CPQARY3_IOCTL_SCSI_PASS		- to pass SCSI commands
 *
 */

#include	"cpqary3.h"

/*
 * Local Functions Declaration
 */

static int32_t cpqary3_ioctl_send_bmiccmd(cpqary3_t *,
    cpqary3_bmic_pass_t *, int);
static void cpqary3_ioctl_fil_bmic(CommandList_t *,
    cpqary3_bmic_pass_t *);
static void cpqary3_ioctl_fil_bmic_sas(CommandList_t *,
    cpqary3_bmic_pass_t *);
static int32_t cpqary3_ioctl_send_scsicmd(cpqary3_t *,
    cpqary3_scsi_pass_t *, int);
static void cpqary3_ioctl_fil_scsi(CommandList_t *, cpqary3_scsi_pass_t *);


/*
 * Global Variables Definitions
 */

cpqary3_driver_info_t gdriver_info = {0};

/* Function Definitions */


/*
 * Function        :	cpqary3_ioctl_driver_info
 * Description     :	This routine will get major/ minor versions, Number of
 *			controllers detected & MAX Number of controllers
 *			supported
 * Called By       :	cpqary3_ioctl
 * Parameters      : 	ioctl_reqp	- address of the parameter sent from
 *			the application
 *			cpqary3p	- address of the PerController structure
 *			mode - mode which comes from application
 * Return Values   :	EFAULT on Failure, 0 on SUCCESS
 */
int32_t
cpqary3_ioctl_driver_info(uint64_t ioctl_reqp, int mode)
{
	cpqary3_ioctl_request_t *request;

	request = (cpqary3_ioctl_request_t *)
	    MEM_ZALLOC(sizeof (cpqary3_ioctl_request_t));

	if (NULL == request) {
		DBG("cpqary3_ioctl_driver_info: No memory for request\n");
		return (FAILURE);
	}

/*
 * First let us copyin the ioctl_reqp user  buffer to request(kernel) memory.
 * This is very much recomended before we access any of the fields.
 */

	if (ddi_copyin((void *)(uintptr_t)ioctl_reqp, (void *)request,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_driver_info ddi_copyin failed\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	/*
	 * Fill up the global structure "gdriver_info" memory.
	 * Fill this structure with available info,
	 * which will be copied back later
	 */

	(void) strcpy(gdriver_info.name, "cpqary3");
	gdriver_info.version.minor = CPQARY3_MINOR_REV_NO;
	gdriver_info.version.major = CPQARY3_MAJOR_REV_NO;
	gdriver_info.version.dd    = CPQARY3_REV_MONTH;
	gdriver_info.version.mm	  = CPQARY3_REV_DATE;
	gdriver_info.version.yyyy  = CPQARY3_REV_YEAR;
	gdriver_info.max_num_ctlr  = MAX_CTLRS;

	/*
	 * First Copy out the driver_info structure
	 */

	if (ddi_copyout((void *)&gdriver_info, (void *)(uintptr_t)request->argp,
	    sizeof (cpqary3_driver_info_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_driver_info:"
		    " driver_info copyout failed\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	/*
	 * Copy out the request structure back
	 */

	if (ddi_copyout((void *)request, (void *)(uintptr_t)ioctl_reqp,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_driver_info:"
		    " request copyout failed\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));

	/*
	 * Everything looks fine. So return SUCCESS
	 */

	return (SUCCESS);

}   /* End of cpqary3_ioctl_driver_info() */


/*
 *	Function      :	cpqary3_ioctl_ctlr_info
 *	Description   :	This routine will get the controller related info, like
 *	                board-id, subsystem-id, num of logical drives,
 *			slot number
 *	Called By     :	cpqary3_ioctl
 *	Parameters    :	ioctl_reqp - address of the parameter sent form the
 *			application cpqary3p   -
 *			address of the PerController structure
 *			mode - mode which comes from application
 *	Return Values :	EFAULT on Failure, 0 on SUCCESS
 */
int32_t
cpqary3_ioctl_ctlr_info(uint64_t ioctl_reqp, cpqary3_t *cpqary3p, int mode)
{
	cpqary3_ioctl_request_t *request;
	cpqary3_ctlr_info_t	*ctlr_info;

	request = (cpqary3_ioctl_request_t *)
	    MEM_ZALLOC(sizeof (cpqary3_ioctl_request_t));

	if (NULL == request) {
		DBG("CPQary3: cpqary3_ctlr_info_t:"
		    " No memory for request\n");
		return (FAILURE);
	}

	/*
	 * First let us copyin the buffer to kernel memory. This is very much
	 * recomended before we access any of the fields.
	 */

	if (ddi_copyin((void *)(uintptr_t)ioctl_reqp, (void *)request,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_ctlr_info ddi_copyin failed\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	ctlr_info = (cpqary3_ctlr_info_t *)
	    MEM_ZALLOC(sizeof (cpqary3_ctlr_info_t));

	if (NULL == ctlr_info) {
		DBG("CPQary3: cpqary3_ctlr_info_t: No memory for ctlr_info\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (FAILURE);
	}

	/*
	 * in the driver, board_id is actually subsystem_id
	 */

	ctlr_info->subsystem_id	= cpqary3p->board_id;
	ctlr_info->bus		= cpqary3p->bus;
	ctlr_info->dev		= cpqary3p->dev;
	ctlr_info->fun		= cpqary3p->fun;
	ctlr_info->num_of_tgts	= cpqary3p->num_of_targets;
	ctlr_info->controller_instance 	= cpqary3p->instance;

	/*
	 * TODO: ctlr_info.slot_num has to be implemented
	 *	state & board_id fields are kept for future implementation i
	 *	if required!
	 */

	/*
	 * First Copy out the ctlr_info structure
	 */

	if (ddi_copyout((void *)ctlr_info, (void *)(uintptr_t)request->argp,
	    sizeof (cpqary3_ctlr_info_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_ctlr_info:"
		    " ctlr_info copyout failed\n");
		MEM_SFREE(ctlr_info, sizeof (cpqary3_ctlr_info_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	/*
	 * Copy out the request structure back
	 */

	if (ddi_copyout((void *)request, (void *)(uintptr_t)ioctl_reqp,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_ctlr_info:"
		    " request copyout failed\n");
		MEM_SFREE(ctlr_info, sizeof (cpqary3_ctlr_info_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	MEM_SFREE(ctlr_info, sizeof (cpqary3_ctlr_info_t));
	MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));

	/*
	 * Everything looks fine. So return SUCCESS
	 */

	return (SUCCESS);

}   /* End of cpqary3_ioctl_ctlr_info() */


/*
 *	Function         :	cpqary3_ioctl_bmic_pass
 *	Description      :	This routine will pass the BMIC
 *				commands to controller
 *	Called By        :	cpqary3_ioctl
 *	Parameters       : 	ioctl_reqp - address of the parameter sent
 *				from the application cpqary3p -
 *				address of the PerController structure
 *				mode - mode which comes directly
 *				from application
 *	Return Values    :	EFAULT on Failure, 0 on SUCCESS
 */
int32_t
cpqary3_ioctl_bmic_pass(uint64_t ioctl_reqp, cpqary3_t *cpqary3p, int mode)
{
	cpqary3_ioctl_request_t	*request;
	cpqary3_bmic_pass_t 	*bmic_pass;
	int32_t			retval = SUCCESS;

	request = (cpqary3_ioctl_request_t *)
	    MEM_ZALLOC(sizeof (cpqary3_ioctl_request_t));

	if (NULL == request) {
		DBG("CPQary3: cpqary3_ioctl_bmic_pass:"
		    " No memory for request\n");
		return (FAILURE);
	}

	/*
	 * First let us copyin the ioctl_reqp(user)
	 * buffer to request(kernel) memory. This is very much recommended
	 * before we access any of the fields.
	 */

	if (ddi_copyin((void *)(uintptr_t)ioctl_reqp, (void *)request,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_bmic_pass:"
		    " ioctl_reqp copyin failed\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	bmic_pass = (cpqary3_bmic_pass_t *)
	    MEM_ZALLOC(sizeof (cpqary3_bmic_pass_t));

	if (NULL == bmic_pass) {
		DBG("CPQary3: cpqary3_ioctl_bmic_pass:"
		    " No memory for bmic_pass\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (FAILURE);
	}

	/*
	 * Copy in "cpqary3_bmic_pass_t"
	 * structure from argp member of ioctl_reqp.
	 */

	if (ddi_copyin((void *)(uintptr_t)request->argp, (void *)bmic_pass,
	    sizeof (cpqary3_bmic_pass_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_bmic_pass: argp copyin failed\n");
		MEM_SFREE(bmic_pass, sizeof (cpqary3_bmic_pass_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	/*
	 * Get the free command list, fill in the bmic command
	 * 'and send it to the  controller. This will return 0 on success.
	 */

	retval = cpqary3_ioctl_send_bmiccmd(cpqary3p, bmic_pass, mode);

	/*
	 * Now copy the  bmic_pass (kernel) to the user argp
	 */

	if (ddi_copyout((void *)bmic_pass, (void *)(uintptr_t)request->argp,
	    sizeof (cpqary3_bmic_pass_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_bmic_pass:"
		    " bmic_pass copyout failed\n");
		MEM_SFREE(bmic_pass, sizeof (cpqary3_bmic_pass_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT); /* copyout failed */
	}

	/*
	 * Now copy the  request(kernel) to ioctl_reqp(user)
	 */

	if (ddi_copyout((void *)request, (void *)(uintptr_t)ioctl_reqp,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_bmic_pass:"
		    " request copyout failed\n");
		MEM_SFREE(bmic_pass, sizeof (cpqary3_bmic_pass_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT); /* copyout failed */
	}

	MEM_SFREE(bmic_pass, sizeof (cpqary3_bmic_pass_t));
	MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));

	return (retval);

}   /* End of cpqary3_ioctl_bmic_pass() */


/*
 *	Function         :	cpqary3_ioctl_send_bmiccmd
 *	Description      :	This routine will get the free command,
 *				allocate memory and send it to controller.
 *	Called By        :	cpqary3_ioctl_bmic_pass
 *	Parameters       : 	cpqary3_t - PerController structure
 *				cpqary3_bmic_pass_t - bmic structure
 *				mode - mode value sent from application
 *	Return Values    :	0 on success
 *				FAILURE, EFAULT, ETIMEOUT based on the failure
 */

uint32_t cpqary3_ioctl_wait_ms = 30000;

static int32_t
cpqary3_ioctl_send_bmiccmd(cpqary3_t *cpqary3p,
    cpqary3_bmic_pass_t *bmic_pass, int mode)
{
	cpqary3_cmdpvt_t	*memp    = NULL;
	CommandList_t		*cmdlist = NULL;
	int8_t			*databuf = NULL;
	uint8_t			retval = 0;

	/* allocate a command with a dma buffer */
	memp = cpqary3_synccmd_alloc(cpqary3p, bmic_pass->buf_len);
	if (memp == NULL) {
		DBG("CPQary3: cpqary3_ioctl_send_bmiccmd:"
		    " unable to allocate cmd\n");
		return (FAILURE);
	}

	/* Get the databuf when buf_len is greater than zero */
	if (bmic_pass->buf_len > 0) {
		databuf = memp->driverdata->sg;
	}

	cmdlist	= memp->cmdlist_memaddr;

	/*
	 * If io_direction is CPQARY3_SCSI_OUT, we have to copy user buffer
	 * to databuf
	 */

	if (bmic_pass->io_direction == CPQARY3_SCSI_OUT) {
		/* Do a copyin when buf_len is greater than zero */
		if (bmic_pass->buf_len > 0) {
			if (ddi_copyin((void*)(uintptr_t)(bmic_pass->buf),
			    (void*)databuf, bmic_pass->buf_len, mode)) {
				cpqary3_synccmd_free(cpqary3p, memp);
				DBG("CPQary3: cpqary3_ioctl_send_bmiccmd:"
				    " databuf copyin failed\n");
				return (EFAULT);
			}
		}
	}

	/*
	 * Now fill the command as per the BMIC
	 */
	if (cpqary3p->bddef->bd_flags & SA_BD_SAS) {
		cpqary3_ioctl_fil_bmic_sas(cmdlist, bmic_pass);
	} else {
		cpqary3_ioctl_fil_bmic(cmdlist, bmic_pass);
	}


	/* send command to controller and wait for a reply */
	if (cpqary3_synccmd_send(cpqary3p, memp, cpqary3_ioctl_wait_ms,
	    CPQARY3_SYNCCMD_SEND_WAITSIG) != 0) {
		cpqary3_synccmd_free(cpqary3p, memp);
		DBG("CPQary3: cpqary3_ioctl_send_bmiccmd: command timed out\n");
		return (ETIMEDOUT);
	}

	/*
	 * Now the command is completed and copy the buffers back
	 * First copy the buffer databuf to bmic_pass.buf
	 * which is used as a buffer before passing the command
	 * to the controller.
	 */

	if (bmic_pass->io_direction == CPQARY3_SCSI_IN) {
		/* Do a copyout when buf_len is greater than zero */
		if (bmic_pass->buf_len > 0) {
			if (ddi_copyout((void *)databuf,
			    (void *)(uintptr_t)bmic_pass->buf,
			    bmic_pass->buf_len, mode)) {
				DBG("CPQary3: cpqary3_ioctl_send_bmiccmd:"
				    " databuf copyout failed\n");
				retval = EFAULT;
			}
		}
	}

	/*
	 * This is case where the command completes with error,
	 * Then tag would have set its 1st(10) bit.
	 */

	if (cmdlist->Header.Tag.drvinfo_n_err == CPQARY3_SYNCCMD_FAILURE) {
		bmic_pass->err_status = 1;
		bcopy((caddr_t)memp->errorinfop, &bmic_pass->err_info,
		    sizeof (ErrorInfo_t));
		switch (memp->errorinfop->CommandStatus) {
			case CISS_CMD_DATA_OVERRUN :
			case CISS_CMD_DATA_UNDERRUN :
			case CISS_CMD_SUCCESS :
			case CISS_CMD_TARGET_STATUS :
				retval = SUCCESS;
				break;
			default :
				DBG("CPQary3: cpqary3_ioctl_send_bmiccmd:"
				    " cmd completed with error\n");
				retval = EIO;
				break;
		}
	}

	cpqary3_synccmd_free(cpqary3p, memp);

	return (retval);

}   /* End of cpqary3_ioctl_send_bmiccmd() */


/*
 *	Function         :	cpqary3_ioctl_fil_bmic
 *	Description      :	This routine will fill the cmdlist
 *				with BMIC details
 *	Called By        :	cpqary3_ioctl_send_bmiccmd
 *	Parameters       : 	cmdlist - command packet
 *				bmic_pass - bmic structure
 *				phys_addr - physical address of DMAable memory
 *	Return Values    :	void
 */
static void
cpqary3_ioctl_fil_bmic(CommandList_t *cmdlist,
    cpqary3_bmic_pass_t *bmic_pass)
{
	cmdlist->Header.SGTotal		= 1;
	cmdlist->Header.SGList 		= 1;
	cmdlist->Request.CDBLen		= bmic_pass->cmd_len;
	cmdlist->Request.Timeout	= bmic_pass->timeout;
	cmdlist->Request.Type.Type	= CISS_TYPE_CMD;
	cmdlist->Request.Type.Attribute	= CISS_ATTR_HEADOFQUEUE;

	switch (bmic_pass->io_direction) {
		case CPQARY3_SCSI_OUT:
			cmdlist->Request.Type.Direction = CISS_XFER_WRITE;
			break;
		case CPQARY3_SCSI_IN:
			cmdlist->Request.Type.Direction = CISS_XFER_READ;
			break;
		case CPQARY3_NODATA_XFER:
			cmdlist->Request.Type.Direction = CISS_XFER_NONE;
			break;
		default:
			cmdlist->Request.Type.Direction = CISS_XFER_RSVD;
			break;
	}

	cmdlist ->Request.CDB[0] =
	    (bmic_pass->io_direction == CPQARY3_SCSI_IN) ? 0x26: 0x27;
	cmdlist ->Request.CDB[1] = bmic_pass->unit_number; /* Unit Number */

	/*
	 * BMIC Detail - bytes 2[MSB] to 5[LSB]
	 */

	cmdlist ->Request.CDB[2] = (bmic_pass->blk_number >> 24) & 0xff;
	cmdlist ->Request.CDB[3] = (bmic_pass->blk_number >> 16) & 0xff;
	cmdlist ->Request.CDB[4] = (bmic_pass->blk_number >> 8) & 0xff;
	cmdlist ->Request.CDB[5] =  bmic_pass->blk_number;

	cmdlist ->Request.CDB[6] = bmic_pass->cmd; /* BMIC Command */

	/* Transfer Length - bytes 7[MSB] to 8[LSB] */

	cmdlist ->Request.CDB[7] = (bmic_pass->buf_len >> 8) & 0xff;
	cmdlist ->Request.CDB[8] = bmic_pass->buf_len & 0xff;
	cmdlist ->Request.CDB[9] = 0x00; /* Reserved */

	/*
	 * Copy the Lun address from the request
	 */

	bcopy(&bmic_pass->lun_addr[0], &(cmdlist->Header.LUN),
	    sizeof (LUNAddr_t));
	cmdlist->SG[0].Len = bmic_pass->buf_len;

	return;

}   /* End of cpqary3_ioctl_fil_bmic() */


/*
 *	Function        : cpqary3_ioctl_scsi_pass
 *	Description     : This routine will pass the SCSI commands to controller
 *	Called By       : cpqary3_ioctl
 *	Parameters      : ioctl_reqp - address of the parameter sent
 *			  from the application
 *			  cpqary3p  - Addess of the percontroller stucture
 *			  mode      - mode which comes directly from application
 *	Return Values   : EFAULT on Failure, 0 on SUCCESS
 */
int32_t
cpqary3_ioctl_scsi_pass(uint64_t ioctl_reqp, cpqary3_t *cpqary3p, int mode)
{
	cpqary3_ioctl_request_t *request;
	cpqary3_scsi_pass_t 	*scsi_pass;
	int32_t			retval = SUCCESS;

	request = (cpqary3_ioctl_request_t *)
	    MEM_ZALLOC(sizeof (cpqary3_ioctl_request_t));

	if (NULL == request) {
		DBG("CPQary3: cpqary3_ioctl_scsi_pass:"
		    " No memory for request\n");
		return (FAILURE);
	}

	/*
	 * First let us copyin the ioctl_reqp(user)
	 * buffer to request(kernel) memory. This is very much
	 * recommended before we access any of the fields.
	 */

	if (ddi_copyin((void *)(uintptr_t)ioctl_reqp, (void *)request,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_scsi_pass:"
		    " ioctl_reqp copyin failed\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	scsi_pass = (cpqary3_scsi_pass_t *)
	    MEM_ZALLOC(sizeof (cpqary3_scsi_pass_t));

	if (NULL == scsi_pass) {
		DBG("CPQary3: cpqary3_ioctl_scsi_pass:"
		    " No memory for scsi_pass\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (FAILURE);
	}

	/*
	 * Copy in "cpqary3_scsi_pass_t"
	 * structure from argp member of ioctl_reqp.
	 */

	if (ddi_copyin((void *)(uintptr_t)request->argp, (void *)scsi_pass,
	    sizeof (cpqary3_scsi_pass_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_scsi_pass: argp copyin failed\n");
		MEM_SFREE(scsi_pass, sizeof (cpqary3_scsi_pass_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	/*
	 * Get the free command list, fill in the scsi
	 * command and send it to the  controller. This
	 * will return 0 on success.
	 */

	retval = cpqary3_ioctl_send_scsicmd(cpqary3p, scsi_pass, mode);

	/*
	 * Now copy the  scsi_pass (kernel) to the user argp
	 */

	if (ddi_copyout((void *)scsi_pass, (void *)(uintptr_t)request->argp,
	    sizeof (cpqary3_scsi_pass_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_scsi_pass:"
		    " scsi_pass copyout failed\n");
		MEM_SFREE(scsi_pass, sizeof (cpqary3_scsi_pass_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT); /* copyout failed */
	}

	/*
	 * Now copy the  request(kernel) to ioctl_reqp(user)
	 */

	if (ddi_copyout((void *)request, (void *)(uintptr_t)ioctl_reqp,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		DBG("CPQary3: cpqary3_ioctl_scsi_pass:"
		    " request copyout failed\n");
		MEM_SFREE(scsi_pass, sizeof (cpqary3_scsi_pass_t));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT); /* copyout failed */
	}

	MEM_SFREE(scsi_pass, sizeof (cpqary3_scsi_pass_t));
	MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));

	return (retval);

}   /* End of cpqary3_ioctl_scsi_pass() */


/*
 *	Function        : cpqary3_ioctl_send_scsiccmd
 *	Description     : This routine will pass the SCSI commands to controller
 *	Called By       : cpqary3_ioctl_scsi_pass
 *	Parameters      : cpqary3_t - PerController structure,
 *			  cpqary3_scsi_pass_t - scsi parameter
 *			  mode - sent from the application
 *	Return Values   : 0 on success
 *			  FAILURE, EFAULT, ETIMEOUT based on the failure
 */
static int32_t
cpqary3_ioctl_send_scsicmd(cpqary3_t *cpqary3p,
    cpqary3_scsi_pass_t *scsi_pass, int mode)
{
	cpqary3_cmdpvt_t 	*memp    = NULL;
	CommandList_t		*cmdlist = NULL;
	int8_t			*databuf = NULL;
	uint8_t			retval  = 0;

	/* allocate a command with a dma buffer */
	memp = cpqary3_synccmd_alloc(cpqary3p, scsi_pass->buf_len);
	if (memp == NULL) {
		DBG("CPQary3: cpqary3_ioctl_send_bmiccmd:"
		    " unable to allocate cmd\n");
		return (FAILURE);
	}

	/* Get the databuf when buf_len is greater than zero */
	if (scsi_pass->buf_len > 0) {
		databuf = memp->driverdata->sg;
	}

	cmdlist	= memp->cmdlist_memaddr;

	if (scsi_pass->io_direction == CPQARY3_SCSI_OUT) {
		/* Do a copyin when buf_len is greater than zero */
		if (scsi_pass->buf_len > 0) {
			if (ddi_copyin((void*)(uintptr_t)(scsi_pass->buf),
			    (void *)databuf, scsi_pass->buf_len, mode)) {
				cpqary3_synccmd_free(cpqary3p, memp);
				DBG("CPQary3: cpqary3_ioctl_send_bmiccmd:"
				    " databuf copyin failed\n");
				return (EFAULT);
			}
		}
	}

	/*
	 * Fill the scsi command
	 */
	cpqary3_ioctl_fil_scsi(cmdlist, scsi_pass);

	/* send command to controller and wait for a reply */
	if (cpqary3_synccmd_send(cpqary3p, memp, cpqary3_ioctl_wait_ms,
	    CPQARY3_SYNCCMD_SEND_WAITSIG) != 0) {
		cpqary3_synccmd_free(cpqary3p, memp);
		DBG("CPQary3: cpqary3_ioctl_send_scsicmd: command timed out\n");
		return (ETIMEDOUT);
	}

	/*
	 * Now the command is completed and copy the buffers back
	 * First copy the buffer databuf to scsi_pass->buf
	 * which is used as a buffer before passing the command
	 * to the controller.
	 */

	if (scsi_pass->io_direction == CPQARY3_SCSI_IN) {
		if (scsi_pass->buf_len > 0) {
			if (ddi_copyout((void *)databuf,
			    (void *)(uintptr_t)scsi_pass->buf,
			    scsi_pass->buf_len, mode)) {
				DBG("CPQary3: cpqary3_ioctl_send_scsicmd:"
				    " copyout of databuf failed\n");
				retval = EFAULT;
			}
		}
	}

	/*
	 * This is case where the command completes with error,
	 * Then tag would have set its 1st(10) bit.
	 */

	if (cmdlist->Header.Tag.drvinfo_n_err == CPQARY3_SYNCCMD_FAILURE) {
		scsi_pass->err_status = 1;
		bcopy((caddr_t)memp->errorinfop, &scsi_pass->err_info,
		    sizeof (ErrorInfo_t));
		switch (memp->errorinfop->CommandStatus) {
		case CISS_CMD_DATA_OVERRUN:
		case CISS_CMD_DATA_UNDERRUN:
		case CISS_CMD_SUCCESS :
		case CISS_CMD_TARGET_STATUS :
			retval = SUCCESS;
		break;
		default :
		DBG("CPQary3: cpqary3_ioctl_send_scsicmd: command"
		    " resulted in error\n");
		retval = EIO;
		break;
		}
	}

	cpqary3_synccmd_free(cpqary3p, memp);

	return (retval);

} /* End of cpqary3_ioctl_send_scsicmd() */


/*
 *	Function         :	cpqary3_ioctl_fil_scsi_
 *	Description      :	This routine will fill the cmdlist with SCSI CDB
 *	Called By        :	cpqary3_ioctl_send_scsicmd
 *	Parameters       : 	cmdlist	- command packet
 *				cpqary3_scsi_pass_t - scsi parameter
 *				phys_addr - physical address of DMAable memory
 *	Return Values    :	void
 */
static void
cpqary3_ioctl_fil_scsi(CommandList_t *cmdlist,
    cpqary3_scsi_pass_t *scsi_pass)
{
	cmdlist->Header.SGTotal 		= 1;
	cmdlist->Header.SGList  		= 1;
	cmdlist->Request.CDBLen			= scsi_pass->cdb_len;
	cmdlist->Request.Timeout		= scsi_pass->timeout;
	cmdlist->Request.Type.Type		= CISS_TYPE_CMD;
	cmdlist->Request.Type.Attribute 	= CISS_ATTR_HEADOFQUEUE;

	switch (scsi_pass->io_direction) {
		case CPQARY3_SCSI_OUT:
			cmdlist -> Request.Type.Direction = CISS_XFER_WRITE;
			break;
		case CPQARY3_SCSI_IN:
			cmdlist->Request.Type.Direction = CISS_XFER_READ;
			break;
		case CPQARY3_NODATA_XFER:
			cmdlist->Request.Type.Direction = CISS_XFER_NONE;
			break;
		default:
			cmdlist->Request.Type.Direction = CISS_XFER_RSVD;
			break;
	}

	/*
	 * Copy the SCSI CDB as is
	 */

	bcopy(&scsi_pass->cdb[0], &cmdlist ->Request.CDB[0],
	    scsi_pass->cdb_len);

	/*
	 * Copy the Lun address from the request
	 */

	bcopy(&scsi_pass->lun_addr[0], &(cmdlist->Header.LUN),
	    sizeof (LUNAddr_t));

	cmdlist->SG[0].Len 	= scsi_pass->buf_len;

	return;

}   /* End of cpqary3_ioctl_fil_scsi() */

/*
 *	Function         : cpqary3_ioctl_fil_bmic_sas
 *	Description      : This routine will fill the cmdlist with BMIC details
 *	Called By        : cpqary3_ioctl_send_bmiccmd
 *	Parameters       : cmdlist 	- command packet
 *			   bmic_pass 	- bmic structure
 *			   phys_addr 	- physical address of DMAable memory
 *	Return Values    : void
 */
static void
cpqary3_ioctl_fil_bmic_sas(CommandList_t *cmdlist,
    cpqary3_bmic_pass_t *bmic_pass)
{
	cmdlist->Header.SGTotal			= 1;
	cmdlist->Header.SGList 			= 1;
	cmdlist->Request.CDBLen			= bmic_pass->cmd_len;
	cmdlist->Request.Timeout		= bmic_pass->timeout;
	cmdlist->Request.Type.Type		= CISS_TYPE_CMD;
	cmdlist->Request.Type.Attribute 	= CISS_ATTR_HEADOFQUEUE;

	switch (bmic_pass->io_direction) {
		case CPQARY3_SCSI_OUT:
			cmdlist->Request.Type.Direction = CISS_XFER_WRITE;
			break;
		case CPQARY3_SCSI_IN:
			cmdlist->Request.Type.Direction = CISS_XFER_READ;
			break;
		case CPQARY3_NODATA_XFER:
			cmdlist->Request.Type.Direction = CISS_XFER_NONE;
			break;
		default:
			cmdlist->Request.Type.Direction = CISS_XFER_RSVD;
			break;
	}

	cmdlist->Request.CDB[0] =
	    (bmic_pass->io_direction == CPQARY3_SCSI_IN) ? 0x26: 0x27;
	cmdlist->Request.CDB[1] = bmic_pass->unit_number; /* Unit Number */

	/*
	 * BMIC Detail - bytes 2[MSB] to 5[LSB]
	 */

	cmdlist->Request.CDB[2] = (bmic_pass->blk_number >> 24) & 0xff;
	cmdlist->Request.CDB[3] = (bmic_pass->blk_number >> 16) & 0xff;
	cmdlist->Request.CDB[4] = (bmic_pass->blk_number >> 8) & 0xff;
	cmdlist->Request.CDB[5] =  bmic_pass->blk_number;

	cmdlist->Request.CDB[6] = bmic_pass->cmd; /* BMIC Command */

	/* Transfer Length - bytes 7[MSB] to 8[LSB] */

	cmdlist->Request.CDB[7] = (bmic_pass->buf_len >> 8) & 0xff;
	cmdlist->Request.CDB[8] = bmic_pass->buf_len & 0xff;
	cmdlist->Request.CDB[9] = 0x00; /* Reserved */

	/* Update CDB[2] = LSB bmix_index and CDB[9] = MSB bmic_index */
	switch (bmic_pass->cmd) {
		case HPSAS_ID_PHYSICAL_DRIVE:
		case HPSAS_TAPE_INQUIRY:
		case HPSAS_SENSE_MP_STAT:
		case HPSAS_SET_MP_THRESHOLD:
		case HPSAS_MP_PARAM_CONTROL:
		case HPSAS_SENSE_DRV_ERR_LOG:
		case HPSAS_SET_MP_VALUE:
		cmdlist -> Request.CDB[2] = bmic_pass->bmic_index & 0xff;
		cmdlist -> Request.CDB[9] = (bmic_pass->bmic_index >>8) & 0xff;
		break;

		case HPSAS_ID_LOG_DRIVE:
		case HPSAS_SENSE_LOG_DRIVE:
		case HPSAS_READ:
		case HPSAS_WRITE:
		case HPSAS_WRITE_THROUGH:
		case HPSAS_SENSE_CONFIG:
		case HPSAS_SET_CONFIG:
		case HPSAS_BYPASS_VOL_STATE:
		case HPSAS_CHANGE_CONFIG:
		case HPSAS_SENSE_ORIG_CONFIG:
		case HPSAS_LABEL_LOG_DRIVE:
		cmdlist -> Request.CDB[9] =
		    (bmic_pass->unit_number >> 8) & 0xff;  /* Unit Number MSB */
		break;
		default:
		break;
	}


	/*
	 * Copy the Lun address from the request
	 */

	bcopy(&bmic_pass->lun_addr[0], &(cmdlist->Header.LUN),
	    sizeof (LUNAddr_t));

	cmdlist->SG[0].Len = bmic_pass->buf_len;

	return;

}   /* End of cpqary3_ioctl_fil_bmic_sas() */


/*
 *	Function	:	cpqary3_ioctl_noe_pass
 *	Description	:	This routine will be called by hmaeventd
 *				to get the NOE buffer.
 *	Called By	:	cpqary3_ioctl
 *	Parameters	:	ioctl_reqp - address of the parameter sent
 *                             from the application
 *                             cpqary3p - Addess of the percontroller stucture
 *                             mode - mode which comes directly from application
 *        Return Values	:      EFAULT on Failure, ENXIO when there are no
 *                             more events in the NOE queue, 0 on SUCCESS
 */
int32_t
cpqary3_ioctl_noe_pass(uint64_t ioctl_reqp, cpqary3_t *cpqary3p, int mode)
{
	cpqary3_ioctl_request_t *request;
	cpqary3_noe_buffer *cpqary3_noe_buf = NULL;

	request = (cpqary3_ioctl_request_t *)
	    MEM_ZALLOC(sizeof (cpqary3_ioctl_request_t));

	if (NULL == request) {
		cmn_err(CE_WARN, "CPQary3: cpqary3_ioctl_noe_pass:"
		    " No memory for request\n");
		return (FAILURE);
	}

	/*
	 * First let us copyin the ioctl_reqp(user) buffer to
	 * request(kernel) memory. This is very much recommended
	 * before we access any of the fields.
	 */

	if (ddi_copyin((void *)(uintptr_t)ioctl_reqp, (void *)request,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		cmn_err(CE_WARN, "CPQary3: cpqary3_ioctl_noe_pass:"
		    " ioctl_reqp copyin failed\n");
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	/* Call retrieve, and get cpqary3_noe_buf */
	cpqary3_noe_buf = cpqary3_retrieve_from_noeq(cpqary3p);
	if (cpqary3_noe_buf == NULL) {
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (ENXIO);
	}

	/*
	 * First Copy out the noebuf structure
	 */

	if (ddi_copyout((void *)&cpqary3_noe_buf->noebuf,
	    (void *)(uintptr_t)request->argp,
	    sizeof (NoeBuffer), mode)) {
		DBG("CPQary3: cpqary3_ioctl_driver_info:"
		    " driver_info copyout failed\n");
		MEM_SFREE(cpqary3_noe_buf, sizeof (cpqary3_noe_buffer));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		return (EFAULT);
	}

	/*
	 * Copy out the request structure back
	 */

	if (ddi_copyout((void *)request, (void *)(uintptr_t)ioctl_reqp,
	    sizeof (cpqary3_ioctl_request_t), mode)) {
		MEM_SFREE(cpqary3_noe_buf, sizeof (cpqary3_noe_buffer));
		MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));
		cmn_err(CE_WARN, "CPQary3: cpqary3_ioctl_noe_pass: request"
		    " copyout failed\n");
		return (EFAULT);
	}

	MEM_SFREE(cpqary3_noe_buf, sizeof (cpqary3_noe_buffer));
	MEM_SFREE(request, sizeof (cpqary3_ioctl_request_t));

	return (SUCCESS);

}   /* End of cpqary3_ioctl_noe_pass() */
