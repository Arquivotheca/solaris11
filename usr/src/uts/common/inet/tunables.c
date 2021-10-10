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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 1990 Mentat Inc.
 */

#include <inet/tunables.h>
#include <sys/md5.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <netinet/icmp6.h>
#include <inet/ip_stack.h>
#include <inet/rawip_impl.h>
#include <inet/tcp_stack.h>
#include <inet/tcp_impl.h>
#include <inet/udp_impl.h>
#include <inet/sctp/sctp_stack.h>
#include <inet/sctp/sctp_impl.h>
#include <inet/tcpcong.h>
#include <inet/tunables.h>

static int
prop_perm2const(mod_prop_info_t *pinfo)
{
	if (pinfo->mpi_setf == NULL)
		return (MOD_PROP_PERM_READ);
	if (pinfo->mpi_getf == NULL)
		return (MOD_PROP_PERM_WRITE);
	return (MOD_PROP_PERM_RW);
}

static mod_prop_info_t *
mod_get_prop_table(uint_t proto, void *arg)
{
	mod_prop_info_t	*ptbl;
	ip_stack_t	*ipst;
	tcp_stack_t	*tcps;
	sctp_stack_t	*sctps;
	udp_stack_t	*us;
	icmp_stack_t	*is;

	switch (proto) {
	case MOD_PROTO_IP:
	case MOD_PROTO_IPV4:
	case MOD_PROTO_IPV6:
		ipst = (ip_stack_t *)arg;
		ptbl = ipst->ips_propinfo_tbl;
		break;
	case MOD_PROTO_RAWIP:
		is = (icmp_stack_t *)arg;
		ptbl = is->is_propinfo_tbl;
		break;
	case MOD_PROTO_TCP:
		tcps = (tcp_stack_t *)arg;
		ptbl = tcps->tcps_propinfo_tbl;
		break;
	case MOD_PROTO_UDP:
		us = (udp_stack_t *)arg;
		ptbl = us->us_propinfo_tbl;
		break;
	case MOD_PROTO_SCTP:
		sctps = (sctp_stack_t *)arg;
		ptbl = sctps->sctps_propinfo_tbl;
		break;
	default:
		ptbl = NULL;
		break;
	}
	return (ptbl);
}

static mod_prop_info_t *
mod_get_prop_info(mod_prop_info_t *ptbl, const char *pname)
{
	mod_prop_info_t	*pinfo;

	for (pinfo = ptbl; pinfo->mpi_name != NULL; pinfo++) {
		if (strcmp(pinfo->mpi_name, pname) == 0)
			return (pinfo);
	}
	return (NULL);
}

/*
 * Modifies the value of the property to default value or to the `pval'
 * specified by the user.
 */
/* ARGSUSED */
int
mod_set_boolean(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void* pval, uint_t flags)
{
	char 		*end;
	unsigned long 	new_value;

	if (flags & MOD_PROP_DEFAULT) {
		pinfo->prop_cur_bval = pinfo->prop_def_bval;
		return (0);
	}

	if (ddi_strtoul(pval, &end, 10, &new_value) != 0 || *end != '\0')
		return (EINVAL);
	if (new_value != B_TRUE && new_value != B_FALSE)
		return (EINVAL);
	pinfo->prop_cur_bval = new_value;
	return (0);
}

/*
 * Retrieves property permission, default value, current value or possible
 * values for those properties whose value type is boolean_t.
 */
/* ARGSUSED */
int
mod_get_boolean(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *pval, uint_t *psize, uint_t ptype)
{
	bzero(pval, *psize);
	switch (ptype) {
	case MOD_PROP_PERM:
		*psize = snprintf(pval, *psize, "%u", prop_perm2const(pinfo));
		break;
	case MOD_PROP_POSSIBLE:
		*psize = snprintf(pval, *psize, "%u,%u", B_FALSE, B_TRUE);
		break;
	case MOD_PROP_DEFAULT:
		*psize = snprintf(pval, *psize, "%u", pinfo->prop_def_bval);
		break;
	case MOD_PROP_ACTIVE:
		*psize = snprintf(pval, *psize, "%u", pinfo->prop_cur_bval);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

int
mod_uint32_value(const void *pval, mod_prop_info_t *pinfo, uint_t flags,
    ulong_t *new_value)
{
	char 		*end;

	if (flags & MOD_PROP_DEFAULT) {
		*new_value = pinfo->prop_def_uval;
		return (0);
	}

	if (ddi_strtoul(pval, &end, 10, (ulong_t *)new_value) != 0 ||
	    *end != '\0')
		return (EINVAL);
	if (*new_value < pinfo->prop_min_uval ||
	    *new_value > pinfo->prop_max_uval) {
		return (ERANGE);
	}
	return (0);
}

/*
 * Modifies the value of the property to default value or to the `pval'
 * specified by the user.
 */
/* ARGSUSED */
int
mod_set_uint32(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void *pval, uint_t flags)
{
	unsigned long	new_value;
	int		err;

	if ((err = mod_uint32_value(pval, pinfo, flags, &new_value)) != 0)
		return (err);
	pinfo->prop_cur_uval = (uint32_t)new_value;
	return (0);
}

/*
 * Rounds up the value to make it multiple of 8.
 */
/* ARGSUSED */
int
mod_set_aligned(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void* pval, uint_t flags)
{
	int	err;

	if ((err = mod_set_uint32(cbarg, cr, pinfo, ifname, pval, flags)) != 0)
		return (err);

	/* if required, align the value to multiple of 8 */
	if (pinfo->prop_cur_uval & 0x7) {
		pinfo->prop_cur_uval &= ~0x7;
		pinfo->prop_cur_uval += 0x8;
	}

	return (0);
}

/*
 * Retrieves property permission, default value, current value or possible
 * values for those properties whose value type is uint32_t.
 */
/* ARGSUSED */
int
mod_get_uint32(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *pval, uint_t *psize, uint_t ptype)
{
	bzero(pval, *psize);
	switch (ptype) {
	case MOD_PROP_PERM:
		*psize = snprintf(pval, *psize, "%u", prop_perm2const(pinfo));
		break;
	case MOD_PROP_POSSIBLE:
		*psize = snprintf(pval, *psize, "%u-%u",
		    pinfo->prop_min_uval, pinfo->prop_max_uval);
		break;
	case MOD_PROP_DEFAULT:
		*psize = snprintf(pval, *psize, "%u", pinfo->prop_def_uval);
		break;
	case MOD_PROP_ACTIVE:
		*psize = snprintf(pval, *psize, "%u", pinfo->prop_cur_uval);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

/*
 * Retrieves property permission, default value, current value or possible
 * values for those properties whose value type is string.
 */
/* ARGSUSED */
int
mod_get_string(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *pval, uint_t *psize, uint_t ptype)
{
	bzero(pval, *psize);
	switch (ptype) {
	case MOD_PROP_PERM:
		*psize = snprintf(pval, *psize, "%u", prop_perm2const(pinfo));
		break;
	case MOD_PROP_POSSIBLE:
		*psize = snprintf(pval, *psize, "-");
		break;
	case MOD_PROP_DEFAULT:
		*psize = snprintf(pval, *psize, "%s", pinfo->prop_def_sval);
		break;
	case MOD_PROP_ACTIVE:
		*psize = snprintf(pval, *psize, "%s", pinfo->prop_cur_sval);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * Implements /sbin/ndd -get /dev/ip ?, for all the modules. Needed for
 * backward compatibility with /sbin/ndd.
 */
/* ARGSUSED */
int
mod_get_allprop(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *val, uint_t *psize, uint_t ptype)
{
	char		*pval = val;
	mod_prop_info_t	*ptbl, *prop;
	uint_t		size;
	uint_t		nbytes = 0, tbytes = 0;

	if (ptype != MOD_PROP_ACTIVE)
		return (ENOTSUP);

	ptbl = mod_get_prop_table(pinfo->mpi_proto, cbarg);
	if (ptbl == NULL)
		return (EINVAL);

	bzero(pval, *psize);
	size = *psize;
	for (prop = ptbl; prop->mpi_name != NULL; prop++) {
		if (prop->mpi_name[0] == '\0' ||
		    strcmp(prop->mpi_name, "?") == 0) {
			continue;
		}
		nbytes = snprintf(pval, size, "%s %d %d", prop->mpi_name,
		    prop->mpi_proto, prop_perm2const(prop));
		tbytes += nbytes + 1;
		if (tbytes >= *psize) {
			/*
			 * insufficient buffer space, lets determine
			 * how much buffer is actually needed
			 */
			pval = NULL;
			size = 0;
		} else {
			size -= nbytes + 1;
			pval += nbytes + 1;
		}
	}
	*psize = tbytes;
	return (0);
}

/*
 * Hold a lock while changing *_epriv_ports to prevent multiple
 * threads from changing it at the same time.
 */
/* ARGSUSED */
int
mod_set_extra_privports(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void* val, uint_t flags)
{
	uint_t		proto = pinfo->mpi_proto;
	tcp_stack_t	*tcps;
	sctp_stack_t	*sctps;
	udp_stack_t	*us;
	unsigned long	new_value;
	char		*end;
	kmutex_t	*lock;
	uint_t		i, nports;
	in_port_t	*ports;
	boolean_t	def = (flags & MOD_PROP_DEFAULT);
	const char	*pval = val;

	if (!def) {
		if (ddi_strtoul(pval, &end, 10, &new_value) != 0 ||
		    *end != '\0') {
			return (EINVAL);
		}

		if (new_value < pinfo->prop_min_uval ||
		    new_value > pinfo->prop_max_uval) {
			return (ERANGE);
		}
	}

	switch (proto) {
	case MOD_PROTO_TCP:
		tcps = (tcp_stack_t *)cbarg;
		lock = &tcps->tcps_epriv_port_lock;
		ports = tcps->tcps_g_epriv_ports;
		nports = tcps->tcps_g_num_epriv_ports;
		break;
	case MOD_PROTO_UDP:
		us = (udp_stack_t *)cbarg;
		lock = &us->us_epriv_port_lock;
		ports = us->us_epriv_ports;
		nports = us->us_num_epriv_ports;
		break;
	case MOD_PROTO_SCTP:
		sctps = (sctp_stack_t *)cbarg;
		lock = &sctps->sctps_epriv_port_lock;
		ports = sctps->sctps_g_epriv_ports;
		nports = sctps->sctps_g_num_epriv_ports;
		break;
	default:
		return (ENOTSUP);
	}

	mutex_enter(lock);

	/* if MOD_PROP_DEFAULT is set then reset the ports list to default */
	if (def) {
		for (i = 0; i < nports; i++)
			ports[i] = 0;
		ports[0] = ULP_DEF_EPRIV_PORT1;
		ports[1] = ULP_DEF_EPRIV_PORT2;
		mutex_exit(lock);
		return (0);
	}

	/* Check if the value is already in the list */
	for (i = 0; i < nports; i++) {
		if (new_value == ports[i])
			break;
	}

	if (flags & MOD_PROP_REMOVE) {
		if (i == nports) {
			mutex_exit(lock);
			return (ESRCH);
		}
		/* Clear the value */
		ports[i] = 0;
	} else if (flags & MOD_PROP_APPEND) {
		if (i != nports) {
			mutex_exit(lock);
			return (EEXIST);
		}

		/* Find an empty slot */
		for (i = 0; i < nports; i++) {
			if (ports[i] == 0)
				break;
		}
		if (i == nports) {
			mutex_exit(lock);
			return (EOVERFLOW);
		}
		/* Set the new value */
		ports[i] = (in_port_t)new_value;
	} else {
		/*
		 * If the user used 'assignment' modifier.
		 * For eg:
		 * 	# ipadm set-prop -p extra_priv_ports=3001 tcp
		 *
		 * We clear all the ports and then just add 3001.
		 */
		ASSERT(flags == MOD_PROP_ACTIVE);
		for (i = 0; i < nports; i++)
			ports[i] = 0;
		ports[0] = (in_port_t)new_value;
	}

	mutex_exit(lock);
	return (0);
}

/*
 * Note: No locks are held when inspecting *_epriv_ports
 * but instead the code relies on:
 * - the fact that the address of the array and its size never changes
 * - the atomic assignment of the elements of the array
 */
/* ARGSUSED */
int
mod_get_extra_privports(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *val, uint_t *psize, uint_t ptype)
{
	uint_t		proto = pinfo->mpi_proto;
	tcp_stack_t	*tcps;
	sctp_stack_t	*sctps;
	udp_stack_t	*us;
	uint_t		i, nports, size;
	in_port_t	*ports;
	char		*pval = val;
	size_t		nbytes = 0, tbytes = 0;

	switch (proto) {
	case MOD_PROTO_TCP:
		tcps = (tcp_stack_t *)cbarg;
		ports = tcps->tcps_g_epriv_ports;
		nports = tcps->tcps_g_num_epriv_ports;
		break;
	case MOD_PROTO_UDP:
		us = (udp_stack_t *)cbarg;
		ports = us->us_epriv_ports;
		nports = us->us_num_epriv_ports;
		break;
	case MOD_PROTO_SCTP:
		sctps = (sctp_stack_t *)cbarg;
		ports = sctps->sctps_g_epriv_ports;
		nports = sctps->sctps_g_num_epriv_ports;
		break;
	default:
		return (ENOTSUP);
	}

	bzero(pval, *psize);
	size = *psize;
	switch (ptype) {
	case MOD_PROP_DEFAULT:
		tbytes = snprintf(pval, *psize, "%u,%u", ULP_DEF_EPRIV_PORT1,
		    ULP_DEF_EPRIV_PORT2);
		break;
	case MOD_PROP_PERM:
		tbytes = snprintf(pval, *psize, "%u", MOD_PROP_PERM_RW);
		break;
	case MOD_PROP_POSSIBLE:
		tbytes = snprintf(pval, *psize, "%u-%u", pinfo->prop_min_uval,
		    pinfo->prop_max_uval);
		break;
	case MOD_PROP_ACTIVE:
		for (i = 0; i < nports; i++) {
			if (ports[i] != 0) {
				if (*psize == size)
					nbytes = snprintf(pval, size, "%u",
					    ports[i]);
				else
					nbytes = snprintf(pval, size, ",%u",
					    ports[i]);
				tbytes += nbytes;
				if (tbytes >= *psize) {
					/*
					 * insufficient buffer space, lets
					 * determine how much buffer is
					 * actually needed
					 */
					pval = NULL;
					size = 0;
				} else {
					size -= nbytes;
					pval += nbytes;
				}
			}
		}
		break;
	default:
		return (EINVAL);
	}
	*psize = tbytes;
	return (0);
}

/* ARGSUSED */
int
mod_set_cong_enabled(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void *val, uint_t flags)
{
	uint_t		proto = pinfo->mpi_proto;
	const char	*pval = val;
	tcp_stack_t	*tcps;
	sctp_stack_t	*sctps;
	list_t		*en;
	kmutex_t	*lock;
	tcpcong_list_ent_t *tce, *tce_next, *tce_default;
	int		ret = 0;

	switch (proto) {
	case MOD_PROTO_TCP:
		tcps = (tcp_stack_t *)cbarg;
		lock = &tcps->tcps_cong_lock;
		en = &tcps->tcps_cong_enabled;
		tce_default = tcps->tcps_cong_default_ent;
		break;
	case MOD_PROTO_SCTP:
		sctps = (sctp_stack_t *)cbarg;
		lock = &sctps->sctps_cong_lock;
		en = &sctps->sctps_cong_enabled;
		tce_default = sctps->sctps_cong_default_ent;
		break;
	default:
		return (EPROTONOSUPPORT);
	}

	mutex_enter(lock);

	if ((flags & MOD_PROP_DEFAULT) || flags == MOD_PROP_ACTIVE) {
		/*
		 * In assignment modifier case, i.e. -p cong_enabled=alg
		 * the builtin alg is the only supported value at this time.
		 */
		if (flags == MOD_PROP_ACTIVE &&
		    strcmp(pval, TCPCONG_ALG_BUILTIN) != 0) {
			mutex_exit(lock);
			return (EINVAL);
		}
		/* Remove all but the builtin alg */
		for (tce = list_head(en); tce != NULL; tce = tce_next) {
			tce_next = list_next(en, tce);
			if (strcmp(tce->tce_name, TCPCONG_ALG_BUILTIN) == 0)
				continue;
			TCPCONG_PROP_INFO_FREE(tce, proto);
			tcpcong_unref(tce->tce_hdl);
			list_remove(en, tce);
			kmem_free(tce, sizeof (*tce));
		}
	} else if (flags & MOD_PROP_REMOVE) {
		/* Builtin and default cannot be disabled */
		if (strcmp(pval, TCPCONG_ALG_BUILTIN) == 0 ||
		    strcmp(pval, tce_default->tce_name) == 0) {
			mutex_exit(lock);
			return (EBUSY);
		}
		for (tce = list_head(en); tce != NULL; tce = tce_next) {
			tce_next = list_next(en, tce);
			if (strcmp(tce->tce_name, pval) == 0) {
				TCPCONG_PROP_INFO_FREE(tce, proto);
				tcpcong_unref(tce->tce_hdl);
				list_remove(en, tce);
				kmem_free(tce, sizeof (*tce));
				break;
			}
		}
		ret = (tce == NULL) ? ESRCH : 0;
	} else if (flags & MOD_PROP_APPEND) {
		/* Check if the alg is already on the list */
		for (tce = list_head(en); tce != NULL; tce = tce_next) {
			tce_next = list_next(en, tce);
			if (strcmp(tce->tce_name, pval) == 0) {
				mutex_exit(lock);
				return (0);
			}
		}
		tce = kmem_zalloc(sizeof (*tce), KM_SLEEP);
		tce->tce_hdl = tcpcong_lookup(pval, &tce->tce_ops);
		if (tce->tce_hdl != NULL) {
			ASSERT(tce->tce_ops != NULL);
			TCPCONG_PROP_INFO_ALLOC(tce, proto);
			tce->tce_name = tce->tce_ops->co_name;
			list_insert_tail(en, tce);
		} else {
			kmem_free(tce, sizeof (*tce));
			ret = EIO;
		}
	} else
		ret = EINVAL;

	mutex_exit(lock);
	return (ret);
}

/* ARGSUSED */
int
mod_get_cong_enabled(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *val, uint_t *psize, uint_t ptype)
{
	uint_t		proto = pinfo->mpi_proto;
	tcp_stack_t	*tcps;
	sctp_stack_t	*sctps;
	list_t		*en;
	kmutex_t	*lock;
	tcpcong_list_ent_t *tce, *tce_next;
	char		*pval = val;
	char		*name;
	uint_t		size;
	size_t		nbytes = 0, tbytes = 0;

	switch (proto) {
	case MOD_PROTO_TCP:
		tcps = (tcp_stack_t *)cbarg;
		lock = &tcps->tcps_cong_lock;
		en = &tcps->tcps_cong_enabled;
		break;
	case MOD_PROTO_SCTP:
		sctps = (sctp_stack_t *)cbarg;
		lock = &sctps->sctps_cong_lock;
		en = &sctps->sctps_cong_enabled;
		break;
	default:
		return (EPROTONOSUPPORT);
	}

	bzero(pval, *psize);
	size = *psize;

	mutex_enter(lock);
	switch (ptype) {
	case MOD_PROP_DEFAULT:
		tbytes = snprintf(pval, *psize, TCPCONG_ALG_BUILTIN);
		break;
	case MOD_PROP_PERM:
		tbytes = snprintf(pval, *psize, "%u", MOD_PROP_PERM_RW);
		break;
	case MOD_PROP_POSSIBLE:
		if (*psize > 0)
			pval[0] = '\0';
		tbytes = 0;
		break;
	case MOD_PROP_ACTIVE:
		for (tce = list_head(en); tce != NULL; tce = tce_next) {
			tce_next = list_next(en, tce);
			name = tce->tce_name;
			if (*psize == size)
				nbytes = snprintf(pval, size, "%s", name);
			else
				nbytes = snprintf(pval, size, ",%s", name);
			tbytes += nbytes;
			if (tbytes >= *psize) {
				/*
				 * insufficient buffer space, lets determine
				 * how much buffer is actually needed
				 */
				pval = NULL;
				size = 0;
			} else {
				size -= nbytes;
				pval += nbytes;
			}
		}
		break;
	default:
		return (EINVAL);
	}
	mutex_exit(lock);

	*psize = tbytes;
	return (0);
}

/* ARGSUSED */
int
mod_set_cong_default(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void *val, uint_t flags)
{
	uint_t		proto = pinfo->mpi_proto;
	boolean_t	set_def = (flags & MOD_PROP_DEFAULT);
	const char	*pval = val;
	const char	*alg;
	tcp_stack_t	*tcps;
	sctp_stack_t	*sctps;
	list_t		*en;
	kmutex_t	*lock;
	tcpcong_list_ent_t *tce, *tce_next, **tce_default;
	int		err = 0;

	switch (proto) {
	case MOD_PROTO_TCP:
		tcps = (tcp_stack_t *)cbarg;
		lock = &tcps->tcps_cong_lock;
		en = &tcps->tcps_cong_enabled;
		tce_default = &tcps->tcps_cong_default_ent;
		break;
	case MOD_PROTO_SCTP:
		sctps = (sctp_stack_t *)cbarg;
		lock = &sctps->sctps_cong_lock;
		en = &sctps->sctps_cong_enabled;
		tce_default = &sctps->sctps_cong_default_ent;
		break;
	default:
		return (EPROTONOSUPPORT);
	}

	mutex_enter(lock);

	alg = set_def ? pinfo->prop_def_sval : pval;

	/* if current value is already that, we're done */
	if (pinfo->prop_cur_sval != NULL &&
	    strcmp(alg, pinfo->prop_cur_sval) == 0)
		goto ret;

	/* find algorithm on the enabled list */
	for (tce = list_head(en); tce != NULL; tce = tce_next) {
		tce_next = list_next(en, tce);
		if (strcmp(alg, tce->tce_name) == 0)
			break;
	}
	if (tce == NULL) {
		err = EINVAL;
		goto ret;
	}
	*tce_default = tce;

	/* replace current value with the new value */
	if (pinfo->prop_cur_sval != NULL &&
	    pinfo->prop_cur_sval != pinfo->prop_def_sval)
		strfree(pinfo->prop_cur_sval);
	if (set_def)
		pinfo->prop_cur_sval = pinfo->prop_def_sval;
	else
		pinfo->prop_cur_sval = strdup(pval);

ret:
	mutex_exit(lock);
	return (err);
}

/*
 * Modifies the value of the property to default value or to the `pval'
 * specified by the user.
 */
/* ARGSUSED */
int
mod_set_buf(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void *pval, uint_t flags)
{
	mod_prop_info_t	*ptbl, *mbuf_prop;
	unsigned long	new_value;
	char		*endp;

	if (flags & MOD_PROP_DEFAULT) {
		new_value = pinfo->prop_def_uval;
	} else {
		/* extract the `pval' */
		if (ddi_strtoul(pval, &endp, 10, &new_value) != 0 ||
		    *endp != '\0') {
			return (EINVAL);
		}
	}

	/*
	 * Send/Recv socket buffer size for given protocol is bounded by the
	 * current value of the MOD_PROPNAME_MAX_BUF property.
	 */
	ptbl = mod_get_prop_table(pinfo->mpi_proto, cbarg);
	mbuf_prop = mod_get_prop_info(ptbl, MOD_PROPNAME_MAX_BUF);

	if (new_value < pinfo->prop_min_uval ||
	    new_value > mbuf_prop->prop_cur_uval) {
		return (ERANGE);
	}
	pinfo->prop_cur_uval = (uint32_t)new_value;
	return (0);
}

/*
 * Get callback function for 'send_buf' and 'recv_buf' property. We need this
 * to print the possible values as the maximum value depends on the 'max_buf'
 * property.
 */
int
mod_get_buf(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *pval, uint_t *psize, uint_t ptype)
{
	mod_prop_info_t	*ptbl, *mbuf_prop;

	if (ptype != MOD_PROP_POSSIBLE) {
		return (mod_get_uint32(cbarg, pinfo, ifname, pval, psize,
		    ptype));
	}

	/*
	 * Send/Recv socket buffer size for given protocol is bounded
	 * by the current value of the MOD_PROPNAME_MAX_BUF property.
	 */
	ptbl = mod_get_prop_table(pinfo->mpi_proto, cbarg);
	mbuf_prop = mod_get_prop_info(ptbl, MOD_PROPNAME_MAX_BUF);

	bzero(pval, *psize);
	*psize = snprintf(pval, *psize, "%u-%u", pinfo->prop_min_uval,
	    mbuf_prop->prop_cur_uval);
	return (0);
}

/*
 * Callback function for property 'max_buf'. It checks for the current value
 * of 'send_buf' and 'recv_buf', before it modifes the value of 'max_buf'.
 */
/* ARGSUSED */
int
mod_set_max_buf(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void *pval, uint_t flags)
{
	mod_prop_info_t	*ptbl, *buf_prop;
	unsigned long	new_value;
	int		err;

	if ((err = mod_uint32_value(pval, pinfo, flags, &new_value)) != 0)
		return (err);

	ptbl = mod_get_prop_table(pinfo->mpi_proto, cbarg);

	buf_prop = mod_get_prop_info(ptbl, MOD_PROPNAME_SEND_BUF);
	if (new_value < buf_prop->prop_cur_uval)
		return (ERANGE);

	buf_prop = mod_get_prop_info(ptbl, MOD_PROPNAME_RECV_BUF);
	if (new_value < buf_prop->prop_cur_uval)
		return (ERANGE);

	pinfo->prop_cur_uval = (uint32_t)new_value;
	return (0);
}

/*
 * Get callback function for 'max_buf' property. We need this to print the
 * possible values.
 */
int
mod_get_max_buf(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *pval, uint_t *psize, uint_t ptype)
{
	mod_prop_info_t	*ptbl, *sbuf, *rbuf;
	uint32_t	min, max;

	if (ptype != MOD_PROP_POSSIBLE) {
		return (mod_get_uint32(cbarg, pinfo, ifname, pval, psize,
		    ptype));
	}

	/*
	 * minimum value of max_buf is equal to
	 * max[recv_buf.CURRENT, send_buf.CURRENT, max_buf.POSSIBLE.min]
	 */
	min = pinfo->prop_min_uval;
	max = pinfo->prop_max_uval;
	ptbl = mod_get_prop_table(pinfo->mpi_proto, cbarg);
	sbuf = mod_get_prop_info(ptbl, MOD_PROPNAME_SEND_BUF);
	rbuf = mod_get_prop_info(ptbl, MOD_PROPNAME_RECV_BUF);
	if (min < sbuf->prop_cur_uval)
		min = sbuf->prop_cur_uval;
	if (min < rbuf->prop_cur_uval)
		min = rbuf->prop_cur_uval;

	bzero(pval, *psize);
	*psize = snprintf(pval, *psize, "%u-%u", min, max);
	return (0);
}

/*
 * Sets the smallest or largest anon port value for TCP, UDP or SCTP. We
 * check to ensure smallest_anon_port <= largest_anon_port and vice-versa.
 */
/* ARGSUSED */
int
mod_set_anon(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void* pval, uint_t flags)
{
	mod_prop_info_t	*ptbl, *aport;
	unsigned long	new_value;
	char		*end;

	if (flags & MOD_PROP_DEFAULT) {
		new_value = pinfo->prop_def_uval;
	} else {
		if (ddi_strtoul(pval, &end, 10, &new_value) != 0 ||
		    *end != '\0')
			return (EINVAL);
	}

	/*
	 * `new_value' contains the value need to be set. Now check for it's
	 * validity. The semantic is that the smallest_anon_port should be
	 * always less than or equal to largest_anon_port.
	 */
	ptbl = mod_get_prop_table(pinfo->mpi_proto, cbarg);
	if (strcmp(pinfo->mpi_name, MOD_PROPNAME_SMALL_ANONPORT) == 0) {
		aport = mod_get_prop_info(ptbl, MOD_PROPNAME_LARGE_ANONPORT);
		if (new_value > aport->prop_cur_uval ||
		    new_value < pinfo->prop_min_uval)
			return (ERANGE);
	} else if (strcmp(pinfo->mpi_name, MOD_PROPNAME_LARGE_ANONPORT) == 0) {
		aport = mod_get_prop_info(ptbl, MOD_PROPNAME_SMALL_ANONPORT);
		if (new_value < aport->prop_cur_uval ||
		    new_value > pinfo->prop_max_uval)
			return (ERANGE);
	} else {
		ASSERT(0);
	}
	pinfo->prop_cur_uval = (uint32_t)new_value;
	return (0);
}

/*
 * Get callback function for 'smallest_anon_port' and 'largest_anon_port'
 * property. We need this to print the possible values.
 */
int
mod_get_anon(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *pval, uint_t *psize, uint_t ptype)
{
	mod_prop_info_t	*ptbl, *aport;
	uint32_t	min, max;

	if (ptype != MOD_PROP_POSSIBLE) {
		return (mod_get_uint32(cbarg, pinfo, ifname, pval, psize,
		    ptype));
	}

	/*
	 * - minimum value of largest_anon_port is equal to the current value
	 * of the smallest_anon_port.
	 *
	 * - maximum value of smallest_anon_port is equal to the current value
	 * of the largest_anon_port.
	 */
	min = pinfo->prop_min_uval;
	max = pinfo->prop_max_uval;
	ptbl = mod_get_prop_table(pinfo->mpi_proto, cbarg);
	if (strcmp(pinfo->mpi_name, MOD_PROPNAME_SMALL_ANONPORT) == 0) {
		aport = mod_get_prop_info(ptbl, MOD_PROPNAME_LARGE_ANONPORT);
		max = aport->prop_cur_uval;
	} else if (strcmp(pinfo->mpi_name, MOD_PROPNAME_LARGE_ANONPORT) == 0) {
		aport = mod_get_prop_info(ptbl, MOD_PROPNAME_SMALL_ANONPORT);
		min = aport->prop_cur_uval;
	} else {
		ASSERT(0);
	}

	bzero(pval, *psize);
	*psize = snprintf(pval, *psize, "%u-%u", min, max);
	return (0);
}
