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
krb5_copy_pa_data(krb5_context context, const krb5_pa_data *in,
    krb5_pa_data **out)
{
	krb5_pa_data *pa_data;

	pa_data = malloc(sizeof (krb5_pa_data));
	if (pa_data == NULL)
		return (ENOMEM);

	pa_data->magic = in->magic;
	pa_data->pa_type = in->pa_type;

	pa_data->contents = malloc(in->length);
	if (pa_data->contents == 0) {
		free(pa_data);
		return (ENOMEM);
	}
	memcpy(pa_data->contents, in->contents, in->length);
	pa_data->length = in->length;

	*out = pa_data;

	return (0);
}

krb5_error_code
krb5_copy_pa_datas(krb5_context context, krb5_pa_data * const *in,
    krb5_pa_data ***out)
{
	krb5_pa_data **pa_data;
	unsigned int i;
	krb5_error_code code = 0;

	for (i = 0; in[i] != NULL; i++)
		;

	pa_data = calloc(i + 1, sizeof (krb5_pa_data *));
	if (pa_data == NULL)
		return (ENOMEM);

	for (i = 0; in[i] != NULL; i++) {
		code = krb5_copy_pa_data(context, in[i], &pa_data[i]);
		if (code)
			break;
	}

	if (code)
		krb5_free_pa_data(context, pa_data);
	else
		*out = pa_data;

	return (code);
}
