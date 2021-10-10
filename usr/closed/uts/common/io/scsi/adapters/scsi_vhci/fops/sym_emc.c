/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * This file contains confidential information of other companies
 * and should not be distributed in source form without approval
 * from Sun Legal.
 */

/*
 * Implementation of "scsi_vhci_f_sym_emc" symmetric failover_ops.
 *
 * This file imports the standard "scsi_vhci_f_sym", but with EMC specific
 * knowledge related to 'gatekeeper' luns in the device_probe implementation.
 */

#include <sys/conf.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/scsi_vhci.h>

/* Supported device table entries.  */
char *emc_symmetric_dev_table[] = {
/*	"                  111111" */
/*	"012345670123456789012345" */
/*	"|-VID--||-----PID------|" */

	"EMC     SYMMETRIX",
	NULL
};

static int	emc_symmetric_device_probe(struct scsi_device *,
			struct scsi_inquiry *, void **);
static void	emc_symmetric_init();

#ifdef	lint
#define	scsi_vhci_failover_ops	scsi_vhci_failover_ops_f_sym_emc
#endif	/* lint */
struct scsi_failover_ops scsi_vhci_failover_ops = {
	SFO_REV,
	SFO_NAME_SYM "_emc",
	emc_symmetric_dev_table,
	emc_symmetric_init,
	emc_symmetric_device_probe,
	/* The rest of the implementation comes from SFO_NAME_SYM import  */
};

static struct modlmisc modlmisc = {
	&mod_miscops, "f_sym_emc"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

/*
 * Be kind to EMC: if powerpath drivers are installed, they take precedence.
 * For mpxio to control SYMMETRIX device, even with powerpath installed, you
 * should add the following line to /etc/system and reboot:
 *	set scsi_vhci_f_sym_emc:emc_powerpath_driver = ""
 */
static char	*emc_powerpath_driver = "emcp";

extern struct scsi_failover_ops	*vhci_failover_ops_by_name(char *);

int
_init()
{
	/*
	 * Be kind to EMC: if powerpath driver is installed then fail
	 * _init with EEXIST.
	 */
	if (ddi_name_to_major(emc_powerpath_driver) != (major_t)-1)
		return (EEXIST);

	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static void
emc_symmetric_init()
{
	struct scsi_failover_ops	*sfo, *ssfo, clone;

	/* clone SFO_NAME_SYM implementation for most things */
	ssfo = vhci_failover_ops_by_name(SFO_NAME_SYM);
	if (ssfo == NULL) {
		VHCI_DEBUG(4, (CE_NOTE, NULL, "!emc_symmetric_init: "
		    "can't import " SFO_NAME_SYM "\n"));
		return;
	}
	sfo			= &scsi_vhci_failover_ops;
	clone			= *ssfo;
	clone.sfo_rev		= sfo->sfo_rev;
	clone.sfo_name		= sfo->sfo_name;
	clone.sfo_devices	= sfo->sfo_devices;
	clone.sfo_init		= sfo->sfo_init;
	clone.sfo_device_probe	= sfo->sfo_device_probe;
	*sfo			= clone;
}

/* ARGSUSED */
static int
emc_symmetric_device_probe(struct scsi_device *sd, struct scsi_inquiry *stdinq,
void **ctpriv)
{
	char	**dt;
	caddr_t	inq_data = (caddr_t)stdinq;

	VHCI_DEBUG(6, (CE_NOTE, NULL, "emc_symmetric_device_probe: vidpid %s\n",
	    stdinq->inq_vid));
	for (dt = emc_symmetric_dev_table; *dt; dt++) {
		if (strncmp(stdinq->inq_vid, *dt, strlen(*dt)))
			continue;

		/*
		 * Found a match in the table, now check if this is the
		 * gatekeeper lun of the EMC array: lsb 3 (equivalent to 0x04)
		 * will be set for inquiry data byte 0x6D for this lun on EMC
		 * SYMMETRIX array.  We don't want to enumerate gatekeeper
		 * luns under scsi_vhci, because it breaks EMC's SymmAPI.
		 */
		if ((inq_data[0x6D] & 0x04) == 0x04) {
			VHCI_DEBUG(4, (CE_NOTE, NULL,
			    "!emc_symmetric_device_probe: "
			    "Detected & Disabling MPxIO on gatekeeper lun "
			    "for EMC SYMMETRIX array.\n"));
			return (SFO_DEVICE_PROBE_PHCI);
		}
		return (SFO_DEVICE_PROBE_VHCI);
	}

	return (SFO_DEVICE_PROBE_PHCI);
}
