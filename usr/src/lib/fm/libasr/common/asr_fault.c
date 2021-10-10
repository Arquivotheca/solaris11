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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/fm/protocol.h>
#include <fm/libtopo.h>
#include <fm/fmd_msg.h>
#include <fm/fmd_api.h>
#include <strings.h>

#include "asr.h"
#include "asr_buf.h"
#include "asr_err.h"
#include "asr_nvl.h"

#define	ASR_FLT_CLASS		"class"
#define	ASR_FLT_CERTAINTY	"certainty"
#define	ASR_FLT_DESCRIPTION	"description"
#define	ASR_FLT_SEVERITY	"severity"
#define	ASR_FLT_REASON		"reason"
#define	ASR_FLT_FRU_NAME	"fru.name"
#define	ASR_FLT_FRU_ID		"fru.id"
#define	ASR_FLT_FRU_PART	"fru.part"
#define	ASR_FLT_FRU_SERIAL	"fru.serial"
#define	ASR_FLT_FRU_REVISION	"fru.revision"

#define	ASR_FLT_SEVERITY_NA		"NA"
#define	ASR_FLT_SEVERITY_NONE		"None"
#define	ASR_FLT_SEVERITY_MAJOR		"Major"
#define	ASR_FLT_SEVERITY_MINOR		"Minor"
#define	ASR_FLT_SEVERITY_CRITICAL	"Critical"

/*
 * Returns a supported ASR severity value from the FMA severity value.
 * NA, None, Major, Minor, Critical
 */
static char *
asr_sev_from_fma_sev(char *fmasev)
{
	if (fmasev == NULL)
		return (ASR_FLT_SEVERITY_NA);
	if (strcmp(fmasev, "none") == 0 ||
	    strcmp(fmasev, ASR_FLT_SEVERITY_NONE) == 0)
		return (ASR_FLT_SEVERITY_NONE);
	if (strcmp(fmasev, "minor") == 0 ||
	    strcmp(fmasev, ASR_FLT_SEVERITY_MINOR) == 0)
		return (ASR_FLT_SEVERITY_MINOR);
	if (strcmp(fmasev, "major") == 0 ||
	    strcmp(fmasev, ASR_FLT_SEVERITY_MAJOR) == 0)
		return (ASR_FLT_SEVERITY_MAJOR);
	if (strcmp(fmasev, "critical") == 0 ||
	    strcmp(fmasev, ASR_FLT_SEVERITY_CRITICAL) == 0)
		return (ASR_FLT_SEVERITY_CRITICAL);
	return (ASR_FLT_SEVERITY_NA);
}

/*
 * Adds the diagnosis engine FMRI as additional information.
 */
static int
asr_add_de(topo_hdl_t *thp, nvlist_t *fault, int pad, asr_buf_t *out)
{
	nvlist_t *de;
	char *fmristr = NULL;
	int err;

	err = nvlist_lookup_nvlist(fault, FM_SUSPECT_DE, &de);
	if (err != 0 || de == NULL)
		goto finally;

	if ((fmristr = asr_topo_fmri2str(thp, de)) == NULL) {
		err = ASR_FAILURE;
		goto finally;
	}

	err = asr_buf_append_xml_ai(out, pad, "de", fmristr);

finally:
	if (fmristr != NULL)
		free(fmristr);

	return (err);
}

/*
 * Pulls out the ASR component name from an FMRI string.
 */
static char *
asr_fmri_name(char *fmri)
{
	char *name = fmri;
	char *cp;
	char *rname;
	for (cp = fmri; *cp != '\0'; cp++)
		if (*cp == '/')
			name = cp;
	if (*name == '/')
		name++;

	rname = strdup(name);

	for (cp = rname; *cp != '\0'; cp++)
		if (*cp == '=')
			*cp = ' ';
	return (rname);
}

/*
 * Truncates the fmri name string so it just contains the fru type.
 */
static void
asr_fmri_type(char *name)
{
	int i;
	for (i = 0; name[i] != '\0'; i++) {
		if (name[i] == ' ') {
			name[i] = '\0';
			break;
		}
	}
}

/*
 * Create hardware component for a manual fault.
 */
static int
asr_fault_fru(asr_buf_t *bp, nvlist_t *event)
{
	int err = 0;
	int pad = 3;
	char *fru_name = asr_nvl_str(event, ASR_FLT_FRU_NAME);
	char *fru_id = asr_nvl_str(event, ASR_FLT_FRU_ID);
	char *fru_serial = asr_nvl_str(event, ASR_FLT_FRU_SERIAL);
	char *fru_part = asr_nvl_str(event, ASR_FLT_FRU_PART);
	char *fru_revision = asr_nvl_str(event, ASR_FLT_FRU_REVISION);

	if (fru_name == NULL && fru_serial == NULL && fru_part == NULL &&
	    fru_id == NULL && fru_revision == NULL)
		return (ASR_FAILURE);

	err |= asr_buf_append_xml_elem(
	    bp, pad - 1, "hardware-component");

	err |= asr_buf_append_xml_nv(bp, pad, "name", fru_name);
	err |= asr_buf_append_xml_nvtoken(bp, pad, "id", fru_id);
	err |= asr_buf_append_xml_nv(bp, pad, "serial", fru_serial);
	err |= asr_buf_append_xml_nv(bp, pad, "part", fru_part);
	err |= asr_buf_append_xml_nvtoken(bp, pad,
	    "revision", fru_revision);
	err |= asr_buf_append_xml_end(
	    bp, pad - 1, "hardware-component");
	return (err);
}

/*
 * Adds and ASR <hardware-component> element for each FRU suspect
 */
static int
asr_fault_components(asr_handle_t *ah,
    asr_buf_t *bp, nvlist_t **faults, uint_t nfaults, nvlist_t *event)
{
	topo_hdl_t *thp;
	uint_t ii;
	int err = 0;
	int pad = 3;
	boolean_t use_schema_2_1 = asr_use_schema_2_1(ah);

	if (nfaults == 0)
		return (asr_fault_fru(bp, event));

	if ((thp = topo_open(TOPO_VERSION, NULL, &err)) == NULL) {
		(void) asr_error(EASR_TOPO,
		    "failed to alloc topo handle: %s", topo_strerror(err));
		return (ASR_FAILURE);
	}

	for (ii = 0; ii < nfaults; ++ii) {
		nvlist_t *fault = faults[ii];
		nvlist_t *frunvl;
		nvlist_t *asru, *resource;

		char *fmristr, *scheme;
		char *hc_id, *hc_name, *hc_part, *hc_revision, *hc_serial_id;

		char *class;
		uint8_t certainty;
		char certaintystr[8];

		if (nvlist_lookup_string(fault, ASR_FLT_CLASS, &class) != 0)
			class = NULL;
		if (nvlist_lookup_uint8(
		    fault, ASR_FLT_CERTAINTY, &certainty) != 0)
			certaintystr[0] = '\0';
		else
			(void) snprintf(certaintystr, sizeof (certaintystr),
			    "%d", certainty);

		if (nvlist_lookup_nvlist(fault, FM_FAULT_ASRU, &asru) != 0)
			asru = NULL;
		if (nvlist_lookup_nvlist(
		    fault, FM_FAULT_RESOURCE, &resource) != 0)
			resource = NULL;

		if (nvlist_lookup_nvlist(fault, FM_FAULT_FRU, &frunvl) != 0) {
			/* This must be a service fault. */
			nvlist_t *fmrinvl;
			char *name;
			char *desc;

			if (asru == NULL && resource == NULL) {
				err = asr_error(EASR_SC, "fault contained "
				    "no fru, asru, or resource");
				goto finally;
			}

			fmrinvl = asru == NULL ? resource : asru;
			if ((fmristr = asr_topo_fmri2str(
			    thp, fmrinvl)) == NULL) {
				err = ASR_FAILURE;
				goto finally;
			}
			name = asr_nvl_str(fmrinvl, "mod-name");
			if (name == NULL)
				name = asr_fmri_str_to_name(fmristr);
			desc = asr_nvl_str(fmrinvl, "mod-desc");
			if (desc == NULL)
				desc = "-";

			err = asr_buf_append_xml_elem(
			    bp, pad, "software-module");
			err |= asr_buf_append_xml_nvtoken(
			    bp, pad+1, "name", name);
			err |= asr_buf_append_xml_nv(
			    bp, pad+1, "description", desc);
			err |= asr_buf_append_xml_end(
			    bp, pad, "software-module");

			free(fmristr);

			if (err != 0)
				goto finally;

			continue;
		}

		if (nvlist_lookup_string(
		    frunvl, FM_FMRI_SCHEME, &scheme) != 0) {
			err = asr_error(EASR_SC,
			    "expected fmri to have scheme");
			goto finally;
		}

		if (strcmp(scheme, FM_FMRI_SCHEME_HC) != 0) {
			err = asr_error(EASR_SC, "expected '%s' scheme",
			    FM_FMRI_SCHEME_HC);
			goto finally;
		}

		if ((fmristr = asr_topo_fmri2str(thp, frunvl)) == NULL) {
			err = ASR_FAILURE;
			goto finally;
		}

		hc_name = asr_fmri_name(fmristr);
		hc_id = asr_fmri_str_to_name(fmristr);

		if (nvlist_lookup_string(frunvl, FM_FMRI_HC_PART,
		    &hc_part) != 0)
			hc_part = "N/A";
		if (nvlist_lookup_string(frunvl, FM_FMRI_HC_REVISION,
		    &hc_revision) != 0)
			hc_revision = "N/A";
		if (nvlist_lookup_string(frunvl, FM_FMRI_HC_SERIAL_ID,
		    &hc_serial_id) != 0)
			hc_serial_id = "N/A";

		err |= asr_buf_append_xml_elem(bp, pad++, "hardware-component");
		err |= asr_buf_append_xml_nv(bp, pad, "name", hc_name);
		err |= asr_buf_append_xml_nvtoken(bp, pad, "id", hc_id);

		if (use_schema_2_1 && frunvl != NULL) {
			nvlist_t *authority;
			if (nvlist_lookup_nvlist(
			    frunvl, FM_FAULT_RESOURCE, &authority) != 0) {
				char *chassis_model = asr_nvl_str(
				    authority, FM_FMRI_AUTH_PRODUCT);
				char *chassis_serial = asr_nvl_str(
				    authority, FM_FMRI_AUTH_CHASSIS);
				err |= asr_buf_append_xml_nv(
				    bp, pad, "chassis-model", chassis_model);
				err |= asr_buf_append_xml_nv(
				    bp, pad, "chassis-serial", chassis_serial);
			}
		}

		err |= asr_buf_append_xml_nv(bp, pad, "serial", hc_serial_id);
		err |= asr_buf_append_xml_nv(bp, pad, "part", hc_part);

		if (use_schema_2_1) {
			err |= asr_buf_append_xml_nvtoken(bp, pad,
			    "fru-revision", hc_revision);
		} else {
			err |= asr_buf_append_xml_nvtoken(bp, pad,
			    "revision", hc_revision);
		}

		if (use_schema_2_1) {
			char *hc_data;
			/* fault class: fault.cpu.intel.IOcache */
			err |= asr_buf_append_xml_nv(
			    bp, pad, "fault", asr_nvl_str(fault, FM_CLASS));
			/* location /SYS/MB/P0 */
			if (nvlist_lookup_string(
			    fault, FM_FAULT_LOCATION, &hc_data) != 0)
				err |= asr_buf_append_xml_nv(
				    bp, pad, "location", hc_data);

			/* manufacturer */
			if (nvlist_lookup_string(
			    frunvl, "manufacturer", &hc_data) != 0)
				err |= asr_buf_append_xml_nv(
				    bp, pad, "manufacturer", hc_data);
			/* model */
			if (nvlist_lookup_string(
			    frunvl, "model", &hc_data) != 0)
				err |= asr_buf_append_xml_nv(
				    bp, pad, "model", hc_data);
			/* capacity */
			if (nvlist_lookup_string(
			    frunvl, "capacity", &hc_data) != 0)
				err |= asr_buf_append_xml_nv(
				    bp, pad, "capacity", hc_data);

			/* certainty */
			err |= asr_buf_append_xml_nv(
			    bp, pad, "certainty", certaintystr);
		}

		/* Add optional additional-information elements */
		err |= asr_add_de(thp, event, pad, bp);

		asr_fmri_type(hc_name);
		err |= asr_buf_append_xml_ai(bp, pad, "type", hc_name);

		err |= asr_buf_append_xml_ai(bp, pad, "class", class);
		if (certaintystr[0] != '\0')
			err |= asr_buf_append_xml_ai(bp, pad, "certainty",
			    certaintystr);

		err |= asr_buf_append_xml_ai(bp, pad, "fmri", fmristr);

		if (asru != NULL) {
			char *asrustr = asr_topo_fmri2str(thp, asru);
			if (asrustr != NULL) {
				err |= asr_buf_append_xml_ai(
				    bp, pad, "asru", asrustr);
				free(asrustr);
			}
		}
		if (resource != NULL) {
			char *rstr = asr_topo_fmri2str(thp, resource);
			if (rstr != NULL) {
				err |= asr_buf_append_xml_ai(
				    bp, pad, "resource", rstr);
				free(rstr);
			}
		}

		err |= asr_buf_append_xml_end(bp, --pad, "hardware-component");

		free(fmristr);
		free(hc_name);
	}
finally:
	topo_close(thp);
	return (err == 0 ? ASR_OK : ASR_FAILURE);
}

/*
 * Gets the description and severity from and FMA event by looking up
 * the dictionary information for the fault code.
 */
static nvlist_t *
asr_get_fmsg_items(nvlist_t *event)
{
	fmd_msg_hdl_t *msghdl;
	nvlist_t *items;

	char *desc = asr_nvl_str(event, ASR_FLT_DESCRIPTION);
	char *sev = asr_nvl_str(event, ASR_FLT_SEVERITY);
	char *reason = asr_nvl_str(event, ASR_FLT_REASON);

	items = asr_nvl_alloc();
	if (items == NULL) {
		(void) ASR_ERROR(EASR_NOMEM);
		return (items);
	}

	if (desc != NULL || sev != NULL) {
		(void) asr_nvl_add_str(items, ASR_FLT_DESCRIPTION, desc);
		(void) asr_nvl_add_str(items, ASR_FLT_SEVERITY, sev);
		(void) asr_nvl_add_str(items, ASR_FLT_REASON, reason);
	} else {
		if ((msghdl = fmd_msg_init(NULL, FMD_MSG_VERSION)) != NULL) {
			char *desc, *sev;

			if ((desc = fmd_msg_getitem_nv(
			    msghdl, NULL, event, FMD_MSG_ITEM_DESC)) == NULL)
				(void) nvlist_add_string(
				    items, ASR_FLT_DESCRIPTION, desc);

			if ((sev = fmd_msg_getitem_nv(msghdl,
			    NULL, event, FMD_MSG_ITEM_SEVERITY)) == NULL)
				(void) nvlist_add_string(
				    items, ASR_FLT_SEVERITY, sev);
			fmd_msg_fini(msghdl);
		} else {
			(void) ASR_ERROR(EASR_NOMEM);
		}
	}

	return (items);
}

static int
asr_fault_addtime(nvlist_t *fault, char *timebuf, int tlen)
{
	struct tm *tm;
	int64_t *tv;
	uint_t tn;

	if (nvlist_lookup_int64_array(fault, FM_SUSPECT_DIAG_TIME, &tv, &tn)) {
		time_t now = time(NULL);
		if ((tm = gmtime(&now)) == NULL)
			tm = localtime(&now);
	} else {
		if ((tm = gmtime((time_t *)tv)) == NULL)
			tm = localtime((time_t *)tv);
	}
	if (strftime(timebuf, tlen, "%FT%T", tm) == 0)
		return (asr_error(EASR_FM, "fault has bad diag time"));
	return (ASR_OK);
}

/*
 * Creates an ASR event message or updates an FMA fault event.
 */
static int
do_asr_fault_msg(asr_handle_t *ah, nvlist_t *fault,
    char *parent, char *msg_name, char *event_name,
    asr_message_t **out_msg)
{
	int err;
	int pad = 1;
	int rollback;
	char *msgid, *uuid;
	char *description = NULL, *severity = NULL, *reason = NULL;
	char timebuf[64];
	nvlist_t *items = NULL;
	asr_message_t *msg = NULL;
	asr_buf_t *buf;
	nvlist_t **faults;
	uint_t nfaults;
	boolean_t use_schema_2_1 = asr_use_schema_2_1(ah);

	/* Pick out particular fields for inclusion directly in the XML. */
	if (nvlist_lookup_string(fault, FM_SUSPECT_DIAG_CODE, &msgid) != 0)
		return (asr_error(EASR_FM, "fault missing diag code"));

	if (nvlist_lookup_string(fault, FM_SUSPECT_UUID, &uuid))
		return (asr_error(EASR_FM, "fault missing uuid"));

	asr_log_debug(ah, "Creating ASR fault message for UUID=%s", uuid);

	if (asr_fault_addtime(fault, timebuf, sizeof (timebuf))) {
		return (asr_error(EASR_FM, "fault missing diag time"));
	}

	/*
	 * Look up Severity and Descripiton values.
	 */
	items = asr_get_fmsg_items(fault);
	if (items != NULL) {
		(void) nvlist_lookup_string(
		    items, ASR_FLT_SEVERITY, &severity);
		(void) nvlist_lookup_string(
		    items, ASR_FLT_DESCRIPTION, &description);
		(void) nvlist_lookup_string(
		    items, ASR_FLT_REASON, &reason);
	}
	severity = asr_sev_from_fma_sev(severity);
	if (description == NULL)
		description = "-";
	if (reason == NULL)
		reason = description;

	if ((buf = asr_buf_alloc(4096)) == NULL) {
		err = asr_set_errno(EASR_NOMEM);
		goto finally;
	}

	err = asr_msg_start(ah, buf);
	err |= asr_buf_append_xml_elem(buf, pad, msg_name);
	pad++;
	err |= asr_buf_append_xml_elem(buf, pad, event_name);
	pad++;
	err |= asr_buf_append_xml_nvtoken(buf, pad, "message-id", msgid);
	err |= asr_buf_append_xml_nvtoken(buf, pad, "event-uuid", uuid);
	err |= asr_buf_append_xml_anv(buf, pad,
	    "timezone", "UTC", "event-time", timebuf);
	err |= asr_buf_append_xml_nv(buf, pad, "severity", severity);

	if (nvlist_lookup_nvlist_array(
	    fault, FM_SUSPECT_FAULT_LIST, &faults, &nfaults) != 0)
		nfaults = 0;

	if (use_schema_2_1) {
		char count[32];
		char *eventType = NULL;
		uint32_t case_state = 0;

		if (nvlist_lookup_uint32(
		    fault, FM_SUSPECT_CASE_STATE, &case_state) == 0) {
			switch (case_state) {
			case FMD_SUSPECT_CASE_SOLVED:
				eventType = "SOLVED";
				break;
				case FMD_SUSPECT_CASE_CLOSE_WAIT:
				eventType = "CLOSE_WAIT";
				break;
				case FMD_SUSPECT_CASE_CLOSED:
				eventType = "CLOSED";
				break;
				case FMD_SUSPECT_CASE_REPAIRED:
				eventType = "REPAIRED";
				break;
				case FMD_SUSPECT_CASE_RESOLVED:
				eventType = "RESOLVED";
				break;
			}
		}
		err |= asr_buf_append_xml_nv(buf, pad, "event-type", eventType);

		(void) snprintf(count, sizeof (count), "%d",
		    nfaults == 0 ? 1 : nfaults);
		err |= asr_buf_append_xml_nv(
		    buf, pad, "component-count", count);
	}

	/* Add info about faulted components. */
	err |= asr_buf_append_xml_elem(buf, pad, "component");
	rollback = buf->asrb_length;
	if (asr_fault_components(ah, buf, faults, nfaults, fault)) {
		if (asr_get_errno() != EASR_SC && asr_get_errno() != EASR_FM) {
			err = ASR_FAILURE;
			goto finally;
		}
		buf->asrb_length = rollback;
		err |= asr_buf_terminate(buf);
		pad++;
		err |= asr_buf_append_xml_nv(buf, pad+1, "uncategorized", "");
		pad--;
	}
	err |= asr_buf_append_xml_end(buf, pad, "component");

	err |= asr_buf_append_xml_nnv(buf, pad, "summary", reason, 80);

	if (use_schema_2_1) {
		nvlist_t *de;
		err |= asr_buf_append_xml_nv(
		    buf, pad, "description", description);

		if (nvlist_lookup_nvlist(
		    fault, FM_SUSPECT_DE, &de) == 0) {
			err |= asr_buf_append_xml_nv(
			    buf, pad, "diagnostic-engine-name",
			    asr_nvl_strd(de, "mod-name", "fmd"));
			err |= asr_buf_append_xml_nv(
			    buf, pad, "diagnostic-engine-version",
			    asr_nvl_strd(de, "mod-version", NULL));
		}
	} else {
		err |= asr_buf_append_xml_nnv(
		    buf, pad, "description", description, 80);
	}

	err |= asr_buf_append_pad(buf, pad);
	err |= asr_buf_append_str(buf, "<payload><![CDATA[");
	asr_nvl_tostringi(buf, fault, 1, '\'', " => ");
	err |= asr_buf_append_str(buf, "]]>\n");
	err |= asr_buf_append_xml_end(buf, pad, "payload");
	err |= asr_buf_append_xml_nv(buf, pad, "parent-event-uuid", parent);
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, event_name);
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, msg_name);
	err |= asr_msg_end(buf);

finally:
	if (err == 0) {
		if ((msg = asr_message_alloc(buf, ASR_MSG_FAULT)) == NULL)
			err = EASR_NOMEM;
	} else {
		asr_buf_free(buf);
	}

	asr_nvl_free(items);
	*out_msg = msg;
	return (err);
}

/*
 * Creates an ASR event message from an FMA fault event.
 */
int
asr_fault(asr_handle_t *ah, nvlist_t *fault, asr_message_t **msg)
{
	return (do_asr_fault_msg(
	    ah, fault, NULL, "event", "primary-event-information", msg));
}

/*
 * Creates an ASR event message that updates an FMA fault event.
 * This type of message is used to identify that there are updates to an
 * already existing event.
 * Event UUIDs are used to identify the parent events.
 */
int
asr_fault_update(asr_handle_t *ah, nvlist_t *fault, char *parent,
    asr_message_t **msg)
{
	return (do_asr_fault_msg(
	    ah, fault, parent, "event-update", "child-event-information", msg));
}
