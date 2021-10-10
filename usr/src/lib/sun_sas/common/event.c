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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include	<sun_sas.h>
#include	<libsysevent.h>
#include	<sys/types.h>
#include	<netinet/in.h>
#include	<inttypes.h>
#include	<ctype.h>
#include	<sys/sysevent/eventdefs.h>
#include	<sys/scsi/impl/scsi_sas.h>

#define	HBA_PORT_MATCH		1
#define	TARGET_PORT_MATCH	2
#define	PHY_MATCH		3

#define	REMOVED		1
#define	ONLINE		2
#define	OFFLINE		3

sysevent_handle_t *gSysEventHandle = NULL;

/* Calls the client callback function, if one is registered */
static HBA_STATUS
updateMatchingPhy(HBA_WWN portAddr, uint8_t phyId, int update, uint8_t linkRate)
{
	const char  ROUTINE[] = "updateMatchingPhy";
	struct sun_sas_hba	*hba_ptr;
	struct sun_sas_port	*hba_port_ptr;
	struct phy_info		*phy_ptr;

	log(LOG_DEBUG, ROUTINE, "- phy matching");
	/* grab write lock */
	lock(&all_hbas_lock);
	/* loop through HBAs */
	for (hba_ptr = global_hba_head; hba_ptr != NULL;
	    hba_ptr = hba_ptr->next) {
		/* loop through HBA ports */
		for (hba_port_ptr = hba_ptr->first_port;
		    hba_port_ptr != NULL;
		    hba_port_ptr = hba_port_ptr->next) {
			if (wwnConversion(hba_port_ptr->
			    port_attributes.PortSpecificAttribute.
			    SASPort->LocalSASAddress.wwn) ==
			    wwnConversion(portAddr.wwn)) {
				/* loop through phys */
				for (phy_ptr = hba_port_ptr->first_phy;
				    phy_ptr != NULL; phy_ptr =
				    phy_ptr->next) {
					if (phy_ptr->phy.PhyIdentifier ==
					    phyId) {
						if (update == REMOVED) {
							phy_ptr->invalid =
							    B_TRUE;
						} else if (update == OFFLINE) {
							phy_ptr->phy.
							    NegotiatedLinkRate
							    = 0;
						} else { /* online */
							phy_ptr->phy.
							    NegotiatedLinkRate
							    = linkRate;
						}
						unlock(&all_hbas_lock);
						return (HBA_STATUS_OK);
					}
				} /* for phys */
			} /* wwn mismatch. continue */
		} /* for HBA ports */
	} /* for HBAs */

	unlock(&all_hbas_lock);
	return (HBA_STATUS_ERROR);
}

/* Event handler called by system */
static void
syseventHandler(sysevent_t *ev)
{

	const char	ROUTINE[] = "syseventHandler";
	nvlist_t 	*attrList = NULL;
	char		*eventStr, *portAddrStr, *charptr;
	int		update;
	uint64_t 	addr;
	uint8_t		phyId, linkRate;
	HBA_WWN		portAddr;

	/* Is the event one of ours? */
	if (strncmp(EC_HBA, sysevent_get_class_name(ev), strlen(EC_HBA)) == 0) {
		/* handle phy events */
		if (strncmp(ESC_SAS_PHY_EVENT, sysevent_get_subclass_name(ev),
		    strlen(ESC_SAS_PHY_EVENT)) == 0) {
			if (sysevent_get_attr_list(ev, &attrList) != 0) {
				log(LOG_DEBUG, ROUTINE,
				    "Failed to get event attributes on %s/%s",
				    EC_HBA, ESC_SAS_PHY_EVENT);
				return;
			} else {
				if (nvlist_lookup_string(attrList,
				    "event_type", &eventStr) != 0) {
					log(LOG_DEBUG, ROUTINE,
					    "Event type not found");
					return;
				} else {
					if (strncmp(eventStr, "phy_online",
					    sizeof (eventStr)) == 0) {
						update = ONLINE;
						if (nvlist_lookup_uint8(
						    attrList, "link_rate",
						    &linkRate) != 0) {
							log(LOG_DEBUG, ROUTINE,
							    "Link Rate not \
							    found");
							return;
						}
					} else if (strncmp(eventStr,
					    "phy_offline",
					    sizeof (eventStr)) == 0) {
						update = OFFLINE;
					} else if (strncmp(eventStr,
					    "phy_remove",
					    sizeof (eventStr)) == 0) {
						update = REMOVED;
					} else {
						log(LOG_DEBUG, ROUTINE,
						    "Invalid event type");
						return;
					}
				}
				if (nvlist_lookup_string(attrList,
				    "port_address", &portAddrStr) != 0) {
					log(LOG_DEBUG, ROUTINE,
					    "Port SAS address not found");
					return;
				} else {
					for (charptr = portAddrStr;
					    charptr != NULL; charptr++) {
						if (isxdigit(*charptr)) {
							break;
						}
					}
					addr = htonll(strtoll(charptr,
					    NULL, 16));
					(void) memcpy(portAddr.wwn, &addr, 8);
				}
				if (nvlist_lookup_uint8(attrList,
				    "PhyIdentifier", &phyId) != 0) {
					log(LOG_DEBUG, ROUTINE,
					    "Port SAS address not found");
					return;
				}
			}
			if (updateMatchingPhy(portAddr, phyId, update,
			    linkRate) != HBA_STATUS_OK) {
				log(LOG_DEBUG, ROUTINE,
				    "updating phy for the events failed.");
			}
		}
	} else if (strncmp(EC_DR,  sysevent_get_class_name(ev), 2) == 0) {
		/* handle DR events */
		log(LOG_DEBUG, ROUTINE,
		    "handle EC_dr events.");
	} else {
		log(LOG_DEBUG, ROUTINE,
		    "Found Unregistered event. - exit");
		return;
	}

	log(LOG_DEBUG, ROUTINE, "- exit");
}

/* Registers events to the sysevent framework */
HBA_STATUS
registerSysevent() {

	const char ROUTINE[] = "registerSysevent";
	const char *hba_subclass_list[] = {
		ESC_SAS_PHY_EVENT
	};
	const char *dr_subclass_list[] = {
		ESC_DR_TARGET_STATE_CHANGE
	};

	gSysEventHandle = sysevent_bind_handle(syseventHandler);
	if (gSysEventHandle == NULL) {
		log(LOG_DEBUG, ROUTINE,
		    "- sysevent_bind_handle() failed");
		log(LOG_DEBUG, ROUTINE, "- error exit");
		return (HBA_STATUS_ERROR);
	}

	if (sysevent_subscribe_event(gSysEventHandle, EC_HBA,
	    hba_subclass_list, 1) != 0) {
		log(LOG_DEBUG, ROUTINE,
		    "- sysevent_subscribe_event() failed for EC_HBA subclass");
		log(LOG_DEBUG, ROUTINE, "- error exit");
		sysevent_unbind_handle(gSysEventHandle);
		return (HBA_STATUS_ERROR);
	}

	if (sysevent_subscribe_event(gSysEventHandle, EC_DR,
	    dr_subclass_list, 1) != 0) {
		log(LOG_DEBUG, ROUTINE,
		    "- sysevent_subscribe_event() failed for DR subclass");
		log(LOG_DEBUG, ROUTINE, "- error exit");
		sysevent_unbind_handle(gSysEventHandle);
		return (HBA_STATUS_ERROR);
	}

	log(LOG_DEBUG, ROUTINE, "- exit");

	return (HBA_STATUS_ERROR);
}
