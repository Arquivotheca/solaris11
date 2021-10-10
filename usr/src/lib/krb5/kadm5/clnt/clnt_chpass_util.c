/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */

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

#include <kadm5/admin.h>
#include "client_internal.h"

kadm5_ret_t kadm5_chpass_principal_util(void *server_handle,
                                        krb5_principal princ,
                                        char *new_pw,
                                        char **ret_pw,
                                        char *msg_ret,
                                        unsigned int msg_len)
{
    kadm5_server_handle_t handle = server_handle;

    CHECK_HANDLE(server_handle);
    return _kadm5_chpass_principal_util(handle, handle->lhandle, princ,
                                        new_pw, ret_pw, msg_ret, msg_len);
}
