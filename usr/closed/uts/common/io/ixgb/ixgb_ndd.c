/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "ixgb.h"

#define	IXGB_DBG		IXGB_DBG_NDD

/*
 * NDD parameter access routines: link_status
 */

static int
ixgb_get_link_status(ixgb_t *ixgbp)
{
	return (ixgbp->link_state == LINK_STATE_UP ? 1 : 0);
}

static int
ixgb_ndget_link_status(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *credp)
{
	_NOTE(ARGUNUSED(q, credp))
	(void) mi_mpprintf(mp, "%d", ixgb_get_link_status((ixgb_t *)cp));
	return (0);
}

/*
 * NDD parameter access routines: link_speed
 */

static int
ixgb_get_link_speed(ixgb_t *ixgbp)
{
	return (ixgbp->link_state == LINK_STATE_UP ? 10000 : 0);
}

static int
ixgb_ndget_link_speed(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *credp)
{
	_NOTE(ARGUNUSED(q, credp))
	(void) mi_mpprintf(mp, "%d", ixgb_get_link_speed((ixgb_t *)cp));
	return (0);
}

/*
 * NDD parameter access routines: pause_time
 */

static int
ixgb_get_pause_time(ixgb_t *ixgbp)
{
	return (ixgbp->pause_time);
}

static int
ixgb_ndget_pause_time(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *credp)
{
	_NOTE(ARGUNUSED(q, credp))
	(void) mi_mpprintf(mp, "%d", ixgb_get_pause_time((ixgb_t *)cp));
	return (0);
}

static int
ixgb_ndset_pause_time(queue_t *q, mblk_t *mp, char *value, caddr_t cp,
    cred_t *credp)
{
	int new_val;
	char *end;
	ixgb_t *ixgbp;

	_NOTE(ARGUNUSED(q, mp, credp))

	new_val = mi_strtol(value, &end, 10);

	if (end == value)
		return (EINVAL);

	if (new_val < PAUSE_TIME_MIN || new_val > PAUSE_TIME_MAX)
		return (EINVAL);

	ixgbp = (ixgb_t *)cp;
	ixgbp->pause_time = new_val;
	ixgb_chip_sync(ixgbp);

	return (0);
}

/*
 * NDD parameter access routines: rx_hiwat
 */

static int
ixgb_get_rx_hiwat(ixgb_t *ixgbp)
{
	return (ixgbp->rx_highwater);
}

static int
ixgb_ndget_rx_hiwat(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *credp)
{
	_NOTE(ARGUNUSED(q, credp))
	(void) mi_mpprintf(mp, "%d", ixgb_get_rx_hiwat((ixgb_t *)cp));
	return (0);
}

static int
ixgb_ndset_rx_hiwat(queue_t *q, mblk_t *mp, char *value, caddr_t cp,
    cred_t *credp)
{
	int new_val;
	char *end;
	ixgb_t *ixgbp;

	_NOTE(ARGUNUSED(q, mp, credp))

	new_val = mi_strtol(value, &end, 10);

	if (end == value)
		return (EINVAL);

	if (new_val < RX_HIGH_WATER_MIN || new_val > RX_HIGH_WATER_MAX)
		return (EINVAL);

	ixgbp = (ixgb_t *)cp;
	ixgbp->rx_highwater = new_val;
	ixgb_chip_sync(ixgbp);

	return (0);
}

/*
 * NDD parameter access routines: rx_lowat
 */

static int
ixgb_get_rx_lowat(ixgb_t *ixgbp)
{
	return (ixgbp->rx_lowwater);
}

static int
ixgb_ndget_rx_lowat(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *credp)
{
	_NOTE(ARGUNUSED(q, credp))
	(void) mi_mpprintf(mp, "%d", ixgb_get_rx_lowat((ixgb_t *)cp));
	return (0);
}

static int
ixgb_ndset_rx_lowat(queue_t *q, mblk_t *mp, char *value, caddr_t cp,
    cred_t *credp)
{
	int new_val;
	char *end;
	ixgb_t *ixgbp;

	_NOTE(ARGUNUSED(q, mp, credp))

	new_val = mi_strtol(value, &end, 10);

	if (end == value)
		return (EINVAL);

	if (new_val < RX_LOW_WATER_MIN || new_val > RX_LOW_WATER_MAX)
		return (EINVAL);

	ixgbp = (ixgb_t *)cp;
	ixgbp->rx_lowwater = new_val;
	ixgb_chip_sync(ixgbp);

	return (0);
}

const nd_template_t nd_template[] = {
	{
		"link_status",
		ixgb_ndget_link_status,
		NULL,
		ixgb_get_link_status
	},
	{
		"link_speed",
		ixgb_ndget_link_speed,
		NULL,
		ixgb_get_link_speed
	},
	{
		"pause_time",
		ixgb_ndget_pause_time,
		ixgb_ndset_pause_time,
		ixgb_get_pause_time
	},
	{
		"rx_hiwat",
		ixgb_ndget_rx_hiwat,
		ixgb_ndset_rx_hiwat,
		ixgb_get_rx_hiwat
	},
	{
		"rx_lowat",
		ixgb_ndget_rx_lowat,
		ixgb_ndset_rx_lowat,
		ixgb_get_rx_lowat
	},
	{ NULL, NULL, NULL, NULL },
};

/*
 * Initialise the per-instance parameter array from the global prototype,
 * and register each element with the named dispatch handler using nd_load()
 */
int
ixgb_param_register(ixgb_t *ixgbp)
{
	const nd_template_t *ndp;
	caddr_t *nddpp = &ixgbp->nd_data_p;

	IXGB_TRACE(("ixgb_param_register($%p)", (void *)ixgbp));

	ASSERT(*nddpp == NULL);

	for (ndp = nd_template; ndp->name; ndp++) {
		if (!nd_load(nddpp, ndp->name, ndp->ndgetfn, ndp->ndsetfn,
		    (caddr_t)ixgbp)) {
			IXGB_DEBUG(("ixgb_param_register: FAILED at index %d",
			    ndp - nd_template));

			nd_free(nddpp);
			return (DDI_FAILURE);
		}
	}

	IXGB_DEBUG(("ixgb_param_register: OK"));
	return (DDI_SUCCESS);
}

int
ixgb_nd_init(ixgb_t *ixgbp)
{
	IXGB_TRACE(("ixgb_nd_init($%p)", (void *)ixgbp));

	/*
	 * Register all the per-instance properties, initialising
	 * them from the table above or from driver properties set
	 * in the .conf file
	 */
	if (ixgb_param_register(ixgbp) != DDI_SUCCESS)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}

enum ioc_reply
ixgb_nd_ioctl(ixgb_t *ixgbp, queue_t *wq, mblk_t *mp, struct iocblk *iocp)
{
	int ok;
	int cmd;

	IXGB_TRACE(("ixgb_nd_ioctl($%p, $%p, $%p, $%p)",
	    (void *)ixgbp, (void *)wq, (void *)mp, (void *)iocp));

	ASSERT(mutex_owned(ixgbp->genlock));

	cmd = iocp->ioc_cmd;

	switch (cmd) {

	default:
		/* NOTREACHED */
		ixgb_error(ixgbp, "ixgb_nd_ioctl: invalid cmd 0x%x", cmd);
		return (IOC_INVAL);

	case ND_SET:
	case ND_GET:
		/*
		 * If nd_getset() returns B_FALSE, the command was
		 * not valid (e.g. unknown name), so we just tell the
		 * top-level ioctl code to send a NAK (with code EINVAL).
		 *
		 * Otherwise, nd_getset() will have built the reply to
		 * be sent (but not actually sent it), so we tell the
		 * caller to send the prepared reply.
		 */
		ok = nd_getset(wq, ixgbp->nd_data_p, mp);
		IXGB_DEBUG(("ixgb_nd_ioctl: get/set %s", ok ? "OK" : "FAIL"));
		return (ok ? IOC_REPLY : IOC_INVAL);

	}
}

/* Free the Named Dispatch Table by calling nd_free */
void
ixgb_nd_cleanup(ixgb_t *ixgbp)
{
	IXGB_TRACE(("ixgb_nd_cleanup($%p)", (void *)ixgbp));

	nd_free(&ixgbp->nd_data_p);
}
