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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file is part of the core Kernel Cryptographic Framework.
 * It implements the management of tables of Providers. Entries to
 * added and removed when cryptographic providers register with
 * and unregister from the framework, respectively. The KCF scheduler
 * and ioctl pseudo driver call this function to obtain the list
 * of available providers.
 *
 * The provider table is indexed by crypto_provider_id_t. Each
 * element of the table contains a pointer to a provider descriptor,
 * or NULL if the entry is free.
 *
 * This file also implements helper functions to allocate and free
 * provider descriptors.
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/spi.h>

#define	KCF_MAX_PROVIDERS	512	/* max number of providers */

/*
 * Prov_tab is an array of providers which is updated when
 * a crypto provider registers with kcf. The provider calls the
 * SPI routine, crypto_register_provider(), which in turn calls
 * kcf_prov_tab_add_provider().
 *
 * A provider unregisters by calling crypto_unregister_provider()
 * which triggers the removal of the prov_tab entry.
 * It also calls kcf_remove_mech_provider().
 *
 * prov_tab entries are not updated from kcf.conf or by cryptoadm(1M).
 */
static kcf_provider_desc_t **prov_tab = NULL;
kmutex_t prov_tab_mutex; /* ensure exclusive access to the table */
static uint_t prov_tab_num = 0; /* number of providers in table */
static uint_t prov_tab_max = KCF_MAX_PROVIDERS;

static void kcf_free_unregistered_provs();
#if DEBUG
extern int kcf_frmwrk_debug;
static void kcf_prov_tab_dump(char *message);
#endif /* DEBUG */


/*
 * Initialize a mutex and the KCF providers table, prov_tab.
 * The providers table is dynamically allocated with prov_tab_max entries.
 * Called from kcf module _init().
 */
void
kcf_prov_tab_init(void)
{
	mutex_init(&prov_tab_mutex, NULL, MUTEX_DRIVER, NULL);

	prov_tab = kmem_zalloc(prov_tab_max * sizeof (kcf_provider_desc_t *),
	    KM_SLEEP);
}

/*
 * Add a provider to the provider table. If no free entry can be found
 * for the new provider, returns CRYPTO_HOST_MEMORY. Otherwise, add
 * the provider to the table, initialize the pd_prov_id field
 * of the specified provider descriptor to the index in that table,
 * and return CRYPTO_SUCCESS. Note that a REFHOLD is done on the
 * provider when pointed to by a table entry.
 */
int
kcf_prov_tab_add_provider(kcf_provider_desc_t *prov_desc)
{
	uint_t i;

	ASSERT(prov_tab != NULL);

	mutex_enter(&prov_tab_mutex);

	/* see if any slots can be freed */
	if (kcf_need_provtab_walk)
		kcf_free_unregistered_provs();

	/* find free slot in providers table */
	for (i = 0; i < KCF_MAX_PROVIDERS && prov_tab[i] != NULL; i++)
		;
	if (i == KCF_MAX_PROVIDERS) {
		/* ran out of providers entries */
		mutex_exit(&prov_tab_mutex);
		cmn_err(CE_WARN, "out of providers entries");
		return (CRYPTO_HOST_MEMORY);
	}

	/* initialize entry */
	prov_tab[i] = prov_desc;
	KCF_PROV_REFHOLD(prov_desc);
	prov_tab_num++;

	mutex_exit(&prov_tab_mutex);

	/* update provider descriptor */
	prov_desc->pd_prov_id = i;

	/*
	 * The KCF-private provider handle is defined as the internal
	 * provider id.
	 */
	prov_desc->pd_kcf_prov_handle =
	    (crypto_kcf_provider_handle_t)prov_desc->pd_prov_id;

#if DEBUG
	if (kcf_frmwrk_debug >= 1)
		kcf_prov_tab_dump("kcf_prov_tab_add_provider");
#endif /* DEBUG */

	return (CRYPTO_SUCCESS);
}

/*
 * Remove the provider specified by its id. A REFRELE is done on the
 * corresponding provider descriptor before this function returns.
 * Returns CRYPTO_UNKNOWN_PROVIDER if the provider id is not valid.
 */
int
kcf_prov_tab_rem_provider(crypto_provider_id_t prov_id)
{
	kcf_provider_desc_t *prov_desc;

	ASSERT(prov_tab != NULL);
	ASSERT(prov_tab_num != (uint_t)-1); /* underflow */

	/*
	 * Validate provider id, since it can be specified by a 3rd-party
	 * provider.
	 */

	mutex_enter(&prov_tab_mutex);
	if (prov_id >= KCF_MAX_PROVIDERS ||
	    ((prov_desc = prov_tab[prov_id]) == NULL)) {
		mutex_exit(&prov_tab_mutex);
		return (CRYPTO_INVALID_PROVIDER_ID);
	}

	if (kcf_need_provtab_walk)
		kcf_free_unregistered_provs();
	mutex_exit(&prov_tab_mutex);

	/*
	 * The provider id must remain valid until the associated provider
	 * descriptor is freed. For this reason, we simply release our
	 * reference to the descriptor here. When the reference count
	 * reaches zero, kcf_free_provider_desc() will be invoked and
	 * the associated entry in the providers table will be released
	 * at that time.
	 */

	KCF_PROV_REFRELE(prov_desc);

#if DEBUG
	if (kcf_frmwrk_debug >= 1)
		kcf_prov_tab_dump("kcf_prov_tab_rem_provider");
#endif /* DEBUG */

	return (CRYPTO_SUCCESS);
}

/*
 * Returns the provider descriptor corresponding to the specified
 * provider id. A REFHOLD is done on the descriptor before it is
 * returned to the caller. It is the responsibility of the caller
 * to do a REFRELE once it is done with the provider descriptor.
 */
kcf_provider_desc_t *
kcf_prov_tab_lookup(crypto_provider_id_t prov_id)
{
	kcf_provider_desc_t *prov_desc;

	mutex_enter(&prov_tab_mutex);

	prov_desc = prov_tab[prov_id];

	if (prov_desc == NULL) {
		mutex_exit(&prov_tab_mutex);
		return (NULL);
	}

	KCF_PROV_REFHOLD(prov_desc);

	mutex_exit(&prov_tab_mutex);

	return (prov_desc);
}

static void
allocate_ops_v1(crypto_ops_t *src, crypto_ops_t *dst, uint_t *mech_list_count)
{
	if (src->co_control_ops != NULL)
		dst->co_control_ops = kmem_alloc(sizeof (crypto_control_ops_t),
		    KM_SLEEP);

	if (src->co_digest_ops != NULL)
		dst->co_digest_ops = kmem_alloc(sizeof (crypto_digest_ops_t),
		    KM_SLEEP);

	if (src->co_cipher_ops != NULL)
		dst->co_cipher_ops = kmem_alloc(sizeof (crypto_cipher_ops_t),
		    KM_SLEEP);

	if (src->co_mac_ops != NULL)
		dst->co_mac_ops = kmem_alloc(sizeof (crypto_mac_ops_t),
		    KM_SLEEP);

	if (src->co_sign_ops != NULL)
		dst->co_sign_ops = kmem_alloc(sizeof (crypto_sign_ops_t),
		    KM_SLEEP);

	if (src->co_verify_ops != NULL)
		dst->co_verify_ops = kmem_alloc(sizeof (crypto_verify_ops_t),
		    KM_SLEEP);

	if (src->co_dual_ops != NULL)
		dst->co_dual_ops = kmem_alloc(sizeof (crypto_dual_ops_t),
		    KM_SLEEP);

	if (src->co_dual_cipher_mac_ops != NULL)
		dst->co_dual_cipher_mac_ops = kmem_alloc(
		    sizeof (crypto_dual_cipher_mac_ops_t), KM_SLEEP);

	if (src->co_random_ops != NULL) {
		dst->co_random_ops = kmem_alloc(
		    sizeof (crypto_random_number_ops_t), KM_SLEEP);

		/*
		 * Allocate storage to store the array of supported mechanisms
		 * specified by provider. We allocate extra mechanism storage
		 * if the provider has random_ops since we keep an internal
		 * mechanism, SUN_RANDOM, in this case.
		 */
		(*mech_list_count)++;
	}

	if (src->co_session_ops != NULL)
		dst->co_session_ops = kmem_alloc(sizeof (crypto_session_ops_t),
		    KM_SLEEP);

	if (src->co_object_ops != NULL)
		dst->co_object_ops = kmem_alloc(sizeof (crypto_object_ops_t),
		    KM_SLEEP);

	if (src->co_key_ops != NULL)
		dst->co_key_ops = kmem_alloc(sizeof (crypto_key_ops_t),
		    KM_SLEEP);

	if (src->co_provider_ops != NULL)
		dst->co_provider_ops = kmem_alloc(
		    sizeof (crypto_provider_management_ops_t), KM_SLEEP);

	if (src->co_ctx_ops != NULL)
		dst->co_ctx_ops = kmem_alloc(sizeof (crypto_ctx_ops_t),
		    KM_SLEEP);
}

static void
allocate_ops_v2(crypto_ops_t *src, crypto_ops_t *dst)
{
	if (src->co_mech_ops != NULL)
		dst->co_mech_ops = kmem_alloc(sizeof (crypto_mech_ops_t),
		    KM_SLEEP);
}

static void
allocate_ops_v3(crypto_ops_t *src, crypto_ops_t *dst)
{
	if (src->co_nostore_key_ops != NULL)
		dst->co_nostore_key_ops =
		    kmem_alloc(sizeof (crypto_nostore_key_ops_t), KM_SLEEP);
}

static void
allocate_ops_v4(crypto_ops_t *src, crypto_ops_t *dst)
{
	if (src->co_fips140_ops != NULL)
		dst->co_fips140_ops =
		    kmem_alloc(sizeof (crypto_fips140_ops_t), KM_SLEEP);
}

/*
 * Allocate a provider descriptor. mech_list_count specifies the
 * number of mechanisms supported by the providers, and is used
 * to allocate storage for the mechanism table.
 * This function may sleep while allocating memory, which is OK
 * since it is invoked from user context during provider registration.
 */
kcf_provider_desc_t *
kcf_alloc_provider_desc(crypto_provider_info_t *info)
{
	int i, j;
	kcf_provider_desc_t *desc;
	uint_t mech_list_count = info->pi_mech_list_count;
	crypto_ops_t *src_ops = info->pi_ops_vector;

	desc = kmem_zalloc(sizeof (kcf_provider_desc_t), KM_SLEEP);

	/*
	 * pd_description serves two purposes
	 * - Appears as a blank padded PKCS#11 style string, that will be
	 *   returned to applications in CK_SLOT_INFO.slotDescription.
	 *   This means that we should not have a null character in the
	 *   first CRYPTO_PROVIDER_DESCR_MAX_LEN bytes.
	 * - Appears as a null-terminated string that can be used by
	 *   other kcf routines.
	 *
	 * So, we allocate enough room for one extra null terminator
	 * which keeps every one happy.
	 */
	desc->pd_description = kmem_alloc(CRYPTO_PROVIDER_DESCR_MAX_LEN + 1,
	    KM_SLEEP);
	(void) memset(desc->pd_description, ' ',
	    CRYPTO_PROVIDER_DESCR_MAX_LEN);
	desc->pd_description[CRYPTO_PROVIDER_DESCR_MAX_LEN] = '\0';

	/*
	 * Since the framework does not require the ops vector specified
	 * by the providers during registration to be persistent,
	 * KCF needs to allocate storage where copies of the ops
	 * vectors are copied.
	 */
	desc->pd_ops_vector = kmem_zalloc(sizeof (crypto_ops_t), KM_SLEEP);

	if (info->pi_provider_type != CRYPTO_LOGICAL_PROVIDER) {
		allocate_ops_v1(src_ops, desc->pd_ops_vector, &mech_list_count);
		if (info->pi_interface_version >= CRYPTO_SPI_VERSION_2)
			allocate_ops_v2(src_ops, desc->pd_ops_vector);
		if (info->pi_interface_version >= CRYPTO_SPI_VERSION_3)
			allocate_ops_v3(src_ops, desc->pd_ops_vector);
		if (info->pi_interface_version == CRYPTO_SPI_VERSION_4)
			allocate_ops_v4(src_ops, desc->pd_ops_vector);
	}

	desc->pd_mech_list_count = mech_list_count;
	desc->pd_mechanisms = kmem_zalloc(sizeof (crypto_mech_info_t) *
	    mech_list_count, KM_SLEEP);
	for (i = 0; i < KCF_OPS_CLASSSIZE; i++)
		for (j = 0; j < KCF_MAXMECHTAB; j++)
			desc->pd_mech_indx[i][j] = KCF_INVALID_INDX;

	desc->pd_prov_id = KCF_PROVID_INVALID;
	desc->pd_state = KCF_PROV_ALLOCATED;

	mutex_init(&desc->pd_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&desc->pd_resume_cv, NULL, CV_DEFAULT, NULL);

	desc->pd_nbins = max_ncpus;
	desc->pd_percpu_bins =
	    kmem_zalloc(desc->pd_nbins * sizeof (kcf_prov_cpu_t), KM_SLEEP);

	return (desc);
}

/*
 * Free a provider descriptor. Caller must hold prov_tab_mutex.
 *
 * Caution: This routine drops prov_tab_mutex.
 */
void
kcf_free_provider_desc(kcf_provider_desc_t *desc)
{
	if (desc == NULL)
		return;

	ASSERT(MUTEX_HELD(&prov_tab_mutex));
	if (desc->pd_prov_id != KCF_PROVID_INVALID) {
		/* release the associated providers table entry */
		ASSERT(prov_tab[desc->pd_prov_id] != NULL);
		prov_tab[desc->pd_prov_id] = NULL;
		prov_tab_num--;
	}
	mutex_exit(&prov_tab_mutex);

	/* free the kernel memory associated with the provider descriptor */

	if (desc->pd_description != NULL)
		kmem_free(desc->pd_description,
		    CRYPTO_PROVIDER_DESCR_MAX_LEN + 1);

	if (desc->pd_ops_vector != NULL) {

		if (desc->pd_ops_vector->co_control_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_control_ops,
			    sizeof (crypto_control_ops_t));

		if (desc->pd_ops_vector->co_digest_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_digest_ops,
			    sizeof (crypto_digest_ops_t));

		if (desc->pd_ops_vector->co_cipher_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_cipher_ops,
			    sizeof (crypto_cipher_ops_t));

		if (desc->pd_ops_vector->co_mac_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_mac_ops,
			    sizeof (crypto_mac_ops_t));

		if (desc->pd_ops_vector->co_sign_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_sign_ops,
			    sizeof (crypto_sign_ops_t));

		if (desc->pd_ops_vector->co_verify_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_verify_ops,
			    sizeof (crypto_verify_ops_t));

		if (desc->pd_ops_vector->co_dual_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_dual_ops,
			    sizeof (crypto_dual_ops_t));

		if (desc->pd_ops_vector->co_dual_cipher_mac_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_dual_cipher_mac_ops,
			    sizeof (crypto_dual_cipher_mac_ops_t));

		if (desc->pd_ops_vector->co_random_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_random_ops,
			    sizeof (crypto_random_number_ops_t));

		if (desc->pd_ops_vector->co_session_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_session_ops,
			    sizeof (crypto_session_ops_t));

		if (desc->pd_ops_vector->co_object_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_object_ops,
			    sizeof (crypto_object_ops_t));

		if (desc->pd_ops_vector->co_key_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_key_ops,
			    sizeof (crypto_key_ops_t));

		if (desc->pd_ops_vector->co_provider_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_provider_ops,
			    sizeof (crypto_provider_management_ops_t));

		if (desc->pd_ops_vector->co_ctx_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_ctx_ops,
			    sizeof (crypto_ctx_ops_t));

		if (desc->pd_ops_vector->co_mech_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_mech_ops,
			    sizeof (crypto_mech_ops_t));

		if (desc->pd_ops_vector->co_nostore_key_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_nostore_key_ops,
			    sizeof (crypto_nostore_key_ops_t));

		if (desc->pd_ops_vector->co_fips140_ops != NULL)
			kmem_free(desc->pd_ops_vector->co_fips140_ops,
			    sizeof (crypto_fips140_ops_t));

		kmem_free(desc->pd_ops_vector, sizeof (crypto_ops_t));
	}

	if (desc->pd_mechanisms != NULL)
		/* free the memory associated with the mechanism info's */
		kmem_free(desc->pd_mechanisms, sizeof (crypto_mech_info_t) *
		    desc->pd_mech_list_count);

	if (desc->pd_name != NULL) {
		kmem_free(desc->pd_name, strlen(desc->pd_name) + 1);
	}

	if (desc->pd_taskq != NULL)
		taskq_destroy(desc->pd_taskq);

	if (desc->pd_percpu_bins != NULL) {
		kmem_free(desc->pd_percpu_bins,
		    desc->pd_nbins * sizeof (kcf_prov_cpu_t));
	}

	kmem_free(desc, sizeof (kcf_provider_desc_t));
}

/*
 * Returns the provider descriptor corresponding to the specified
 * module name. A REFHOLD is done on the descriptor before it is
 * returned to the caller. It is the responsibility of the caller
 * to do a REFRELE once it is done with the provider descriptor.
 * Only software providers are returned by this function.
 */
kcf_provider_desc_t *
kcf_prov_tab_lookup_by_name(char *module_name)
{
	kcf_provider_desc_t *prov_desc;
	uint_t i;

	mutex_enter(&prov_tab_mutex);

	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    (!KCF_IS_PROV_REMOVED(prov_desc)) &&
		    prov_desc->pd_prov_type == CRYPTO_SW_PROVIDER) {
			ASSERT(prov_desc->pd_name != NULL);
			if (strncmp(module_name, prov_desc->pd_name,
			    MAXNAMELEN) == 0) {
				KCF_PROV_REFHOLD(prov_desc);
				mutex_exit(&prov_tab_mutex);
				return (prov_desc);
			}
		}
	}

	mutex_exit(&prov_tab_mutex);
	return (NULL);
}

/*
 * Returns the provider descriptor corresponding to the specified
 * device name and instance. A REFHOLD is done on the descriptor
 * before it is returned to the caller. It is the responsibility
 * of the caller to do a REFRELE once it is done with the provider
 * descriptor. Only hardware providers are returned by this function.
 */
kcf_provider_desc_t *
kcf_prov_tab_lookup_by_dev(char *name, uint_t instance)
{
	kcf_provider_desc_t *prov_desc;
	uint_t i;

	mutex_enter(&prov_tab_mutex);

	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    (!KCF_IS_PROV_REMOVED(prov_desc)) &&
		    prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER) {
			ASSERT(prov_desc->pd_name != NULL);
			if (strncmp(prov_desc->pd_name, name,
			    MAXNAMELEN) == 0 &&
			    prov_desc->pd_instance == instance) {
				KCF_PROV_REFHOLD(prov_desc);
				mutex_exit(&prov_tab_mutex);
				return (prov_desc);
			}
		}
	}

	mutex_exit(&prov_tab_mutex);
	return (NULL);
}

/*
 * Returns an array of hardware and logical provider descriptors,
 * a.k.a the PKCS#11 slot list. A REFHOLD is done on each descriptor
 * before the array is returned. The entire table can be freed by
 * calling kcf_free_provider_tab().
 */
int
kcf_get_slot_list(uint_t *count, kcf_provider_desc_t ***array,
    boolean_t unverified)
{
	kcf_provider_desc_t *prov_desc;
	kcf_provider_desc_t **p = NULL;
	char *last;
	uint_t cnt = 0;
	uint_t i, j;
	int rval = CRYPTO_SUCCESS;
	size_t n, final_size;

	/* count the providers */
	mutex_enter(&prov_tab_mutex);
	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    ((prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    (prov_desc->pd_flags & CRYPTO_HIDE_PROVIDER) == 0) ||
		    prov_desc->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)) {
			if (KCF_IS_PROV_USABLE(prov_desc) ||
			    (unverified && KCF_IS_PROV_UNVERIFIED(prov_desc))) {
				cnt++;
			}
		}
	}
	mutex_exit(&prov_tab_mutex);

	if (cnt == 0)
		goto out;

	n = cnt * sizeof (kcf_provider_desc_t *);
again:
	p = kmem_zalloc(n, KM_SLEEP);

	/* pointer to last entry in the array */
	last = (char *)&p[cnt-1];

	mutex_enter(&prov_tab_mutex);
	/* fill the slot list */
	for (i = 0, j = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    ((prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER &&
		    (prov_desc->pd_flags & CRYPTO_HIDE_PROVIDER) == 0) ||
		    prov_desc->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)) {
			if (KCF_IS_PROV_USABLE(prov_desc) ||
			    (unverified && KCF_IS_PROV_UNVERIFIED(prov_desc))) {
				if ((char *)&p[j] > last) {
					mutex_exit(&prov_tab_mutex);
					kcf_free_provider_tab(cnt, p);
					n = n << 1;
					cnt = cnt << 1;
					goto again;
				}
				p[j++] = prov_desc;
				KCF_PROV_REFHOLD(prov_desc);
			}
		}
	}
	mutex_exit(&prov_tab_mutex);

	final_size = j * sizeof (kcf_provider_desc_t *);
	cnt = j;
	ASSERT(final_size <= n);

	/* check if buffer we allocated is too large */
	if (final_size < n) {
		char *final_buffer = NULL;

		if (final_size > 0) {
			final_buffer = kmem_alloc(final_size, KM_SLEEP);
			bcopy(p, final_buffer, final_size);
		}
		kmem_free(p, n);
		p = (kcf_provider_desc_t **)(void *)final_buffer;
	}
out:
	*count = cnt;
	*array = p;
	return (rval);
}

/*
 * Returns an array of hardware provider descriptors. This routine
 * used by cryptoadm(1M). A REFHOLD is done on each descriptor before
 * the array is returned. The entire table can be freed by calling
 * kcf_free_provider_tab().
 *
 * A NULL name argument puts all hardware providers in the array.
 * A non-NULL name argument puts only those providers in the array
 * which match the name and instance arguments.
 */
int
kcf_get_hw_prov_tab(uint_t *count, kcf_provider_desc_t ***array,  int kmflag,
    char *name, uint_t instance, boolean_t unverified)
{
	kcf_provider_desc_t *prov_desc;
	kcf_provider_desc_t **p = NULL;
	char *last;
	uint_t cnt = 0;
	uint_t i, j;
	int rval = CRYPTO_SUCCESS;
	size_t n, final_size;

	/* count the providers */
	mutex_enter(&prov_tab_mutex);
	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER) {
			if (KCF_IS_PROV_USABLE(prov_desc) ||
			    (unverified && KCF_IS_PROV_UNVERIFIED(prov_desc))) {
				if (name == NULL ||
				    (strncmp(prov_desc->pd_name, name,
				    MAXNAMELEN) == 0 &&
				    prov_desc->pd_instance == instance)) {
					cnt++;
				}
			}
		}
	}
	mutex_exit(&prov_tab_mutex);

	if (cnt == 0)
		goto out;

	n = cnt * sizeof (kcf_provider_desc_t *);
again:
	p = kmem_zalloc(n, kmflag);
	if (p == NULL) {
		rval = CRYPTO_HOST_MEMORY;
		goto out;
	}
	/* pointer to last entry in the array */
	last = (char *)&p[cnt-1];

	mutex_enter(&prov_tab_mutex);
	for (i = 0, j = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((prov_desc = prov_tab[i]) != NULL &&
		    prov_desc->pd_prov_type == CRYPTO_HW_PROVIDER) {
			if (KCF_IS_PROV_USABLE(prov_desc) ||
			    (unverified && KCF_IS_PROV_UNVERIFIED(prov_desc))) {
				if (name == NULL ||
				    (strncmp(prov_desc->pd_name, name,
				    MAXNAMELEN) == 0 &&
				    prov_desc->pd_instance == instance)) {
					if ((char *)&p[j] > last) {
						mutex_exit(&prov_tab_mutex);
						kcf_free_provider_tab(cnt, p);
						n = n << 1;
						cnt = cnt << 1;
						goto again;
					}
					p[j++] = prov_desc;
					KCF_PROV_REFHOLD(prov_desc);
				}
			}
		}
	}
	mutex_exit(&prov_tab_mutex);

	final_size = j * sizeof (kcf_provider_desc_t *);
	ASSERT(final_size <= n);

	/* check if buffer we allocated is too large */
	if (final_size < n) {
		char *final_buffer = NULL;

		if (final_size > 0) {
			final_buffer = kmem_alloc(final_size, kmflag);
			if (final_buffer == NULL) {
				kcf_free_provider_tab(cnt, p);
				cnt = 0;
				p = NULL;
				rval = CRYPTO_HOST_MEMORY;
				goto out;
			}
			bcopy(p, final_buffer, final_size);
		}
		kmem_free(p, n);
		p = (kcf_provider_desc_t **)(void *)final_buffer;
	}
	cnt = j;
out:
	*count = cnt;
	*array = p;
	return (rval);
}

/*
 * Free an array of hardware provider descriptors.  A REFRELE
 * is done on each descriptor before the table is freed.
 */
void
kcf_free_provider_tab(uint_t count, kcf_provider_desc_t **array)
{
	kcf_provider_desc_t *prov_desc;
	int i;

	for (i = 0; i < count; i++) {
		if ((prov_desc = array[i]) != NULL) {
			KCF_PROV_REFRELE(prov_desc);
		}
	}
	kmem_free(array, count * sizeof (kcf_provider_desc_t *));
}

/*
 * Returns in the location pointed to by pd a pointer to the descriptor
 * for the software provider for the specified mechanism.
 * The provider descriptor is returned held and it is the caller's
 * responsibility to release it when done. The mechanism entry
 * is returned if the optional argument mep is non NULL.
 *
 * Returns one of the CRYPTO_ * error codes on failure, and
 * CRYPTO_SUCCESS on success.
 */
int
kcf_get_sw_prov(crypto_mech_type_t mech_type, kcf_provider_desc_t **pd,
    kcf_mech_entry_t **mep, boolean_t log_warn)
{
	kcf_mech_entry_t *me;
	kcf_lock_withpad_t *mp;

	/* get the mechanism entry for this mechanism */
	if (kcf_get_mech_entry(mech_type, &me) != KCF_SUCCESS)
		return (CRYPTO_MECHANISM_INVALID);

	/*
	 * Get the software provider for this mechanism.
	 * Lock the mech_entry until we grab the 'pd'.
	 */
	mp = &me_mutexes[CPU_SEQID];
	mutex_enter(&mp->kl_lock);

	if (me->me_sw_prov == NULL ||
	    (*pd = me->me_sw_prov->pm_prov_desc) == NULL) {
		/* no SW provider for this mechanism */
		if (log_warn)
			cmn_err(CE_WARN, "no SW provider for \"%s\"\n",
			    me->me_name);
		mutex_exit(&mp->kl_lock);
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	KCF_PROV_REFHOLD(*pd);
	mutex_exit(&mp->kl_lock);

	if (mep != NULL)
		*mep = me;

	return (CRYPTO_SUCCESS);
}

#if DEBUG
/*
 * Dump the Kernel crypto providers table, prov_tab.
 * If kcf_frmwrk_debug is >=2, also dump the mechanism lists.
 */
static void
kcf_prov_tab_dump(char *message)
{
	uint_t i, j;

	mutex_enter(&prov_tab_mutex);
	printf("Providers table prov_tab at %s:\n",
	    message != NULL ? message : "");

	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		kcf_provider_desc_t *p = prov_tab[i];
		if (p != NULL) {
			printf("[%d]: (%s) %d mechanisms, %s\n", i,
			    (p->pd_prov_type == CRYPTO_HW_PROVIDER) ?
			    "HW" : "SW",
			    p->pd_mech_list_count, p->pd_description);
			if (kcf_frmwrk_debug >= 2) {
				printf("\tpd_mechanisms: ");
				for (j = 0; j < p->pd_mech_list_count; ++j) {
					printf("%s \n",
					    p->pd_mechanisms[j].cm_mech_name);
				}
				printf("\n");
			}
		}
	}
	printf("(end of providers table)\n");

	mutex_exit(&prov_tab_mutex);
}

#endif /* DEBUG */

/*
 * This function goes through the provider table and marks
 * any non FIPS providers as ready, as they no longer need
 * any type of verification.  It also makes sure all necessary
 * notifications happen as this provider is officially registered.
 *
 */
void
verify_unverified_providers()
{
	int i;
	kcf_provider_desc_t *pd;
	boolean_t do_notify = B_FALSE;

	mutex_enter(&prov_tab_mutex);

	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((pd = prov_tab[i]) == NULL)
			continue;

		if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
			continue;

		KCF_PROV_REFHOLD(pd);

		mutex_enter(&pd->pd_lock);
		if (pd->pd_state == KCF_PROV_UNVERIFIED) {
			pd->pd_state = KCF_PROV_READY;
			do_notify = B_TRUE;
		}

		mutex_exit(&pd->pd_lock);

		if (do_notify) {
			/* Dispatch events for this new provider */
			kcf_do_notify(pd, B_TRUE);
		}

		KCF_PROV_REFRELE(pd);

	}

	mutex_exit(&prov_tab_mutex);
}

/* protected by prov_tab_mutex */
boolean_t kcf_need_provtab_walk = B_FALSE;

/* Caller must hold prov_tab_mutex */
static void
kcf_free_unregistered_provs()
{
	int i;
	kcf_provider_desc_t *pd;
	boolean_t walk_again = B_FALSE;

	ASSERT(MUTEX_HELD(&prov_tab_mutex));
	for (i = 0; i < KCF_MAX_PROVIDERS; i++) {
		if ((pd = prov_tab[i]) == NULL ||
		    pd->pd_prov_type == CRYPTO_SW_PROVIDER ||
		    pd->pd_state != KCF_PROV_UNREGISTERED)
			continue;

		if (kcf_get_refcnt(pd, B_TRUE) == 0) {
			/* kcf_free_provider_desc drops prov_tab_mutex */
			kcf_free_provider_desc(pd);
			mutex_enter(&prov_tab_mutex);
		} else
			walk_again = B_TRUE;
	}

	kcf_need_provtab_walk = walk_again;
}
