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

#include "k5-int.h"

krb5_error_code
krb5_copy_kdc_req(krb5_context context, const krb5_kdc_req *in,
    krb5_kdc_req **out)
{
	krb5_kdc_req *req;
	krb5_error_code code = 0;

	req = calloc(1, sizeof (krb5_kdc_req));
	if (req == NULL)
		return (ENOMEM);

	req->magic = in->magic;
	req->msg_type = in->msg_type;

	if (in->padata != NULL) {
		code = krb5_copy_pa_datas(context, in->padata, &req->padata);
		if (code != 0)
			goto out;
	}

	req->kdc_options = in->kdc_options;

	if (in->client != NULL) {
		code = krb5_copy_principal(context, in->client, &req->client);
		if (code != 0)
			goto out;
	}

	if (in->server != NULL) {
		code = krb5_copy_principal(context, in->server, &req->server);
		if (code != 0)
			goto out;
	}

	req->from = in->from;
	req->till = in->till;
	req->rtime = in->rtime;

	req->nonce = in->nonce;

	if (in->nktypes != 0) {
		req->ktype = calloc(in->nktypes, sizeof (krb5_enctype));
		if (req->ktype == NULL) {
			code = ENOMEM;
			goto out;
		}
		memcpy(req->ktype, in->ktype,
		    in->nktypes * sizeof (krb5_enctype));
		req->nktypes = in->nktypes;
	}

	if (in->addresses != NULL) {
		code = krb5_copy_addresses(context, in->addresses,
		    &req->addresses);
		if (code != 0)
			goto out;
	}

	req->authorization_data.magic = in->authorization_data.magic;
	req->authorization_data.enctype = in->authorization_data.enctype;
	req->authorization_data.kvno = in->authorization_data.kvno;
	if (in->authorization_data.ciphertext.data != NULL) {
		code = krb5int_copy_data_contents(context,
		    &in->authorization_data.ciphertext,
		    &req->authorization_data.ciphertext);
		if (code != 0)
			goto out;
	}

	if (in->unenc_authdata != NULL) {
		code = krb5_copy_authdata(context, in->unenc_authdata,
		    &req->unenc_authdata);
		if (code != 0)
			goto out;
	}

	if (in->second_ticket != NULL) {
		code = krb5_copy_tickets(context, in->second_ticket,
		    &req->second_ticket);
		if (code != 0)
			goto out;
	}

	req->kdc_state = in->kdc_state;

out:
	if (code != 0)
		krb5_free_kdc_req(context, req);
	else
		*out = req;

	return (code);
}
