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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <errno.h>
#include <security/cryptoki.h>
#include <string.h>
#include <sys/crypto/ioctl.h>
#include "kernelGlobal.h"
#include "kernelSlot.h"

CK_ULONG	slot_count = 0;
kernel_slot_t	**slot_table;

static CK_RV
kernel_get_slot_number()
{
	CK_RV rv;
	crypto_get_provider_list_t *pl;
	int r;

	pl = malloc(sizeof (crypto_get_provider_list_t));
	if (pl == NULL)
		return (CKR_HOST_MEMORY);

	pl->pl_count = 0;
	while ((r = ioctl(kernel_fd, CRYPTO_GET_PROVIDER_LIST, pl)) < 0) {
		if (errno != EINTR)
			break;
	}
	if (r < 0) {
		rv = CKR_FUNCTION_FAILED;
	} else {
		if (pl->pl_return_value != CRYPTO_SUCCESS) {
			rv = crypto2pkcs11_error_number(pl->pl_return_value);
		} else {
			rv = CKR_OK;
		}
	}

	if (rv == CKR_OK) {
		slot_count = pl->pl_count;
	}

	(void) free(pl);
	return (rv);
}

/*
 * This function will be used by metaslot to get the kernel
 * provider's threshold value for the supported mechanisms.
 */
void
_SUNW_GetThreshold(void *thresholdp)
{

	cipher_mechs_threshold_t *tp = (cipher_mechs_threshold_t *)thresholdp;
	kernel_slot_t *pslot;
	int i;

	/*
	 * We alway use the 1st slot in the kernel to
	 * get the threshold because all the kernel
	 * slots will have the same threshold value
	 * with the same mechanism.
	 */
	pslot = slot_table[0];

	for (i = 0; i < pslot->total_threshold_count; i++) {
		tp[i].mech_type =
		    pslot->sl_mechs_threshold[i].mech_type;
		tp[i].mech_threshold =
		    pslot->sl_mechs_threshold[i].mech_threshold;
	}
}

/*
 * To retrieve the crypto_function_list structure with boolean entries
 * indicating which functions are supported by the hardware provider which
 * is specified by the slot ID.
 */
static CK_RV
kernel_get_func_list(kernel_slot_t *pslot)
{
	CK_RV rv = CKR_OK;
	crypto_get_function_list_t  fl;
	int r;
	int i;

	(void) memset(&fl, 0, sizeof (fl));
	fl.fl_provider_id = pslot->sl_provider_id;

	while ((r = ioctl(kernel_fd, CRYPTO_GET_FUNCTION_LIST, &fl)) < 0) {
		if (errno != EINTR)
			break;
	}
	if (r < 0) {
		rv = CKR_FUNCTION_FAILED;
	} else {
		if (fl.fl_return_value == 0) {
			rv = CKR_OK;
		} else {
			rv = crypto2pkcs11_error_number(fl.fl_return_value);
		}
	}

	if (rv != CKR_OK) {
		return (rv);
	}

	/* copy data structure received from kernel */
	pslot->sl_func_list = fl.fl_list;

	pslot->sl_flags = 0;
	if (fl.fl_list.prov_is_hash_limited) {
		pslot->sl_flags |= CRYPTO_LIMITED_HASH_SUPPORT;
		pslot->sl_hash_max_inlen = fl.fl_list.prov_hash_limit;
	}

	if (fl.fl_list.prov_is_hmac_limited) {
		pslot->sl_flags |= CRYPTO_LIMITED_HMAC_SUPPORT;
		pslot->sl_hmac_max_inlen = fl.fl_list.prov_hmac_limit;
	}

	if (fl.fl_list.prov_is_hash_limited | fl.fl_list.prov_is_hmac_limited) {
		pslot->sl_threshold = fl.fl_list.prov_hash_threshold;
	}

	pslot->total_threshold_count = fl.fl_list.total_threshold_count;

	for (i = 0; i < pslot->total_threshold_count; i++) {
		pslot->sl_mechs_threshold[i].mech_type =
		    fl.fl_list.fl_threshold[i].mech_type;
		pslot->sl_mechs_threshold[i].mech_threshold =
		    fl.fl_list.fl_threshold[i].mech_threshold;
	}

	return (CKR_OK);
}

/*
 * Initialize the slot table.
 *
 * This function is called from C_Initialize() only.  Since C_Initialize()
 * holds the global mutex lock, there is no need to acquire another lock
 * in this routine to protect the slot table.
 */
CK_RV
kernel_slottable_init()
{
	int i, cur_slot_num = 0;
	CK_RV rv = CKR_OK;
	crypto_get_provider_list_t *pl = NULL;
	int r;

	/*
	 * Find out how many slots are presented from kernel hardware
	 * providers. If there is no slot presented, just return.
	 */
	rv = kernel_get_slot_number();
	if (rv != CKR_OK || slot_count == 0) {
		return (rv);
	}

	/* Allocate space for the slot table */
	slot_table = malloc(sizeof (kernel_slot_t *) * slot_count);
	if (slot_table == NULL) {
		return (CKR_HOST_MEMORY);
	}

	/* For each slot, allocate space and initialize the slot's mutex. */
	for (i = 0; i < slot_count; i++) {
		slot_table[i] = malloc(sizeof (kernel_slot_t));
		if (slot_table[i] == NULL) {
			rv = CKR_HOST_MEMORY;
			goto failed;
		}

		slot_table[i]->sl_sess_list = NULL;
		slot_table[i]->sl_tobj_list = NULL;
		slot_table[i]->sl_state = CKU_PUBLIC;

		/* Initialize this slot's mutex */
		if (pthread_mutex_init(&slot_table[i]->sl_mutex, NULL) != 0) {
			rv = CKR_FUNCTION_FAILED;
			(void) free(slot_table[i]);
			goto failed;
		}

		cur_slot_num = i;
	}

	/*
	 * Get the provider ID for each slot from kernel and save it in the
	 * slot table.
	 */
	pl = malloc(slot_count * sizeof (crypto_get_provider_list_t));
	if (pl == NULL) {
		rv = CKR_HOST_MEMORY;
		goto failed;
	}

	pl->pl_count = slot_count;
	while ((r = ioctl(kernel_fd, CRYPTO_GET_PROVIDER_LIST, pl)) < 0) {
		if (errno != EINTR)
			break;
	}
	if (r < 0) {
		rv = CKR_FUNCTION_FAILED;
		goto failed;
	} else {
		if (pl->pl_return_value != CRYPTO_SUCCESS) {
			rv = crypto2pkcs11_error_number(pl->pl_return_value);
			goto failed;
		} else {
			rv = CKR_OK;
		}
	}

	for (i = 0; i < slot_count; i++) {
		slot_table[i]->sl_provider_id = pl->pl_list[i].pe_provider_id;
	}

	/*
	 * Get the function list for each slot from kernel and save it in
	 * the slot table.
	 */
	for (i = 0; i < slot_count; i++) {
		rv = kernel_get_func_list(slot_table[i]);
		if (rv != CKR_OK) {
			goto failed;
		}
	}

	(void) free(pl);
	return (CKR_OK);

failed:
	for (i = 0; i < cur_slot_num; i++) {
		(void) pthread_mutex_destroy(&slot_table[i]->sl_mutex);
		(void) free(slot_table[i]);
	}

	(void) free(slot_table);
	(void) free(pl);
	return (rv);
}
