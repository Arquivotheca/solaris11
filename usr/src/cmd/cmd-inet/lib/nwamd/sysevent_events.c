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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <errno.h>
#include <libdllink.h>
#include <librcm.h>
#include <libsysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/dev.h>
#include <sys/types.h>
#include <libnvpair.h>
#include <string.h>
#include <unistd.h>

#include "events.h"
#include "ncp.h"
#include "ncu.h"
#include "objects.h"
#include "util.h"

/*
 * sysevent_events.c - this file contains routines to retrieve sysevents
 * from the system and package them for high level processing.
 */

static sysevent_handle_t *sysevent_handle;

/*
 * Handle EC_DEV_REMOVE sysevents of subclass ESC_NETWORK.  These signify
 * hotplug device removal.  We:
 * - extract the driver/instance sysevent attribute
 * - combine these to get the device name
 * - find the linkname for the device from libdladm
 * - create a REMOVE action event to remove the NCUs from the Automatic NCP
 */
static void
dev_sysevent_handler(nvlist_t *nvl)
{
	int32_t instance;
	char *driver, dev_name[LIFNAMSIZ], linkname[LIFNAMSIZ];
	datalink_id_t linkid;
	dladm_status_t status;
	char errstr[DLADM_STRSIZE];
	nwamd_event_t link_event = NULL;
	int err;

	/*
	 * Retrieve driver name and instance attributes, and combine to
	 * get interface name.
	 */
	if ((err = nvlist_lookup_string(nvl, DEV_DRIVER_NAME, &driver)) != 0 ||
	    (err = nvlist_lookup_int32(nvl, DEV_INSTANCE, &instance)) != 0) {
		nlog(LOG_ERR, "dev_sysevent_handler: "
		    "nvlist_lookup of attributes failed: %s", strerror(err));
		return;
	}
	(void) snprintf(dev_name, LIFNAMSIZ, "%s%d", driver, instance);

	/* Convert the device name to linkname that nwamd uses */
	if ((status = dladm_dev2linkid(dld_handle, dev_name, &linkid))
	    != DLADM_STATUS_OK) {
		nlog(LOG_ERR, "dev_sysevent_handler: "
		    "could not retrieve linkid from device name %s: %s",
		    dev_name, dladm_status2str(status, errstr));
		return;
	}
	if ((status = dladm_datalink_id2info(dld_handle, linkid, NULL, NULL,
	    NULL, linkname, sizeof (linkname))) != DLADM_STATUS_OK) {
		nlog(LOG_ERR, "dev_sysevent_handler: "
		    "could not retrieve linkname from linkid %d for %s: %s",
		    linkid, dev_name, dladm_status2str(status, errstr));
		return;
	}

	/* Ignore sysevent events for links that nwamd should ignore */
	if (nwamd_ignore_link(linkname))
		return;

	/* Create event for link */
	link_event = nwamd_event_init_link_action(linkname, NWAM_ACTION_REMOVE);
	if (link_event != NULL)
		nwamd_event_enqueue(link_event);
}

/*
 * Handle EC_DATALINK sysevents of subclass ESC_DATALINK_PHYS_ADD.  This
 * signifies a link was added by dlmgmtd.  We create an ADD action event to
 * create link and IP NCUs for the link in the Automatic NCP.
 */
static void
datalink_sysevent_handler(nvlist_t *nvl)
{
	char link[LIFNAMSIZ];
	uint64_t linkid;
	dladm_status_t status;
	char errstr[DLADM_STRSIZE];
	int err;
	nwamd_event_t link_event = NULL;

	/* Retrieve the linkid from the sysevent */
	if ((err = nvlist_lookup_uint64(nvl, RCM_NV_LINKID, &linkid)) != 0) {
		nlog(LOG_ERR, "datalink_sysevent_handler: "
		    "nvlist_lookup of attributes failed: %s", strerror(err));
		return;
	}

	if ((status = dladm_datalink_id2info(dld_handle,
	    (datalink_id_t)linkid, NULL, NULL, NULL, link, sizeof (link)))
	    != DLADM_STATUS_OK) {
		nlog(LOG_ERR, "datalink_sysevent_handler: "
		    "could not retrieve the linkname from linkid %d: %s",
		    linkid, dladm_status2str(status, errstr));
		return;
	}

	if (nwamd_ignore_link(link))
		return;

	link_event = nwamd_event_init_link_action(link, NWAM_ACTION_ADD);
	if (link_event != NULL)
		nwamd_event_enqueue(link_event);
}

static void
sysevent_handler(sysevent_t *ev)
{
	nvlist_t *attr_list;
	char *class = sysevent_get_class_name(ev);
	char *subclass = sysevent_get_subclass_name(ev);

	nlog(LOG_DEBUG, "sysevent_handler: event %s/%s", class, subclass);

	/* Get the attribute list pointer */
	if (sysevent_get_attr_list(ev, &attr_list) != 0) {
		nlog(LOG_ERR, "sysevent_handler: sysevent_get_attr_list: %s",
		    strerror(errno));
		return;
	}

	if (strcmp(class, EC_DEV_REMOVE) == 0 &&
	    strcmp(subclass, ESC_NETWORK) == 0) {
		dev_sysevent_handler(attr_list);
	} else if (strcmp(class, EC_DATALINK) == 0 &&
	    strcmp(subclass, ESC_DATALINK_PHYS_ADD) == 0) {
		datalink_sysevent_handler(attr_list);
	} else {
		nlog(LOG_ERR, "sysevent_handler: unexpected sysevent "
		    "class/subclass %s/%s", class, subclass);
	}

	nvlist_free(attr_list);
}

/*
 * Subscribe to the following class/subclass of sysevents:
 * - EC_DEV_REMOVE/ESC_NETWORK
 *	hotplug remove events
 * - EC_DATALINK/ESC_DATALINK_PHYS_ADD
 *	new physical link events sent by dlmgmtd
 */
/* ARGSUSED0 */
static void *
sysevent_initialization(void *arg)
{
	const char *subclass1 = ESC_NETWORK, *subclass2 = ESC_DATALINK_PHYS_ADD;
	boolean_t success = B_FALSE;

retry:
	/* Try to bind to the subscriber handle until successful */
	do {
		nwamd_become_root();
		sysevent_handle = sysevent_bind_handle(sysevent_handler);
		nwamd_release_root();
		(void) sleep(1);
	} while (sysevent_handle == NULL);

	nwamd_become_root();
	if (sysevent_subscribe_event(sysevent_handle, EC_DEV_REMOVE, &subclass1,
	    1) != 0 ||
	    sysevent_subscribe_event(sysevent_handle, EC_DATALINK, &subclass2,
	    1) != 0) {
		nlog(LOG_DEBUG,
		    "sysevent_subscribe_event failed: %s, retrying..",
		    strerror(errno));
		sysevent_unbind_handle(sysevent_handle);
	} else {
		success = B_TRUE;
	}
	nwamd_release_root();

	/*
	 * If we are not able to subscribe to the sysevents, wait a while and
	 * retry.
	 */
	if (!success) {
		(void) sleep(1);
		goto retry;
	}

	/*
	 * Walk the physical configuration again.  On first boot, if links
	 * have not been registered with dlmgmtd, then the first walk of links
	 * will not find any links.  There is a window between the first walk
	 * and sysevent initialization completing.  This second walk of the
	 * links will ensure that if links have been added by dlmgmtd, they
	 * will be found by nwamd to add to the Automatic NCP.
	 */
	nwamd_walk_physical_configuration();

	return (NULL);
}

/*
 * We can't initialize in the main thread because we may need to wait until
 * svc:/system/sysevent:default finishes starting up.  So we create a thread to
 * initialize in.
 */
void
nwamd_sysevent_events_init(void)
{
	int rc;
	pthread_attr_t attr;

	rc = pthread_attr_init(&attr);
	if (rc != 0) {
		pfail("nwamd_sysevents_init: pthread_attr_init failed: %s",
		    strerror(rc));
	}

	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		pfail("nwamd_sysevents_init: pthread_attr_setdetachstate "
		    "failed: %s", strerror(rc));
	}

	rc = pthread_create(NULL, &attr, sysevent_initialization, NULL);
	if (rc != 0) {
		pfail("nwamd_sysevents_init: couldn't start sysevent init "
		    "thread: %s", strerror(rc));
	}

	(void) pthread_attr_destroy(&attr);
}

void
nwamd_sysevent_events_fini(void)
{
	if (sysevent_handle != NULL) {
		nwamd_become_root();
		sysevent_unbind_handle(sysevent_handle);
		nwamd_release_root();
	}
	sysevent_handle = NULL;
}
