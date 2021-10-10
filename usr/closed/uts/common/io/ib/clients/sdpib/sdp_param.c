/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/ib/clients/sdpib/sdp_main.h>

static int sdp_param_get(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *cr);
static int sdp_param_set(queue_t *q, mblk_t *mp, char *value, caddr_t cp,
    cred_t *cr);
static int sdp_extra_priv_ports_add(queue_t *q, mblk_t *mp, char *value,
    caddr_t cp, cred_t *cr);
static int sdp_extra_priv_ports_get(queue_t *q, mblk_t *mp, caddr_t cp,
    cred_t *cr);
static int sdp_extra_priv_ports_del(queue_t *q, mblk_t *mp, char *value,
    caddr_t cp, cred_t *cr);

/* Get callback routine passed to nd_load by sdp_param_register */
/* ARGSUSED */
static int
sdp_param_get(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *cr)
{
	/* LINTED */
	sdpparam_t	*sdppa = (sdpparam_t *)cp;
	sdp_stack_t	*sdps = (sdp_stack_t *)q->q_ptr;
	uint32_t 	value;

	mutex_enter(&sdps->sdps_param_lock);
	value = sdppa->sdp_param_val;
	mutex_exit(&sdps->sdps_param_lock);
	(void) mi_mpprintf(mp, "%u", value);
	return (0);
}

/* Set callback routine passed to nd_load by sdp_param_register */
/* ARGSUSED */
static int
sdp_param_set(queue_t *q, mblk_t *mp, char *value, caddr_t cp, cred_t *cr)
{
	/* LINTED */
	sdpparam_t	*sdppa = (sdpparam_t *)cp;
	sdp_stack_t	*sdps = (sdp_stack_t *)q->q_ptr;
	char		*end;
	int32_t		new_value;

	new_value = (uint32_t)mi_strtol(value, &end, 10);
	if (end == value || new_value < sdppa->sdp_param_min ||
	    new_value > sdppa->sdp_param_max)
		return (EINVAL);
	mutex_enter(&sdps->sdps_param_lock);
	sdppa->sdp_param_val = new_value;
	mutex_exit(&sdps->sdps_param_lock);
	return (0);
}


boolean_t
sdp_param_register(caddr_t *ndp, sdpparam_t *sdppa, int cnt)
{
	for (; cnt-- > 0; sdppa++) {
		if (sdppa->sdp_param_name && sdppa->sdp_param_name[0]) {
			if (!nd_load(ndp, sdppa->sdp_param_name,
			    sdp_param_get, sdp_param_set,
			    (caddr_t)sdppa)) {
				nd_free(ndp);
				return (B_FALSE);
			}
		}
	}

	if (!nd_load(ndp, "sdp_extra_priv_ports",
	    sdp_extra_priv_ports_get, NULL, NULL)) {
		nd_free(ndp);
		return (B_FALSE);
	}

	if (!nd_load(ndp, "sdp_extra_priv_ports_add",
	    NULL, sdp_extra_priv_ports_add, NULL)) {
		nd_free(ndp);
		return (B_FALSE);
	}

	if (!nd_load(ndp, "sdp_extra_priv_ports_del",
	    NULL, sdp_extra_priv_ports_del, NULL)) {
		nd_free(ndp);
		return (B_FALSE);
	}

	return (B_TRUE);
}


/*
 * Hold a lock while changing sdps->sdps_epriv_ports to prevent multiple
 * threads from changing it at the same time.
 */
/* ARGSUSED */
static int
sdp_extra_priv_ports_add(queue_t *q, mblk_t *mp, char *value, caddr_t cp,
    cred_t *cr)
{
	sdp_stack_t *sdps = (sdp_stack_t *)q->q_ptr;
	long new_value;
	int i;

	/*
	 * Fail the request if the new value does not lie within the
	 * port number limits.
	 */
	if (ddi_strtol(value, NULL, 10, &new_value) != 0 ||
	    new_value <= 0 || new_value >= 65536) {
		return (EINVAL);
	}

	mutex_enter(&sdps->sdps_epriv_port_lock);
	/* Check if the value is already in the list */
	for (i = 0; i < sdps->sdps_num_epriv_ports; i++) {
		if (new_value == sdps->sdps_epriv_ports[i]) {
			mutex_exit(&sdps->sdps_epriv_port_lock);
			return (EEXIST);
		}
	}
	/* Find an empty slot */
	for (i = 0; i < sdps->sdps_num_epriv_ports; i++) {
		if (sdps->sdps_epriv_ports[i] == 0)
			break;
	}
	if (i == sdps->sdps_num_epriv_ports) {
		mutex_exit(&sdps->sdps_epriv_port_lock);
		return (EOVERFLOW);
	}
	/* Set the new value */
	sdps->sdps_epriv_ports[i] = (uint16_t)new_value;
	mutex_exit(&sdps->sdps_epriv_port_lock);
	return (0);
}

/* ARGSUSED */
static int
sdp_extra_priv_ports_get(queue_t *q, mblk_t *mp, caddr_t cp, cred_t *cr)
{
	sdp_stack_t *sdps = (sdp_stack_t *)q->q_ptr;
	int i;

	mutex_enter(&sdps->sdps_epriv_port_lock);
	for (i = 0; i < sdps->sdps_num_epriv_ports; i++) {
		if (sdps->sdps_epriv_ports[i] != 0) {
			(void) mi_mpprintf(mp, "%d ",
			    sdps->sdps_epriv_ports[i]);
		}
	}
	mutex_exit(&sdps->sdps_epriv_port_lock);
	return (0);
}

/*
 * Hold a lock while changing sctp_g_epriv_ports to prevent multiple
 * threads from changing it at the same time.
 */
/* ARGSUSED */
static int
sdp_extra_priv_ports_del(queue_t *q, mblk_t *mp, char *value, caddr_t cp,
    cred_t *cr)
{
	sdp_stack_t *sdps = (sdp_stack_t *)q->q_ptr;
	long new_value;
	int i;

	/*
	 * Fail the request if the new value does not lie within the
	 * port number limits.
	 */
	if (ddi_strtol(value, NULL, 10, &new_value) != 0 ||
	    new_value <= 0 || new_value >= 65536) {
		return (EINVAL);
	}

	mutex_enter(&sdps->sdps_epriv_port_lock);
	/* Check that the value is already in the list */
	for (i = 0; i < sdps->sdps_num_epriv_ports; i++) {
		if (sdps->sdps_epriv_ports[i] == new_value)
			break;
	}
	if (i == sdps->sdps_num_epriv_ports) {
		mutex_exit(&sdps->sdps_epriv_port_lock);
		return (ESRCH);
	}
	/* Clear the value */
	sdps->sdps_epriv_ports[i] = 0;
	mutex_exit(&sdps->sdps_epriv_port_lock);
	return (0);
}

boolean_t
sdp_is_extra_priv_port(sdp_stack_t *sdps, uint16_t port)
{
	boolean_t	found = B_FALSE;
	int		i;

	mutex_enter(&sdps->sdps_epriv_port_lock);
	for (i = 0; i < sdps->sdps_num_epriv_ports; i++) {
		if (sdps->sdps_epriv_ports[i] == port) {
			found = B_TRUE;
			break;
		}
	}
	mutex_exit(&sdps->sdps_epriv_port_lock);
	return (found);
}
