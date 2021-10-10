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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/sysmacros.h>
#include <sys/strsubr.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/vfs.h>
#include <inet/sdp_itf.h>
#include <fs/sockfs/sockcommon.h>
#include "socksdp.h"

struct sonode *socksdp_create(struct sockparams *, int, int, int,
    int, int, int *, cred_t *);
static void socksdp_destroy(struct sonode *);

static __smod_priv_t sosdp_priv = {
	socksdp_create,
	socksdp_destroy,
	NULL
};

static smod_reg_t sinfo = {
	SOCKMOD_VERSION,
	"socksdp",
	SOCK_UC_VERSION,
	SOCK_DC_VERSION,
	NULL,
	&sosdp_priv
};

/*
 * Module linkage information for the kernel
 */
static struct modlsockmod modlsockmod = {
	&mod_sockmodops, "SDP socket module", &sinfo
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modlsockmod,
	NULL
};

/*
 * Creates a sdp socket data structure.
 */
/* ARGSUSED */
struct sonode *
socksdp_create(struct sockparams *sp, int family, int type, int protocol,
		    int version, int sflags, int *errorp, cred_t *cr)
{
	struct sonode *so;
	int kmflags = (sflags & SOCKET_NOSLEEP) ? KM_NOSLEEP : KM_SLEEP;

	dprint(4, ("Inside sosdp_create: domain:%d proto:%d type:%d",
	    family, protocol, type));

	*errorp = 0;
	if (is_system_labeled() && crgetzoneid(cr) != GLOBAL_ZONEID) {
		*errorp = EOPNOTSUPP;
		return (NULL);
	}

	/*
	 * enforce socket versioning
	 */
	if (version != SOV_DEFAULT && version != so_default_version) {
		*errorp = EINVAL;
		return (NULL);
	}
	/*
	 * We only support one type of SDP socket.  Let sotpi_create()
	 * handle all other cases, such as raw socket.
	 */
	if (!(family == AF_INET || family == AF_INET6 ||
	    family == AF_INET_SDP) || !(type == SOCK_STREAM)) {
		*errorp = EINVAL;
		return (NULL);
	}

	so = kmem_cache_alloc(socket_cache, kmflags);
	if (so == NULL) {
		*errorp = ENOMEM;
		return (NULL);
	}

	sonode_init(so, sp, family, type, protocol, &sosdp_sonodeops);
	so->so_pollev |= SO_POLLEV_ALWAYS;

	dprint(2, ("sosdp_create: %p domain %d type %d\n", (void *)so, family,
	    type));

	if (version == SOV_DEFAULT) {
		version = so_default_version;
	}

	so->so_version = (short)version;

	/*
	 * set the default values to be INFPSZ
	 * if a protocol desires it can change the value later
	 */
	so->so_proto_props.sopp_rxhiwat = SOCKET_RECVHIWATER;
	so->so_proto_props.sopp_rxlowat = SOCKET_RECVLOWATER;
	so->so_proto_props.sopp_maxpsz = INFPSZ;
	so->so_proto_props.sopp_maxblk = INFPSZ;

	return (so);
}

static void
socksdp_destroy(struct sonode *so)
{
	ASSERT(so->so_ops == &sosdp_sonodeops);

	sosdp_fini(so, CRED());

	kmem_cache_free(socket_cache, so);
}

#include <sys/sunldi.h>
#include <sys/ib/ibtl/ibti_common.h>
static ldi_handle_t	sosdp_transport_handle = NULL;
ldi_ident_t sosdp_li;

static int
sdp_open_sdpib_driver()
{
	int ret = 0;

	ASSERT(sosdp_transport_handle == NULL);
	if (sosdp_transport_handle != NULL) {
		cmn_err(CE_PANIC, "sosdp_transport_handle != NULL");
	}

	if (ibt_hw_is_present() == 0) {
		ret = ENODEV;
		goto done;
	}

	ret = ldi_open_by_name("/devices/pseudo/sdpib@0:sdpib",
	    FREAD | FWRITE, kcred, &sosdp_transport_handle, sosdp_li);
	if (ret != 0) {
		ret = EPROTONOSUPPORT;
		sosdp_transport_handle = NULL;
		goto done;
	}
done:
	return (ret);
}

int
_init(void)
{
	int ret;

	ret = ldi_ident_from_mod(&modlinkage, &sosdp_li);
	if (ret != 0) {
		goto done;
	}

	ret = sdp_open_sdpib_driver();
	if (ret != 0) {
		goto done;
	}
	ret = mod_install(&modlinkage);
done:
	return (ret);
}

int
_fini(void)
{
	int ret;

	ret = mod_remove(&modlinkage);
	if (ret != 0) {
		return (ret);
	}

	(void) ldi_close(sosdp_transport_handle, FNDELAY, kcred);
	ldi_ident_release(sosdp_li);

	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
