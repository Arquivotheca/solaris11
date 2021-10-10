/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Functions to initialize and examine the Certificate Database
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <strings.h>
#include <libintl.h>
/* Needs "certlib.h" and its sshincludes.h behind it. */
#define	DLLEXPORT
#define	LIBIKE_MAKE
#include <ike/certlib.h>
#include <ike/oid.h>
#include <ike/isakmp.h>
#include <ike/pkcs11-glue.h>
#include <sshfileio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#undef	snprintf	/* SSH weirdness... */

#define	 debug (mode & CERTLIB_DEBUG)

#define	BUFLEN 256

static char certlib_blank_string[] = "";

static const char sunscreen_etc[] = "/etc/sunscreen/";
static const char solaris_etc[] = "/etc/inet/";
static const char solariscrypto_etc[] = "/etc/crypto/";

static const char default_secrets_dir[] = "secret/ike.privatekeys";
static const char default_crls_dir[] = "ike/crls";
static const char default_public_dir[] = "ike/publickeys";

const char dl_modp[] = "dl-modp";
const char if_modn[] = "if-modn";

static const char solariscrypto_public_dir[] = "/etc/crypto/certs/";
static const char solariscrypto_crls_dir[] = "/etc/crypto/crls/";

/* Mode of database (Normal or SunScreen, Debug) */
static uint_t mode;
/* Which database objects to load (bit flags) */
static uint_t load_param;

static const char *top_dir, *secrets_dir, *crls_dir, *public_dir;

/*
 * Library allowed to go interactive and prompt user?
 */
boolean_t certlib_interactive;
uchar_t *certlib_token_pin = NULL;

/*
 * The four global object pools: each of these pointers points to a "head" node
 * that does not correspond to a real object, but merely holds the head and
 * tail pointers.
 */

static struct certlib_crl  *list_crl;
static struct certlib_cert *list_cert; /* holds both pkcs#11 and file based */
/*
 * list_keys and list_pkcs11_keys are both certlib_key lists, but since
 * pkcs11 key ops have special needs to avoid prompting the user for
 * a pin unnecessarily, we keep them separated to avoid unnecessary
 * searching and to optimize for the more common usage case of
 * cleartext on-disk keys and certs.
 */
static struct certlib_keys *list_keys;
static struct certlib_keys *list_pkcs11_keys;

/* only used in debugging messages */
static int num_of_keys, num_of_crl, num_of_cert;

static boolean_t certlib_load(void *list, int type, const char *dirname);
boolean_t certlib_next_cert(struct certlib_cert **certp);
static boolean_t certlib_next_crl(struct certlib_crl **crlp);
static boolean_t certlib_next_key(struct certlib_keys **keyp,
    struct certlib_keys *keylist);
void certlib_denull_cert(struct certlib_cert *certp);
void certlib_denull_crl(struct certlib_crl *crlp);
SshSKBType buf_to_privkey_blob(uchar_t *, size_t, uchar_t **, size_t *);

/* NULL terminated linked list */
struct pkcs11_key_ref {
	SshPrivateKey key;			/* SSH private keys structure */
	char pkcs11_label[PKCS11_TOKSIZE];	/* PKCS#11 token label. */
	char *pkcs11_id;			/* PKCS#11 object ID. */
	struct pkcs11_key_ref *next;
};

static struct pkcs11_key_ref *key_ref = NULL;

/*
 * Add an object to the global pool of objects that type.
 * list - the linked list to add it to.
 * type - type of list been added to.
 * object - pointer to the object being added
 */
static void
certlib_addobject(void *list, int type, void *object)
{
	struct certlib_keys *ck, *listk;
	struct certlib_crl  *cc, *listc;
	struct certlib_cert *ct, *listt;

	switch (type) {
	case CERTLIB_KEYS:
		listk = list;
		ck = object;
		listk->prev->next = ck;
		ck->prev = listk->prev;
		ck->next = listk;
		listk->prev = ck;
		num_of_keys++;
		break;
	case CERTLIB_CRL:
		listc = list;
		cc = object;
		listc->prev->next = cc;
		cc->prev = listc->prev;
		cc->next = listc;
		listc->prev = cc;
		num_of_crl++;
		break;
	case CERTLIB_CERT:
		listt = list;
		ct = object;
		listt->prev->next = ct;
		ct->prev = listt->prev;
		ct->next = listt;
		listt->prev = ct;
		num_of_cert++;
		break;
	}
}

/*
 * Frees a specific database entry.
 * p - pointer to object being freed.
 * type - type of object pointed to by p.
 */
static void
certlib_free(void *p, int type)
{
	struct certlib_keys *kp;
	struct certlib_crl  *lp;
	struct certlib_cert *cp;

	switch (type) {
	case CERTLIB_KEYS:
		kp = p;
		ssh_free(kp->data);
		ssh_free((void *)kp->slotname);
		ssh_private_key_free(kp->key);
		/* If a PKCS#11 object, "type" points to inside "data". */
		if (kp->pkcs11_id != NULL)
			break;
		ssh_free((void *)kp->type);
		ssh_mprz_clear(&kp->e);
		ssh_mprz_clear(&kp->d);
		ssh_mprz_clear(&kp->p);
		ssh_mprz_clear(&kp->q);
		ssh_mprz_clear(&kp->n);
		ssh_mprz_clear(&kp->u);
		ssh_mprz_clear(&kp->g);
		ssh_mprz_clear(&kp->x);
		ssh_mprz_clear(&kp->y);
		break;
	case CERTLIB_CRL:
		lp = p;
		ssh_free(lp->data);
		ssh_free((void *)lp->slotname);
		ssh_x509_crl_free(lp->crl);
		if (lp->issuer_name != certlib_blank_string)
			ssh_free(lp->issuer_name);
		break;
	case CERTLIB_CERT:
		cp = p;
		ssh_free(cp->data);
		ssh_free((void *)cp->slotname);
		ssh_x509_cert_free(cp->cert);
		if (cp->key != NULL)
			ssh_public_key_free(cp->key);
		/* If a PKCS#11 object, "type" points to inside "orig_data". */
		if (cp->orig_data)
			ssh_free(cp->orig_data);
		else
			ssh_free((void *)cp->type);
		if (cp->subject_name != certlib_blank_string)
			ssh_free(cp->subject_name);
		if (cp->issuer_name != certlib_blank_string)
			ssh_free(cp->issuer_name);
		break;
	}
	ssh_free(p);
}

/*
 * "Closes" the certlib and frees all data except the pkcs11 keys, which
 * might be "pre-accelerated".
 *
 * Maybe this should become a public interface at some point; until then
 * certlib assumes that the calling program doesn't mind leaving all this
 * stuff in memory until exit.
 */
static void
certlib_freeall(void)
{
	void *next;
	struct certlib_keys *kp;
	struct certlib_crl  *lp;
	struct certlib_cert *cp;

	/* free on-disk keys */
	for (kp = list_keys->next; kp != list_keys; kp = next) {
		next = kp->next;
		certlib_free(kp, CERTLIB_KEYS);
	}
	ssh_free(list_keys);
	list_keys = NULL;

	/* free crls */
	for (lp = list_crl->next; lp != list_crl; lp = next) {
		next = lp->next;
		certlib_free(lp, CERTLIB_CRL);
	}
	ssh_free(list_crl);
	list_crl = NULL;

	/* free certs */
	for (cp = list_cert->next; cp != list_cert; cp = next) {
		next = cp->next;
		certlib_free(cp, CERTLIB_CERT);
	}
	ssh_free(list_cert);
	list_cert = NULL;

	ssh_free((void *)secrets_dir);
	secrets_dir = NULL;
	ssh_free((void *)public_dir);
	public_dir = NULL;
	ssh_free((void *)crls_dir);
	crls_dir = NULL;
}

static void
certlib_free_pkcs11_keys(struct certlib_keys *list)
{
	void *next;
	struct certlib_keys *kp;

	if (list == NULL)
		return;

	/* free pkcs#11 keys */
	for (kp = list->next; kp != list; kp = next) {
		next = kp->next;
		certlib_free(kp, CERTLIB_KEYS);
	}
	ssh_free(list);
	list = NULL;
}

/*
 * Cross link any matching CRL nodes to Certificate nodes.
 */
static void
link_crl_to_cert(void)
{
	struct certlib_cert *cert = NULL;
	struct certlib_crl *crl;

	while (certlib_next_cert(&cert)) {
		crl = NULL;

		while (certlib_next_crl(&crl)) {
			if (strcmp(crl->issuer_name, cert->issuer_name) == 0) {
				cert->crl = crl;
				break;
			}
		}
	}
}

static int
link_pkcs11_key(struct certlib_keys *key, struct certlib_cert *cert)
{
	struct pkcs11_key_ref *kr;

	if ((strncmp(key->pkcs11_label, cert->pkcs11_label,
	    PKCS11_TOKSIZE) == 0) &&
	    (strcmp(key->pkcs11_id, cert->pkcs11_id) == 0)) {
		cert->keys = key;
		key->cert = cert;
		/* Check for pre-accelerated keys */
		for (kr = key_ref; kr != NULL; kr = kr->next) {
			if ((strncmp(key->pkcs11_label, kr->pkcs11_label,
			    PKCS11_TOKSIZE - 1) == 0) &&
			    (strcmp(key->pkcs11_id, kr->pkcs11_id) == 0)) {
				(void) ssh_private_key_copy(kr->key, &key->key);
			}
		}
		return (0);
	}
	return (1);
}

static void
clear_key_ref() {
	struct pkcs11_key_ref *kr;

	/* Delete cached pkcs11 key copies */
	if (key_ref == NULL)
		return;

	kr = key_ref;
	while (kr != NULL) {
		struct pkcs11_key_ref *krtmp;

		ssh_private_key_free(kr->key);
		krtmp = kr;
		kr = kr->next;
		ssh_free(krtmp->pkcs11_id);
		ssh_free(krtmp);
	}
	key_ref = NULL;
}

/*
 * Cross link any matching Keys nodes to Certificate nodes.
 */
static void
link_key_to_cert(void)
{
	struct certlib_cert *cert = NULL;
	struct certlib_keys *key;
	SshMPIntegerStruct certkey;

	ssh_mprz_init(&certkey);

	while (certlib_next_cert(&cert)) {
		if (cert->pkcs11_id != NULL) {
			/*
			 * Handle keystore cases.  Right now use matching
			 * PKCS#11 labels, because we do this in the
			 * generation utilities.
			 */
			key = NULL;
			while (certlib_next_key(&key, list_pkcs11_keys)) {
				if (link_pkcs11_key(key, cert) == 0)
					break;
			}
			continue;	/* To next certificate. */
		}

		switch (cert->cert->subject_pkey.pk_type) {
		case SSH_X509_PKALG_RSA:
			(void) ssh_public_key_get_info(
			    cert->cert->subject_pkey.public_key,
			    SSH_PKF_MODULO_N, &certkey,
			    SSH_PKF_END);

			/*
			 * This function may be called during different
			 * stages of initialization.  We do not know if
			 * cert->type will be NULL or not.  If it is NULL,
			 * give it a value.
			 */
			if (cert->type == NULL)
				cert->type = ssh_strdup("rsa");
			/* If we return NULL, let things drop for now. */

			key = NULL;
			while (certlib_next_key(&key, list_keys))
				if (ssh_mprz_cmp(&key->n, &certkey) == 0) {
					cert->keys = key;
					key->cert = cert;
					break;
				}
			break;

		case SSH_X509_PKALG_DSA:
			/*
			 * This function may be called during different
			 * stages of initialization.  We do not know if
			 * cert->type will be NULL or not.  If it is NULL,
			 * give it a value.
			 */
			if (cert->type == NULL)
				cert->type = ssh_strdup("dsa");
			/* If we return NULL, let things drop for now. */
			ssh_public_key_get_info(
			    cert->cert->subject_pkey.public_key,
			    SSH_PKF_PUBLIC_Y, &certkey, SSH_PKF_END);
			key = NULL;
			while (certlib_next_key(&key, list_keys))
				if (ssh_mprz_cmp(&key->y, &certkey) == 0) {
					cert->keys = key;
					key->cert = cert;
					break;
				}
			break;

		default:
			cert->type = ssh_strdup("unknown");
			/* If we return NULL, let things drop for now. */
			break;
		}
	}
	ssh_mprz_clear(&certkey);

	clear_key_ref();
}

static boolean_t
extract_x509_ber(CK_SESSION_HANDLE session, struct certlib_cert *pub,
    CK_OBJECT_HANDLE obj)
{
	CK_RV pkcs11_rc;
	CK_ATTRIBUTE template[] = { {CKA_VALUE, NULL, 0} };

	pkcs11_rc = p11f->C_GetAttributeValue(session, obj, template, 1);

	if (pkcs11_rc != CKR_OK)
		return (_B_FALSE);

	pub->datalen = template[0].ulValueLen;
	pub->data = ssh_malloc(pub->datalen);
	if (pub->data == NULL) {
		pub->datalen = 0;
		return (NULL);
	}
	template[0].pValue = pub->data;

	pkcs11_rc = p11f->C_GetAttributeValue(session, obj, template, 1);
	if (pkcs11_rc != CKR_OK ||
	    ((pub->cert = ssh_x509_cert_allocate(SSH_X509_PKIX_CERT)) ==
	    NULL) || ssh_x509_cert_decode(pub->data, pub->datalen,
	    pub->cert) != SSH_X509_OK) {
		if (pub->cert != NULL)
			ssh_x509_cert_free(pub->cert);
		ssh_free(pub->data);
		pub->data = NULL;
		pub->datalen = 0;
		return (_B_FALSE);
	}

	return (_B_TRUE);
}

/*
 * Using the ID, find the PKCS#11 object.
 */
static CK_OBJECT_HANDLE
find_pkcs11_obj(pkcs11_inst_t *p11i, struct certlib_cert *pub,
    struct certlib_keys *priv, CK_OBJECT_CLASS *classp,
    CK_KEY_TYPE *keytypep)
{
	char *id;
	CK_RV pkcs11_rc;
	CK_ATTRIBUTE_PTR template;
	CK_ULONG obj_count;
	CK_OBJECT_HANDLE public = NULL, private = NULL, cert = NULL, obj;
	CK_OBJECT_CLASS lclass = *classp;
	CK_OBJECT_CLASS list[] = {0, 0, 0, 0}, *listp;
	CK_BBOOL true = TRUE;
	CK_CERTIFICATE_TYPE ctype = CKC_X_509;

	template = ssh_calloc(4, sizeof (CK_ATTRIBUTE));
	if (template == NULL)
		return (NULL);

	list[0] = lclass;
	if (lclass == CKO_PUBLIC_KEY) {
		/* Need to find cert and public key. */
		list[1] = CKO_CERTIFICATE;
		id = pub->pkcs11_id;
	} else {
		/* Need to find public key, private key, and certificate. */
		list[1] = CKO_PUBLIC_KEY;
		list[2] = CKO_CERTIFICATE;
		id = priv->pkcs11_id;
	}

	ATTR_INIT(template[0], CKA_CLASS, &lclass, sizeof (lclass));
	ATTR_INIT(template[1], CKA_ID, id, strlen(id));
	ATTR_INIT(template[2], CKA_TOKEN, &true, sizeof (true));

	for (listp = list; *listp != 0; listp++) {
		lclass = *listp;
		/* NOTE: For certificate, change KEY_TYPE to CERTIFICATE_TYPE */
		if (lclass == CKO_CERTIFICATE) {
			ATTR_INIT(template[3], CKA_CERTIFICATE_TYPE, &ctype,
			    sizeof (ctype));
		} else {
			ATTR_INIT(template[3], CKA_KEY_TYPE, keytypep,
			    sizeof (*keytypep));
		}
		pkcs11_rc = p11f->C_FindObjectsInit(p11i->p11i_session,
		    template, 4);
		if (pkcs11_rc != CKR_OK) {
			/* Free stuff... */
			ssh_free(template);
			return (NULL);
		}

		/*
		 * Iterate through PKCS#11 objects.  Grab the correct one,
		 * and keep it at the appropriate public/private/cert holder.
		 */
		for (; ; ) {
			obj = NULL;
			obj_count = 0;
			pkcs11_rc = p11f->C_FindObjects(p11i->p11i_session,
			    &obj, 1, &obj_count);
			if (pkcs11_rc != CKR_OK || obj_count == 0)
				break;

			switch (lclass) {
			case CKO_PUBLIC_KEY:
				if (public != NULL) {
					(void) fprintf(stderr, "Multiple "
					    "public keys with same ID:\n%s\n",
					    id);
					(void) fprintf(stderr, "In PKCS#11 "
					    "token %.32s.\n",
					    p11i->p11i_token_label);
					break;
				}
				public = obj;
				break;
			case CKO_PRIVATE_KEY:
				if (private != NULL) {
					(void) fprintf(stderr, "Multiple "
					    "private keys with same ID:\n%s\n",
					    id);
					(void) fprintf(stderr, "In PKCS#11 "
					    "token %.32s.\n",
					    p11i->p11i_token_label);
					break;
				}
				private = obj;
				break;
			case CKO_CERTIFICATE:
				if (cert != NULL) {
					(void) fprintf(stderr, "Multiple "
					    "certificates with same ID:\n%s\n",
					    id);
					(void) fprintf(stderr, "In PKCS#11 "
					    "token %.32s.\n",
					    p11i->p11i_token_label);
					break;
				}
				cert = obj;
				break;
			}
		}

		pkcs11_rc = p11f->C_FindObjectsFinal(p11i->p11i_session);
		if (pkcs11_rc != CKR_OK) {
			ssh_free(template);
			return (NULL);
		}
	}

	ssh_free(template);

	if (cert == NULL || public == NULL ||
	    (*classp == CKO_PRIVATE_KEY && private == NULL)) {
		/*
		 * Something's wrong.  We need all three object types for
		 * use with an IKE certificate.
		 */
		(void) fprintf(stderr, "Missing for id = %s\n", id);
		if (cert == NULL)
			(void) fprintf(stderr, "certificate, ");
		if (public == NULL)
			(void) fprintf(stderr, "public-key, ");
		if (*classp == CKO_PRIVATE_KEY && private == NULL)
			(void) fprintf(stderr, "private-key, ");
		(void) fprintf(stderr, "in PKCS#11 keystore %.32s.\n",
		    p11i->p11i_token_label);
		return (NULL);
	}

	return ((*classp == CKO_PUBLIC_KEY) ? public : private);
}

static uint32_t
find_keybits_len(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE obj,
    CK_KEY_TYPE class)
{
	uint32_t rc = 0;
	CK_RV pkcs11_rc;
	CK_ATTRIBUTE rsa_attr = {CKA_MODULUS, NULL, 0};
	CK_ATTRIBUTE dsa_attr = {CKA_PRIME, NULL, 0};
	CK_ATTRIBUTE_PTR lattr = (class == CKK_DSA) ? &dsa_attr : &rsa_attr;

	/*
	 * I assume that the PKCS#11 attribute length is sufficient.
	 */
	pkcs11_rc = p11f->C_GetAttributeValue(session, obj, lattr, 1);
	if (pkcs11_rc == CKR_OK)
		rc = lattr->ulValueLen;

	return (rc);
}

/*
 * Generate an accelerated key object for a private or public key.
 */
boolean_t
pre_accelerate_key(void *certlibobj, CK_OBJECT_CLASS keyclass)
{
	struct certlib_cert *pub = (struct certlib_cert *)certlibobj;
	struct certlib_keys *priv = (struct certlib_keys *)certlibobj;
	pkcs11_key_t *p11k = NULL;
	pkcs11_inst_t *p11i = NULL;
	CK_OBJECT_HANDLE p11obj;
	CK_RV pkcs11_rc;
	CK_OBJECT_CLASS class = keyclass;
	CK_KEY_TYPE keytype;
	uchar_t *pin;
	uint32_t keysizebits;
	SshProxyKeyTypeId proxykeytype;
	const char *type;
	uint_t p11i_flags;

	if (keyclass == CKO_PUBLIC_KEY) {
		p11i = find_p11i_slot(pub->pkcs11_label);
		pin = pub->pkcs11_pin;
		type = pub->type;
		if (p11i != NULL && (p11i->p11i_flags & P11F_LOGIN) == 0 &&
		    certlib_token_pin != NULL) {
			/* User has tried to unlock token, try it */
			pin = certlib_token_pin;
			pkcs11_rc = p11f->C_Login(p11i->p11i_session, CKU_USER,
			    pin, strlen((char *)pin));
			if (pkcs11_rc == CKR_OK) {
				p11i->p11i_pin = ssh_strdup((char *)pin);
				if (p11i->p11i_pin == NULL) {
					pkcs11_error(0,
					    "pre_accelerate_key: strdup() "
					    "failed");
					(void) p11f->C_Logout(
					    p11i->p11i_session);
					goto bail;
				}
				p11i->p11i_flags |= P11F_LOGIN;
			}
		}
	} else {
		if (priv->key != NULL) {
			/* Already accelerated */
			return (_B_TRUE);
		}
		p11i = find_p11i_slot(priv->pkcs11_label);
		pin = priv->pkcs11_pin;
		type = priv->type;
	}

	/*
	 * Assign the key type.  Can use strcmp.
	 *
	 * We know from parse_pkcs11_item() that there's useful data
	 * beyond the terminator of "type".  We essentially assert
	 * so below.  Make note of that in case we need to make sure
	 * we store the hash (for RSA) in the p11k structure.
	 */
	if (strcmp(type, "rsa") == 0) {
		keytype = CKK_RSA;
		proxykeytype = SSH_PROXY_RSA;
		p11i_flags = P11F_RSA;
	} else if (strcmp(type, "dsa") == 0) {
		keytype = CKK_DSA;
		proxykeytype = SSH_PROXY_DSA;
		p11i_flags = P11F_DSA;
	} else {
		goto bail;
	}

	/*
	 * If an on-disk key, find a p11i using more conventional methods.
	 */
	if (p11i == NULL) {
		assert(pin == NULL);
		p11i = find_p11i_flags(p11i_flags);
		if (p11i == NULL)
			return (_B_FALSE);
	}

	P11I_REFHOLD(p11i);

	assert(p11i->p11i_session != NULL);
	if ((keyclass != CKO_PUBLIC_KEY) && (pin != NULL)) {
		if ((p11i->p11i_flags & P11F_LOGIN) == 0) {
			pkcs11_rc = p11f->C_Login(p11i->p11i_session, CKU_USER,
			    pin, strlen((char *)pin));
			if (pkcs11_rc != CKR_OK) {
				if (*pin == '\0')
					pkcs11_error(0,
					    "pre_accelerate_key: "
					    "PIN unavailable.");
				else
					pkcs11_error(pkcs11_rc,
					    "pre_accelerate_key: C_Login");
				goto bail;
			}
			p11i->p11i_pin = ssh_strdup((char *)pin);
			if (p11i->p11i_pin == NULL) {
				pkcs11_error(0,
				    "pre_accelerate_key: strdup() failed");
				(void) p11f->C_Logout(p11i->p11i_session);
				goto bail;
			}
			p11i->p11i_flags |= P11F_LOGIN;
		} else {
			assert(p11i->p11i_pin != NULL);
			if (strcmp(p11i->p11i_pin, (char *)pin) != 0) {
				if (*pin == '\0') {
					pkcs11_error(0,
					    "pre_accelerate_key: "
					    "PIN unavailable.");
				} else {
					/* Filesystem object inconsistencies. */
					pkcs11_error(0,
					    "pre_accelerate_key: "
					    "PIN inconsistencies.");
				}
				goto bail;
			}
		}
	}

	p11k = ssh_calloc(1, sizeof (*p11k));
	if (p11k == NULL)
		goto bail;

	p11k->p11k_p11i = p11i;

	if (pin != NULL) {
		/*
		 * See if we can find the key object on the board or in
		 * the session.
		 */
		p11obj = find_pkcs11_obj(p11i, pub, priv, &class, &keytype);
		if (p11obj == NULL)
			goto bail;
	} else {
		/*
		 * Otherwise, convert the on-disk object to a session object.
		 * NOTE:  On reload, we should've first cleared all of the
		 * existing session objects!
		 */
		if (keyclass == CKO_PUBLIC_KEY) {
			if (!ssh_x509_cert_get_public_key(pub->cert,
			    &(p11k->p11k_origpub)))
				goto bail;
			p11obj = pkcs11_convert_public(p11i, keytype,
			    p11k->p11k_origpub, pub->subject_name);
			if (p11obj == NULL) {
				ssh_public_key_free(p11k->p11k_origpub);
				goto bail;
			}
		} else {
			/* Use "slotname" for transient private keys. */
			p11obj = pkcs11_convert_private(p11i, keytype,
			    priv->key, (char *)priv->slotname);
			if (p11obj == NULL)
				goto bail;
			p11k->p11k_origpriv = priv->key;
		}
	}

	keysizebits = find_keybits_len(p11i->p11i_session, p11obj,
	    keytype) << 3;
	if (keysizebits == 0) {
		syslog(LOG_WARNING | LOG_DAEMON, "pre_accelerate_%s(): "
		    "can't find RSA modulus length.\n",
		    (keyclass == CKO_PUBLIC_KEY) ? "public" : "private");
		p11f->C_DestroyObject(p11i->p11i_session, p11obj);
		goto bail;
	}

	p11k->p11k_bufsize = (keytype == CKK_DSA) ?
	    DSA_BUFSIZE : (keysizebits >> 3);
	p11k->p11k_p11obj = p11obj;

	/*
	 * Call proxy-key interface and set the new one.
	 *
	 * NOTE:  For private-key, set priv->key to new one!  Object reference
	 * of the original will carry over to p11k.
	 */

	if (keyclass == CKO_PUBLIC_KEY) {
		if (pin == NULL && certlib_token_pin != NULL)
			pin = certlib_token_pin;
		p11k->p11k_pubkey = ssh_public_key_create_proxy(proxykeytype,
		    keysizebits, pkcs11_public_key_dispatch,
		    pkcs11_public_key_free, p11k);
		if (p11k->p11k_pubkey == NULL) {
			(void) p11f->C_DestroyObject(p11i->p11i_session,
			    p11obj);
			if (p11k->p11k_origpub != NULL)
				ssh_public_key_free(p11k->p11k_origpub);
			goto bail;
		}
		/* NOTE:  This is a reference, not a copy. */
		pub->key = p11k->p11k_pubkey;

		/*
		 * Note from SafeNet 16 Nov 2004:
		 *
		 * We tested with the lib/sshcrypto/tests/t-proxykey.c and got
		 * the same result as you if we only created the public key
		 * but did not select any scheme. The comments in sshpk.h are
		 * misleading as they say you can use *select_scheme()
		 * functions to "change scheme choice on an existing
		 * key". The scheme must be explicitly selected for a public
		 * key before it is used.
		 *
		 * If you have set the scheme for both private and public key
		 * there and still hit this problem, please let us know and
		 * we will investigate further.
		 *
		 * Yet another implementation detail that we need to know
		 * without the docs.
		 */
	} else {
		p11k->p11k_privkey = ssh_private_key_create_proxy(proxykeytype,
		    keysizebits, pkcs11_private_key_dispatch,
		    pkcs11_private_key_free, p11k);
		if (p11k->p11k_privkey == NULL) {
			(void) p11f->C_DestroyObject(p11i->p11i_session,
			    p11obj);
			goto bail;
		}
		/* NOTE:  This is a reference, not a copy. */
		priv->key = p11k->p11k_privkey;
	}

	return (_B_TRUE);

bail:
	if (p11i != NULL)
		P11I_REFRELE(p11i);
	ssh_free(p11k);
	return (_B_FALSE);
}

/*
 * Extract fields from "keystore" file.
 *
 * "data" should point to a text file consisting of:
 *
 * PKCS#11 token label.<nl>
 * PKCS#11 token PIN (optional - will be blank line otherwise.)<nl>
 * PKCS#11 key identifier (probably SubjectName or SubjectAltName).<nl>
 * "rsa-sha1", "rsa-md5", or "dsa-sha1"
 */
void
parse_pkcs11_file(uchar_t *data, size_t size, char **tl, char **tp,
    char **ki, char **kt)
{
	char *seeker, *boundary, *token_label, *token_pin, *key_id, *key_type;
	char *sanity;

	/*
	 * Since we are only allowed to generate these, we can safely assume
	 * the format is either there, or we can just reject the file as
	 * garbage.
	 */

#define	NEXT_NEWLINE(newtok) while (*seeker != '\n') { \
				seeker++; \
				if (seeker > boundary) { \
					*tl = NULL; \
					return; \
				} \
			} \
			*seeker = '\0'; \
			(newtok) = ++seeker

	token_label = (char *)data;
	boundary = token_label + size - 1;
	seeker = token_label;

	/*
	 * Check for blank or truncated file or a private key file.
	 * Private key files start with 0x30 0x82.  Since 0x30 is a valid
	 * ASCII character and 0x82 is not a valid ASCII character or start
	 * of a UTF-8 encoding, this sequence can't possibly
	 * be UTF-8 encoded so we know this is not a PKCS#11 hint file.
	 */
	if (seeker + 1 > boundary ||
	    ((uint8_t)*(seeker) == 0x30 && (uint8_t)*(seeker + 1) == 0x82)) {
		*tl = NULL;
		return;
	}

	NEXT_NEWLINE(token_pin);
	NEXT_NEWLINE(key_id);
	NEXT_NEWLINE(key_type);
	NEXT_NEWLINE(sanity);

#undef NEXT_NEWLINE

	/*
	 * Sanity check - seeker should be the end of the file + 1
	 * If we got this far, seeker also found a '\n' at the end
	 */
	seeker--;

	if (seeker <  boundary) {
		*tl = NULL;
		return;
	}

	*tl = token_label;
	*tp = token_pin;
	*ki = key_id;
	*kt = key_type;
}

/*
 * See if the file data is a PKCS#11 item.  This should work for either
 * public or private keys.
 */
static void *
parse_pkcs11_item(uchar_t *data, size_t size, boolean_t private,
    char *slotname)
{
	struct certlib_keys *privrc;
	struct certlib_cert *pubrc;
	void *rc = NULL;
	char *token_label, *token_pin, *key_id, *key_type;

	parse_pkcs11_file(data, size, &token_label, &token_pin, &key_id,
	    &key_type);
	if (token_label == NULL)
		return (NULL);

	/*
	 * Compare the first three letters as the signature type is
	 * irrelevant and technically not correct in this context
	 */
	if (strncmp(key_type, "rsa", 3) != 0 &&
	    strncmp(key_type, "dsa", 3) != 0)
		return (NULL);

	key_type[3] = '\0';  /* Turn rsa-<foo> or dsa-<sha1> to rsa or dsa. */

	/*
	 * Okay!  At this point, we have the four components needed to query
	 * PKCS#11 about things.
	 */
	rc = NULL;
	if (private) {
		privrc = ssh_calloc(1, sizeof (*privrc));
		if (privrc == NULL)
			return (NULL);

		privrc->data = data;
		privrc->datalen = size;
		privrc->slotname = ssh_strdup(slotname);
		if (privrc->slotname == NULL) {
			ssh_free(privrc);
			return (NULL);
		}
		/* Point to stuff already in "data". */
		pkcs11_pad_out(privrc->pkcs11_label, token_label);
		privrc->pkcs11_pin = (uchar_t *)token_pin;
		privrc->pkcs11_id = key_id;
		privrc->type = key_type;
		privrc->key = NULL;
		privrc->cert = NULL;

		rc = privrc;
	} else {
		pubrc = ssh_calloc(1, sizeof (*pubrc));
		if (pubrc == NULL)
			return (NULL);

		/* Point to stuff already in "data". */
		pkcs11_pad_out(pubrc->pkcs11_label, token_label);
		pubrc->pkcs11_pin = (uchar_t *)token_pin;
		pubrc->pkcs11_id = key_id;
		pubrc->type = key_type;

		rc = pubrc;
	}

	return (rc);
}

/*
 * Find a PKCS#11 object and extract the relevant X.509 BER.
 */
static SshX509Certificate
extract_x509_from_pkcs11(struct certlib_cert *clib)
{
	CK_RV pkcs11_rc;
	CK_ATTRIBUTE template[4];
	CK_ULONG count = 0;
	CK_OBJECT_HANDLE target;
	CK_OBJECT_CLASS class = CKO_CERTIFICATE;
	CK_CERTIFICATE_TYPE cert_type = CKC_X_509;
	CK_BBOOL true = TRUE;
	uint8_t *ber;
	size_t berlen;
	SshX509Certificate rc = NULL;
	CK_SESSION_HANDLE session = PKCS11_NO_SUCH_TOKEN;
	pkcs11_inst_t *p11i;
	boolean_t need_login = _B_FALSE;
	boolean_t pin_entered = _B_FALSE;

	if ((p11i = find_p11i_slot(clib->pkcs11_label)) != NULL) {
		char *pin = (char *)clib->pkcs11_pin;
		char unpadded_label[PKCS11_TOKSIZE];
		int i = PKCS11_TOKSIZE;

		P11I_REFHOLD(p11i);
		session = p11i->p11i_session;
		assert(session != NULL);

		/* Get a printable label */
		(void) strncpy(unpadded_label, clib->pkcs11_label,
		    PKCS11_TOKSIZE);
		/* Deal with padded and unpadded cases */
		while (unpadded_label[i] == ' ' || unpadded_label[i] == '\0') {
			unpadded_label[i] = '\0';
			i--;
		}

try_login:
		if (need_login) {
			if (certlib_token_pin != NULL) {
				pin = (char *)certlib_token_pin;
			} else {
				char prompt[80];

				snprintf(prompt, 80,
				    "Enter PIN for PKCS#11 token \"%.32s\": ",
				    unpadded_label);
				pin = getpassphrase(prompt);
			}
			/*
			 * protect against just hitting return
			 * so that error messages are sensible
			 */
			pin_entered = _B_TRUE;
		}

		if (*pin != '\0' || pin_entered) {
			if ((p11i->p11i_flags & P11F_LOGIN) == 0) {
				pkcs11_rc = p11f->C_Login(session, CKU_USER,
				    (uchar_t *)pin, strlen(pin));
				if (pkcs11_rc != CKR_OK) {
					(void) fprintf(stderr,
					    "PKCS#11 token login failed.\n");
					pkcs11_error(pkcs11_rc,
					    "extract_x509_from_pkcs11: "
					    "C_Login");
					goto bail;
				}
				p11i->p11i_pin = ssh_strdup(pin);
				if (p11i->p11i_pin == NULL) {
					pkcs11_error(0,
					    "extract_x509_from_pkcs11:"
					    " strdup() failed");
					(void) p11f->C_Logout(
					    p11i->p11i_session);
					goto bail;
				}
				p11i->p11i_flags |= P11F_LOGIN;
			} else {
				assert(p11i->p11i_pin != NULL);
				if (strcmp(p11i->p11i_pin, pin) != 0) {
					/*
					 * Filesystem object inconsistencies.
					 */
					pkcs11_error(0,
					    "extract_x509_from_pkcs11: "
					    "PIN inconsistencies");
					goto bail;
				}
			}
		}
	}
	switch (session) {
	case PKCS11_NO_SUCH_TOKEN:
		(void) fprintf(stderr,
		    "certlib: token \"%.32s\" not present.\n",
		    clib->pkcs11_label);
		return (rc);
	case PKCS11_OPEN_FAILED:
		(void) fprintf(stderr,
		    "certlib: C_OpenSession failed for token \"%.32s\".\n",
		    clib->pkcs11_label);
		return (rc);
	case PKCS11_LOGIN_FAILED:
		(void) fprintf(stderr,
		    "certlib: C_Login failed for token \"%.32s\".\n",
		    clib->pkcs11_label);
		return (rc);
	}


	/* LINTED */
	ATTR_INIT(template[0], CKA_CLASS, &class, sizeof (class));
	/* LINTED */
	ATTR_INIT(template[1], CKA_ID, clib->pkcs11_id,
	    strlen(clib->pkcs11_id));
	/* LINTED */
	ATTR_INIT(template[2], CKA_TOKEN, &true, sizeof (true));
	/* LINTED */
	ATTR_INIT(template[3], CKA_CERTIFICATE_TYPE, &cert_type,
	    sizeof (cert_type));

	pkcs11_rc = p11f->C_FindObjectsInit(session, template, 4);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "certdb: C_FindObjectsInit error");
		goto bail;
	}

	pkcs11_rc = p11f->C_FindObjects(session, &target, 1, &count);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "certdb: C_FindObjects error");
		goto bail;
	}

	/*
	 * We need to finalize the object so it can be
	 * reinitialized later, either on success or failure
	 * so just do it now.  Otherwise we end up with
	 * CKA_OPERATION_ACTIVE next time we call C_FindObjectsInit().
	 */
	pkcs11_rc = p11f->C_FindObjectsFinal(session);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "certdb: C_FindObjectsFinal error");
		goto bail;
	}

	if (count == 0) {
		/*
		 * The public key object may have the attribute
		 * CKA_PRIVATE, which caused the failure.  There
		 * is no way to know this without access to the
		 * object, so try again, logging in.
		 */
		if (!need_login && (certlib_interactive ||
		    certlib_token_pin != NULL) &&
		    (p11i->p11i_flags & P11F_LOGIN) == 0) {
			need_login = _B_TRUE;
			goto try_login;
		} else {
			(void) fprintf(stderr,
			    "certlib: Object %s not in PKCS#11 token or "
			    "currently unavailable.\n",
			    clib->pkcs11_id);
			goto bail;
		}
	}

	if (!extract_x509_ber(session, clib, target)) {
		(void) fprintf(stderr, "certlib: Can't get X.509 BER.\n");
		goto bail;
	}

	rc = clib->cert;

bail:
	if (p11i == NULL) {
		/*
		 * Belt-and-suspenders move, in case C_CloseSession() is
		 * broken.
		 */
		(void) p11f->C_Logout(session);
		(void) p11f->C_CloseSession(session);
	} else {
		P11I_REFRELE(p11i);
	}
	return (rc);
}


/*
 * Turn a PKCS#8 or SafeNet key file format buffer to ssh key blob.
 * Returned buffer is allocated, must be freed by caller
 * with ssh_free.  On failure, type is still returned if
 * known, but newbuf will be NULL and unallocated
 */
SshSKBType
buf_to_privkey_blob(uchar_t *buffer, size_t len, uchar_t **newbuf,
    size_t *newlen)
{
	SshSKBType kind;
	SshPrivateKey newkey = NULL;

	*newbuf = NULL;
	*newlen = 0;

	/*
	 * The ssh_skb_get_info function only returns the encrypted
	 * keytype SSH_SKB_PKCS8_SHROUDED if the password is
	 * NULL, i.e. the key was encrypted with a blank password,
	 * which is not very useful.  We'll need prior
	 * knowledge if we want to deal with it.
	 *
	 * Future Approach:
	 * If SSH_SKB_PKCS8_SHROUDED is returned, prompt for a password.
	 *
	 * The current implementation only supports unencrypted
	 * PKCS8 private key files and our original SafeNet
	 * proprietary format.
	 */
	if (ssh_skb_get_info((const unsigned char *)buffer, len, NULL, NULL,
	    NULL, NULL, &kind, NULL) != SSH_CRYPTO_OK)
		return (SSH_SKB_UNKNOWN);

	switch (kind) {

	case SSH_SKB_PKCS8:
		/*
		 * Input file is in PKCS#8 format.  Convert it
		 * to the SafeNet proprietary format before writing
		 * to the keystore.
		 */

		/* Decode into a raw key */
		if (ssh_pkcs8_decode_private_key((const unsigned char *)buffer,
		    len, &newkey) != SSH_X509_OK) {
			break;
		}
		/*
		 * Re-encode into the full key file format "blob"
		 * Failure here leaves newkey as NULL, which is
		 * checked by the caller.
		 */
		(void) ssh_x509_encode_private_key(newkey, newbuf, newlen);
		break;

	case SSH_SKB_SSH_X509:
		/*
		 * Input file is in the SSH proprietary plaintext
		 * key format.
		 */

		/* Decode into a raw key */
		newkey = ssh_x509_decode_private_key(
		    (const unsigned char *)buffer, len);
		if (newkey == NULL) {
			break;
		}
		/*
		 * Copy the key into the new buffer
		 */

		*newbuf = ssh_malloc(len);
		if (*newbuf == NULL)
			break;
		*newlen = len;
		bcopy(buffer, *newbuf, len);
		break;
	}
	if (newkey != NULL)
		ssh_private_key_free(newkey);
	return (kind);
}

/*
 * Loads all the objects from one file system database.
 * list - pointer to linked list that stores objects.
 * type - type of objects being stored.
 * dirname - directory where database files are located.
 */
static boolean_t
certlib_load(void *list, int type, const char *dirname)
{
	DIR *diro;
	struct dirent *dir;
	uchar_t *data;
	int err, fd;
	void *object;
	struct stat buf;
	char *s, *signature;
	char *entity_type;

	/* Human readable names for log */
	switch (type) {
	case CERTLIB_CERT:
		entity_type = dgettext(TEXT_DOMAIN, "certificate");
		break;
	case CERTLIB_KEYS:
		entity_type = dgettext(TEXT_DOMAIN, "private key");
		break;
	case CERTLIB_CRL:
		entity_type = dgettext(TEXT_DOMAIN, "CRL");
		break;
	default:
		entity_type = dgettext(TEXT_DOMAIN, "?");
	}

	/* chdir to and open up database (directory) */
	if (chdir(dirname) != 0 ||
	    (diro = opendir(".")) == NULL) {
		if (errno == EACCES) {
			(void) fprintf(stderr, "%s (%s).\n",
			    dgettext(TEXT_DOMAIN,
			    "Insufficient privilege to open database"),
			    entity_type);
		} else {
			(void) fprintf(stderr, "%s (%s): %s\n",
			    dgettext(TEXT_DOMAIN,
			    "Unable to open database"),
			    entity_type, strerror(errno));
		}
		return (_B_FALSE);
	}

	/* Iterate through each file for loading */
	while ((dir = readdir(diro)) != NULL) {
		/*
		 * We don't want to fail on errors for
		 * any individual file since there can be,
		 * and often is, crud in these directories.
		 */
		if (dir->d_name[0] == '.')
			continue;
		fd = open(dir->d_name, O_RDONLY);
		if (fd == -1) {
			if (debug) {
				if (errno == EACCES) {
					(void) fprintf(stderr, "%s %s %s.\n",
					    dgettext(TEXT_DOMAIN,
					    "Insufficient privilege to read"),
					    entity_type, dir->d_name);
				} else {
					(void) fprintf(stderr,
					    "%s %s %s: %s.\n",
					    dgettext(TEXT_DOMAIN,
					    "Cannot read"), entity_type,
					    dir->d_name, strerror(errno));
				}
			}
			continue;
		}
		if (fstat(fd, &buf) != 0) {
			if (debug) {
				(void) fprintf(stderr, "%s %s %s: %s.\n",
				    dgettext(TEXT_DOMAIN,
				    "Cannot get file attributes,"), entity_type,
				    dir->d_name, strerror(errno));
			}
			close(fd);
			continue;
		}

		if ((type == CERTLIB_KEYS) && INSECURE_PERMS(buf)) {
			(void) fprintf(stderr, "%s %s.\n",
			    dgettext(TEXT_DOMAIN, "Insecure permissions, "
			    "skipped untrusted private key"),
			    dir->d_name);
			close(fd);
			continue;
		}

		/* CRLs can be quite large so this sanity check doesn't apply */
		if (buf.st_size == 0 || (buf.st_size > 100000 &&
		    type != CERTLIB_CRL)) {
			if (debug) {
				(void) fprintf(stderr,
				    "%s %s %s: (%ld bytes)\n",
				    dgettext(TEXT_DOMAIN,
				    "File size invalid for"), entity_type,
				    dir->d_name, (long)buf.st_size);
			}
			close(fd);
			continue;
		}
		data = ssh_malloc(buf.st_size);
		if (data == NULL) {
			(void) fprintf(stderr,
			    "certlib_load: malloc for whole-file failed.");
			close(fd);
			continue;
		}
		if (read(fd, data, buf.st_size) == 0) {
			if (debug) {
				(void) fprintf(stderr, "%s %s %s: %s.\n",
				    dgettext(TEXT_DOMAIN,
				    "read failed for"), entity_type,
				    dir->d_name, strerror(errno));
			}
			ssh_free(data);
			close(fd);
			continue;
		}
		close(fd);

		switch (type) {
			struct certlib_keys *kp;
			struct certlib_crl  *lp;
			struct certlib_cert *cp;
			SshPrivateKey keys;
			SshX509Certificate cert;
			SshX509Crl crl;
			SshX509Status rc;
			SshSKBType kind;
			size_t newlen;
			uchar_t *newbuf;

		case CERTLIB_KEYS:
			if (list == list_keys) {
				kind = buf_to_privkey_blob(data, buf.st_size,
				    &newbuf, &newlen);
				keys =
				    ssh_x509_decode_private_key(newbuf, newlen);
			} else if (list == list_pkcs11_keys) {
				keys = NULL;
				/* Go back to original */
				object = parse_pkcs11_item(data, buf.st_size,
				    _B_TRUE, dir->d_name);
				if (object != NULL) {
					break;	/* Out of switch. */
				}
				newbuf = NULL;
			}

			if (keys == NULL) {
				/*
				 * Again, don't fail for crud or dangling
				 * references, they can be common, especially
				 * for PKCS#11 token objects.
				 */
				ssh_free(data);
				ssh_free(newbuf);
				continue;
			} else {
				ssh_free(data);
			}

			kp = ssh_calloc(1, sizeof (*kp));	/* zeroize */
			if (kp == NULL) {
				(void) fprintf(stderr,
				    "certlib_load: malloc for private failed.");
				ssh_free(newbuf);
				continue;
			}

			err = ssh_private_key_get_info(keys,
			    SSH_PKF_KEY_TYPE, &s,
			    SSH_PKF_SIGN, &signature,
			    SSH_PKF_SIZE, &kp->size,
			    SSH_PKF_END);

			if (err != SSH_CRYPTO_OK ||
			    (strcmp(s, if_modn) != 0 &&
			    strcmp(s, dl_modp) != 0)) {
				/* unsupported type of key file */
				ssh_free(kp);
				ssh_private_key_free(keys);
				ssh_free(newbuf);
				continue;
			}
			/* s and signature are not allocated  */
			if (strstr(signature, "dsa") != NULL)
				kp->type = ssh_strdup("dsa");
			else if (strstr(signature, "rsa") != NULL)
				kp->type = ssh_strdup("rsa");
			else
				kp->type = NULL;

			if (kp->type == NULL) {
				ssh_free(kp);
				ssh_private_key_free(keys);
				ssh_free(newbuf);
				continue;
			}
			kp->data = newbuf;
			kp->datalen = newlen;
			kp->slotname = ssh_strdup(dir->d_name);
			if (kp->slotname == NULL) {
				(void) fprintf(stderr,
				    "certlib_load: private slotname failed.");
				ssh_free(newbuf);
				continue;
			}

			kp->key = keys;
			ssh_mprz_init(&kp->n);
			ssh_mprz_init(&kp->e);
			ssh_mprz_init(&kp->d);
			ssh_mprz_init(&kp->p);
			ssh_mprz_init(&kp->q);
			ssh_mprz_init(&kp->u);
			ssh_mprz_init(&kp->g);
			ssh_mprz_init(&kp->x);
			ssh_mprz_init(&kp->y);

			if (strcmp(s, if_modn) == 0) {
				err = ssh_private_key_get_info(keys,
				    SSH_PKF_SECRET_D,	 &kp->d,
				    SSH_PKF_PUBLIC_E,	 &kp->e,
				    SSH_PKF_MODULO_N,	 &kp->n,
				    SSH_PKF_PRIME_P,	 &kp->p,
				    SSH_PKF_PRIME_Q,	 &kp->q,
				    SSH_PKF_INVERSE_U,	 &kp->u,
				    SSH_PKF_END);
			} else {
				err = ssh_private_key_get_info(keys,
				    SSH_PKF_GENERATOR_G, &kp->g,
				    SSH_PKF_PRIME_P,	 &kp->p,
				    SSH_PKF_PRIME_Q,	 &kp->q,
				    SSH_PKF_SECRET_X,	 &kp->x,
				    SSH_PKF_PUBLIC_Y,	 &kp->y,
				    SSH_PKF_END);
			}
			if (err != SSH_CRYPTO_OK) {
				if (debug)
					(void) fprintf(stderr,
					    "Invalid Key: (%s) Error %d\n",
					    dir->d_name, err);
				certlib_free(kp, CERTLIB_KEYS);
				continue;
			}

			kp->cert = NULL;
			object = kp;
			break;

		case CERTLIB_CRL:
			crl = ssh_x509_crl_allocate();
			if (ssh_x509_crl_decode(data, buf.st_size, crl) !=
			    SSH_X509_OK) {
				if (debug)
					(void) fprintf(stderr,
					    "Invalid CRL: (%s)\n",
					    dir->d_name);
				ssh_x509_crl_free(crl);
				ssh_free(data);
				continue;
			}

			lp = ssh_calloc(1, sizeof (*lp));  /* Zero-out. */
			if (lp == NULL) {
				(void) fprintf(stderr, "certlib_load:"
				    "  crl allocate failed.\n");
				ssh_x509_crl_free(crl);
				ssh_free(data);
				continue;
			}

			lp->data = data;
			lp->datalen = buf.st_size;
			lp->slotname = ssh_strdup(dir->d_name);
			if (lp->slotname == NULL) {
				(void) fprintf(stderr, "certlib_load:"
				    "  slotname allocate failed.\n");
				ssh_x509_crl_free(crl);
				ssh_free(data);
				ssh_free(lp);
				continue;
			}

			lp->crl = crl;
			ssh_x509_crl_get_issuer_name(crl, &lp->issuer_name);
			ssh_x509_name_reset(crl->issuer_name);
			object = lp;
			certlib_denull_crl(lp);
			break;

		case CERTLIB_CERT:
			cert = ssh_x509_cert_allocate(SSH_X509_PKIX_CERT);
			rc = ssh_x509_cert_decode(data, buf.st_size, cert);
			if (rc != SSH_X509_OK) {
				ssh_x509_cert_free(cert);
				object = parse_pkcs11_item(data, buf.st_size,
				    _B_FALSE, dir->d_name);
				if (object == NULL) {
					if (debug)
						(void) fprintf(stderr,
						    "Invalid Cert: (%s).\n",
						    dir->d_name);
					ssh_free(data);
					continue;
				}
				cp = (struct certlib_cert *)object;
				cp->orig_data = data;
				cp->orig_data_len = buf.st_size;
				/* cp->data & datalen are set here. */
				cert = extract_x509_from_pkcs11(cp);
				if (cert == NULL) {
					/* extract() already printed error. */
					ssh_free(data);
					ssh_free(cp);
					continue;
				}
			} else {
				cp = ssh_calloc(1, sizeof (*cp));
				if (cp == NULL) {
					ssh_free(data);
					continue;
				}
				cp->data = data;
				cp->datalen = buf.st_size;
			}

			cp->slotname = ssh_strdup(dir->d_name);
			if (cp->slotname == NULL) {
				ssh_free(data);
				ssh_free(cp);
				continue;
			}

			cp->cert = cert;
			ssh_x509_name_reset(cert->subject_name);
			ssh_x509_name_reset(cert->issuer_name);
			if (!ssh_x509_cert_get_subject_name(cert,
			    &cp->subject_name) ||
			    !ssh_x509_cert_get_issuer_name(cert,
			    &cp->issuer_name)) {
				ssh_free(data);
				ssh_free(cp->subject_name); /* Can take NULL */
				ssh_free(cp);
			}
			ssh_x509_name_reset(cert->subject_name);
			ssh_x509_name_reset(cert->issuer_name);
			object = cp;
			certlib_denull_cert(cp);
			break;

		default:
			abort();
		}
		certlib_addobject(list, type, object);
		if (debug)
			(void) fprintf(stderr,
			    "Added %s %s.\n", entity_type, dir->d_name);
	}

	(void) closedir(diro);
	return (_B_TRUE);
}


/*
 * With a given certlib_cert node, get the next one.  To start at the
 * beginning set the argument to NULL.  Return _B_FALSE if we're at the end.
 */
boolean_t
certlib_next_cert(struct certlib_cert **certp)
{
	struct certlib_cert *cert = *certp;

	if (cert == NULL)
		cert = list_cert;

	*certp = cert = cert->next;

	return (list_cert != cert);
}


/*
 * With a given certlib_crl node, get the next one.  To start at the beginning
 * set the argument to NULL.  Return _B_FALSE if we're at the end.
 */
static boolean_t
certlib_next_crl(struct certlib_crl **crlp)
{
	struct certlib_crl *crl = *crlp;

	if (crl == NULL)
		crl = list_crl;

	*crlp = crl = crl->next;

	return (list_crl != crl);
}


/*
 * With a given certlib_key node, get the next one.  To start at the beginning
 * set the argument to NULL.  Return _B_FALSE if we're at the end.
 */
static boolean_t
certlib_next_key(struct certlib_keys **keyp, struct certlib_keys *keylist)
{
	struct certlib_keys *key = *keyp;

	if (key == NULL)
		key = keylist;

	*keyp = key = key->next;

	return (keylist != key);
}

/*
 * Add a string to an array of strings, growing the array of strings.
 */
static char **
add_array(char **array, char *value, int *count)
{
	int newcount = (*count) + 1;
	int oldcount = *count;

	array = ssh_realloc(array, sizeof (char *) * oldcount,
	    sizeof (char *) * newcount);
	if (array == NULL)
		return (NULL);
	array[oldcount] = value;
	*count = newcount;
	return (array);
}

/*
 * Get the remainder of the certificate information.
 */
static void
certlib_get_x509_remainder(SshX509Certificate cert, char ***cert_array,
    int *cert_in_count)
{
	char **cert_pattern = *cert_array, **holder;
	char *extract, *newpat;
	int cert_count = *cert_in_count;
	uchar_t *ipaddr;
	char *s;
	SshX509Name name;
	uint_t af, crit;
	size_t len;
	SshMPIntegerStruct sint;

	/* Load Subject Alternative Names into certspec */
	if (ssh_x509_cert_get_subject_alternative_names(cert, &name, &crit)) {
		/* Ignore "crit"... */

		while (ssh_x509_name_pop_ip(name, &ipaddr, &len)) {
			if (len == 4) {
				/* IPv4 address. */
				len = INET_ADDRSTRLEN + 3; /* Add "IP=" */
				af = AF_INET;
			} else if (len == 16) {
				len = INET6_ADDRSTRLEN + 3; /* Add "IP=" */
				af = AF_INET6;
			} else {
				/* Broken cert... */
				continue;
			}
			newpat = ssh_malloc(len);
			if (newpat == NULL) {
				/* Broken malloc... */
				fprintf(stderr, "Low memory in certlib.\n");
				continue;
			}

			/*
			 * Can use strcat() and strcpy() here because
			 * inet_ntop() is  trustworthy, as is the constant
			 * "IP=".  (And that's where the 3 comes from also!)
			 */
			(void) strcpy(newpat, "IP=");
			(void) inet_ntop(af, ipaddr, newpat + 3,
			    INET6_ADDRSTRLEN);
			ssh_free(ipaddr);
			holder = add_array(cert_pattern, newpat,
			    &cert_count);
			if (holder == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				continue;
			}
			cert_pattern = holder;
			/*
			 * Remove "IP=".
			 * Can use strdup() here, because we generated and
			 * bounded cert_pattern[...].
			 */
			newpat = ssh_strdup(cert_pattern[cert_count - 1] + 3);
			if (newpat == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				continue;
			}
			holder = add_array(cert_pattern, newpat,
			    &cert_count);
			if (holder == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				continue;
			}
			cert_pattern = holder;
		}

		/*
		 * The current certlib model of handling low memory situations
		 * *WAS* to rely on ssh_x*alloc() to bail if it failed.
		 *
		 * As a step toward remedying that non-self-healing behavior,
		 * we use normal malloc(), and send errors to stdout.
		 */

		while (ssh_x509_name_pop_dns(name, &extract)) {
			len = 5 + strlen(extract);	/* 5 for "DNS=\0". */
			newpat = ssh_malloc(len);
			if (newpat == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				break;
			}
			snprintf(newpat, len, "DNS=%s", extract);
			cert_pattern = add_array(cert_pattern, newpat,
			    &cert_count);
			/* No need to free extract, just add it! */
			cert_pattern = add_array(cert_pattern, extract,
			    &cert_count);
		}

		while (ssh_x509_name_pop_email(name, &extract)) {
			len = 7 + strlen(extract);	/* 7 for "EMAIL=\0" */
			newpat = ssh_malloc(len);
			if (newpat == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				break;
			}
			snprintf(newpat, len, "EMAIL=%s", extract);
			cert_pattern = add_array(cert_pattern, newpat,
			    &cert_count);
			/* No need to free extract, just add it! */
			cert_pattern = add_array(cert_pattern, extract,
			    &cert_count);
		}

		while (ssh_x509_name_pop_uri(name, &extract)) {
			len = 5 + strlen(extract);	/* 5 for "URI=\0" */
			newpat = ssh_malloc(len);
			if (newpat == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				break;
			}
			snprintf(newpat, len, "URI=%s", extract);
			cert_pattern = add_array(cert_pattern, newpat,
			    &cert_count);
			/* No need to free extract, just add it! */
			cert_pattern = add_array(cert_pattern, extract,
			    &cert_count);
		}

		while (ssh_x509_name_pop_rid(name, &extract)) {
			len = 5 + strlen(extract);	/* 5 for "RID=\0" */
			newpat = ssh_malloc(len);
			if (newpat == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				break;
			}
			snprintf(newpat, len, "RID=%s", extract);
			cert_pattern = add_array(cert_pattern, newpat,
			    &cert_count);
			/* No need to free extract, just add it! */
			cert_pattern = add_array(cert_pattern, extract,
			    &cert_count);
		}
		while (ssh_x509_name_pop_directory_name(name, &extract)) {
			len = 4 + strlen(extract);	/* 4 for "DN=\0" */
			newpat = ssh_malloc(len);
			if (newpat == NULL) {
				fprintf(stderr, "Low memory in certlib.\n");
				break;
			}
			snprintf(newpat, len, "DN=%s", extract);
			cert_pattern = add_array(cert_pattern, newpat,
			    &cert_count);
			/* No need to free extract, just add it! */
			cert_pattern = add_array(cert_pattern, extract,
			    &cert_count);
		}
		ssh_x509_name_reset(name);
	}

	/* Add Serial Number */
	ssh_mprz_init(&sint);
	if (ssh_x509_cert_get_serial_number(cert, &sint)) {
		s = ssh_mprz_get_str(&sint, (SshWord)10);
		len = 4 + strlen(s);
		newpat = ssh_malloc(len);
		if (newpat != NULL) {
			snprintf(newpat, len, "SN=%s", s);
			cert_pattern = add_array(cert_pattern, newpat,
			    &cert_count);
		} else {
			fprintf(stderr, "Low memory in certlib.\n");
		}
		/* ssh_mprz_get_str() allocates a string... */
		ssh_free(s);
	}
	ssh_mprz_clear(&sint);

	*cert_array = cert_pattern;
	*cert_in_count = cert_count;
}

/*
 * Gather the provided X.509 Certificate's attributes into an array.
 */
int
certlib_get_x509_pattern(SshX509Certificate cert, char ***cert_array)
{
	int cert_count = 0;
	char *name, *tagval;

	/* Load Subject name into certspec, with and without tag. */
	if (ssh_x509_cert_get_subject_name(cert, &name)) {
		/* name is allocated, so we can just add it. */
		*cert_array = add_array(*cert_array, name, &cert_count);
		tagval = ssh_malloc(strlen(name) + 9);
		if (tagval != NULL) {
			snprintf(tagval, strlen(name) + 9, "SUBJECT=%s", name);
			*cert_array = add_array(*cert_array, tagval,
			    &cert_count);
		}
	}

	/* Load issuer name into certspec, with tag. */
	if (ssh_x509_cert_get_issuer_name(cert, &name)) {
		tagval = ssh_malloc(strlen(name) + 8);
		if (tagval != NULL) {
			snprintf(tagval, strlen(name) + 8, "ISSUER=%s", name);
			*cert_array = add_array(*cert_array, tagval,
			    &cert_count);
		}
		/* name is allocated, so we can just add it. */
		ssh_free(name);
	}

	/* Get the rest. */
	certlib_get_x509_remainder(cert, cert_array, &cert_count);

	return (cert_count);
}

/*
 * Gather the provided Certificate's attributes into an array.
 */
static int
certlib_get_cert_pattern(const struct certlib_cert *cert, char ***cert_array)
{
	char *x;
	int cert_count = 0;
	uint_t len;
	char **cert_pattern = NULL;

	/* Load Subject Names into certspec without TAG */
	len = strlen(cert->subject_name);
	x = ssh_malloc(len + 1);
	if (x != NULL) {
		(void) strlcpy(x, cert->subject_name, len + 1);
		cert_pattern = add_array(cert_pattern, x, &cert_count);
	}

	/* Load Subject Names into certspec */
	len = 9 + strlen(cert->subject_name);
	x = ssh_malloc(len);
	if (x != NULL) {
		snprintf(x, len, "SUBJECT=%s", cert->subject_name);
		cert_pattern = add_array(cert_pattern, x, &cert_count);
	}

	/* Load Issuer Name into certspec */
	len = 8 + strlen(cert->issuer_name);
	x = ssh_malloc(len);
	if (x != NULL) {
		snprintf(x, len, "ISSUER=%s", cert->issuer_name);
		cert_pattern = add_array(cert_pattern, x, &cert_count);
	}

	/* Add Slot number */
	len = 6+10; /* 10 digits for index.. yep, a lot */
	x = ssh_malloc(len);
	if (x != NULL) {
		snprintf(x, len, "SLOT=%s", cert->slotname);
		cert_pattern = add_array(cert_pattern, x, &cert_count);
	}

	/* Get the rest. */
	certlib_get_x509_remainder(cert->cert, &cert_pattern, &cert_count);

	*cert_array = cert_pattern;
	return (cert_count);
}

/*
 * Gather the provided CRL's attributes into an array.
 */
static int
get_crl_pattern(const struct certlib_crl *crl, char ***cert_array)
{
	char *x;
	uint_t len;
	char **crl_pattern = NULL;
	int crl_count = 0;

	/* Load Issuer Name into certspec without TAG */
	len = strlen(crl->issuer_name);
	x = ssh_malloc(len);
	if (x != NULL) {
		(void) strlcpy(x, crl->issuer_name, len);
		crl_pattern = add_array(crl_pattern, x, &crl_count);
	}
	/* Load Issuer Name into certspec */
	len = 8 + strlen(crl->issuer_name);
	x = ssh_malloc(len);
	if (x != NULL) {
		snprintf(x, len, "ISSUER=%s", crl->issuer_name);
		crl_pattern = add_array(crl_pattern, x, &crl_count);
	}

	/* Add Slot number */
	len = 6+10;
	x = ssh_malloc(len);
	if (x != NULL) {
		snprintf(x, len, "SLOT=%s", crl->slotname);
		crl_pattern = add_array(crl_pattern, x, &crl_count);
	}

	*cert_array = crl_pattern;
	return (crl_count);
}

/*
 * Free a certlib matching pattern (array of strings).
 */
void
certlib_clear_cert_pattern(char **cert_pattern, int cert_count)
{
	int i = -1;

	while (cert_count > ++i) {
		ssh_free(cert_pattern[i]);
	}
	ssh_free(cert_pattern);
}

/*
 * Make a copy of the pre-accelerated key list temporarily.
 * We keep just enough information around for later linking
 * of key to cert.  We don't want to keep the original list
 * because some references may have been deleted out from
 * under us.  This list gets deleted after the link check.
 * Only fails for low memory condition.
 */
static boolean_t
certlib_copy_pkcs11_keys(struct certlib_keys *list)
{
	struct pkcs11_key_ref *kr;
	struct certlib_keys *kp;
	void *next;

	if (list == NULL)
		return (_B_TRUE);
	/* We have some references from before */
	for (kp = list->next; kp != list; kp = next) {
		if (kp->key != NULL) {
			kr = ssh_malloc(sizeof (struct pkcs11_key_ref));
			if (kr == NULL)
				return (_B_FALSE);

			(void) ssh_private_key_copy(kp->key, &kr->key);
			(void) strlcpy(kr->pkcs11_label, kp->pkcs11_label,
			    PKCS11_TOKSIZE);
			kr->pkcs11_id = ssh_strdup(kp->pkcs11_id);

			if (key_ref == NULL) {
				key_ref = kr;
				kr->next = NULL;
			} else {
				kr->next = key_ref;
				key_ref = kr;
			}
		}
		next = kp->next;
	}
	return (_B_TRUE);
}

/*
 * This is the initialization function for the cert library.  It loads the
 * data requested via 'param'.	 'type' defines the mode of operation, whether
 * that be "normal" (Solaris native IKE) or SunScreen, or elfsign(1).
 *
 * type -  CERTLIB_NORMAL or CERTLIB_SUNSCREEN or CERTLIB_SOLARISCRYPTO
 * param - CERTLIB_KEYS, CERTLIB_CRL, CERTLIB_CERT, or CERTLIB_ALL
 * (param is bit flags, so keys, crl & certs can be OR'ed together)
 *
 */
boolean_t
certlib_init(int type, int param)
{
	char wd[MAXPATHLEN];
	char *data;
	int len;
	boolean_t status = _B_TRUE;

	mode = type;
	load_param = param;

	getwd(wd);

	/* Setup values for a given mode */
	if (mode & CERTLIB_SUNSCREEN) {
		if (debug)
			(void) fprintf(stderr, "Running in SunScreen Mode.\n");
		top_dir = sunscreen_etc;
	} else if (mode & CERTLIB_SOLARISCRYPTO) {
		if (debug)
			fprintf(stderr,
			    "Running in Solaris Crypto Framework Mode.\n");
		top_dir = solariscrypto_etc;
		param |= CERTLIB_KEYS;
		param |= CERTLIB_CRL;
	} else {
		if (debug)
			(void) fprintf(stderr, "Running in Normal Mode.\n");
		top_dir = solaris_etc;
	}

	/* Load data from database(s) */
	if (param & CERTLIB_CERT) {
		if (mode & CERTLIB_SOLARISCRYPTO) {
			public_dir = ssh_strdup(solariscrypto_public_dir);
			if (public_dir == NULL) {
				fprintf(stderr,
				    "Low memory in certlib_init(CERT).\n");
				return (_B_FALSE);
			}
		} else {
			len = strlen(top_dir) + strlen(default_public_dir) + 2;
			data = ssh_malloc(len);
			if (data == NULL) {
				fprintf(stderr,
				    "Low memory in certlib_init(CERT).\n");
				return (_B_FALSE);
			}
			snprintf(data, len, "%s%s/", top_dir,
			    default_public_dir);
			public_dir = data;
		}
		list_cert = ssh_malloc(sizeof (struct certlib_cert));
		if (list_cert == NULL) {
			fprintf(stderr, "Low memory in certlib_init(CERT).\n");
			ssh_free((void *)public_dir);
			return (_B_FALSE);
		}
		list_cert->next = list_cert->prev = list_cert;
		status &= certlib_load(list_cert, CERTLIB_CERT, public_dir);
	}
	if (param & CERTLIB_KEYS) {
		if (mode & CERTLIB_SOLARISCRYPTO) {
			secrets_dir = "/etc/crypto/";
		} else {
			len = strlen(top_dir) + strlen(default_secrets_dir) + 2;
			data = ssh_malloc(len);
			if (data == NULL) {
				fprintf(stderr,
				    "Low memory in certlib_init(KEYS).\n");
				return (_B_FALSE);
			}
			snprintf(data, len, "%s%s/", top_dir,
			    default_secrets_dir);
			secrets_dir = data;
		}
		list_keys = ssh_malloc(sizeof (struct certlib_keys));
		if (list_keys == NULL) {
			fprintf(stderr,
			    "Low memory in certlib_init(KEYS).\n");
			ssh_free((void *)secrets_dir);
			return (_B_FALSE);
		}
		list_keys->next = list_keys->prev = list_keys;
		status &= certlib_load(list_keys, CERTLIB_KEYS, secrets_dir);

		if (!certlib_copy_pkcs11_keys(list_pkcs11_keys)) {
			certlib_free_pkcs11_keys(list_pkcs11_keys);
			fprintf(stderr,
			    "Low memory in certlib_init(KEYS).\n");
			return (_B_FALSE);
		}

		/*
		 * Now we have a copy of all PKCS#11 pre-accelerated keys
		 * attached to the primary objects
		 */
		certlib_free_pkcs11_keys(list_pkcs11_keys);

		/* Create a new list */
		list_pkcs11_keys = ssh_malloc(sizeof (struct certlib_keys));
		if (list_pkcs11_keys == NULL) {
			fprintf(stderr,
			    "Low memory in certlib_init(KEYS).\n");
			ssh_free((void *)secrets_dir);
			return (_B_FALSE);
		}
		list_pkcs11_keys->next = list_pkcs11_keys->prev =
		    list_pkcs11_keys;
		status &= certlib_load(list_pkcs11_keys, CERTLIB_KEYS,
		    secrets_dir);

		link_key_to_cert();
	}
	if (param & CERTLIB_CRL) {
		if (mode & CERTLIB_SOLARISCRYPTO) {
			crls_dir = ssh_strdup(solariscrypto_crls_dir);
			if (crls_dir == NULL) {
				fprintf(stderr,
				    "Low memory in certlib_init(CRL).\n");
				return (_B_FALSE);
			}
		} else {
			len = strlen(top_dir) + strlen(default_crls_dir) + 2;
			data = ssh_malloc(len);
			if (data == NULL) {
				fprintf(stderr,
				    "Low memory in certlib_init(CRL).\n");
				return (_B_FALSE);
			}
			snprintf(data, len, "%s%s/", top_dir, default_crls_dir);
			crls_dir = data;
		}
		list_crl = ssh_malloc(sizeof (struct certlib_crl));
		if (list_crl == NULL) {
			fprintf(stderr, "Low memory in certlib_init(CRL).\n");
			ssh_free((void *)crls_dir);
			return (_B_FALSE);
		}
		list_crl->next = list_crl->prev = list_crl;
		status &= certlib_load(list_crl, CERTLIB_CRL, crls_dir);
		link_crl_to_cert();
	}

	(void) chdir(wd);
	return (status);
}


/*
 * Someday this function will be more intelligent about calling the callback
 * only if a certificate has been updated, but for now it essentially
 * "deletes" all the certificates and then re-adds them.
 */
void
certlib_refresh(int (*func)(struct certlib_cert *))
{
	struct certlib_cert *p;

	for (p = list_cert->next; p != list_cert; p = p->next) {
		/* pretend it was deleted */
		const char *s = p->slotname;
		p->slotname = NULL;
		func(p);
		p->slotname = s;
	}
	certlib_freeall(); /* Frees all but pkcs11 key list */
	certlib_init(mode, load_param); /* Also transitions key list */
	for (p = list_cert->next; p != list_cert; p = p->next)
		func(p);
}


/*
 * Iterate through the entire list of public certificates, calling the callback
 * function for each one.
 */
void
certlib_iterate_certs(int (*func)(struct certlib_cert *))
{
	struct certlib_cert *p;

	for (p = list_cert->next; p != list_cert; p = p->next)
		if (func(p) != 0)
			break;
}

/*
 * Iterate through the entire list of public certificates, calling the callback
 * function for each one and counting successes.
 */
int
certlib_iterate_certs_count(int (*func)(struct certlib_cert *))
{
	struct certlib_cert *p;
	int i = 0;

	for (p = list_cert->next; p != list_cert; p = p->next)
		if (func(p) == 0)
			i++;
	return (i);
}

/*
 * Iterate through the list of public certificates, calling the callback
 * function for each one.  Return on first success with the exit status
 * of the callback function.  A callback function is considered a non-match
 * if it returns 0. -1 is reserved as a special indicator by the callback fn.
 */
int
certlib_iterate_certs_first_match(int (*func)(struct certlib_cert *))
{
	struct certlib_cert *p;
	int i, j = -1;

	for (p = list_cert->next; p != list_cert; p = p->next) {
		if ((i = func(p)) > 0)
			return (i);
		if (i == 0)
			j = 0;
	}
	return (j);
}

/*
 * Iterate through the entire list of key pairs, calling the callback function
 * for each one.
 */
void
certlib_iterate_keys(int (*func)(struct certlib_keys *))
{
	struct certlib_keys *p;

	for (p = list_keys->next; p != list_keys; p = p->next)
		if (func(p) != 0)
			break;

	for (p = list_pkcs11_keys->next; p != list_pkcs11_keys; p = p->next)
		if (func(p) != 0)
			break;
}

/*
 * Iterate through the entire list of key pairs, calling the callback function
 * for each one.
 */
int
certlib_iterate_keys_count(int (*func)(struct certlib_keys *))
{
	struct certlib_keys *p;
	int i = 0;

	for (p = list_keys->next; p != list_keys; p = p->next)
		if (func(p) == 0)
			i++;

	for (p = list_pkcs11_keys->next; p != list_pkcs11_keys; p = p->next)
		if (func(p) == 0)
			i++;
	return (i);
}

/*
 * Iterate through the list of private keys, calling the callback
 * function for each one.  Return on first success with the exit status
 * of the callback function.  A callback function is considered a non-match
 * if it returns 0. -1 is reserved as a special indicator by the callback fn.
 */
int
certlib_iterate_keys_first_match(int (*func)(struct certlib_keys *))
{
	struct certlib_keys *p;
	int i, j = -1;

	for (p = list_keys->next; p != list_keys; p = p->next) {
		if ((i = func(p)) > 0)
			return (i);
		if (i == 0)
			j = 0;
	}

	for (p = list_pkcs11_keys->next; p != list_pkcs11_keys; p = p->next) {
		if ((i = func(p)) > 0)
			return (i);
		if (i == 0)
			j = 0;
	}
	return (j);
}


/*
 * Iterate through the entire list of CRLs, calling the callback function for
 * each one.
 */
void
certlib_iterate_crls(int (*func)(struct certlib_crl *))
{
	struct certlib_crl *p;

	for (p = list_crl->next; p != list_crl; p = p->next)
		if (func(p) != 0)
			break;
}

/*
 * Match a certificate.  Returns 0 (false) if it doesn't match, 1 (true) if
 * it does.
 */
int
certlib_match_cert(const struct certlib_cert *certp,
    const struct certlib_certspec *certspec)
{
	char **cert_pattern = NULL;
	int cert_count = 0;
	int i, iv;

	/* Load up certificate's patterns into certspec. */
	if ((cert_count = certlib_get_cert_pattern(certp, &cert_pattern)) ==
	    0) {
		certlib_clear_cert_pattern(cert_pattern, cert_count);
		return (0);
	}

	/* Check certificate's certspec with provided exclusion certspecs. */
	for (i = 0; i < certspec->num_excludes; ++i) {
		for (iv = 0; iv < cert_count; ++iv) {
			if (strcmp(certspec->excludes[i], cert_pattern[iv]) ==
			    0) {
				if (debug)
					(void) fprintf(stderr,
					    "Cert Match: Excluded %s found\n",
					    certspec->excludes[i]);
				certlib_clear_cert_pattern(cert_pattern,
				    cert_count);
				return (0);
			}
		}
	}

	/* Check certificate's certspec with provided inclusion certspecs. */
	for (i = 0; i < certspec->num_includes; ++i) {
		for (iv = 0; iv < cert_count; ++iv) {
			if (strcmp(certspec->includes[i], cert_pattern[iv]) ==
			    0) {
				/* Clear certificate's certspec */
				certlib_clear_cert_pattern(cert_pattern,
				    cert_count);
				return (1);
			}
		}
		if (debug)
			(void) fprintf(stderr,
			    "Cert match: Pattern %s not found\n",
			    certspec->includes[i]);
	}

	/* Clear certificate's certspec. */
	certlib_clear_cert_pattern(cert_pattern, cert_count);

	return (0);
}


/*
 * See if a CRL matches a given certspec.
 */
int
certlib_match_crl(const struct certlib_crl *crlp,
    const struct certlib_certspec *certspec)
{

	char **pattern = NULL;
	int count = 0;
	int i, iv, ret;

	/* Load up crl's patterns into certspec */
	if ((count = get_crl_pattern(crlp, &pattern)) == 0) {
		certlib_clear_cert_pattern(pattern, count);
		return (0);
	}

	/* Check crl's certspec with provided inclusion certspecs */
	for (i = 0; certspec->num_includes > i; i++) {
		iv = -1;
		ret = 0;
		while (count > (++iv)) {
			if (strcmp(certspec->includes[i],
			    pattern[iv]) == 0) {
				ret = 1;
				break;
			}
		}
		if (!ret) {
			if (debug)
				(void) fprintf(stderr,
				    "CRL match: Pattern %s not found\n",
				    certspec->includes[i]);
			break;
		}
	}

	/* Check crl's certspec with provided exclusion certspecs */
	for (i = 0; certspec->num_excludes > i; i++) {
		iv = -1;
		while (count > (++iv)) {
			if (strcmp(certspec->excludes[i],
			    pattern[iv]) == 0) {
				ret = 0;
				break;
			}

		}
		if (!ret) {
			if (debug)
				(void) fprintf(stderr,
				    "CRL Match: Excluded %s found\n",
				    certspec->excludes[i]);
			break;
		}
	}

	/* Clear certificate's certspec */
	certlib_clear_cert_pattern(pattern, count);

	return (ret);

}

/*
 * Find all certificates that match the given certspec, calling the callback
 * function for each one.  Stop early if func returns non-zero.
 */
void
certlib_find_cert_spec(const struct certlib_certspec *certspec,
    int (*func)(struct certlib_cert *certp))
{
	struct certlib_cert *p;

	for (p = list_cert->next; p != list_cert; p = p->next)
		if (certlib_match_cert(p, certspec))
			if (func(p) != 0)
				break;
}

/*
 * Find all locally stored (never from LDAP) certificates that match the given
 * certspec, calling the callback function for each one.  This is not  limited
 * to those that represent local identities; the term local in this function's
 * name refers to local filesystem storage.  Stop early if func returns
 * non-zero.
 */
void
certlib_find_local_cert_spec(const struct certlib_certspec *certspec,
    int (*func)(struct certlib_cert *certp))
{
	struct certlib_cert *p;

	for (p = list_cert->next; p != list_cert; p = p->next)
		if (certlib_match_cert(p, certspec))
			if (func(p) != 0)
				break;
}

/*
 * Find a pkcs11-linked certificate that matches the given certspec
 */
struct certlib_cert *
certlib_find_local_pkcs11_cert(const struct certlib_certspec *certspec)
{
	struct certlib_cert *p;

	for (p = list_cert->next; p != list_cert; p = p->next)
		if (p->pkcs11_id && certlib_match_cert(p, certspec))
			return (p);

	return (NULL);
}


/*
 * Find a certificate that matches the given certspec and that represents a
 * local identity (has a corresponding entry in the private key database).
 */
struct certlib_cert *
certlib_find_local_ident(const struct certlib_certspec *certspec)
{
	struct certlib_cert *p;

	for (p = list_cert->next; p != list_cert; p = p->next)
		if (p->keys && certlib_match_cert(p, certspec))
			return (p);

	return (NULL);
}

/*
 * Find a pkcs11-linked certificate that matches the given certspec and
 * that represents a local pkcs11-linked identity (has a corresponding
 * entry in the private key database).
 */
struct certlib_cert *
certlib_find_local_pkcs11_ident(const struct certlib_certspec *certspec)
{
	struct certlib_cert *p;

	for (p = list_cert->next; p != list_cert; p = p->next)
		if (p->keys && certlib_match_cert(p, certspec) &&
		    p->pkcs11_id && p->keys->pkcs11_id)

			return (p);

	return (NULL);
}

/*
 * Find all CRLs that match the given certspec, calling the callback function
 * for each one.  Stop early if func returns non-zero.
 */
void
certlib_find_crl_spec(const struct certlib_certspec *certspec,
    int (*func)(struct certlib_crl *crlp))
{
	struct certlib_crl *p;

	for (p = list_crl->next; p != list_crl; p = p->next)
		if (certlib_match_crl(p, certspec))
			if (func(p) != 0)
				break;
}

/*
 * The following three functions obtain certificate pathnames for the
 * filesystem.
 */

const char *
certlib_keys_dir(void)
{
	return (secrets_dir);
}

const char *
certlib_crls_dir(void)
{
	return (crls_dir);
}

const char *
certlib_certs_dir(void)
{
	return (public_dir);
}

void
certlib_denull_cert(struct certlib_cert *certp)
{
	/* right now, assume only subject_name and/or issuer_name can be NULL */

	if (certp == NULL)
		return;

	if (certp->issuer_name == NULL)
		certp->issuer_name = certlib_blank_string;

	if (certp->subject_name == NULL)
		certp->subject_name = certlib_blank_string;
}

void
certlib_denull_crl(struct certlib_crl *crlp)
{
	/* right now, assume only issuer_name can be NULL */

	if (crlp == NULL)
		return;

	if (crlp->issuer_name == NULL)
		crlp->issuer_name = certlib_blank_string;
}
