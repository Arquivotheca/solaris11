/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved.
 *
 * $Id: clnt_privs.c 23100 2009-10-31 00:48:38Z tlyu $
 * $Source$
 *
 */

/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 *	Openvision retains the copyright to derivative works of
 *	this source code.  Do *NOT* create a derivative of this
 *	source code before consulting with your legal department.
 *	Do *NOT* integrate *ANY* of this source code into another
 *	product before consulting with your legal department.
 *
 *	For further information, read the top-level Openvision
 *	copyright which is contained in the top-level MIT Kerberos
 *	copyright.
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 */

#if !defined(lint) && !defined(__CODECENTER__)
static char *rcsid = "$Header$";
#endif

/* Solaris Kerberos */
#include    <rpc/rpc.h>
#include    <kadm5/admin.h>
#include    <kadm5/kadm_rpc.h>
#include    "client_internal.h"

kadm5_ret_t kadm5_get_privs(void *server_handle, long *privs)
{
    getprivs_ret *r;
    kadm5_server_handle_t handle = server_handle;

    r = get_privs_2(&handle->api_version, handle->clnt);
    if (r == NULL)
        return KADM5_RPC_ERROR;
    else if (r->code == KADM5_OK)
        *privs = r->privs;

    return r->code;
}
