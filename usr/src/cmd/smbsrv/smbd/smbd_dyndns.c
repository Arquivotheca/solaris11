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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/list.h>
#include <strings.h>
#include <errno.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include "smbd.h"

#define	DYNDNS_OP_CLEAR			1
#define	DYNDNS_OP_UPDATE		2

#define	DYNDNS_STATE_INIT		0
#define	DYNDNS_STATE_READY		1
#define	DYNDNS_STATE_PUBLISHING		2
#define	DYNDNS_STATE_STOPPING		3

typedef struct dyndns_qentry {
	list_node_t	dqe_lnd;
	int		dqe_op;
	/* DNS zone name must be fully qualified and in lower case */
	char		dqe_zone[MAXHOSTNAMELEN];
} dyndns_qentry_t;

typedef struct dyndns_queue {
	list_t		ddq_list;
	mutex_t		ddq_mtx;
	cond_t		ddq_cv;
	uint32_t	ddq_state;
} dyndns_queue_t;

static dyndns_queue_t dyndns_queue;

static void smbd_dyndns_queue_request(int, const char *);
static void smbd_dyndns_queue_flush(list_t *);
static void *smbd_dyndns_publisher(void *);
static void smbd_dyndns_process(list_t *);

void
smbd_dyndns_start(void)
{
	(void) mutex_lock(&dyndns_queue.ddq_mtx);

	if (dyndns_queue.ddq_state != DYNDNS_STATE_INIT) {
		(void) mutex_unlock(&dyndns_queue.ddq_mtx);
		return;
	}

	list_create(&dyndns_queue.ddq_list, sizeof (dyndns_qentry_t),
	    offsetof(dyndns_qentry_t, dqe_lnd));
	dyndns_queue.ddq_state = DYNDNS_STATE_READY;

	(void) mutex_unlock(&dyndns_queue.ddq_mtx);

	(void) smbd_thread_create("DynDNS publisher", smbd_dyndns_publisher,
	    NULL);
}

void
smbd_dyndns_stop(void)
{
	(void) mutex_lock(&dyndns_queue.ddq_mtx);

	switch (dyndns_queue.ddq_state) {
	case DYNDNS_STATE_READY:
	case DYNDNS_STATE_PUBLISHING:
		dyndns_queue.ddq_state = DYNDNS_STATE_STOPPING;
		(void) cond_signal(&dyndns_queue.ddq_cv);
		break;
	default:
		break;
	}

	(void) mutex_unlock(&dyndns_queue.ddq_mtx);
}

/*
 * Clear all records in both zones.
 */
void
smbd_dyndns_clear(void)
{
	char dns_domain[MAXHOSTNAMELEN];
	if (smb_getdomainname_dns(dns_domain, MAXHOSTNAMELEN) != 0) {
		smbd_log(LOG_NOTICE, "dyndns: failed to get domainname");
		return;
	}

	smbd_dyndns_queue_request(DYNDNS_OP_CLEAR, dns_domain);
}

/*
 * Update all records in both zones.
 */
void
smbd_dyndns_update(void)
{
	char dns_domain[MAXHOSTNAMELEN];

	if (smb_getdomainname_dns(dns_domain, MAXHOSTNAMELEN) != 0) {
		smbd_log(LOG_NOTICE, "dyndns: failed to get domainname");
		return;
	}

	smbd_dyndns_queue_request(DYNDNS_OP_UPDATE, dns_domain);
}

/*
 * Add a request to the queue.
 *
 * To comply with RFC 4120 section 6.2.1, entry->dqe_zone is converted
 * to lower case.
 */
static void
smbd_dyndns_queue_request(int op, const char *dns_domain)
{
	dyndns_qentry_t *entry;

	if (!smb_config_getbool(SMB_CI_DYNDNS_ENABLE))
		return;

	if ((entry = malloc(sizeof (dyndns_qentry_t))) == NULL)
		return;

	bzero(entry, sizeof (dyndns_qentry_t));
	entry->dqe_op = op;
	(void) strlcpy(entry->dqe_zone, dns_domain, MAXNAMELEN);
	(void) smb_strlwr(entry->dqe_zone);

	(void) mutex_lock(&dyndns_queue.ddq_mtx);

	switch (dyndns_queue.ddq_state) {
	case DYNDNS_STATE_READY:
	case DYNDNS_STATE_PUBLISHING:
		list_insert_tail(&dyndns_queue.ddq_list, entry);
		(void) cond_signal(&dyndns_queue.ddq_cv);
		break;
	default:
		free(entry);
		break;
	}

	(void) mutex_unlock(&dyndns_queue.ddq_mtx);
}

/*
 * Flush all remaining items from the specified list/queue.
 */
static void
smbd_dyndns_queue_flush(list_t *lst)
{
	dyndns_qentry_t *entry;

	while ((entry = list_remove_head(lst)) != NULL)
		free(entry);
}

/*
 * Dyndns update thread.  While running, the thread waits on a condition
 * variable until notified that an entry needs to be updated.
 *
 * If the outgoing queue is not empty, the thread wakes up every 60 seconds
 * to retry.
 */
/*ARGSUSED*/
void *
smbd_dyndns_publisher(void *arg)
{
	list_t publist;

	smbd_online_wait("smbd_dyndns_publisher");

	(void) mutex_lock(&dyndns_queue.ddq_mtx);
	if (dyndns_queue.ddq_state != DYNDNS_STATE_READY) {
		(void) mutex_unlock(&dyndns_queue.ddq_mtx);
		smbd_thread_exit();
		return (NULL);
	}
	dyndns_queue.ddq_state = DYNDNS_STATE_PUBLISHING;
	(void) mutex_unlock(&dyndns_queue.ddq_mtx);

	list_create(&publist, sizeof (dyndns_qentry_t),
	    offsetof(dyndns_qentry_t, dqe_lnd));

	while (smbd_online()) {
		(void) mutex_lock(&dyndns_queue.ddq_mtx);

		while (list_is_empty(&dyndns_queue.ddq_list) &&
		    (dyndns_queue.ddq_state == DYNDNS_STATE_PUBLISHING)) {
			(void) cond_wait(&dyndns_queue.ddq_cv,
			    &dyndns_queue.ddq_mtx);
		}

		if (dyndns_queue.ddq_state != DYNDNS_STATE_PUBLISHING) {
			(void) mutex_unlock(&dyndns_queue.ddq_mtx);
			break;
		}

		/*
		 * Transfer queued items to the local list so that
		 * the mutex can be released.
		 */
		list_move_tail(&publist, &dyndns_queue.ddq_list);
		(void) mutex_unlock(&dyndns_queue.ddq_mtx);

		smbd_dyndns_process(&publist);
	}

	(void) mutex_lock(&dyndns_queue.ddq_mtx);
	smbd_dyndns_queue_flush(&dyndns_queue.ddq_list);
	list_destroy(&dyndns_queue.ddq_list);
	dyndns_queue.ddq_state = DYNDNS_STATE_INIT;
	(void) mutex_unlock(&dyndns_queue.ddq_mtx);

	smbd_dyndns_queue_flush(&publist);
	list_destroy(&publist);
	smbd_thread_exit();
	return (NULL);
}

/*
 * Remove items from the queue and process them.
 */
static void
smbd_dyndns_process(list_t *publist)
{
	dyndns_qentry_t *entry;

	while ((entry = list_head(publist)) != NULL) {
		list_remove(publist, entry);

		switch (entry->dqe_op) {
		case DYNDNS_OP_CLEAR:
			(void) dyndns_zone_clear(entry->dqe_zone);
			break;
		case DYNDNS_OP_UPDATE:
			(void) dyndns_zone_update(entry->dqe_zone);
			break;
		default:
			break;
		}

		free(entry);
	}
}
