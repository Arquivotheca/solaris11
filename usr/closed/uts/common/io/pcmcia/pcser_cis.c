/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if defined(DEBUG)
#define	PCSER_DEBUG
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/kmem.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/termiox.h>
#include <sys/stermio.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/tty.h>
#include <sys/ptyvar.h>
#include <sys/cred.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/ioccom.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/mkdev.h>
#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/strtty.h>
/* #include <sys/suntty.h> */
#include <sys/ksynch.h>
#include <sys/cpu.h>
#include <sys/consdev.h>
#include <sys/conf.h>

/*
 * PCMCIA and DDI related header files
 */
#include <sys/pccard.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * pcser-related header files
 */
#include <sys/pcmcia/pcser_io.h>
#include <sys/pcmcia/pcser_var.h>
#include <sys/pcmcia/pcser_reg.h>
#include <sys/pcmcia/pcser_manuspec.h>

#undef	isalpha
#undef	isxdigit
#undef	ixdigit
#undef	toupper

#define	isalpha(ch)	(((ch) >= 'a' && (ch) <= 'z') || \
			((ch) >= 'A' && (ch) <= 'Z'))
#define	isxdigit(ch)	(isdigit(ch) || ((ch) >= 'a' && (ch) <= 'f') || \
			((ch) >= 'A' && (ch) <= 'F'))
#define	isdigit(ch)	((ch) >= '0' && (ch) <= '9')
#define	toupper(C)	(((C) >= 'a' && (C) <= 'z')? (C) - 'a' + 'A': (C))

int pcser_pcspp_flags = 0;

/*
 * Card Services related functions and variables
 * This gets set in pcser.c
 */
extern csfunction_t *cardservices;

/*
 * Local prototypes and global variables
 */
int pcser_parse_cis(pcser_unit_t *, pcser_cftable_t **);
static int pcser_modify_manspec_params(pcser_unit_t *, pcser_cftable_t *);
void pcser_destroy_cftable_list(pcser_cftable_t **);
static void pcser_set_cftable_desireability(pcser_cftable_t *);
static void pcser_sort_cftable_list(pcser_cftable_t **);
static void pcser_swap_cft(pcser_cftable_t **, pcser_cftable_t *);
static void pcser_parse_manuspec_prop(unsigned, char *,
						pcser_manuspec_t *, int);
static uchar_t *find_token(uchar_t **, int *);
static int parse_token(uchar_t *);
static int token_to_hex(uchar_t *, unsigned *);
static int token_to_dec(uchar_t *, unsigned *);
static int pcser_ms_parse_line(unsigned, uchar_t *, pcser_manuspec_t *);
static int pcser_set_manuspec_params(unsigned, uchar_t *,
					uchar_t *, pcser_manuspec_t *);
static int pcser_match_manuspec_params(pcser_unit_t *,
				pcser_manuspec_t *, int, pcser_cftable_t *);

#ifdef	PCSER_DEBUG
extern int pcser_debug;
void pcser_display_cftable_list(pcser_cftable_t *, int);
#endif

/*
 * pcser_cis_addr_tran_t - This structure is used to provide a mapping
 *	between a base address and a desierability factor for the
 *	CISTPL_CFTABLE_ENTRY tuple processing.
 */
typedef struct pcser_cis_addr_tran_t {
	uint32_t	desireability;	/* how desireable this entry is */
	uint32_t	modem_base;	/* base offset of UART registers */
} pcser_cis_addr_tran_t;

/*
 * We generally don't want to use the standard MS-DOS/x86 COMx:
 *	addresses if we don't have to - this hopefully prevents
 *	conflicts between PCMCIA serial devices and built-in
 *	serial devices.
 */
pcser_cis_addr_tran_t pcser_cis_addr_tran[] = {
	/*   desireability    modem_base */
	{	0x0fffc,	0x2e8	},	/* COM4: */
	{	0x0fffd,	0x3e8	},	/* COM3: */
	{	0x0fffe,	0x2f8	},	/* COM2: */
	{	0x0ffff,	0x3f8	},	/* COM1: */
};

/*
 * pcser_parse_cis - gets CIS information to configure the card.
 *
 *	calling: pcser - pointer to the pcser_unit_t struct for this card
 *
 *	returns: CS_SUCCESS - if CIS information retreived correctly
 *		 {various CS return codes} - if problem getting CIS
 *			inforamtion
 */
int
pcser_parse_cis(pcser_unit_t *pcser, pcser_cftable_t **cftable)
{
	pcser_line_t *line = &pcser->line;
	pcser_cis_vars_t *cis_vars = &line->cis_vars;
	cistpl_config_t cistpl_config;
	cistpl_cftable_entry_t cistpl_cftable_entry;
	struct cistpl_cftable_entry_io_t *io = &cistpl_cftable_entry.io;
	cistpl_vers_1_t cistpl_vers_1;
	pcser_cftable_t *cft, *ocft, *dcft, default_cftable;
	cistpl_manfid_t cistpl_manfid;
	tuple_t tuple;
	int ret, last_config_index;

	dcft = &default_cftable;

	/*
	 * Clear the PCSER_IGNORE_CD_ON_OPEN and PCSER_VALID_IO_INFO flags
	 *	here. These will be set if necessary as we parse the CIS and
	 *	check the manufacturer specific overrides later on.
	 */
	line->flags &= ~(PCSER_IGNORE_CD_ON_OPEN | PCSER_VALID_IO_INFO);

	/*
	 * Clear the CIS information structure.
	 */
	bzero((caddr_t)cis_vars, sizeof (pcser_cis_vars_t));

	/*
	 * CISTPL_CONFIG processing. Search for the first config tuple
	 *	so that we can get a pointer to the card's configuration
	 *	registers. If this tuple is not found, there's no point
	 *	in searching for anything else.
	 */
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_CONFIG;
	if ((ret = csx_GetFirstTuple(pcser->client_handle,
					&tuple)) != CS_SUCCESS) {
	    cmn_err(CE_CONT, "pcser_parse_cis: socket %d CISTPL_CONFIG "
					"tuple not found\n", (int)pcser->sn);
	    return (ret);
	} /* GetFirstTuple */

	/*
	 * We shouldn't ever fail parsing this tuple.  If we do,
	 * there's probably an internal error in the CIS parser.
	 */
	if ((ret = csx_Parse_CISTPL_CONFIG(pcser->client_handle, &tuple,
					&cistpl_config)) != CS_SUCCESS) {
	    return (ret);
	} else {
		/*
		 * This is the last CISTPL_CFTABLE_ENTRY
		 *	tuple index that we need to look at.
		 */
	    last_config_index = cistpl_config.last;

	    if (cistpl_config.nr) {
		cis_vars->config_base = cistpl_config.base;
		cis_vars->present = cistpl_config.present;
	    } else {
		cmn_err(CE_CONT, "pcser_parse_cis: socket %d CISTPL_CONFIG "
				"no configuration registers found\n",
							(int)pcser->sn);
		return (CS_BAD_CIS);
	    } /* if (cistpl_config.nr) */
	} /* Parse_CISTPL_CONFIG */

	/*
	 * CISTPL_VERS_1 processing. The information from this tuple is
	 *	mainly used for display purposes.
	 */
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_VERS_1;
	if ((ret = csx_GetFirstTuple(pcser->client_handle,
					&tuple)) != CS_SUCCESS) {
		/*
		 * It's OK not to find the tuple if it's not in the CIS, but
		 *	this test will catch other errors.
		 */
	    if (ret != CS_NO_MORE_ITEMS)
		return (ret);
	} else {
		/*
		 * We shouldn't ever fail parsing this tuple.  If we do,
		 * there's probably an internal error in the CIS parser.
		 */
	    if ((ret = csx_Parse_CISTPL_VERS_1(pcser->client_handle, &tuple,
					&cistpl_vers_1)) != CS_SUCCESS) {
		return (ret);
	    } else {
		int i;

		cis_vars->major_revision = cistpl_vers_1.major;
		cis_vars->minor_revision = cistpl_vers_1.minor;

		/*
		 * The first byte of the unused prod_strings will be NULL
		 *	since we did a bzero(cis_vars) above.
		 */
		for (i = 0; i < cistpl_vers_1.ns; i++)
		    (void) strcpy(cis_vars->prod_strings[i],
				cistpl_vers_1.pi[i]);

	    } /* csx_Parse_CISTPL_VERS_1 */
	} /* GetFirstTuple */

	/*
	 * CISTPL_CFTABLE_ENTRY processing. Search for the first config tuple
	 *	so that we can get a card configuration. If this tuple is not
	 *	found, there's no point in searching for anything else.
	 */
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	if ((ret = csx_GetFirstTuple(pcser->client_handle,
						&tuple)) != CS_SUCCESS) {
	    cmn_err(CE_CONT, "pcser_parse_cis: socket %d CISTPL_CFTABLE_ENTRY "
					"tuple not found\n", (int)pcser->sn);
	    return (ret);
	} /* GetFirstTuple */

	/*
	 * Clear the default values.
	 */
	bzero((caddr_t)dcft, sizeof (pcser_cftable_t));

	/*
	 * Some cards don't provide enough information in their CIS to
	 *	allow us to configure them using CIS information alone,
	 *	so we have to set some default values here.
	 */
	dcft->p.modem_vcc = 50;

	/*
	 * Look through the CIS for all CISTPL_CFTABLE_ENTRY tuples and
	 *	store them in a list. Stop searching if we find a tuple
	 *	with a config index equal to the "last_config_index"
	 *	value that we got from the CISTPL_CONFIG tuple.
	 */
	do {

	    ocft = kmem_zalloc(sizeof (pcser_cftable_t), KM_SLEEP);

	    if (!*cftable) {
		*cftable = ocft;
		cft = ocft;
		cft->prev = NULL;
	    } else {
		cft->next = ocft;
		cft->next->prev = cft;
		cft = cft->next;
	    }

	    cft->next = NULL;

	    bzero((caddr_t)&cistpl_cftable_entry,
			sizeof (struct cistpl_cftable_entry_t));

		/*
		 * We shouldn't ever fail parsing this tuple.  If we do,
		 * there's probably an internal error in the CIS parser.
		 */
	    if ((ret = csx_Parse_CISTPL_CFTABLE_ENTRY(
				pcser->client_handle, &tuple,
					&cistpl_cftable_entry)) != CS_SUCCESS) {
		return (ret);
	    } else {
		int default_cftable;

		/*
		 * See if this tuple has default values that we should save.
		 *	If so, copy the default values that we've seen so far
		 *	into the current cftable structure.
		 */
		if (cistpl_cftable_entry.flags & CISTPL_CFTABLE_TPCE_DEFAULT) {
		    default_cftable = 1;
		} else {
		    default_cftable = 0;
		}

		bcopy((caddr_t)&dcft->p, (caddr_t)&cft->p,
					sizeof (pcser_cftable_params_t));

		cft->p.config_index = cistpl_cftable_entry.index;

		if (cistpl_cftable_entry.flags & CISTPL_CFTABLE_TPCE_IF) {
		    cft->p.pin = cistpl_cftable_entry.pin;
		    if (default_cftable)
			dcft->p.pin = cistpl_cftable_entry.pin;
		}

		if (cistpl_cftable_entry.flags & CISTPL_CFTABLE_TPCE_FS_PWR) {
		    struct cistpl_cftable_entry_pd_t *pd;

		    pd = &cistpl_cftable_entry.pd;

		    if (pd->flags & CISTPL_CFTABLE_TPCE_FS_PWR_VCC) {
			if (pd->pd_vcc.nomV_flags & CISTPL_CFTABLE_PD_EXISTS) {
			    cft->p.modem_vcc = pd->pd_vcc.nomV;
			    if (default_cftable)
				dcft->p.modem_vcc = pd->pd_vcc.nomV;
			} /* CISTPL_CFTABLE_PD_EXISTS */
		    } /* CISTPL_CFTABLE_TPCE_FS_PWR_VCC */

		    if (pd->flags & CISTPL_CFTABLE_TPCE_FS_PWR_VPP1) {
			if (pd->pd_vpp1.nomV_flags & CISTPL_CFTABLE_PD_EXISTS) {
			    cft->p.modem_vpp1 = pd->pd_vpp1.nomV;
			    if (default_cftable)
				dcft->p.modem_vpp1 = pd->pd_vpp1.nomV;
			} /* CISTPL_CFTABLE_PD_EXISTS */
		    } /* CISTPL_CFTABLE_TPCE_FS_PWR_VPP1 */

		    if (pd->flags & CISTPL_CFTABLE_TPCE_FS_PWR_VPP2) {
			if (pd->pd_vpp2.nomV_flags & CISTPL_CFTABLE_PD_EXISTS) {
			    cft->p.modem_vpp2 = pd->pd_vpp2.nomV;
			    if (default_cftable)
				dcft->p.modem_vpp2 = pd->pd_vpp2.nomV;
			} /* CISTPL_CFTABLE_PD_EXISTS */
		    } /* CISTPL_CFTABLE_TPCE_FS_PWR_VPP2 */

		} /* CISTPL_CFTABLE_TPCE_FS_PWR */

		if (cistpl_cftable_entry.flags & CISTPL_CFTABLE_TPCE_FS_IO) {
		    line->flags |= PCSER_VALID_IO_INFO;
		    cft->p.addr_lines = io->addr_lines;
		    if (default_cftable)
			dcft->p.addr_lines = io->addr_lines;
		    if (io->ranges) {
			cft->p.modem_base = io->range[0].addr;
			cft->p.length = io->range[0].length;
			if (default_cftable) {
			    dcft->p.modem_base = io->range[0].addr;
			    dcft->p.length = io->range[0].length;
			}
		    } else {
			/*
			 * If there's no IO ranges for this configuration,
			 *	then we need to calculate the length of
			 *	the IO space by using the number of IO
			 *	address lines value.
			 */
			if (!(cistpl_cftable_entry.io.flags &
					CISTPL_CFTABLE_TPCE_FS_IO_RANGE)) {
			    cft->p.length = (1 << cft->p.addr_lines);
			    if (default_cftable)
				dcft->p.length = cft->p.length;
			} /* if (CISTPL_CFTABLE_TPCE_FS_IO_RANGE) */
		    } /* io->ranges */
		} /* CISTPL_CFTABLE_TPCE_FS_IO */
	    } /* csx_Parse_CISTPL_CFTABLE_ENTRY */

		/*
		 * Setup the desirability of this particular entry.
		 */
	    pcser_set_cftable_desireability(cft);
	} while ((cistpl_cftable_entry.index != last_config_index) &&
		((ret = csx_GetNextTuple(pcser->client_handle,
						&tuple)) == CS_SUCCESS));

#ifdef	PCSER_DEBUG
if (pcser_debug & PCSER_DEBUG_CIS_UCFT) {
	cmn_err(CE_CONT, "====== socket %d unsorted cftable ======\n",
							(int)pcser->sn);
	pcser_display_cftable_list(*cftable, 0);
}
#endif

	/*
	 * Now we've got all of the possible configurations, so sort
	 *	the list of configurations.
	 */
	pcser_sort_cftable_list(cftable);

#ifdef	PCSER_DEBUG
if (pcser_debug & PCSER_DEBUG_CIS_SCFT) {
	cmn_err(CE_CONT, "====== socket %d sorted cftable ======\n",
							(int)pcser->sn);
	pcser_display_cftable_list(*cftable, 0);
}
#endif

	/*
	 * If GetNextTuple gave us any error code other than
	 * 	CS_NO_MORE_ITEMS, it means that there is probably
	 *	an internal error in the CIS parser.
	 */
	if ((ret != CS_SUCCESS) && (ret != CS_NO_MORE_ITEMS))
	    return (ret);	/* this is a real error */

	/*
	 * Since some cards don't have CISTPL_FUNCID or CISTPL_FUNCE
	 *	tuples, we need to set up some reasonable default values
	 *	for the information that would normally be specified in
	 *	these tuples.
	 */
	cis_vars->txbufsize = 1;
	cis_vars->rxbufsize = 1;

	/*
	 * CISTPL_FUNCID and CISTPL_FUNCE processing
	 *
	 * XXX - We should really check to be sure that the CISTPL_FUNCID
	 *	tuple we see is for a serial port. The assumption is made
	 *	that by the time we get here, Card Services would have
	 *	already validated this. Also, we should only search for
	 *	one CISTPL_FUNCID tuple, since a CIS is only allowed to
	 *	have one CISTPL_FUNCID tuple per function.
	 */
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_FUNCID;
	if ((ret = csx_GetFirstTuple(pcser->client_handle,
						&tuple)) != CS_SUCCESS) {
		/*
		 * It's OK not to find the tuple if it's not in the CIS, but
		 *	this test will catch other errors.
		 */
	    if (ret != CS_NO_MORE_ITEMS)
		return (ret);
	} else {
	    do {
		cistpl_funcid_t cistpl_funcid;
		cistpl_funce_t cistpl_funce;

		bzero((caddr_t)&cistpl_funcid, sizeof (struct cistpl_funcid_t));

		/*
		 * We shouldn't ever fail parsing this tuple.  If we do,
		 * there's probably an internal error in the CIS parser.
		 */
		if ((ret = csx_Parse_CISTPL_FUNCID(pcser->client_handle,
				&tuple, &cistpl_funcid)) != CS_SUCCESS) {
		    return (ret);
		} /* csx_Parse_CISTPL_FUNCID */

		/*
		 * Search for any CISTPL_FUNCE tuples.
		 */
		tuple.DesiredTuple = CISTPL_FUNCE;
		while ((ret = csx_GetNextTuple(pcser->client_handle,
						&tuple)) == CS_SUCCESS) {
		    bzero((caddr_t)&cistpl_funce, sizeof (cistpl_funce_t));

		    if ((ret = csx_Parse_CISTPL_FUNCE(pcser->client_handle,
					&tuple, &cistpl_funce,
					cistpl_funcid.function)) != CS_SUCCESS)
			return (ret);

		    if (cistpl_funcid.function == TPLFUNC_SERIAL)
			if ((cistpl_funce.subfunction & 0xf) ==
						TPLFE_SUB_SERIAL) {
			    switch (cistpl_funce.data.serial.ua) {
				case TPLFE_UA_8250:
				    cis_vars->txbufsize = 1;
				    cis_vars->rxbufsize = 1;
				    break;
				case TPLFE_UA_16450:
				    cis_vars->txbufsize = 1;
				    cis_vars->rxbufsize = 1;
				    break;
				case TPLFE_UA_16550:
				    cis_vars->txbufsize = 16;
				    cis_vars->rxbufsize = 16;
				    cis_vars->fifo_enable = 0x0c1;
				    cis_vars->fifo_disable = 0x0;
				    cis_vars->flags |= (PCSER_FIFO_ENABLE |
							PCSER_FIFO_DISABLE);
				    break;
				default:
				    cmn_err(CE_CONT, "pcser_parse_cis: "
					"socket %d unknown serial "
					"interface 0x%x\n",
					(int)pcser->sn,
					cistpl_funce.data.serial.ua);
				    break;
			    } /* switch */
			} /* TPLFE_SUB_SERIAL */
		} /* while (GetNextTuple) */
		tuple.DesiredTuple = CISTPL_FUNCID;
	    } while ((ret = csx_GetNextTuple(pcser->client_handle,
							&tuple)) == CS_SUCCESS);
	} /* GetFirstTuple */

	/*
	 * CISTPL_MANFID processing. The information from this tuple is
	 *	used to augment the information we get from the
	 *	CISTPL_FUNCID and CISTPL_FUNCE tuples.
	 */
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_MANFID;
	if ((ret = csx_GetFirstTuple(pcser->client_handle,
					&tuple)) != CS_SUCCESS) {
		/*
		 * It's OK not to find the tuple if it's
		 *	not in the CIS, but this test will
		 *	catch other errors.
		 */
	    if (ret != CS_NO_MORE_ITEMS)
		return (ret);
	} else {
		/*
		 * We shouldn't ever fail parsing this tuple.  If we do,
		 * there's probably an internal error in the CIS parser.
		 */
	    if ((ret = csx_Parse_CISTPL_MANFID(pcser->client_handle, &tuple,
					&cistpl_manfid)) != CS_SUCCESS) {
		return (ret);
	    } else {
		cis_vars->manufacturer_id = cistpl_manfid.manf;
		cis_vars->card_id = cistpl_manfid.card;
	    } /* csx_Parse_CISTPL_MANFID */
	} /* GetFirstTuple */

	/*
	 * Search the manufactuer specific list and modify any
	 *	card parameters that are appropriate.
	 */
	(void) pcser_modify_manspec_params(pcser, *cftable);

	/*
	 * Check to see if we found a valid configuration. It is possible
	 *	that the CIS on this card does not have enough information
	 *	to configure the card and specify the resources that it
	 *	requires, however if a manufacturer override for this card
	 *	provides the necessary missing/correct information, the
	 *	PCSER_VALID_IO_INFO flag will be set.
	 * Note that in some cases, if the CIS does not contain valid IO
	 *	information, it may also not contain a configuration index
	 *	that is valid for this function. This test will not catch
	 *	that condition, however pcser_modify_manspec_params will
	 *	require an override that specifies a config index as well
	 *	as complete IO information.
	 */
	if (!(line->flags & PCSER_VALID_IO_INFO)) {
	    cmn_err(CE_CONT, "pcser: socket %d not enough "
				"configuration information available\n",
							(int)pcser->sn);
	    return (CS_BAD_CIS);
	} /* if (!io->ranges) */

	return (CS_SUCCESS);
}

/*
 * pcser_modify_manspec_params - Get any manufactuer specific overrides
 *		that might be specified in a property.
 *
 *	return:	DDI_SUCCESS if a match occured
 *		DDI_FAILURE if no match occured
 */
static int
pcser_modify_manspec_params(pcser_unit_t *pcser, pcser_cftable_t *cftable)
{
	pcser_manuspec_t *pcms;
	int i = 0, length = 0, count = 0, ret = DDI_FAILURE;
	char *card_spec = NULL, *card_prop = PCSER_MODIFY_MANSPEC_PARAMS;
	char *cp;

	if (ddi_getlongprop(DDI_DEV_T_ANY, pcser->dip,
				(DDI_PROP_CANSLEEP | DDI_PROP_NOTPROM),
					card_prop, (caddr_t)&card_spec,
						&length) == DDI_PROP_SUCCESS) {

		/*
		 * Count the number of NULLs in the property -
		 *	this will tell us how many manufactuer
		 *	specific override entries the property
		 *	contains. One day we may have a DDI
		 *	function that will directly return an
		 *	array of property values from a property.
		 */
	    cp = card_spec;
	    for (i = 0; i < length; i++)
		if (*cp++ == NULL)
		    count++;

#ifdef  PCSER_DEBUG
	    if (pcser_debug & PCSER_DEBUG_MANUSPEC)
		cmn_err(CE_CONT, "pcser_modify_manspec_params: socket %d "
				"found %d manuspec properties\n",
						(int)pcser->sn, count);
#endif

	    pcms = (pcser_manuspec_t *)kmem_zalloc((count *
					sizeof (pcser_manuspec_t)), KM_SLEEP);

		/*
		 * Parse the property values and create an array of
		 *	pcser_manuspec_t structures for them.
		 */
	    pcser_parse_manuspec_prop(pcser->sn, card_spec, pcms, count);

		/*
		 * Check for a match with one of the
		 *	overrides specified in the property.
		 */
	    ret = pcser_match_manuspec_params(pcser, pcms, count, cftable);
	    kmem_free(pcms, (count * sizeof (pcser_manuspec_t)));
	    kmem_free(card_spec, length);

		/*
		 * If we found a match, then just return since
		 *	we don't need to check the compiled-in list.
		 */
	    if (ret == DDI_SUCCESS)
		return (ret);

	} /* ddi_getlongprop */

		/*
		 * We didn't find a match or there wasn't
		 *	a property, so check the compiled-in list.
		 */
	ret = pcser_match_manuspec_params(pcser, pcser_manuspec,
			sizeof (pcser_manuspec)/sizeof (pcser_manuspec_t),
								cftable);

	return (ret);

}

/*
 * pcser_parse_manuspec_prop - Parse the manufactuer specific override
 *				property and put the parsed entries into
 *				the array of pcser_manuspec_t structures.
 *
 * Note: If there is an error parsing a line in the property, just display
 *	an error message and continue on with parsing the next line.
 */
static void
pcser_parse_manuspec_prop(unsigned sn, char *card_spec,
					pcser_manuspec_t *pcms, int count)
{
	pcser_manuspec_t *ppcms;
	uchar_t *cp, *cpp;
	int l;

	ppcms = pcms;
	cp = (uchar_t *)card_spec;

	for (l = 0; l < count; l++) {
	    cpp = cp + strlen((char *)cp) + 1;

	    if (pcser_ms_parse_line(sn, cp, ppcms) != DDI_SUCCESS) {
		/*EMPTY*/
#ifdef	PCSER_DEBUG
		cmn_err(CE_CONT, "pcser_parse_manuspec_prop: socket %d "
					"error parsing line %d\n", sn, l);
#endif
	    }

	    cp = cpp;
	    ppcms++;
	}

	ppcms = pcms;

	for (l = 0; l < count; l++) {

	    if ((pcser_pcspp_flags & PCSPP_DISPLAY) && ppcms->flags) {
		cmn_err(CE_CONT, "======== pcser_parse_manuspec_prop "
					"socket %d line %d ========\n", sn, l);
		cmn_err(CE_CONT, "flags =          0x%08x\n",
						(int)ppcms->flags);
		cmn_err(CE_CONT, "manufacturer =   0x%08x\n",
						(int)ppcms->manufacturer);
		cmn_err(CE_CONT, "card =           0x%08x\n",
						(int)ppcms->card);
		cmn_err(CE_CONT, "txbufsize =      0x%08x bytes\n",
						(int)ppcms->txbufsize);
		cmn_err(CE_CONT, "rxbufsize =      0x%08x bytes\n",
						(int)ppcms->rxbufsize);
		cmn_err(CE_CONT, "fifo_enable =    0x%08x\n",
						(int)ppcms->fifo_enable);
		cmn_err(CE_CONT, "fifo_disable =   0x%08x\n",
						(int)ppcms->fifo_disable);
		cmn_err(CE_CONT, "auto_rts =       0x%08x\n",
						(int)ppcms->auto_rts);
		cmn_err(CE_CONT, "auto_cts =       0x%08x\n",
						(int)ppcms->auto_cts);
		cmn_err(CE_CONT, "ready_delay_1 =  %010d mS\n",
						(int)ppcms->ready_delay_1);
		cmn_err(CE_CONT, "ready_delay_2 =  %010d mS\n",
						(int)ppcms->ready_delay_2);
		cmn_err(CE_CONT, "config_index =   0x%08x\n",
						(int)ppcms->config_index);
		cmn_err(CE_CONT, "addr_lines =     0x%08x\n",
						(int)ppcms->addr_lines);
		cmn_err(CE_CONT, "length =         0x%08x bytes\n",
						(int)ppcms->length);
		cmn_err(CE_CONT, "modem_base =     0x%08x\n",
						(int)ppcms->modem_base);
		cmn_err(CE_CONT, "CD_ignore_time = %010d mS\n",
						(int)ppcms->CD_ignore_time);
		cmn_err(CE_CONT, "vers_1 =         [%s]\n",
				ppcms->vers_1?ppcms->vers_1:"(no string)");
		cmn_err(CE_CONT, "====\n");
	    }
	    ppcms++;
	}

}

/*
 * pcser_match_manuspec_params - Search through the manufactuer specific
 *		list to see if we can find this card in our list; if we find
 *		it, then modify some of the paramters based on what's in
 *		this list.
 *
 *	If we find a match, we also modify any of the values in the
 *		pcser_cftable_params_t structure that have been
 *		specified as an override; the thought here is that
 *		if we find an override for this card and that override
 *		modifies a CIS paramter, we can't trust any of the
 *		CIS parameters for that value that we might read.
 *
 *	returns: DDI_SUCCESS - if match occured
 *		 DDI_FAILURE - if match did not occur
 */
static int
pcser_match_manuspec_params(pcser_unit_t *pcser, pcser_manuspec_t *pcms,
					int count, pcser_cftable_t *cftable)
{
	pcser_line_t *line = &pcser->line;
	pcser_cis_vars_t *cis_vars = &line->cis_vars;
	uint32_t match = 0;
	int j, tsize = 0, ret = DDI_FAILURE;
	char *vers_1;

	/*
	 * Construct a single VERSION_1 string from the VERSION_1 strings
	 *	read from the CIS.
	 */
	for (j = 0; j < CISTPL_VERS_1_MAX_PROD_STRINGS; j++)
	    if (cis_vars->prod_strings[j][0] != NULL)
		tsize +=
		    (strlen((char *)cis_vars->prod_strings[j]) + 2);

	vers_1 = (char *)kmem_zalloc(tsize, KM_SLEEP);

	for (j = 0; j < CISTPL_VERS_1_MAX_PROD_STRINGS; j++)
	    if (cis_vars->prod_strings[j][0] != NULL) {
		(void) strcat(vers_1, (char *)cis_vars->prod_strings[j]);
		/*
		 * We add a space after each VERS_1 string that
		 *	we copy since normally there isn't one
		 *	at the end of each string and this makes
		 *	it easier to create the pcms->vers_1
		 *	string.
		 */
		(void) strcat(vers_1, " ");
	    }

	/*
	 * Strip off the trailing space.
	 */
	if (strlen(vers_1) > 1)
	    vers_1[strlen(vers_1) - 1] = NULL;

	/*
	 * Go through each of the overrides to see if we can find a match.
	 */
	while (count--) {

#ifdef	PCSER_DEBUG
	    if (pcser_debug & PCSER_DEBUG_MANUSPEC)
		cmn_err(CE_CONT, "pcser_match_manuspec_params: socket %d "
				"flags = 0x%x\n",
				(int)pcser->sn, (int)pcms->flags);
#endif

	    if ((pcms->flags & PCSER_MATCH_MANUFACTURER) &&
				(pcms->manufacturer ==
						cis_vars->manufacturer_id))

		match |= PCSER_MATCH_MANUFACTURER;

	    if ((pcms->flags & PCSER_MATCH_CARD) &&
				(pcms->card == cis_vars->card_id))
		match |= PCSER_MATCH_CARD;

	    if (pcms->flags & PCSER_MATCH_VERS_1) {
		if (strncmp(pcms->vers_1, vers_1,
			min(strlen(pcms->vers_1), strlen(vers_1))) == 0)
		    match |= PCSER_MATCH_VERS_1;
	    }

		/*
		 * Found a match, now modify the
		 *	appropriate parameters.
		 */
	    if ((match != 0) && (match == (pcms->flags & PCSER_MATCH_MASK))) {
		int IO_match = 0;

#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_MANUSPEC)
		    cmn_err(CE_CONT, "  MATCH: pcms->flags = 0x%x, match = "
				"0x%x\n", (int)pcms->flags,
				(int)match);
#endif

		/*
		 * Display a matching message if we are asked to.
		 */
		if (pcser_pcspp_flags & PCSPP_DSPMATCH)
		    cmn_err(CE_CONT, "pcser: socket %d match: [%s]\n",
						(int)pcser->sn, vers_1);

		if (pcms->flags & PCSER_MANSPEC_TXBUFSIZE)
		    cis_vars->txbufsize = pcms->txbufsize;

		if (pcms->flags & PCSER_MANSPEC_RXBUFSIZE)
		    cis_vars->rxbufsize = pcms->rxbufsize;

		if (pcms->flags & PCSER_MANSPEC_FIFO_ENABLE) {
		    cis_vars->fifo_enable = pcms->fifo_enable;
		    cis_vars->flags |= PCSER_FIFO_ENABLE;
		}

		if (pcms->flags & PCSER_MANSPEC_FIFO_DISABLE) {
		    cis_vars->fifo_disable = pcms->fifo_disable;
		    cis_vars->flags |= PCSER_FIFO_DISABLE;
		}

		if (pcms->flags & PCSER_MANSPEC_AUTO_RTS) {
		    cis_vars->auto_rts = pcms->auto_rts;
		    cis_vars->flags |= PCSER_AUTO_RTS;
		}

		if (pcms->flags & PCSER_MANSPEC_AUTO_CTS) {
		    cis_vars->auto_cts = pcms->auto_cts;
		    cis_vars->flags |= PCSER_AUTO_CTS;
		}

		if (pcms->flags & PCSER_MANSPEC_READY_DELAY_1)
		    cis_vars->ready_delay_1 = pcms->ready_delay_1;

		if (pcms->flags & PCSER_MANSPEC_READY_DELAY_2)
		    cis_vars->ready_delay_2 = pcms->ready_delay_2;

		if (pcms->flags & PCSER_MANSPEC_CONFIG_INDEX) {
		    pcser_cftable_t *cft = cftable;

		    while (cft) {
			cft->p.config_index = pcms->config_index;
			cft = cft->next;
		    }

		    cis_vars->config_index = pcms->config_index;

		    IO_match |= PCSER_MANSPEC_CONFIG_INDEX;
		}

		if (pcms->flags & PCSER_MANSPEC_CONFIG_ADDR) {
		    cis_vars->config_base = pcms->config_address;

		    IO_match |= PCSER_MANSPEC_CONFIG_ADDR;
		}

		if (pcms->flags & PCSER_MANSPEC_CONFIG_PRESENT) {
		    cis_vars->present = pcms->present;

		    IO_match |= PCSER_MANSPEC_CONFIG_PRESENT;
		}

		if (pcms->flags & PCSER_MANSPEC_NUM_IO_LINES) {
		    pcser_cftable_t *cft = cftable;

		    while (cft) {
			cft->p.addr_lines = pcms->addr_lines;
			cft = cft->next;
		    }

		    cis_vars->addr_lines = pcms->addr_lines;

		    IO_match |= PCSER_MANSPEC_NUM_IO_LINES;
		}

		if (pcms->flags & PCSER_MANSPEC_NUM_IO_PORTS) {
		    pcser_cftable_t *cft = cftable;

		    while (cft) {
			cft->p.length = pcms->length;
			cft = cft->next;
		    }

		    cis_vars->length = pcms->length;

		    IO_match |= PCSER_MANSPEC_NUM_IO_PORTS;
		}

		if (pcms->flags & PCSER_MANSPEC_IO_ADDR) {
		    pcser_cftable_t *cft = cftable;

		    while (cft) {
			cft->p.modem_base = pcms->modem_base;
			cft = cft->next;
		    }

		    cis_vars->modem_base = pcms->modem_base;

		    IO_match |= PCSER_MANSPEC_IO_ADDR;
		}

		if (pcms->flags & PCSER_MANSPEC_CD_TIME)
		    line->pcser_ignore_cd_time = pcms->CD_ignore_time;

		if (pcms->flags & PCSER_MANSPEC_IGN_CD_ON_OPEN)
		    line->flags |= PCSER_IGNORE_CD_ON_OPEN;

		/*
		 * Default to a successful return.
		 */
		ret = DDI_SUCCESS;
		count = 0;

		/*
		 * If we need to generate complete IO info from this
		 *	override (since the CIS didn't provide it
		 *	for us), we need to be sure that this override
		 *	specifies all six of the following:
		 *
		 *	PCSER_MANSPEC_CONFIG_INDEX - configuration index
		 *	PCSER_MANSPEC_CONFIG_ADDR - config regs address
		 *	PCSER_MANSPEC_CONFIG_PRESET - config regs present mask
		 *	PCSER_MANSPEC_NUM_IO_LINES - IO address lines
		 *	PCSER_MANSPEC_NUM_IO_PORTS - num IO ports
		 *	PCSER_MANSPEC_IO_ADDR - IO base address
		 *
		 * If any of these parameters are missing, display a
		 *	message and reject this override, as well as
		 *	terminate further override processing.
		 */
		if (!(line->flags & PCSER_VALID_IO_INFO)) {
		    if (IO_match != (PCSER_MANSPEC_CONFIG_INDEX |
					PCSER_MANSPEC_CONFIG_ADDR |
					PCSER_MANSPEC_CONFIG_PRESENT |
					PCSER_MANSPEC_NUM_IO_LINES |
					PCSER_MANSPEC_NUM_IO_PORTS |
					PCSER_MANSPEC_IO_ADDR)) {

			cmn_err(CE_CONT, "pcser: socket %d missing "
						"manufacturer specific "
						"overrides\n"
						"       for: [%s]\n",
						(int)pcser->sn, vers_1);

			ret = DDI_FAILURE;

			if (!(IO_match & PCSER_MANSPEC_CONFIG_INDEX)) {
			    cmn_err(CE_CONT, "       \"config_index\" "
						"override missing\n");
			}

			if (!(IO_match & PCSER_MANSPEC_CONFIG_ADDR)) {
			    cmn_err(CE_CONT, "       \"config_address\" "
						"override missing\n");
			}
			if (!(IO_match & PCSER_MANSPEC_CONFIG_PRESENT)) {
			    cmn_err(CE_CONT, "       \"config_regs_present\" "
						"override missing\n");
			}

			if (!(IO_match & PCSER_MANSPEC_NUM_IO_LINES)) {
			    cmn_err(CE_CONT, "       \"IO_addr_lines\" "
						"override missing\n");
			}

			if (!(IO_match & PCSER_MANSPEC_NUM_IO_PORTS)) {
			    cmn_err(CE_CONT, "       \"IO_num_ports\" "
						"override missing\n");
			}

			if (!(IO_match & PCSER_MANSPEC_IO_ADDR)) {
			    cmn_err(CE_CONT, "       \"IO_base_addr\" "
						"override missing\n");
			}
		    } else {
			line->flags |= PCSER_VALID_IO_INFO;
		    } /* if IO_match */
		} /* if !PCSER_VALID_IO_INFO */
	    } /* match */

	    pcms++;

	} /* while */

	kmem_free(vers_1, tsize);
	return (ret);
}

void
pcser_destroy_cftable_list(pcser_cftable_t **cftable)
{
	pcser_cftable_t *cft, *ocft = NULL;

	cft = *cftable;

	while (cft) {
	    ocft = cft;
	    cft = cft->next;
	}

	while (ocft) {
	    cft = ocft->prev;
	    kmem_free(ocft, sizeof (pcser_cftable_t));
	    ocft = cft;
	}

	*cftable = NULL;
}

static void
pcser_set_cftable_desireability(pcser_cftable_t *cft)
{
	int i;

	cft->desireability = (
		((cft->p.addr_lines == 3)?0:(cft->p.addr_lines << 16)) |
		(cft->p.modem_base & 0x0ffff));

	for (i = 0; i < (sizeof (pcser_cis_addr_tran) /
					sizeof (pcser_cis_addr_tran_t)); i++) {
	    if (cft->p.modem_base == pcser_cis_addr_tran[i].modem_base) {
		cft->desireability = ((cft->desireability & 0x0ffff0000) |
					(pcser_cis_addr_tran[i].desireability));
		return;
	    }
	} /* while */
}

static void
pcser_sort_cftable_list(pcser_cftable_t **cftable)
{
	pcser_cftable_t *cft;
	int did_swap = 1;

	do {
	    cft = *cftable;
	    did_swap = 0;
	    while (cft) {
		if (cft->prev) {
		    if (cft->desireability < cft->prev->desireability) {
			pcser_swap_cft(cftable, cft);
			did_swap = 1;
		    } /* if (cft->desireability) */
		} /* if (cft->prev) */
		cft = cft->next;
	    } /* while (cft) */
	} while (did_swap);
}

static void
pcser_swap_cft(pcser_cftable_t **cftable, pcser_cftable_t *cft)
{
	pcser_cftable_t *cfttmp;

	if (cft->next)
	    cft->next->prev = cft->prev;

	cft->prev->next = cft->next;
	cft->next = cft->prev;

	if (cft->prev->prev)
	    cft->prev->prev->next = cft;

	cfttmp = cft->prev;

	if ((cft->prev = cft->prev->prev) == NULL)
	    *cftable = cft;

	cfttmp->prev = cft;
}

#ifdef	PCSER_DEBUG
void
pcser_display_cftable_list(pcser_cftable_t *cft, int single)
{
	int i = 0;

	while (cft) {
	    if (!single)
		cmn_err(CE_CONT, "====== cftable entry %d ======\n", i++);
	    cmn_err(CE_CONT,
			"   desireability: 0x%x\n", (int)cft->desireability);
	    cmn_err(CE_CONT,
			"    config_index: 0x%x\n", (int)cft->p.config_index);
	    cmn_err(CE_CONT,
			"      addr_lines: 0x%x\n", (int)cft->p.addr_lines);
	    cmn_err(CE_CONT,
			"          length: 0x%x\n", (int)cft->p.length);
	    cmn_err(CE_CONT,
			"             pin: 0x%x\n", (int)cft->p.pin);
	    cmn_err(CE_CONT,
			"       modem_vcc: %d\n",   (int)cft->p.modem_vcc);
	    cmn_err(CE_CONT,
			"      modem_vpp1: %d\n",   (int)cft->p.modem_vpp1);
	    cmn_err(CE_CONT,
			"      modem_vpp2: %d\n",   (int)cft->p.modem_vpp2);
	    cmn_err(CE_CONT,
			"      modem_base: 0x%x\n", (int)cft->p.modem_base);
	    cmn_err(CE_CONT, "====\n");
	    delay(MS2HZ(20));
	    if (single)
		break;
	    cft = cft->next;
	}

}
#endif

static uchar_t *
find_token(uchar_t **cp, int *l)
{
	uchar_t *cpp = *cp;

	while ((**cp && (isalpha(**cp) || isxdigit(**cp) ||
				(**cp == PCSER_PARSE_UNDERSCORE)))) {
	    (*cp)++;
	    (*l)++;
	}

	 **cp = NULL;

	return (cpp);
}

static int
parse_token(uchar_t *token)
{
	pcser_manuspec_parse_tree_t *pt = pcser_manuspec_parse_tree;
	int k = sizeof (pcser_manuspec_parse_tree) /
			sizeof (pcser_manuspec_parse_tree_t);

	while (k--) {
	    if (strcmp((char *)token, pt->token) == 0)
		return (pt->state);
	    pt++;
	}

	return (PT_STATE_UNKNOWN);
}

static int
token_to_hex(uchar_t *token, unsigned *val)
{
	uchar_t c;

	*val = 0;

	while (*token) {
	    if (!isxdigit(*token))
		return (0);
	    c = toupper(*token);
	    if (c >= 'A')
		c = c - 'A' + 10+ '0';
	    *val = ((*val * 16) + (c - '0'));
	    token++;
	}

	return (1);
}

static int
token_to_dec(uchar_t *token, unsigned *val)
{
	*val = 0;

	while (*token) {
	    if (!isdigit(*token))
		return (0);
	    *val = ((*val * 10) + (*token - '0'));
	    token++;
	}

	return (1);
}

/*
 * pcser_ms_parse_line - parse the tokens in the line and fill out
 *				the pcser_manuspec_t structure
 *
 *	returns: DDI_SUCCESS - if no error parsing line
 *		 DDI_FAILURE - if error parsing line
 */
static int
pcser_ms_parse_line(unsigned sn, uchar_t *cp, pcser_manuspec_t *pcms)
{
	int state = PT_STATE_TOKEN, qm = 0, em = 0, l = 0;
	int length;
	uchar_t *token = (uchar_t *)"beginning of line";
	uchar_t *ptoken = NULL, *quote;

	length = strlen((char *)cp);

	if (pcser_pcspp_flags & PCSPP_DEBUG_PARSE_LINE) {
	    if (*cp)
		printf("==> [%s]\n", cp);
	    else
		printf("==> (empty string)\n");
	}

	while ((*cp) && (l < length)) {

		/*
		 * Check for comment
		 */
	    if (!qm && (*cp == PCSER_PARSE_COMMENT)) {
		if (pcser_pcspp_flags & PCSPP_COMMENT)
		    cmn_err(CE_CONT, "%s\n", cp);
		return (DDI_SUCCESS);
	    }

		/*
		 * Check for escaped characters
		 */
	    if (*cp == PCSER_PARSE_ESCAPE) {
		uchar_t *cpp = cp, *cppp = cp + 1;

		em = 1;

		if (!qm) {
		    cmn_err(CE_CONT, "pcser_ms_parse_line: socket %d "
						"escape not allowed outside "
						"of quotes at [%s]\n",
								sn, token);
		    return (DDI_FAILURE);

		} /* if (!qm) */

		while (*cppp)
		    *cpp++ = *cppp++;

		l++;

		*cpp = NULL;
	    } /* PCSER_PARSE_ESCAPE */

		/*
		 * Check for quoted strings
		 */
	    if (!em && (*cp == PCSER_PARSE_QUOTE)) {
		qm ^= 1;
		if (qm) {
		    quote = cp + 1;
		} else {
		    *cp = NULL;
		    if (state != PT_STATE_STRING_VAR) {
			cmn_err(CE_CONT, "pcser_ms_parse_line: socket %d "
						"unexpected string [%s] after "
						"[%s]\n", sn, quote, token);
			return (DDI_FAILURE);
		    } else {
			if (pcser_set_manuspec_params(sn, token, quote, pcms)
								== DDI_FAILURE)
			    return (DDI_FAILURE);
		    }
		    state = PT_STATE_TOKEN;
		} /* if (qm) */
	    } /* PCSER_PARSE_QUOTE */

	    em = 0;

		/*
		 * Check for tokens
		 */
	    if (!qm && (isalpha(*cp) || isxdigit(*cp))) {
		ptoken = token;
		token = find_token(&cp, &l);

		switch (state) {
		    case PT_STATE_TOKEN:
			switch (state = parse_token(token)) {
			    case PT_STATE_UNKNOWN:
				cmn_err(CE_CONT, "pcser_ms_parse_line: "
							"socket %d unknown "
							"token [%s]\n",
								sn, token);
				return (DDI_FAILURE);
			    case PT_STATE_TOKEN:
				if (pcser_set_manuspec_params(sn, token,
								NULL, pcms)
								== DDI_FAILURE)
				    return (DDI_FAILURE);
				break;
			} /* switch (parse_token) */
			break;
		    case PT_STATE_HEX_VAR:
		    case PT_STATE_DEC_VAR:
		    case PT_STATE_STRING_VAR:
			if (pcser_set_manuspec_params(sn, ptoken, token, pcms)
								== DDI_FAILURE)
			    return (DDI_FAILURE);
			state = PT_STATE_TOKEN;
			break;
		    default:
			cmn_err(CE_CONT, "pcser_ms_parse_line: socket %d "
						"unknown state machine "
						"state = %d\n", sn, state);
			return (DDI_FAILURE);
		} /* switch (state) */
	    }
	    cp++;
	    l++;
	} /* while (*cp) */

	if (qm) {
	    cmn_err(CE_CONT, "pcser_ms_parse_line: socket %d unterminated "
						"string = [%s]\n", sn, quote);
	    return (DDI_FAILURE);
	}

	if (state != PT_STATE_TOKEN) {
	    cmn_err(CE_CONT, "pcser_ms_parse_line: socket %d token [%s] "
						"requires value\n", sn, token);
	    return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * pcser_set_manuspec_params - set the appropriate values in the
 *				pcser_manuspec_t structure based
 *				on the passed-in token and param
 *
 *	returns: DDI_SUCCESS - if no error parsing line
 *		 DDI_FAILURE - if error parsing line
 */
static int
pcser_set_manuspec_params(unsigned sn, uchar_t *token, uchar_t *param,
						pcser_manuspec_t *pcms)
{
	pcser_manuspec_parse_tree_t *pt = pcser_manuspec_parse_tree;
	int k = sizeof (pcser_manuspec_parse_tree) /
			sizeof (pcser_manuspec_parse_tree_t);
	unsigned val;

	while (k--) {

	    if (strcmp((char *)token, pt->token) == 0) {

		switch (pt->fmt) {
		    case PT_VAR_HEX:
			if (!token_to_hex(param, &val)) {
			    cmn_err(CE_CONT, "pcser_set_manuspec_params: "
						"socket %d invalid hex value "
						"[%s] for token [%s]\n",
							sn, param, token);
			    return (DDI_FAILURE);
			} else {

			    pcms->flags |= pt->flags;

			    *((unsigned *)((uintptr_t)pcms +
						(uintptr_t)pt->var)) = val;

			    if (pcser_pcspp_flags & PCSPP_DEBUG_SET_MSP)
				cmn_err(CE_CONT, "PT_VAR_HEX: token [%s] "
							"value [%s] "
							"hex 0x%x\n",
							token, param, val);
			} /* token_to_hex */
			break;
		    case PT_VAR_DEC:
			if (!token_to_dec(param, &val)) {
			    cmn_err(CE_CONT, "pcser_set_manuspec_params: "
						"socket %d invalid dec value "
						"[%s] for token [%s]\n",
							sn, param, token);
			    return (DDI_FAILURE);
			} else {

			    pcms->flags |= pt->flags;

			    *((unsigned *)((uintptr_t)pcms +
						(uintptr_t)pt->var)) = val;

			    if (pcser_pcspp_flags & PCSPP_DEBUG_SET_MSP)
				cmn_err(CE_CONT, "PT_VAR_DEC: token [%s] "
							"value [%s] "
							"dec %d\n",
							token, param, val);
			} /* token_to_dec */
			break;
		    case PT_VAR_STRING:
			pcms->flags |= pt->flags;

			*((uchar_t **)(((caddr_t)pcms) +
				((uintptr_t)pt->var))) = param;

			if (pcser_pcspp_flags & PCSPP_DEBUG_SET_MSP)
				cmn_err(CE_CONT, "PT_VAR_STRING: token [%s] "
						"string [%s]\n", token, param);
			break;
		    case PT_VAR_BOOL:

			if (pcser_pcspp_flags & PCSPP_DEBUG_SET_MSP)
			    cmn_err(CE_CONT, "PT_VAR_BOOL: token [%s]\n",
									token);
			switch (pt->ctl) {
			    case PT_VAR_BOOL_DISPLAY_ON:
				pcser_pcspp_flags |= PCSPP_DISPLAY;
				break;
			    case PT_VAR_BOOL_DISPLAY_OFF:
				pcser_pcspp_flags &= ~PCSPP_DISPLAY;
				break;
			    case PT_VAR_BOOL_DSPMATCH_ON:
				pcser_pcspp_flags |= PCSPP_DSPMATCH;
				break;
			    case PT_VAR_BOOL_DSPMATCH_OFF:
				pcser_pcspp_flags &= ~PCSPP_DSPMATCH;
				break;
			    case PT_VAR_BOOL_CD_IGN:
				pcms->flags |= PCSER_MANSPEC_IGN_CD_ON_OPEN;
				break;
			    case PT_VAR_BOOL_DEBUG_STAT:
				cmn_err(CE_CONT, "flags=0x%x, display=%s, "
							"comments=%s "
							"display_match=%s"
#ifdef	PCSER_DEBUG
							" pcser_deubg=0x%x"
#endif
							"\n",
							pcser_pcspp_flags,
							pcser_pcspp_flags &
								PCSPP_DISPLAY ?
								"on":"off",
							pcser_pcspp_flags &
								PCSPP_COMMENT ?
								"on":"off",
							pcser_pcspp_flags &
								PCSPP_DSPMATCH ?
								"on":"off"
#ifdef	PCSER_DEBUG
							/* CSTYLED */
							, pcser_debug);
#else
							/* CSTYLED */
							);
#endif
				break;
			    case PT_VAR_BOOL_COMMENT_ON:
				pcser_pcspp_flags |= PCSPP_COMMENT;
				break;
			    case PT_VAR_BOOL_COMMENT_OFF:
				pcser_pcspp_flags &= ~PCSPP_COMMENT;
				break;
			    default:
				cmn_err(CE_CONT, "PT_VAR_BOOL: no action for "
							"boolean token [%s]\n",
									token);
				break;
			} /* switch (pt->ctl) */
			break;
		    case PT_VAR_HEX_CTL:
			if (!token_to_hex(param, &val)) {
			    cmn_err(CE_CONT, "pcser_set_manuspec_params: "
						"socket %d invalid hex value "
						"[%s] for token [%s]\n",
							sn, param, token);
			    return (DDI_FAILURE);
			} else {
			    switch (pt->ctl) {
				case PT_VAR_HEX_CTL_DEBUG:
				    pcser_pcspp_flags = val |
							(pcser_pcspp_flags &
							(PCSPP_DISPLAY |
							PCSPP_COMMENT));
				    break;
				case PT_VAR_HEX_CTL_PCSER_DEBUG:
#ifdef	PCSER_DEBUG
				    pcser_debug = val;
#endif
				    break;
				default:
				    cmn_err(CE_CONT, "PT_VAR_HEX_CTL: no "
							"action for hex "
							"token [%s]\n", token);
				    break;
			    } /* switch (pt->ctl) */

			    if (pcser_pcspp_flags & PCSPP_DEBUG_SET_MSP)
				cmn_err(CE_CONT, "PT_VAR_HEX: token [%s] "
							"value [%s] "
							"hex 0x%x\n",
							token, param, val);
			} /* token_to_hex */
			break;
		    default:
			cmn_err(CE_CONT, "pcser_set_manuspec_params: socket %d "
						"invalid data format 0x%x\n",
								sn, pt->fmt);
			return (DDI_FAILURE);
		} /* switch */

		return (DDI_SUCCESS);
	    } /* strcmp */
	    pt++;
	} /* while */

	return (DDI_FAILURE);
}
