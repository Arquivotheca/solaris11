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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <net/pfkeyv2.h>
#include <stdio.h>
#include <locale.h>

#include <ipsec_util.h>
#include <ike/pkcs11-glue.h>
#include <ike/sshcrypt_i.h>
#include "defs.h"
#include <ike/cmi-internal.h>
#include "lock.h"


/*
 * Functions to initialize and deal with the SSH Certificate Management
 * Interface (CMI).
 */

/* Globals... */
static SshCMConfig cm_config;
static SshCMContext cm_context;

static SshCMCertificate *ca_certs;
static int num_ca_certs;

extern boolean_t use_http;
extern boolean_t ignore_crls;

/*
 * Add a CRL we found associated with a certificate from certlib to the SSH CMI
 * CRL pool.
 */
static void
add_crl(struct certlib_crl *p)
{
	SshCMStatus rc;
	SshCMCrl crl;

	PRTDBG(D_CERT, ("Adding CRLS."));

	crl = ssh_cm_crl_allocate(cm_context);
	rc = ssh_cm_crl_set_ber(crl, p->data, p->datalen);

	if (rc != SSH_CM_STATUS_OK) {
		PRTDBG(D_CERT, ("  Failed to decode CRL, error code = %d (%s).",
		    rc, cm_status_to_string(rc)));
		ssh_cm_crl_free(crl);
		return;
	}
	rc = ssh_cm_add_crl(crl);
	if (rc != SSH_CM_STATUS_OK) {
		PRTDBG(D_CERT, ("  Failed to add CRL, error code = %d (%s).",
		    rc, cm_status_to_string(rc)));
		ssh_cm_crl_free(crl);
	}
}

/* ARGSUSED */
static void
dummy_destructor(SshCMCertificate cert, void *context)
{
	/* Empty function!  Other callers take care of things. */
}

/* ARGSUSED */
static void
active_destructor(SshCMCertificate cert, void *context)
{
	SshPublicKey nukeme = (SshPublicKey)context;

	ssh_public_key_free(nukeme);
}

/*
 * This is a certlib callback function for adding a certificate known to certlib
 * to the SSH CMI.  If this function returns non-zero, certlib will abort the
 * iteration but there is no reason to do that for a failure while processing
 * one particular cert, we want it to keep processing the rest of them, so we
 * always return 0.
 */
static int
add_cert(struct certlib_cert *p)
{
	SshCMStatus rc;
	SshCMCertificate newcert;
	SshBerTimeStruct not_before, not_after, now;
	SshX509Certificate x509;
	boolean_t signer = B_FALSE, trusted = B_FALSE, invalid = B_FALSE;
	uint8_t *data;
	size_t datalen;

	newcert = ssh_cm_cert_allocate(cm_context);
	/* This panics for us! :-P */
	if (newcert == NULL)	/* But we'll check anyway! */
		return (0);

	/*
	 * Mark the cm_cert with a private attribute (the accelerated
	 * key) to help out ssh_policy_find_public_key()!
	 */
	if (pre_accelerate_key(p, CKO_PUBLIC_KEY)) {
		(void) ssh_cm_cert_set_private_data(newcert, p->key,
		    dummy_destructor);
		PRTDBG(D_CERT, ("Pre-accelerated key for "
		    "public keyid \"%s\"", (p->pkcs11_id != '\0') ?
		    p->pkcs11_id : "<on-disk cert>"));
	} else {
		PRTDBG(D_CERT, ("Could not pre-accelerate key for "
		    "public keyid \"%s\"", (p->pkcs11_id != '\0') ?
		    p->pkcs11_id : "<on-disk cert>"));
		/* TODO:  When certlib can deal with deletes, do so. */
		ssh_cm_cert_free(newcert);
		return (0);
	}

	/* Get X.509 BER right from the certlib_cert's data. */
	data = p->data;
	datalen = p->datalen;

	rc = ssh_cm_cert_set_ber(newcert, data, datalen);
	if (rc != SSH_CM_STATUS_OK) {
		PRTDBG(D_CERT,
		    ("Could not initialize internal certificate "
		    "structure with BER encoded cert: error %d (%s).", rc,
		    cm_status_to_string(rc)));
		ssh_cm_cert_free(newcert);
		return (0);
	}

	/* Let's add it! */

	/* Should we trust it as a signer? */
	if (certlib_match_cert(p, &root_certs)) {
		SshCMCertificate *new_arr;
		int i;

		signer = B_TRUE;
		rc = ssh_cm_cert_force_trusted(newcert);
		if (rc != SSH_CM_STATUS_OK) {
			/*
			 * This function will always return OK in the current
			 * ssh library code; be dramatic here so we notice if
			 * this changes in a future version of the library.
			 */
			EXIT_FATAL("Failed to add certificate as signer!");
		}

		/*
		 * Put newcert into the ca_certs array.
		 */

		/* no increment num_ca_certs if the cert is in ca_certs. */
		for (i = 0; i < num_ca_certs; i++) {
			size_t ber_length;
			unsigned char *ber;
			rc = ssh_cm_cert_get_ber(ca_certs[i], &ber,
			    &ber_length);
			if ((rc != SSH_CM_STATUS_OK) ||
			    memcmp(data, ber, ber_length) == 0) {
				PRTDBG(D_CERT, ("Duplicate root CA"));
				ssh_cm_cert_free(newcert);
				return (0);
			}
		}
		PRTDBG(D_CERT, ("Number of CA Certs: %d.", num_ca_certs));
		new_arr = ssh_realloc(ca_certs,
		    num_ca_certs * sizeof (SshCMCertificate),
		    (num_ca_certs + 1) * sizeof (SshCMCertificate));
		if (new_arr != NULL) {
			ca_certs = new_arr;
			ca_certs[num_ca_certs] = newcert;
			num_ca_certs++;
		}
	}

	/* Should we trust it as an identity? */
	if (certlib_match_cert(p, &trusted_certs)) {
		trusted = B_TRUE;
		rc = ssh_cm_cert_force_trusted(newcert);
		if (rc != SSH_CM_STATUS_OK) {
			/*
			 * This function will always return OK in the current
			 * ssh library code; be dramatic here so we notice if
			 * this changes in a future version of the library.
			 */
			EXIT_FATAL("Failed to add certificate "
			    "as trusted identity!");
		}
	}

	if (ignore_crls) {
		/*
		 * Right now, ignore_crls is a global switch, and we may need
		 * per-cert granularity for this decision.
		 */
		(void) ssh_cm_cert_non_crl_issuer(newcert);
	} else if (p->crl != NULL) {
		/*
		 * LDAP and HTTP may be able to retreive CRLs for the cert
		 * also.  So don't despair if there's no CRL in
		 * /etc/inet/ike/crls/.
		 */
		add_crl(p->crl);
	}

	/*
	 * This iterator is called for on-disk certs.  Keep it in the
	 * CMI/Validator cache permanently.
	 */
	(void) ssh_cm_cert_set_locked(newcert);

	/* Add it to the CMI context. */
	if ((rc = ssh_cm_add(newcert)) != SSH_CM_STATUS_OK) {
		if (rc != SSH_CM_STATUS_ALREADY_EXISTS)
			PRTDBG(D_CERT,
			    ("Failed to add cert to slot %s, %s.",
			    p->slotname, cm_status_to_string(rc)));
		/* cert load once but not signer the first time */
		if (signer == B_FALSE)
			ssh_cm_cert_free(newcert);
		return (0);
	}
	/*
	 * Save the ssh struct for future operations...
	 */
	p->appdata = newcert;

	/* Do some validity date checks */
	if ((rc = ssh_cm_cert_get_x509(newcert, &x509)) !=
	    SSH_CM_STATUS_OK) {
		PRTDBG(D_CERT,
		    ("Failed to get x509 cert from library,\n"
		    "returned %d (%s)", rc, cm_status_to_string(rc)));
		ssh_x509_cert_free(x509);
		return (0);
	}
	rc = ssh_x509_cert_get_validity(x509, &not_before, &not_after);
	ssh_ber_time_set_from_unix_time(&now, ssh_time());

	if (!rc || (ssh_ber_time_cmp(&now, &not_before) == -1) ||
	    (ssh_ber_time_cmp(&not_after, &now) == -1))
		invalid = B_TRUE;

	PRTDBG(D_CERT, ("Added cert %s:\n\t<%s>%s%s%s%s",
	    p->slotname, p->subject_name,
	    p->keys != NULL ? " [local]" : "",
	    trusted ? " [trusted]" : "",
	    signer ? " [signer]" : "",
	    invalid ? " [validity dates out of range]" : ""));

	ssh_x509_cert_free(x509);
	return (0);
}

/* Globals needed for certlib callbacks */
static char global_subject[DN_MAX];
static char global_issuer[DN_MAX];
static SshMPIntegerStruct global_key;

static int
key_and_cert_linkage(struct certlib_keys *key)
{
	int i = 0;
	struct certlib_cert *cert = key->cert;

	if ((cert == NULL) || (cert->cert == NULL) ||
	    (cert->cert->subject_pkey.public_key == NULL) ||
	    (strncmp(global_subject, cert->subject_name, DN_MAX) != 0) ||
	    (strncmp(global_issuer, cert->issuer_name, DN_MAX) != 0))
		return (-1);

	if (cert->keys != NULL) {
		i++;
		if (cert->keys->key != NULL)
			i++;
	}
	return (i);
}

cachent_t *cacheptr_head = NULL, *cacheptr_current = NULL;
boolean_t cache_error = B_FALSE;

/* ARGSUSED */
void
enumerate_cb(SshCMCertificate cmcert, void *context)
{
	int linkage;
	uint32_t	cache_id, certclass;
	char *subject = NULL, *issuer = NULL;
	SshX509Certificate cert = NULL;
	cachent_t	*cache_ent = NULL;
	ike_certcache_t *cache_ptr = NULL;


	if (cache_error)
		return;
	if (ssh_cm_cert_get_x509(cmcert, &cert) != SSH_CM_STATUS_OK)
		goto bail;
	if ((cache_ent = ssh_malloc(sizeof (cachent_t))) == NULL)
		goto bail;
	if ((cache_ptr = ssh_malloc(sizeof (ike_certcache_t))) == NULL)
		goto bail;

	cache_ent->cache_ptr = cache_ptr;
	cache_ent->next = NULL;

	certclass = ssh_cm_cert_get_class(cmcert);
	cache_ptr->certclass = certclass;

	cache_id = ssh_cm_cert_get_cache_id(cmcert);
	cache_ptr->cache_id = cache_id;

	if (!ssh_x509_cert_get_subject_name(cert, &subject)) {
		cache_ptr->subject[0] = 0;
	} else {
		(void) strlcpy(cache_ptr->subject, subject, DN_MAX);
		ssh_free(subject);
	}

	if (!ssh_x509_cert_get_issuer_name(cert, &issuer)) {
		cache_ptr->issuer[0] = 0;
	} else {
		(void) strlcpy(cache_ptr->issuer, issuer, DN_MAX);
		ssh_free(issuer);
	}

	ssh_mprz_init(&global_key);
	switch (cert->subject_pkey.pk_type) {
	case SSH_X509_PKALG_RSA:
		(void) ssh_public_key_get_info(
		    cert->subject_pkey.public_key,
		    SSH_PKF_MODULO_N, &global_key,
		    SSH_PKF_END);
		break;
	case SSH_X509_PKALG_DSA:
		(void) ssh_public_key_get_info(
		    cert->subject_pkey.public_key,
		    SSH_PKF_PUBLIC_Y, &global_key, SSH_PKF_END);
		break;
	default:
		ssh_mprz_clear(&global_key);
	}

	/* Now check for linked key and locality of cert */

	(void) strlcpy(global_subject, cache_ptr->subject, DN_MAX);
	(void) strlcpy(global_issuer, cache_ptr->issuer, DN_MAX);

	/* Check for available key and linkage */
	linkage = certlib_iterate_keys_first_match(key_and_cert_linkage);
	ssh_mprz_clear(&global_key);

	if (strncmp(cache_ptr->subject, cache_ptr->issuer, DN_MAX) == 0)
		(void) strlcpy(cache_ptr->issuer, gettext("Self-signed"),
		    DN_MAX);

	ssh_x509_cert_free(cert);

	/* Disambiguate on-disk certs from payload obtained certs */
	if (linkage == -1) {
		if (cmcert->private_data_destructor == dummy_destructor)
			linkage++;
	}

	/*
	 * Fill in the values...
	 */
	cache_ptr->linkage = linkage;

	if (cacheptr_head == NULL)
		cacheptr_head = cache_ent;
	else
		cacheptr_current->next = cache_ent;
	cacheptr_current = cache_ent;
	return;
bail:
	cache_error = B_TRUE;
	ssh_x509_cert_free(cert);
	ssh_free(subject);
	ssh_free(issuer);
	ssh_free(cache_ptr);
	ssh_free(cache_ent);
}

/*
 * Free door-friendly certificate cache - must be protected by door_lock
 */
void
free_cert_cache()
{
	cachent_t *walker = cacheptr_head;
	cachent_t *next;

	assert(MUTEX_HELD(&door_lock));
	while (walker != NULL) {
		next = walker->next;
		walker->next = NULL;
		ssh_free(walker->cache_ptr);
		ssh_free(walker);
		walker = NULL;
		walker = next;
	}
	cacheptr_head = cacheptr_current = NULL;
}

/*
 * Get certificate cache in format suitable for dumping over door
 * Must be protected by door_lock
 */
void
get_cert_cache(void)
{
	assert(MUTEX_HELD(&door_lock));
	/* Enumerate classes and call backback for each */
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_INVALID,
	    enumerate_cb, NULL);
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_DEFAULT,
	    enumerate_cb, NULL);
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_LOCKED,
	    enumerate_cb, NULL);
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_TRUSTED,
	    enumerate_cb, NULL);
}

/*
 * Callback for flushing certificates from libike internal CMI cache
 */
/* ARGSUSED */
void
enumerate_cb_flush(SshCMCertificate cmcert, void *context)
{
	ssh_cm_cert_remove(cmcert);
}

/* Flush certificates from libike internal CMI cache */
void
flush_cert_cache(void)
{

	/* Enumerate classes and call backback for each */
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_INVALID,
	    enumerate_cb_flush, NULL);
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_DEFAULT,
	    enumerate_cb_flush, NULL);
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_LOCKED,
	    enumerate_cb_flush, NULL);
	(void) ssh_cm_cert_enumerate_class(cm_context, SSH_CM_CCLASS_TRUSTED,
	    enumerate_cb_flush, NULL);
}

/* Null out and ssh_free() the key for delpin */
int
del_private(struct certlib_keys *key)
{
	char *keyname;
	char storage[60];	/* Good a number as any. */

	if (key->pkcs11_id == NULL) {
		keyname = storage;
		if (ssh_snprintf(storage, sizeof (storage),
		    "on-disk slot \"%s\"", key->slotname) < 0)
			keyname = "on-disk, can't retrieve slotname";
	} else {
		keyname = key->pkcs11_id;
	}

	ssh_private_key_free(key->key);
	key->key = NULL;

	PRTDBG(D_CERT, ("Deleted key for private keyid \"%s\"", keyname));
	return (0);
}

/*
 * Certlib iterator callback that will pre-accelerate any keystore-based
 * private keys.  Pre-acceleration gets the internal PKCS#11 scaffolding up
 * so IKE may use PKCS#11 ops on this key.
 */
int
accel_private(struct certlib_keys *key)
{
	char *keyname;
	char storage[60];	/* Good a number as any. */

	if (key->pkcs11_id == NULL) {
		keyname = storage;
		if (ssh_snprintf(storage, sizeof (storage),
		    "on-disk slot \"%s\"", key->slotname) < 0)
			keyname = "on-disk, can't retrieve slotname";
	} else {
		keyname = key->pkcs11_id;
		if (key->key != NULL) {
			/* Already accelerated */
			PRTDBG(D_CERT, ("Key previously unlocked: "
			    "private keyid \"%s\"", keyname));
			return (0);
		}
	}

	if (!pre_accelerate_key(key, CKO_PRIVATE_KEY)) {
		/* Suppress error during token login initialization */
		if (certlib_token_pin == NULL)
			PRTDBG(D_CERT, ("Could not pre-accelerate key for "
			    "private keyid \"%s\"", (keyname != '\0') ? keyname
			    : "<on-disk private key>"));
		/* TODO:  When certlib can deal with deletes, do so. */
		return (1);
	} else {
		PRTDBG(D_CERT, ("Pre-accelerated key for "
		    "private keyid \"%s\"", (keyname != '\0') ? keyname
		    : "<on-disk private key>"));
	}
	return (0);
}

/*
 * Initialize the SSH Certificate Management Interface (CMI).
 * This'll mostly be trusted CA root certificates, along with any public
 * certificates stored in local files for whatever reason.
 */
boolean_t
cmi_init(void)
{
	extern SshCMLocalNetworkStruct proxy_info;
	extern uint32_t max_certs;
	int n;

	/*
	 * pkcs11_setup() returns a list of PKCS#11 functions.  This app
	 * doesn't need the list (unlike the ikecert(1m) commands), so we just
	 * check for NULL and move on.  No leakage to worry about either.
	 */
	if (pkcs11_setup(pkcs11_path) == NULL)
		EXIT_FATAL("pkcs11 setup failed.");

	/* Use the certlib functions to load up */
	if (!certlib_init(CERTLIB_NORMAL, CERTLIB_ALL))
		return (B_FALSE);

	cm_config = ssh_cm_config_allocate();

	/* Set the cache to be memory-unbounded. */
	ssh_cm_config_set_cache_size(cm_config, 0);
	/* But set the entries to be bounded. */
	ssh_cm_config_set_cache_max_entries(cm_config, max_certs);

	cm_context = ssh_cm_allocate(cm_config);
	if (cm_context == NULL) {
		EXIT_FATAL("Could not allocate certificate manager context!");
	}

	/* See SSH manual 7.2.1 for initialization details... */

	/* Initialize if socks or proxy are needed. */
	ssh_cm_edb_set_local_network(cm_context, &proxy_info);

	/* Initialize HTTP if so configured. */
	if (use_http && !ssh_cm_edb_http_init(cm_context))
		EXIT_FATAL("Could not initialize HTTP context for certificate "
		    "manager!");

	/* Initialize with LDAP servers: "ldap.sun.com:NNN,ldap.kebe.com:X" */
	if (ldap_path != NULL &&
	    !ssh_cm_edb_ldap_init(cm_context, ldap_path)) {
		EXIT_FATAL("Could not initialize LDAP context for certificate "
		    "manager!");
	}

	/* Initialize public/private keys. */
	PRTDBG(D_CERT, ("Adding certificates..."));
	n = certlib_iterate_certs_count(add_cert);
	PRTDBG(D_CERT, ("%d certificates successfully added", n));

	PRTDBG(D_CERT, ("Adding private keys..."));
	n = certlib_iterate_keys_count(accel_private);
	PRTDBG(D_CERT, ("%d private keys successfully added.", n));
	return (B_TRUE);
}


/*
 * This is a certlib_refresh callback function for deleting, updating, or
 * adding any certificates that have been affected by a certlib_refresh.
 */
static int
update_cert(struct certlib_cert *p)
{
	/*
	 * Cert has been modified or removed.  Remove it from the SSH
	 * CMI; it will be re-added below if necessary.
	 */
	if (p->appdata != NULL) {
		PRTDBG(D_CERT, ("Removing cert %s <%s>%s",
		    p->slotname == NULL ? "[NULL]" : p->slotname,
		    p->subject_name == NULL ? "[NULL]" : p->subject_name,
		    p->keys != NULL ? " [local]" : ""));
		(void) ssh_cm_cert_set_unlocked(p->appdata);
		ssh_cm_cert_remove(p->appdata);
		/*
		 * We don't ssh_cm_cert_free() here; ssh_cm_cert_remove()
		 * appears to do that (though the docs claim that the
		 * cert may be re-added later).
		 */
		p->appdata = NULL;
	}

	if (p->slotname != NULL) {
		/* Cert has been added or modified. */
		(void) add_cert(p);
	}

	return (0);
}


/*
 * Reload the certificate database into the SSH CMI.
 * This is done when the certlib databases (may) have been updated.
 */
void
cmi_reload(void)
{
	/* Refresh globals in this file. */
	num_ca_certs = 0;
	ssh_free(ca_certs);
	ca_certs = NULL;

	p1_localcert_reset();
	certlib_refresh(update_cert);
	(void) certlib_iterate_keys_count(accel_private);
}


/*
 *
 * THE FOLLOWING USED TO LIVE IN isakmp_policy.c FROM SSH.
 * It has been cstyled, hdrchked, and linted.
 *
 */

/*
 * Context structure for the find public key function
 */
typedef struct SshPolicyFindPublicKeyContextRec {
	SshIkePMPhaseI pm_info;
	SshPolicyKeyType key_type_in;
	const char *hash_alg_in;
	SshPolicyFindPublicKeyCB callback_in;
	void *callback_context_in;
} *SshPolicyFindPublicKeyContext;

/*
 * Start cmi search.
 */
static void
ssh_policy_find_public_key_search(SshPolicyFindPublicKeyContext context,
    SshCertDBKey *keys, SshCMSearchResult callback)
{
	SshCMSearchConstraints search_constraints;
	SshBerTimeStruct start_time, end_time;
	SshIkePMPhaseI pm_info = context->pm_info;
	extern SshCMContext cm_context;
	SshCMStatus ret;

	search_constraints = ssh_cm_search_allocate();

	ssh_ber_time_set_from_unix_time(&start_time, pm_info->sa_start_time);
	ssh_ber_time_set_from_unix_time(&end_time, pm_info->sa_expire_time);

	ssh_cm_search_set_time(search_constraints, &start_time, &end_time);

	switch (context->key_type_in) {
		case SSH_IKE_POLICY_KEY_TYPE_RSA_SIG:
			ssh_cm_search_set_key_type(search_constraints,
			    SSH_X509_PKALG_RSA);
			ssh_cm_search_set_key_usage(search_constraints,
			    SSH_X509_UF_DIGITAL_SIGNATURE);
			break;
		case SSH_IKE_POLICY_KEY_TYPE_RSA_ENC:
			ssh_cm_search_set_key_type(search_constraints,
			    SSH_X509_PKALG_RSA);
			ssh_cm_search_set_key_usage(search_constraints,
			    SSH_X509_UF_KEY_ENCIPHERMENT);
			break;
		case SSH_IKE_POLICY_KEY_TYPE_DSS_SIG:
			ssh_cm_search_set_key_type(search_constraints,
			    SSH_X509_PKALG_DSA);
			ssh_cm_search_set_key_usage(search_constraints,
			    SSH_X509_UF_DIGITAL_SIGNATURE);
			break;
	}

	(void) ssh_cm_search_set_keys(search_constraints, keys);

	ssh_cm_search_set_group_mode(search_constraints);

	ret = ssh_cm_find(cm_context, search_constraints, callback, context);

	if (ret != SSH_CM_STATUS_OK && ret != SSH_CM_STATUS_SEARCHING) {
		PRTDBG(D_CERT, ("Initializing certificate search failed "
		    "directly, error = %d (%s)",
		    ret, cm_status_to_string(ret)));
		(*callback)(context, NULL, NULL);
	}
}

/*
 * Found public key for remote host.
 */
static void
ssh_policy_find_public_key_found(SshPolicyFindPublicKeyContext context,
    SshCMCertificate certificate, Boolean multiple)
{
	SshX509Certificate x509_cert;
	SshCMStatus rc;
	SshPublicKey public_key_out, accelerated_key = NULL, holder;
	unsigned char *hash_out = NULL;
	size_t hash_out_len = 0;
	unsigned char *exported = NULL;
	size_t exported_len = 0;
	extern boolean_t policy_notify_remote_cert(SshIkePMPhaseI,
	    SshX509Certificate);

	if ((rc = ssh_cm_cert_get_x509(certificate, &x509_cert)) !=
	    SSH_CM_STATUS_OK) {
		PRTDBG(D_CERT,
		    ("Failed to get x509 cert from certificate manager,\n"
		    "returned %d (%s)", rc, cm_status_to_string(rc)));
		(*context->callback_in)(NULL, NULL, 0,
		    context->callback_context_in);
		ssh_free(context);
		return;
	}

	if ((rc = ssh_cm_cert_get_ber(certificate, &exported, &exported_len)) !=
	    SSH_CM_STATUS_OK) {
		PRTDBG(D_CERT, ("Failed to get ber cert from certificate "
		    "manager,\nreturned %d (%s)", rc, cm_status_to_string(rc)));
		(*context->callback_in)(NULL, NULL, 0,
		    context->callback_context_in);
		ssh_x509_cert_free(x509_cert);
		ssh_free(context);
		return;
	}

	if (!ssh_x509_cert_get_public_key(x509_cert, &public_key_out)) {
		PRTDBG(D_CERT,
		    ("Could not extract public key from x509 certificate."));
		(*context->callback_in)(NULL, NULL, 0,
		    context->callback_context_in);
		ssh_x509_cert_free(x509_cert);
		ssh_free(context);
		return;
	}

	if (multiple && context->hash_alg_in) {
		/* Block cipher */
		SshHash hash_ctx;
		SshCryptoStatus cret;

		cret = ssh_hash_allocate(context->hash_alg_in, &hash_ctx);
		if (cret != SSH_CRYPTO_OK) {
			PRTDBG(D_CERT, ("ssh_hash_allocate failed: %.200s (%d)",
			    ssh_crypto_status_message(cret), cret));
			ssh_public_key_free(public_key_out);
			(*context->callback_in)(NULL, NULL, 0,
			    context->callback_context_in);
			ssh_x509_cert_free(x509_cert);
			ssh_free(context);
			return;
		}

		hash_out_len = ssh_hash_digest_length(context->hash_alg_in);
		hash_out = ssh_malloc(hash_out_len);
		if (hash_out == NULL) {
			PRTDBG(D_CERT, ("Could not allocate memory for hash."));
			ssh_public_key_free(public_key_out);
			(*context->callback_in)(NULL, NULL, 0,
			    context->callback_context_in);
			ssh_x509_cert_free(x509_cert);
			ssh_free(context);
			ssh_hash_free(hash_ctx);
			return;
		}
		ssh_hash_reset(hash_ctx);
		ssh_hash_update(hash_ctx, exported, exported_len);
		(void) ssh_hash_final(hash_ctx, hash_out);
		ssh_hash_free(hash_ctx);
	}

	if (context->pm_info->auth_data)
		ssh_free(context->pm_info->auth_data);
	context->pm_info->auth_data = ssh_memdup(exported, exported_len);
	if (context->pm_info->auth_data == NULL) {
		PRTDBG(D_CERT, ("Could not memdup() exported ber cert."));
		ssh_public_key_free(public_key_out);
		(*context->callback_in)(NULL, NULL, 0,
		    context->callback_context_in);
		ssh_x509_cert_free(x509_cert);
		ssh_free(context);
		return;
	}
	context->pm_info->auth_data_len = exported_len;

	/* Check policy to see if the remote identity is acceptable. */
	if (!policy_notify_remote_cert(context->pm_info, x509_cert)) {
		ssh_public_key_free(public_key_out);
		public_key_out = NULL;
	} else if (ssh_cm_cert_get_private_data(certificate,
	    (void **)&accelerated_key) == SSH_CM_STATUS_OK &&
	    accelerated_key != NULL) {
		/*
		 * Check for private attribute.  Return pre-accelerated public
		 * key if available!
		 */
		if (ssh_public_key_copy(accelerated_key, &holder) ==
		    SSH_CRYPTO_OK) {
			ssh_public_key_free(public_key_out);
			public_key_out = holder;
		}
	} else {
		/*
		 * Use a dummy certlib object for sleazy access to
		 * pre_accelerate_key().
		 */

		struct certlib_cert tmpobj;

		(void) memset(&tmpobj, 0, sizeof (tmpobj));

		if (context->key_type_in == SSH_IKE_POLICY_KEY_TYPE_DSS_SIG)
			tmpobj.type = "dsa";
		else
			tmpobj.type = "rsa";
		tmpobj.cert = x509_cert;
		tmpobj.subject_name = "received-public-key";

		if (pre_accelerate_key(&tmpobj, CKO_PUBLIC_KEY) &&
		    ssh_public_key_copy(tmpobj.key, &holder) == SSH_CRYPTO_OK) {
			(void) ssh_cm_cert_set_private_data(certificate,
			    tmpobj.key, active_destructor);
			tmpobj.key = NULL;
			ssh_public_key_free(public_key_out);
			public_key_out = holder;
		}
	}

	ssh_x509_cert_free(x509_cert);

	(*context->callback_in)(public_key_out, hash_out, hash_out_len,
	    context->callback_context_in);
	ssh_free(context);
}

/*
 * Find public key for remote host.  Secondary selector find done, check
 * result.
 */
static void
ssh_policy_find_public_key_reply_2(void *ctx, SshCMSearchInfo info,
    SshCMCertList list)
{
	SshPolicyFindPublicKeyContext context = ctx;
	extern SshCMContext cm_context;

	if (info && info->status == SSH_CM_STATUS_OK &&
	    !ssh_cm_cert_list_empty(list)) {
		ssh_policy_find_public_key_found(ctx,
		    ssh_cm_cert_list_last(list),
		    ssh_cm_cert_list_first(list) !=
		    ssh_cm_cert_list_last(list));
		ssh_cm_cert_list_free(cm_context, list);
		return;
	}
	/* Return error */
	(*context->callback_in)(NULL, NULL, 0, context->callback_context_in);
	ssh_free(context);
}

/*
 * Find public key for remote host. Primary selector find done, check result.
 */
static void
ssh_policy_find_public_key_reply_1(void *ctx, SshCMSearchInfo info,
    SshCMCertList list)
{
	SshPolicyFindPublicKeyContext context = ctx;
	SshIkePMPhaseI pm_info = context->pm_info;
	extern SshCMContext cm_context;
	unsigned char buf[16];
	size_t len;

	if (info && info->status == SSH_CM_STATUS_OK &&
	    !ssh_cm_cert_list_empty(list)) {
			ssh_policy_find_public_key_found(ctx,
			    ssh_cm_cert_list_last(list),
			    ssh_cm_cert_list_first(list) !=
			    ssh_cm_cert_list_last(list));
			ssh_cm_cert_list_free(cm_context, list);
			return;
		}

	len = sizeof (buf);
	if (!ssh_inet_strtobin(pm_info->remote_ip, buf, &len)) {
		ssh_policy_find_public_key_reply_2(context, NULL, NULL);
	} else {
		SshCertDBKey *keys;

		keys = NULL;
		(void) ssh_cm_key_set_ip(&keys, buf, len);

		/* Start the search */
		ssh_policy_find_public_key_search(context, keys,
		    ssh_policy_find_public_key_reply_2);
	}
}

/*
 * Find public key for remote host. The primary selector is the id fields if
 * they are given, and if they are NULL then the ip and port numbers are used
 * as selector.
 *
 * If hash_alg_in is not NULL and there is multiple keys for the host, then
 * return hash of the selected key in the hash_out buffer. The length of hash
 * is hash_len_out. The isakmp library will free the buffer, after it is no
 * longer needed. If the isakmp/oakley should't send hash of key to remote end
 * then, then hash_len_out is set to zero, and hash_out to NULL.
 *
 * Call callback_in when the data is available (it can also be called
 * immediately).
 */
void
ssh_policy_find_public_key(SshIkePMPhaseI pm_info,
    SshPolicyKeyType key_type_in, const uchar_t *hash_alg_in,
    SshPolicyFindPublicKeyCB callback_in, void *callback_context_in)
{
	SshCertDBKey *keys;
	SshPolicyFindPublicKeyContext context;
	extern SshIkePayloadID construct_remote_id(SshIkePMPhaseI);
	SshIkePayloadID remote_id = NULL;
	boolean_t freeme = B_FALSE;

	PRTDBG(D_CERT, ("Looking for public key for remote host:\n"
	    "  Key type = %d (%s), local = %s:%s, remote = %s:%s",
	    key_type_in, policy_key_type_to_string(key_type_in),
	    pm_info->local_ip, pm_info->local_port,
	    pm_info->remote_ip, pm_info->remote_port));

	keys = NULL;
	remote_id = pm_info->remote_id;

	/*
	 * If we're doing RSA encryption, preemptively set the
	 * pm_info->remote_id to something.
	 */
	if (pm_info->auth_method_type ==
	    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION && remote_id == NULL) {
		remote_id = construct_remote_id(pm_info);
		freeme = B_TRUE;
	} else {
		remote_id = pm_info->remote_id;
	}

	PRTDBG(D_CERT, ("  Remote id = %s.", freeme ? "<see above...>" :
	    pm_info->remote_id_txt));

	if (remote_id != NULL)	{
		switch (remote_id->id_type) {
		case IPSEC_ID_IPV4_ADDR:
			(void) ssh_cm_key_set_ip(&keys,
			    remote_id->identification.ipv4_addr,
			    sizeof (remote_id->identification.ipv4_addr));
			break;
		case IPSEC_ID_FQDN:
			(void) ssh_cm_key_set_dns(&keys,
			    (char *)remote_id->identification.fqdn,
			    remote_id->identification_len);
			break;
		case IPSEC_ID_USER_FQDN:
			(void) ssh_cm_key_set_email(&keys,
			    (char *)remote_id->identification.fqdn,
			    remote_id->identification_len);
			break;
		case IPSEC_ID_IPV6_ADDR:
			(void) ssh_cm_key_set_ip(&keys,
			    remote_id->identification.ipv6_addr,
			    sizeof (remote_id->identification.ipv6_addr));
			break;
		case IPSEC_ID_IPV4_ADDR_SUBNET:
		case IPSEC_ID_IPV6_ADDR_SUBNET:
		case IPSEC_ID_IPV4_ADDR_RANGE:
		case IPSEC_ID_IPV6_ADDR_RANGE:
			break;
		case IPSEC_ID_DER_ASN1_DN:
			(void) ssh_cm_key_set_dn(&keys,
			    remote_id->identification.asn1_data,
			    remote_id->identification_len);
			break;
		case IPSEC_ID_DER_ASN1_GN:
			break;
		case IPSEC_ID_KEY_ID:
			break;
		default:
			break;
		}

		if (freeme)
			ssh_ike_id_free(remote_id);
	}

	context = ssh_malloc(sizeof (*context));
	if (context == NULL) {
		callback_in(NULL, NULL, 0, callback_context_in);
		return;
	}
	context->pm_info = pm_info;
	context->key_type_in = key_type_in;
	context->hash_alg_in = (char *)hash_alg_in;
	context->callback_in = callback_in;
	context->callback_context_in = callback_context_in;

	if (keys == NULL) {
		/*
		 * Directly call the first reply function to start trying to
		 * the other keys.
		 */
		ssh_policy_find_public_key_reply_1(context, NULL, NULL);
	} else {
		/* Start the search */
		ssh_policy_find_public_key_search(context, keys,
		    ssh_policy_find_public_key_reply_1);
	}
}

/*
 * Process certificate data. Add the certificate to certificate tables, and if
 * we can trust new keys, add them to public key database. If we do not trust
 * the keys then just ignore the certificate. The certificate encoding can be
 * any of supported certificate types found in isakmp.h.
 */
/* ARGSUSED */
void
ssh_policy_new_certificate(SshIkePMPhaseI pm_info,
    SshIkeCertificateEncodingType cert_encoding,
    unsigned char *certificate_data, size_t certificate_data_len)
{
	extern SshCMContext cm_context;

	PRTDBG(D_CERT, ("Attempting to add cert payload from IKE into\n"
	    "local cache, if appropriate.  Certificate encoding = %d (%s)."
	    "\n\tdata[0..%lu]="
	    " %02x%02x%02x%02x%02x%02x%02x%02x "
	    "%02x%02x%02x%02x%02x%02x%02x%02x", cert_encoding,
	    ike_cert_encoding_to_string(cert_encoding),
	    (ulong_t)certificate_data_len, certificate_data[0],
	    certificate_data[1], certificate_data[2], certificate_data[3],
	    certificate_data[4], certificate_data[5], certificate_data[6],
	    certificate_data[7], certificate_data[8], certificate_data[9],
	    certificate_data[10], certificate_data[11], certificate_data[12],
	    certificate_data[13], certificate_data[14], certificate_data[15]));

	switch (cert_encoding) {
		/* At the moment only X.509 certificates are supported. */
	case SSH_IKE_CERTIFICATE_ENCODING_NONE: /* None */
	case SSH_IKE_CERTIFICATE_ENCODING_PGP: /* PGP */
	case SSH_IKE_CERTIFICATE_ENCODING_DNS: /* DNS signed key */
	case SSH_IKE_CERTIFICATE_ENCODING_KERBEROS: /* Kerberos tokens */
	default:
		PRTDBG(D_CERT,
		    ("  Unsupported certificate encoding = %d (%s)",
		    cert_encoding, ike_cert_encoding_to_string(cert_encoding)));
		return;
	case SSH_IKE_CERTIFICATE_ENCODING_PKCS7: /* PKCS #7 wrapped X.509  */
	{
		SshCMStatus ret;

		ret = ssh_cm_add_pkcs7_ber(cm_context, certificate_data,
		    certificate_data_len);

		if (ret == SSH_CM_STATUS_OK)
			return;
		if (ret != SSH_CM_STATUS_ALREADY_EXISTS)
			PRTDBG(D_CERT, ("  Could not add cert, error %s (%d)",
			    cm_status_to_string(ret), ret));
		break;
	}
	case SSH_IKE_CERTIFICATE_ENCODING_CRL: /* Certificate revocation list */
	case SSH_IKE_CERTIFICATE_ENCODING_ARL: /* Authority revocation list */
	{
		SshCMCrl crl;
		SshCMStatus ret;

		crl = ssh_cm_crl_allocate(cm_context);

		if ((ret = ssh_cm_crl_set_ber(crl, certificate_data,
		    certificate_data_len)) != SSH_CM_STATUS_OK) {
			PRTDBG(D_CERT, ("  Could not add BER encoded CRL to "
			    "certificate manager, error = %s (%d)",
			    cm_status_to_string(ret), ret));
			ssh_cm_crl_free(crl);
			return;
		}

		ret = ssh_cm_add_crl(crl);
		if (ret == SSH_CM_STATUS_OK)
			return;
		if (ret != SSH_CM_STATUS_ALREADY_EXISTS)
			PRTDBG(D_CERT, ("Could not add CRL to the database, "
			    "%s (%d)", cm_status_to_string(ret), ret));
		ssh_cm_crl_free(crl);
		break;
	}
	case SSH_IKE_CERTIFICATE_ENCODING_X509_SIG: /* X.509 - signature */
	case SSH_IKE_CERTIFICATE_ENCODING_X509_KE:  /* X.509 - key exchange */
	{
		SshCMCertificate cert;
		SshCMStatus ret;

		cert = ssh_cm_cert_allocate(cm_context);
		if ((ret = ssh_cm_cert_set_ber(cert, certificate_data,
		    certificate_data_len)) != SSH_CM_STATUS_OK) {
			PRTDBG(D_CERT, ("  Could not initialize certificate "
			    "manager cert structure, error %s (%d)",
			    cm_status_to_string(ret), ret));
			ssh_cm_cert_free(cert);
			return;
		}

		ret = ssh_cm_add(cert);

		if (ret == SSH_CM_STATUS_OK)
			return;
		if (ret != SSH_CM_STATUS_ALREADY_EXISTS)
			PRTDBG(D_CERT, ("  Could not add cert to database, "
			    "error%s (%d)", cm_status_to_string(ret), ret));

		ssh_cm_cert_free(cert);
		break;
	}
	}
}

/*
 * Context structure for the find public key function
 */
typedef struct SshPolicyRequestCertificatesContextRec {
	SshIkePMPhaseI pm_info;
	int number_of_cas;
	SshIkeCertificateEncodingType *ca_encodings;
	unsigned char **certificate_authorities;
	size_t *certificate_authority_lens;
	SshPolicyRequestCertificatesCB callback_in;
	void *callback_context_in;

	int current_cas;
	int *number_of_certificates;
	SshIkeCertificateEncodingType **tmp_cert_encodings;
	unsigned char ***tmp_certs;
	size_t **tmp_certs_len;
} *SshPolicyRequestCertificatesContext;

static boolean_t
grow_more_encodings(SshPolicyRequestCertificatesContext context,
    int old_size, int new_size)
{
	SshIkeCertificateEncodingType *new_encodings;
	uint8_t **new_certs;
	size_t *new_lens;

	new_encodings = ssh_realloc(
	    context->tmp_cert_encodings[context->current_cas],
	    old_size * sizeof (SshIkeCertificateEncodingType),
	    new_size * sizeof (SshIkeCertificateEncodingType));
	new_certs = ssh_realloc(context->tmp_certs[context->current_cas],
	    old_size * sizeof (unsigned char *),
	    new_size * sizeof (unsigned char *));
	new_lens = ssh_realloc(context->tmp_certs_len[context->current_cas],
	    old_size * sizeof (size_t), new_size * sizeof (size_t));

	if (new_encodings == NULL || new_certs == NULL || new_lens == NULL) {
		ssh_free(new_encodings);
		ssh_free(new_certs);
		ssh_free(new_lens);
		return (B_FALSE);
	}

	context->tmp_cert_encodings[context->current_cas] = new_encodings;
	context->tmp_certs[context->current_cas] = new_certs;
	context->tmp_certs_len[context->current_cas] = new_lens;
	return (B_TRUE);
}

static void
request_one_now(SshPolicyRequestCertificatesContext context,
    SshCMCertList list, SshBerTimeStruct *start_time,
    SshBerTimeStruct *end_time)
{
	SshCertDBKey *keys;
	int allocated, cnt;
	SshCMCertificate cert;
	SshCMCrlList crl_list;
	SshCMSearchConstraints crl_search_constraints;
	Boolean first;
	unsigned char *ber;
	size_t ber_length;
	SshCMStatus ret;

	allocated = 10;
	cnt = 0;
	if (!grow_more_encodings(context, cnt, allocated)) {
		PRTDBG(D_CERT, ("Out of memory for more encodings."));
		return;
	}

	first = TRUE;

	cert = ssh_cm_cert_list_first(list);
	do {
		if (first) {
			first = FALSE;
		} else {
			if (ssh_cm_cert_get_ber(cert, &ber, &ber_length) !=
			    SSH_CM_STATUS_OK) {
				PRTDBG(D_CERT, (" CA %d, cert %d: failed "
				    "to extract BER from certificate manager.",
				    context->current_cas, cnt));
			} else {
				if (cnt == allocated) {
					allocated += 10;
					if (!grow_more_encodings(context, cnt,
					    allocated)) {
						PRTDBG(D_CERT,
						    ("Out of memory for more "
						    "encodings"));
						return;
					}


				}
				context->tmp_cert_encodings[
				    context->current_cas][cnt] =
				    SSH_IKE_CERTIFICATE_ENCODING_X509_SIG;
				context->tmp_certs[context->current_cas][cnt] =
				    ssh_memdup(ber, ber_length);
				if (context->tmp_certs[
				    context->current_cas][cnt] == NULL) {
					PRTDBG(D_CERT, ("Out of memory."));
					return;
				}
				context->tmp_certs_len[context->current_cas][
				    cnt] = ber_length;
				PRTDBG(D_CERT,
				    (" CA %d, cert %d", context->current_cas,
				    cnt));
				cnt++;
			}
		}

		crl_list = NULL;

		crl_search_constraints = ssh_cm_search_allocate();
		ssh_cm_search_set_time(crl_search_constraints,
		    start_time, end_time);
		keys = NULL;
		(void) ssh_cm_key_set_from_cert(&keys,
		    SSH_CM_KEY_CLASS_SUBJECT, ssh_cm_cert_list_current(list));

		(void) ssh_cm_search_set_keys(crl_search_constraints, keys);
		ret = ssh_cm_find_local_crl(cm_context,
		    crl_search_constraints, &crl_list);

		if (ret == SSH_CM_STATUS_OK &&
		    !ssh_cm_crl_list_empty(crl_list)) {
			SshCMCrl crl;

			crl = ssh_cm_crl_list_first(crl_list);
			do {
				if (ssh_cm_crl_get_ber(crl, &ber,
				    &ber_length) != SSH_CM_STATUS_OK) {
					PRTDBG(D_CERT,
					    (" CA %d, CRL %d: failed to get "
					    "BER from certificate manager.",
					    context->current_cas, cnt));
					return;
				} else {
					if (cnt == allocated) {
						allocated += 10;
						if (!grow_more_encodings(
						    context, cnt, allocated)) {
							PRTDBG(D_CERT,
							    ("Out of "
							    "memory for more "
							    "encodings."));
							return;
						}
					}
					context->tmp_cert_encodings[
					    context->current_cas][cnt] =
					    SSH_IKE_CERTIFICATE_ENCODING_CRL;

					context->tmp_certs[
					    context->current_cas][cnt] =
					    ssh_memdup(ber, ber_length);
					if (context->tmp_certs[
					    context->current_cas][cnt] ==
					    NULL) {
						PRTDBG(D_CERT,
						    ("Out of memory."));
						return;
					}
					context->tmp_certs_len[
					    context->current_cas][cnt] =
					    ber_length;
					PRTDBG(D_CERT,
					    (" CA %d, CRL %d",
					    context->current_cas, cnt));
					cnt++;
				}
			} while ((crl = ssh_cm_crl_list_next(crl_list)) !=
			    NULL);
		}
		ssh_cm_crl_list_free(cm_context, crl_list);
	} while ((cert = ssh_cm_cert_list_next(list)) != NULL);

	PRTDBG(D_CERT, (" Found %d certificates and CRLs", cnt));
	context->number_of_certificates[context->current_cas] = cnt;

	ssh_cm_cert_list_free(cm_context, list);
}

/*
 * Get one chain of certificates with given encoding and to given certificate
 * authority.
 */
static void
ssh_policy_request_certificates_one(void *ctx, SshCMSearchInfo info,
    SshCMCertList list)
{
	SshPolicyRequestCertificatesContext context = ctx;
	SshCMSearchConstraints search_constraints;
	SshCMSearchConstraints ca_search_constraints;
	SshBerTimeStruct start_time, end_time;
	SshIkePMPhaseI pm_info = context->pm_info;
	phase1_t *p1 = (phase1_t *)pm_info->policy_manager_data;
	extern SshCMContext cm_context;
	SshCertDBKey *keys;
	SshCMStatus ret;

	/* Must be valid for next 2 minutes */
	ssh_ber_time_set_from_unix_time(&start_time, pm_info->sa_start_time);
	ssh_ber_time_set_from_unix_time(&end_time,
	    pm_info->sa_start_time + 120);
	/* XXX pm_info->expire_time */

	if (context->current_cas != -1) {
		if (info == NULL || info->status != SSH_CM_STATUS_OK ||
		    ssh_cm_cert_list_empty(list)) {
			/*
			 * Use the "IKE Identifier" to get a free BER
			 * decoding.
			 */
			int offset = context->current_cas;
			struct SshIkePayloadIDRec pl;
			char buf[100];

			/* Not found */
			if (debug & D_CERT) {
				/*
				 * Lotsa overhead for a debug-printf, so
				 * put all of the massaging in the check
				 * we normally just have for PRTDBG().
				 */
				dbgprintf("Could not retrieve "
				    "certificate list, ca=%d.",	offset);
				(void) memset(&pl, 0, sizeof (pl));
				pl.identification.asn1_data =
				    context->certificate_authorities[offset];
				pl.identification_len =
				    context->certificate_authority_lens[offset];
				pl.id_type = IPSEC_ID_DER_ASN1_DN;
				dbgprintf("  <%s>", ssh_ike_id_to_string(buf,
				    sizeof (buf), &pl));
			}

			context->number_of_certificates[offset] = 0;
			context->tmp_cert_encodings[offset] = NULL;
			context->tmp_certs[offset] = NULL;
			context->tmp_certs_len[offset] = NULL;

			if (list)
				ssh_cm_cert_list_free(cm_context, list);
		} else {
			request_one_now(context, list, &start_time, &end_time);
		}
	}

	context->current_cas++;

	/* Ignore unsupported encodings */
	while (context->current_cas != context->number_of_cas &&
	    (context->ca_encodings[context->current_cas] !=
	    SSH_IKE_CERTIFICATE_ENCODING_X509_SIG &&
	    context->ca_encodings[context->current_cas] !=
	    SSH_IKE_CERTIFICATE_ENCODING_X509_KE)) {
		PRTDBG(D_CERT,
		    ("Unsupported encoding for %d", context->current_cas));
		context->number_of_certificates[context->current_cas] = 0;
		context->tmp_cert_encodings[context->current_cas] = NULL;
		context->tmp_certs[context->current_cas] = NULL;
		context->tmp_certs_len[context->current_cas] = NULL;
		context->current_cas++;
	}

	if (context->current_cas == context->number_of_cas) {
		(*context->callback_in)(context->number_of_certificates,
		    context->tmp_cert_encodings, context->tmp_certs,
		    context->tmp_certs_len,
		    context->callback_context_in);
		ssh_free(context);
		return;
	}

	search_constraints = ssh_cm_search_allocate();

	ssh_cm_search_set_time(search_constraints, &start_time, &end_time);

	switch (pm_info->auth_method) {
	case SSH_IKE_VALUES_AUTH_METH_DSS_SIGNATURES:
		ssh_cm_search_set_key_type(search_constraints,
		    SSH_X509_PKALG_DSA);
		ssh_cm_search_set_key_usage(search_constraints,
		    SSH_X509_UF_DIGITAL_SIGNATURE);
		break;
	case SSH_IKE_VALUES_AUTH_METH_RSA_SIGNATURES:
		ssh_cm_search_set_key_type(search_constraints,
		    SSH_X509_PKALG_RSA);
		ssh_cm_search_set_key_usage(search_constraints,
		    SSH_X509_UF_DIGITAL_SIGNATURE);
		break;
	case SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION:
	case SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION_REVISED:
		ssh_cm_search_set_key_type(search_constraints,
		    SSH_X509_PKALG_RSA);
		ssh_cm_search_set_key_usage(search_constraints,
		    SSH_X509_UF_KEY_ENCIPHERMENT);
		break;
	default:
		EXIT_FATAL("Internal error, auth_method not rsa or dss");
		break;
	}

	keys = NULL;

	/*
	 * We should've been able to grab the public key from inside the
	 * certlib stuff (specifically either use the Proxy Key or extract the
	 * original SshPublicKey), but SafeNet won't let us do that.
	 * So instead use X.509 routines to extract a new public key.
	 */
	if (pm_info->public_key == NULL &&
	    !ssh_x509_cert_get_public_key(p1->p1_localcert->cert,
	    &pm_info->public_key)) {
		PRTDBG(D_CERT, ("Could not retrieve public key from cert"));
	}
	if (!ssh_cm_key_set_public_key(&keys, pm_info->public_key)) {
		PRTDBG(D_CERT, ("Could not set public key as search term"));
	}

	ssh_cm_search_set_keys(search_constraints, keys);

	ca_search_constraints = ssh_cm_search_allocate();
	ssh_cm_search_set_time(ca_search_constraints, &start_time, &end_time);
	keys = NULL;

	(void) ssh_cm_key_set_dn(&keys,
	    context->certificate_authorities[context->current_cas],
	    context->certificate_authority_lens[context->current_cas]);

	(void) ssh_cm_search_set_keys(ca_search_constraints, keys);

	ret = ssh_cm_find_path(cm_context, ca_search_constraints,
	    search_constraints, ssh_policy_request_certificates_one, context);
	if (ret != SSH_CM_STATUS_OK && ret != SSH_CM_STATUS_SEARCHING) {
		PRTDBG(D_CERT,
		    ("Initializing path for chain of trust failed directly, "
		    "error %d (%s)", ret, cm_status_to_string(ret)));
		ssh_policy_request_certificates_one(ctx, NULL, NULL);
	}
}

/*
 * Get chain of certificates with given encoding and to given certificate
 * authority. Call callback_in when the data is available (it can also be
 * called immediately).
 */
void
ssh_policy_request_certificates(SshIkePMPhaseI pm_info, int number_of_cas,
    SshIkeCertificateEncodingType *ca_encodings,
    unsigned char **certificate_authorities,
    size_t *certificate_authority_lens,
    SshPolicyRequestCertificatesCB callback_in, void *callback_context_in)
{
	SshPolicyRequestCertificatesContext context;

	PRTDBG(D_CERT, ("Looking for certificate chain of trust."));

	switch (pm_info->auth_method) {
	case SSH_IKE_VALUES_AUTH_METH_DSS_SIGNATURES:
	case SSH_IKE_VALUES_AUTH_METH_RSA_SIGNATURES:
	case SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION:
	case SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION_REVISED:
		context = ssh_malloc(sizeof (*context));
		if (context != NULL)
			break;
		/* FALLTHRU */
	default:
		(*callback_in)(0, NULL, NULL, NULL, callback_context_in);
		return;
	}

	context->pm_info = pm_info;
	context->number_of_cas = number_of_cas;
	context->ca_encodings = ca_encodings;
	context->certificate_authorities = certificate_authorities;
	context->certificate_authority_lens = certificate_authority_lens;
	context->callback_in = callback_in;
	context->callback_context_in = callback_context_in;

	context->current_cas = -1;
	context->number_of_certificates =
	    ssh_calloc(sizeof (*context->number_of_certificates),
	    number_of_cas);
	context->tmp_cert_encodings =
	    ssh_calloc(sizeof (*context->tmp_cert_encodings), number_of_cas);
	context->tmp_certs = ssh_calloc(sizeof (*context->tmp_certs),
	    number_of_cas);
	context->tmp_certs_len =
	    ssh_calloc(sizeof (*context->tmp_certs_len), number_of_cas);
	if (context->number_of_certificates == NULL ||
	    context->tmp_cert_encodings == NULL ||
	    context->tmp_certs == NULL || context->tmp_certs_len == NULL) {
		ssh_free(context->number_of_certificates);
		ssh_free(context->tmp_cert_encodings);
		ssh_free(context->tmp_certs);
		ssh_free(context->tmp_certs_len);
		ssh_free(context);
		(*callback_in)(0, NULL, NULL, NULL, callback_context_in);
		return;
	}

	PRTDBG(D_CERT,
	    ("  Requesting certs for %d CAs", context->number_of_cas));
	ssh_policy_request_certificates_one(context, NULL, NULL);
}

/*
 * Get certificate authority list to be sent to other end. Call callback_in
 * when the data is available (it can also be called immediately).
 */
/* ARGSUSED */
void
ssh_policy_get_certificate_authorities(SshIkePMPhaseI pm_info,
    SshPolicyGetCAsCB callback_in, void *callback_context_in)
{
	SshIkeCertificateEncodingType *ca_encodings = NULL;
	unsigned char **ca_names = NULL;
	size_t *ca_name_lens = NULL;
	int i = 0;
	SshX509Certificate x509;
	boolean_t problem = B_FALSE;

	if (num_ca_certs > 0) {
		ca_encodings = ssh_calloc(num_ca_certs,
		    sizeof (SshIkeCertificateEncodingType));
		ca_names = ssh_calloc(num_ca_certs, sizeof (uint8_t *));
		ca_name_lens = ssh_calloc(num_ca_certs, sizeof (size_t));
		if (ca_encodings == NULL || ca_names == NULL ||
		    ca_name_lens == NULL) {
			PRTDBG(D_CERT,
			    ("Out of memory to allocate CAs."));
			problem = B_TRUE;
			goto bail;
		}

		for (; i < num_ca_certs; i++) {
			ca_encodings[i] =
			    SSH_IKE_CERTIFICATE_ENCODING_X509_SIG;
			if (ssh_cm_cert_get_x509(ca_certs[i], &x509) !=
			    SSH_CM_STATUS_OK ||
			    !ssh_x509_cert_get_subject_name_der(x509,
			    ca_names + i, ca_name_lens + i)) {
				PRTDBG(D_CERT,
				    ("Failure getting CAs by "
				    "x509 or DER subject name."
				    "(CA number = %d)!\n", i));
				problem = B_TRUE;
			}
			ssh_x509_cert_free(x509);
			if (problem)
				break;	/* For loop. */
		}
	}

bail:
	if (problem) {
		ssh_free(ca_encodings);
		ssh_free(ca_names);
		ssh_free(ca_name_lens);
		ca_encodings = NULL;
		ca_names = NULL;
		ca_name_lens = NULL;
		i = 0;
	}

	callback_in(i, ca_encodings, ca_names, ca_name_lens,
	    callback_context_in);
}
