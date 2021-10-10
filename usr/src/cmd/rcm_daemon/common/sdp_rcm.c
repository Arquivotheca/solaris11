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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This RCM module adds support to the RCM framework for SDP connections
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <synch.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <strings.h>
#include <stropts.h>
#include <inet/sdp_itf.h>
#include <libintl.h>
#include <sys/ib/ibnex/ibnex_devctl.h>
#include "rcm_module.h"

/*
 * Definitions
 */
#ifndef lint
#define	_(x)	gettext(x)
#else
#define	_(x)	x
#endif

/* SDP Cache state flags */
typedef enum {
	CACHE_NODE_STALE		= 0x01,	/* stale cached data */
	CACHE_NODE_NEW			= 0x02,	/* new cached nodes */
	CACHE_NODE_OFFLINED		= 0x04	/* node offlined */
} cache_node_state_t;

/* Network Cache lookup options */
#define	CACHE_NO_REFRESH	0x1		/* cache refresh not needed */
#define	CACHE_REFRESH		0x2		/* refresh cache */

/*
 * Cache element. It is used to keep a list of HCAs on the system
 */
typedef struct hca_cache {
	struct hca_cache	*vc_next;	/* next cached resource */
	struct hca_cache	*vc_prev;	/* prev cached resource */
	char			vc_resource[MAXPATHLEN]; /* resource name */
	ib_guid_t		vc_guid;	/* guid of the HCA */
	cache_node_state_t	vc_state;	/* cache state flags */
} hca_cache_t;

/*
 * Global cache for HCAs
 */
static hca_cache_t	cache_head;
static hca_cache_t	cache_tail;
static mutex_t		cache_lock;

/*
 * RCM module interface prototypes
 */
static int		sdp_register(rcm_handle_t *);
static int		sdp_unregister(rcm_handle_t *);
static int		sdp_get_info(rcm_handle_t *, char *, id_t, uint_t,
			    char **, char **, nvlist_t *, rcm_info_t **);
static int		sdp_suspend(rcm_handle_t *, char *, id_t,
			    timespec_t *, uint_t, char **, rcm_info_t **);
static int		sdp_resume(rcm_handle_t *, char *, id_t, uint_t,
			    char **, rcm_info_t **);
static int		sdp_offline(rcm_handle_t *, char *, id_t, uint_t,
			    char **, rcm_info_t **);
static int		sdp_undo_offline(rcm_handle_t *, char *, id_t, uint_t,
			    char **, rcm_info_t **);
static int		sdp_remove(rcm_handle_t *, char *, id_t, uint_t,
			    char **, rcm_info_t **);

/* Module private routines */
static int 		cache_update(rcm_handle_t *);
static void 		cache_remove(hca_cache_t *);
static void 		cache_insert(hca_cache_t *);
static void 		node_free(hca_cache_t *);
static hca_cache_t	*cache_lookup(rcm_handle_t *, char *, char);
static int		sdp_offline_hca(hca_cache_t *, boolean_t, boolean_t);
static int		sdp_online_hca(hca_cache_t *);
static void 		sdp_log_err(char *, char **, char *);

/* Module-Private data */
static struct rcm_mod_ops sdp_ops =
{
	RCM_MOD_OPS_VERSION,
	sdp_register,
	sdp_unregister,
	sdp_get_info,
	sdp_suspend,
	sdp_resume,
	sdp_offline,
	sdp_undo_offline,
	sdp_remove,
	NULL,
	NULL,
	NULL
};

/*
 * rcm_mod_init() - Update registrations, and return the ops structure.
 */
struct rcm_mod_ops *
rcm_mod_init(void)
{
	rcm_log_message(RCM_TRACE1, "SDP: mod_init\n");

	cache_head.vc_next = &cache_tail;
	cache_head.vc_prev = NULL;
	cache_tail.vc_prev = &cache_head;
	cache_tail.vc_next = NULL;
	(void) mutex_init(&cache_lock, 0, NULL);

	/* Return the ops vectors */
	return (&sdp_ops);
}

/*
 * rcm_mod_info() - Return a string describing this module.
 */
const char *
rcm_mod_info(void)
{
	rcm_log_message(RCM_TRACE1, "SDP: mod_info\n");

	return ("SDP module version 1.1");
}

/*
 * rcm_mod_fini() - Destroy the network SDP cache.
 */
int
rcm_mod_fini(void)
{
	hca_cache_t *node;

	rcm_log_message(RCM_TRACE1, "SDP: mod_fini\n");

	/*
	 * We free module private data structures here.
	 * The unregister entry point is called separately
	 * and directly by the RCM frame work.
	 */
	(void) mutex_lock(&cache_lock);
	node = cache_head.vc_next;
	while (node != &cache_tail) {
		cache_remove(node);
		node_free(node);
		node = cache_head.vc_next;
	}
	(void) mutex_unlock(&cache_lock);
	(void) mutex_destroy(&cache_lock);

	return (RCM_SUCCESS);
}

/*
 * sdp_register() - Make sure the cache is properly sync'ed, and its
 *		 registrations are in order.
 */
static int
sdp_register(rcm_handle_t *hd)
{
	rcm_log_message(RCM_TRACE1, "SDP: register\n");

	if (cache_update(hd) < 0)
		return (RCM_FAILURE);

	return (RCM_SUCCESS);
}

/*
 * sdp_unregister() - Walk the cache, unregistering all the networks.
 */
static int
sdp_unregister(rcm_handle_t *hd)
{
	hca_cache_t *node;

	rcm_log_message(RCM_TRACE1, "SDP: unregister\n");

	/* Walk the cache, unregistering everything */
	(void) mutex_lock(&cache_lock);
	node = cache_head.vc_next;
	while (node != &cache_tail) {
		if (rcm_unregister_interest(hd, node->vc_resource, 0)
		    != RCM_SUCCESS) {
			/* unregister failed for whatever reason */
			rcm_log_message(RCM_ERROR,
			    _("SDP: failed to unregister %s\n"),
			    node->vc_resource);
			(void) mutex_unlock(&cache_lock);
			return (RCM_FAILURE);
		}
		cache_remove(node);
		node_free(node);
		node = cache_head.vc_next;
	}
	(void) mutex_unlock(&cache_lock);
	return (RCM_SUCCESS);
}

/*
 * sdp_offline() - Offline SDPs connections on a specific link.
 */
static int
sdp_offline(rcm_handle_t *hd, char *rsrc, id_t id, uint_t flags,
    char **errorp, rcm_info_t **depend_info)
{
	hca_cache_t *node;

	rcm_log_message(RCM_TRACE1, "SDP: offline(%s)\n", rsrc);

	/* Lock the cache and lookup the resource */
	(void) mutex_lock(&cache_lock);
	node = cache_lookup(hd, rsrc, CACHE_REFRESH);
	if (node == NULL) {
		/* should not happen because the resource is registered. */
		sdp_log_err(rsrc, errorp, "offline, unrecognized resource");
		(void) mutex_unlock(&cache_lock);
		return (RCM_SUCCESS);
	}

	if (sdp_offline_hca(node, flags & RCM_FORCE, flags & RCM_QUERY) !=
	    RCM_SUCCESS) {
		sdp_log_err(rsrc, errorp, "offline HCA failed");
		(void) mutex_unlock(&cache_lock);
		return (RCM_FAILURE);
	}

	rcm_log_message(RCM_TRACE1, "SDP: Offline succeeded(%s)\n", rsrc);
	(void) mutex_unlock(&cache_lock);
	return (RCM_SUCCESS);
}

/*
 * sdp_undo_offline() - Undo offline of a previously offlined link.
 */
/*ARGSUSED*/
static int
sdp_undo_offline(rcm_handle_t *hd, char *rsrc, id_t id, uint_t flags,
    char **errorp, rcm_info_t **depend_info)
{
	hca_cache_t *node;

	rcm_log_message(RCM_TRACE1, "SDP: online(%s)\n", rsrc);

	(void) mutex_lock(&cache_lock);
	node = cache_lookup(hd, rsrc, CACHE_NO_REFRESH);
	if (node == NULL) {
		sdp_log_err(rsrc, errorp,
		    "undo offline, unrecognized resource");
		(void) mutex_unlock(&cache_lock);
		errno = ENOENT;
		return (RCM_FAILURE);
	}

	/*
	 * Since we cannot restore the SDP connections that were previously
	 * tear down, do nothing
	 */
	if (sdp_online_hca(node) != RCM_SUCCESS) {
		sdp_log_err(rsrc, errorp, "online HCA failed");
		(void) mutex_unlock(&cache_lock);
		return (RCM_FAILURE);
	}

	rcm_log_message(RCM_TRACE1, "SDP: online succeeded(%s)\n", rsrc);
	(void) mutex_unlock(&cache_lock);
	return (RCM_SUCCESS);
}

static int
sdp_offline_hca(hca_cache_t *node, boolean_t force, boolean_t query)
{
	struct strioctl str;
	int fd;
	sdp_ioc_hca_t sih;

	rcm_log_message(RCM_TRACE2, "SDP: sdp_offline_hca %s force=%s "
	    "query=%s\n", node->vc_resource, force ? "true" : "false",
	    query ? "true" : "false");

	if ((fd = open("/dev/sdpib", O_RDWR | O_NONBLOCK)) == -1) {
		rcm_log_message(RCM_WARNING,
		    _("SDP (%s) offline failed: failed to open sdpib node\n"),
		    node->vc_resource);
		return (RCM_FAILURE);
	}

	sih.sih_guid = node->vc_guid;
	sih.sih_force = force;
	sih.sih_query = query;
	sih.sih_offline = B_TRUE;

	str.ic_cmd = SDP_IOC_HCA;
	str.ic_timout = INFTIM;
	str.ic_len = sizeof (sih);
	str.ic_dp = (char *)&sih;
	if (ioctl(fd, I_STR, &str) == -1) {
		rcm_log_message(RCM_WARNING, _("SDP (%s) offline failed: %s\n"),
		    node->vc_resource, strerror(errno));
		(void) close(fd);
		return (RCM_FAILURE);
	}

	(void) close(fd);
	rcm_log_message(RCM_TRACE1,
	    "SDP (%s) offline succeeded\n", node->vc_resource);
	node->vc_state |= CACHE_NODE_OFFLINED;
	return (RCM_SUCCESS);
}

static int
sdp_online_hca(hca_cache_t *node)
{
	struct strioctl str;
	int fd;
	sdp_ioc_hca_t sih;

	rcm_log_message(RCM_TRACE2, "SDP: sdp_online_hca %s\n",
	    node->vc_resource);

	if ((fd = open("/dev/sdpib", O_RDWR | O_NONBLOCK)) == -1) {
		rcm_log_message(RCM_WARNING,
		    _("SDP (%s) offline failed: failed to open sdpib node\n"),
		    node->vc_resource);
		return (RCM_FAILURE);
	}

	sih.sih_guid = node->vc_guid;
	sih.sih_offline = B_FALSE;

	str.ic_cmd = SDP_IOC_HCA;
	str.ic_timout = INFTIM;
	str.ic_len = sizeof (sih);
	str.ic_dp = (char *)&sih;
	if (ioctl(fd, I_STR, &str) == -1) {
		rcm_log_message(RCM_WARNING, _("SDP (%s) online failed: %s\n"),
		    node->vc_resource, strerror(errno));
		(void) close(fd);
		return (RCM_FAILURE);
	}

	(void) close(fd);
	rcm_log_message(RCM_TRACE1,
	    "SDP (%s) online succeeded\n", node->vc_resource);
	node->vc_state &= ~CACHE_NODE_OFFLINED;
	return (RCM_SUCCESS);
}

/*
 * sdp_get_info() - Gather usage information for this resource.
 */
/*ARGSUSED*/
int
sdp_get_info(rcm_handle_t *hd, char *rsrc, id_t id, uint_t flags,
    char **usagep, char **errorp, nvlist_t *props, rcm_info_t **depend_info)
{
	hca_cache_t *node;
	int bufsize;

	rcm_log_message(RCM_TRACE1, "SDP: get_info(%s)\n", rsrc);

	(void) mutex_lock(&cache_lock);
	node = cache_lookup(hd, rsrc, CACHE_REFRESH);
	if (node == NULL) {
		rcm_log_message(RCM_INFO,
		    _("SDP: get_info(%s) unrecognized resource\n"), rsrc);
		(void) mutex_unlock(&cache_lock);
		errno = ENOENT;
		return (RCM_FAILURE);
	}
	(void) mutex_unlock(&cache_lock);

	bufsize = strlen("SDP connection(s) over IB HCA ") + MAXPATHLEN;
	if ((*usagep = malloc(bufsize)) == NULL) {
		/* most likely malloc failure */
		rcm_log_message(RCM_ERROR,
		    _("SDP: getinfo(%s) malloc failure\n"), rsrc);
		errno = ENOMEM;
		return (RCM_FAILURE);
	}

	(void) snprintf(*usagep, bufsize, "SDP connection(s) over IB HCA ",
	    rsrc);
	/* Set client/role properties */
	(void) nvlist_add_string(props, RCM_CLIENT_NAME, "SDP");
	rcm_log_message(RCM_TRACE1, "SDP: getinfo(%s) info = %s\n",
	    rsrc, *usagep);
	return (RCM_SUCCESS);
}

/*
 * sdp_suspend() - Nothing to do, always okay
 */
/*ARGSUSED*/
static int
sdp_suspend(rcm_handle_t *hd, char *rsrc, id_t id, timespec_t *interval,
    uint_t flags, char **errorp, rcm_info_t **depend_info)
{
	rcm_log_message(RCM_TRACE1, "SDP: suspend(%s)\n", rsrc);
	return (RCM_SUCCESS);
}

/*
 * sdp_resume() - Nothing to do, always okay
 */
/*ARGSUSED*/
static int
sdp_resume(rcm_handle_t *hd, char *rsrc, id_t id, uint_t flags,
    char **errorp, rcm_info_t **depend_info)
{
	rcm_log_message(RCM_TRACE1, "SDP: resume(%s)\n", rsrc);
	return (RCM_SUCCESS);
}

/*
 * sdp_remove() - remove a resource from cache
 */
/*ARGSUSED*/
static int
sdp_remove(rcm_handle_t *hd, char *rsrc, id_t id, uint_t flags,
    char **errorp, rcm_info_t **depend_info)
{
	hca_cache_t *node;
	int rv = RCM_SUCCESS;

	rcm_log_message(RCM_TRACE1, "SDP: remove(%s)\n", rsrc);

	(void) mutex_lock(&cache_lock);
	node = cache_lookup(hd, rsrc, CACHE_NO_REFRESH);
	if (node == NULL) {
		rcm_log_message(RCM_INFO,
		    _("SDP: remove(%s) unrecognized resource\n"), rsrc);
		(void) mutex_unlock(&cache_lock);
		errno = ENOENT;
		return (RCM_FAILURE);
	}

	/* remove the cached entry for the resource */
	cache_remove(node);
	(void) mutex_unlock(&cache_lock);

	node_free(node);
	return (rv);
}

/*
 * Cache management routines, all cache management functions should be
 * be called with cache_lock held.
 */

/*
 * cache_lookup() - Get a cache node for a resource.
 *		  Call with cache lock held.
 *
 * This ensures that the cache is consistent with the system state and
 * returns a pointer to the cache element corresponding to the resource.
 */
static hca_cache_t *
cache_lookup(rcm_handle_t *hd, char *rsrc, char options)
{
	hca_cache_t *node;

	rcm_log_message(RCM_TRACE2, "SDP: cache lookup(%s)\n", rsrc);
	assert(MUTEX_HELD(&cache_lock));

	if (options & CACHE_REFRESH) {
		/* drop lock since update locks cache again */
		(void) mutex_unlock(&cache_lock);
		(void) cache_update(hd);
		(void) mutex_lock(&cache_lock);
	}

	node = cache_head.vc_next;
	for (; node != &cache_tail; node = node->vc_next) {
		if (strcmp(rsrc, node->vc_resource) == 0) {
			rcm_log_message(RCM_TRACE2,
			    "SDP: cache lookup succeeded(%s)\n", rsrc);
			return (node);
		}
	}
	return (NULL);
}

/*
 * node_free - Free a node from the cache
 */
static void
node_free(hca_cache_t *node)
{
	free(node);
}

/*
 * cache_insert - Insert a resource node in cache
 */
static void
cache_insert(hca_cache_t *node)
{
	assert(MUTEX_HELD(&cache_lock));

	/* insert at the head for best performance */
	node->vc_next = cache_head.vc_next;
	node->vc_prev = &cache_head;

	node->vc_next->vc_prev = node;
	node->vc_prev->vc_next = node;
}

/*
 * cache_remove() - Remove a resource node from cache.
 *		  Call with the cache_lock held.
 */
static void
cache_remove(hca_cache_t *node)
{
	assert(MUTEX_HELD(&cache_lock));
	node->vc_next->vc_prev = node->vc_prev;
	node->vc_prev->vc_next = node->vc_next;
	node->vc_next = NULL;
	node->vc_prev = NULL;
}

#define	SLASH_DEVICES	"/devices"

/*
 * sdp_update_all_hca() - Determine all IB HCAs on the system
 */
static int
sdp_update_all_hca(rcm_handle_t *hd)
{
	int fd, nhcas, i, ret = -1;
	ibnex_ctl_get_hca_list_t hca_list;
	ibnex_ctl_query_hca_t query_hca;
	ib_guid_t *hca_guidp;
	char rsrc[MAXPATHLEN];

	assert(MUTEX_HELD(&cache_lock));

	rcm_log_message(RCM_TRACE2, "SDP: sdp_update_all_hca\n");
	if ((fd = open(IBNEX_DEVCTL_DEV, O_RDONLY)) == -1) {
		/*
		 * don't log error just in case if the IB pkgs are not
		 * installed
		 */
		return (-1);
	}

	/*
	 * First, get the list of HCAs.
	 */
	bzero(&hca_list, sizeof (hca_list));

	/*
	 * The first pass of calling IBNEX_CTL_GET_HCA_LIST is for getting the
	 * number of HCAs present in the system. In the second pass
	 * we allocate memory for hca guids and call the ioctl again.
	 */
again:
	nhcas = hca_list.nhcas;
	hca_list.hca_guids_alloc_sz = nhcas;

	hca_list.hca_guids = NULL;
	if (nhcas > 0 && ((hca_list.hca_guids =
	    malloc(nhcas * sizeof (ib_guid_t))) == NULL)) {
		rcm_log_message(RCM_ERROR,
		    "SDP: failed to allocate memory: %s\n", strerror(errno));
		(void) close(fd);
		return (-1);
	}

	if (ioctl(fd, IBNEX_CTL_GET_HCA_LIST, &hca_list) == -1) {
		rcm_log_message(RCM_ERROR, "SDP: failed to get hca list: %s\n",
		    strerror(errno));
		(void) close(fd);
		return (-1);
	}

	if (hca_list.nhcas != nhcas) {
		if (hca_list.hca_guids)
			free(hca_list.hca_guids);
		goto again;
	}

	if (nhcas == 0) {
		assert(hca_list.hca_guids == NULL);
		rcm_log_message(RCM_TRACE1, "SDP: no hca found\n");
		(void) close(fd);
		return (0);
	}

	(void) strlcpy(rsrc, SLASH_DEVICES, sizeof (rsrc));
	for (i = 0, hca_guidp = hca_list.hca_guids; i < nhcas;
	    i++, hca_guidp++) {
		hca_cache_t *node;

		query_hca.hca_guid = *hca_guidp;
		query_hca.hca_device_path = rsrc + strlen(SLASH_DEVICES);
		query_hca.hca_device_path_alloc_sz = MAXPATHLEN -
		    strlen(SLASH_DEVICES);

		if (ioctl(fd, IBNEX_CTL_QUERY_HCA, &query_hca) == -1) {
			rcm_log_message(RCM_ERROR,
			    "SDP: failed to query hca: %s\n", strerror(errno));
			goto out;
		}

		node = cache_lookup(hd, rsrc, CACHE_NO_REFRESH);
		if (node != NULL) {
			rcm_log_message(RCM_DEBUG,
			    "SDP: %s already registered\n", rsrc);
			node->vc_state &= ~CACHE_NODE_STALE;
			assert(node->vc_guid == *hca_guidp);
		} else {
			rcm_log_message(RCM_DEBUG,
			    "SDP: %s is a new resource\n", rsrc);

			node = calloc(1, sizeof (hca_cache_t));
			if (node == NULL) {
				rcm_log_message(RCM_ERROR,
				    _("SDP: calloc: %s\n"), strerror(errno));
				goto out;
			}
			(void) strlcpy(node->vc_resource, rsrc, MAXPATHLEN);
			node->vc_guid = *hca_guidp;
			node->vc_state |= CACHE_NODE_NEW;
			cache_insert(node);
		}
	}
out:
	ret = 0;
	assert(hca_list.hca_guids != NULL);
	free(hca_list.hca_guids);

	(void) close(fd);
	return (ret);
}

/*
 * cache_update() - Update cache with latest interface info
 */
static int
cache_update(rcm_handle_t *hd)
{
	hca_cache_t *node, *next;
	int ret = 0;

	rcm_log_message(RCM_TRACE2, "SDP: cache_update\n");
	(void) mutex_lock(&cache_lock);

	/* walk the entire cache, marking each entry stale */
	node = cache_head.vc_next;
	for (; node != &cache_tail; node = node->vc_next)
		node->vc_state |= CACHE_NODE_STALE;

	ret = sdp_update_all_hca(hd);

	/*
	 * Even sdp_update_all_hca() fails, continue to delete all the stale
	 * resources. First, unregister links that are not offlined and
	 * still in cache.
	 */
	for (node = cache_head.vc_next; node != &cache_tail; node = next) {
		next = node->vc_next;
		if (node->vc_state & CACHE_NODE_STALE) {
			(void) rcm_unregister_interest(hd, node->vc_resource,
			    0);
			rcm_log_message(RCM_DEBUG,
			    "SDP: unregistered %s\n", node->vc_resource);
			cache_remove(node);
			node_free(node);
			continue;
		}

		if (!(node->vc_state & CACHE_NODE_NEW))
			continue;

		if (rcm_register_interest(hd, node->vc_resource, 0,
		    NULL) != RCM_SUCCESS) {
			rcm_log_message(RCM_ERROR,
			    _("SDP: failed to register %s\n"),
			    node->vc_resource);
			ret = -1;
		} else {
			rcm_log_message(RCM_DEBUG, "SDP: registered %s\n",
			    node->vc_resource);

			node->vc_state &= ~CACHE_NODE_NEW;
		}
	}
done:
	(void) mutex_unlock(&cache_lock);
	return (ret);
}

/*
 * sdp_log_err() - RCM error log wrapper
 */
static void
sdp_log_err(char *rsrc, char **errorp, char *errmsg)
{
	int len;
	const char *errfmt;
	char *error;

	rcm_log_message(RCM_ERROR, _("SDP: %s(%s)\n"), errmsg, rsrc);

	errfmt = _("SDP: %s(%s)");
	len = strlen(errfmt) + strlen(errmsg) + MAXPATHLEN + 1;
	if ((error = malloc(len)) != NULL) {
		(void) sprintf(error, errfmt, errmsg, rsrc);
	}

	if (errorp != NULL)
		*errorp = error;
}
