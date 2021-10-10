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

#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <libintl.h>
#include <locale.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ike/sshincludes.h>
#include <ike/sshfileio.h>
#include <ike/sshbase64.h>
#include <ike/x509.h>
#include <ike/oid.h>
#include <ike/sshocsp.h>
#include <ike/pkcs11-glue.h>
#include <ike/certlib.h>
#include "dumputils.h"

/*
 * SSH's IKE defines these to halt compiles and force use of ssh_snprintf...
 * We bypass that madness here.
 */
#undef sprintf
#undef snprintf

extern char *optarg;
extern int optind;

extern char *dump_name(const char *name);
extern char *dump_time(SshBerTime ber_time);
extern char *dump_number(const SshMPIntegerStruct *number);
extern char *dump_public_hash(const SshMPIntegerStruct *key);


/* verbose list option */
int verbose = 0;

/* Certificate Library Mode */
int certlib_mode;

#define	BIGBUFSZ 2048
#define	PASSPHRASE_MAX 256

#define	ISSUER_OID "1.3.6.1.5.5.7.48.2"

/* Command allowed to go interactive */
static boolean_t interactive = B_FALSE;

/*
 * Globals needed for certlib iterators.
 * They don't take parameters and we want to have to only
 * prompt the user for a pin once.  Since we might be accessing
 * several objects on the same token several times in order
 * to match a key and cert, we do not want to annoy the user
 * for no reason.
 */
static CK_SESSION_HANDLE global_session = NULL;

static char pkcs11_token_storage[PKCS11_TOKSIZE];
char *pkcs11_token_id = pkcs11_token_storage;

static boolean_t write_pin = B_FALSE;
static char global_pinstore[PASSPHRASE_MAX] = {'\0'};

#define	debug (certlib_mode & CERTLIB_DEBUG)

/* For libike's X.509 routines. */
static struct SshX509ConfigRec x509config_storage;
SshX509Config x509config;

static void usage(void)
{
	(void) fprintf(stderr,
	    "%s\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n"
	    "\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n",
	    gettext("Usage:"),
	    gettext("certdb -l [-v] [pattern]"),
	    gettext(" List Certificates"),
	    gettext(
	    "certdb -a [[-p] -T <PKCS#11 token>] [-f output_format] certname"),
	    gettext(" Add certificate to database."),
	    gettext("certdb -e [-f output_format] pattern"),
	    gettext(" Extract certificate from database."),
	    gettext("certdb -r [pattern]"),
	    gettext(" Remove certificate from database."),
	    gettext("certdb -T <PKCS#11 token> -U [slotname]"),
	    gettext(" Unlink PKCS#11 keystore object from IKE database."),
	    gettext("certdb [-p] -T <PKCS#11 token> -C [pattern]"),
	    gettext(" Copy certificate from disk to keystore."),
	    gettext("certdb [-p] -T <PKCS#11 token> -L [pattern]"),
	    gettext(" Link PKCS#11 keystore object with IKE database."),
	    gettext("certdb -h"),
	    gettext(" This help page"));
	exit(1);
}

/*
 * This extracts the data from a BER formated buffer.	Required are the
 * buffer & size of the buffer.  The important value is the type.  The
 * type must be CERTLIB_CERT or CERTLIB_REQ.
 *
 * If extraction fails, due to incorrect buffer format (ie PEM) or not
 * IKE data, NULL is returned.
 * If successful, the certificate structure is returned.
 */
static SshX509Certificate
extract_from_ber(unsigned char *buf, size_t len, int type)
{
	SshX509Certificate cert;

	if (type == CERTLIB_CERT) {
		cert = ssh_x509_cert_allocate(SSH_X509_PKIX_CERT);
		if (ssh_x509_cert_decode(buf, len, cert) == SSH_X509_OK) {
			if (debug)
				(void) fprintf(stderr, "%s\n",
				    gettext("This is a Certificate."));
		} else {
			ssh_x509_cert_free(cert);
			cert = NULL;
		}

	} else if (type == CERTLIB_REQ) {
		cert = ssh_x509_cert_allocate(SSH_X509_PKIX_CRMF);
		/*
		 * This used to be ssh_x509_cert_request_decode() in the
		 * 2.1 version of SSH's IKE.  Now it's combined with
		 * cert_decode.
		 */
		if (ssh_x509_cert_decode(buf, len, cert) == SSH_X509_OK) {
			if (debug)
				(void) fprintf(stderr, "%s\n",
				    gettext("This is a CA Request."));
		} else {
			ssh_x509_cert_free(cert);
			cert = NULL;
		}
	}
	if (cert == NULL) {
		if (debug)
			(void) fprintf(stderr, "%s\n",
			    gettext("Not BER format."));
	}

	return (cert);
}

/*
 * Extract a relatively unique ID string for a given certificate.
 * Don't worry about freeing things, because this program exits after
 * dealing with a single object.  (And by the time we get to a better system
 * for IKE's certificate management, it'll use a real systemwide certificate
 * management system.
 */
static char *
get_cert_id(SshX509Certificate cert)
{
	SshX509Name names = NULL;
	Boolean critical;
	char *object_id;

	if (!ssh_x509_cert_get_subject_name(cert, &object_id) &&
	    !ssh_x509_cert_get_subject_alternative_names(cert, &names,
	    &critical)) {
		(void) fprintf(stderr,
		    gettext("Imported object has no name.\n"));
		return (NULL);
	}

	if (object_id == NULL) {
		uint8_t *ip_addr = NULL;
		size_t ip_len;

		/*
		 * Find an object_id from one of the SubjectAltNames.
		 * Since this is internal to IKE and the PKCS#11 provider,
		 * we don't need to be too picky.
		 */
		if (!ssh_x509_name_pop_dns(names, &object_id) &&
		    !ssh_x509_name_pop_email(names, &object_id) &&
		    !ssh_x509_name_pop_uri(names, &object_id) &&
		    !ssh_x509_name_pop_directory_name(names, &object_id) &&
		    !ssh_x509_name_pop_ip(names, &ip_addr, &ip_len)) {
			(void) fprintf(stderr, gettext(
			    "Imported object has no readable names.\n"));
			return (NULL);
		}

		if (ip_addr != NULL) {
			/* Allocate memory for object_id */
			object_id = ssh_malloc(INET6_ADDRSTRLEN);
			if (object_id == NULL)
				memory_bail();
			if ((ip_len == sizeof (in6_addr_t) &&
			    inet_ntop(AF_INET6, ip_addr, object_id,
			    INET6_ADDRSTRLEN) == NULL) ||
			    (ip_len == sizeof (in_addr_t) &&
			    inet_ntop(AF_INET, ip_addr, object_id,
			    INET6_ADDRSTRLEN) == NULL)) {
				(void) fprintf(stderr,
				    gettext("Address conversion failed.\n"));
				return (NULL);
			}
		}
	}

	return (object_id);
}

/*
 * Verify that the public key object matches the imported certificate.
 */
static boolean_t
verify_key(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE obj,
    SshX509Certificate cert, char **key_type)
{
	SshPublicKey cert_key;
	SshMPIntegerStruct cert_bignum, obj_bignum;
	boolean_t rc = B_FALSE;

	if (!ssh_x509_cert_get_public_key(cert, &cert_key))
		return (rc);

	if (ssh_public_key_get_info(cert_key,
	    SSH_PKF_SIGN, key_type, SSH_PKF_END) != SSH_CRYPTO_OK)
		return (rc);

	/* Rewhack to fit with command-line input. */
	ssh_sign_to_keytype(key_type);

	/* SPECIAL CASE:  If the object is null, it "matches" per se. */
	if (obj == NULL)
		return (B_TRUE);

	ssh_mp_init(&cert_bignum);
	ssh_mp_init(&obj_bignum);

	if (**key_type == 'd') {
		/* Get and verify DSA public key info. */
		if (ssh_public_key_get_info(cert_key, SSH_PKF_PUBLIC_Y,
		    &cert_bignum, SSH_PKF_END) != SSH_CRYPTO_OK)
			goto bail;

		/*
		 * Find the public value Y from obj, and convert it to an
		 * SshMPIntegerStruct in obj_bignum.
		 */
		if (!extract_pkcs11_public(session, obj, &obj_bignum, CKK_DSA,
		    CKA_VALUE))
			goto bail;
	} else if (**key_type == 'r') {
		/*
		 * Get and verify RSA public key info.  Unlike DSA, even
		 * the PKCS#11 private key object has the public key part
		 * in it.
		 */
		if (ssh_public_key_get_info(cert_key, SSH_PKF_MODULO_N,
		    &cert_bignum, SSH_PKF_END) != SSH_CRYPTO_OK)
			goto bail;
		/*
		 * Find the public exponent from obj, and convert it to an
		 * SshMPIntegerStruct in obj_bignum.
		 */
		if (!extract_pkcs11_public(session, obj, &obj_bignum, CKK_RSA,
		    CKA_MODULUS))
			goto bail;
	} else {
		goto bail;
	}

	/* Compare two bigints. */
	rc = (ssh_mp_cmp(&cert_bignum, &obj_bignum) == 0);

bail:
	ssh_mp_clear(&cert_bignum);
	ssh_mp_clear(&obj_bignum);

	return (rc);
}

/*
 * Make sure that an object identified by the passed-in identifier:
 *
 *	- Has no colliding certificate object.
 *
 * Also set the rsa-md5/rsa-sha1/dsa-sha1 certificate type.
 */
/* ARGSUSED */
static boolean_t
cert_reality_checks(SshX509Certificate cert, CK_SESSION_HANDLE session,
    char *object_id, char **cert_type, boolean_t add_public)
{
	CK_OBJECT_HANDLE obj;
	SshPublicKey cert_key;

	if (!ssh_x509_cert_get_public_key(cert, &cert_key))
		return (B_FALSE);

	if (ssh_public_key_get_info(cert_key, SSH_PKF_SIGN, cert_type,
	    SSH_PKF_END) != SSH_CRYPTO_OK)
		return (B_FALSE);

	/* Rewhack to fit with command-line input. */
	ssh_sign_to_keytype(cert_type);

	obj = find_object(session, object_id, CKO_CERTIFICATE,
	    (CK_KEY_TYPE)CKC_X_509);
	if (obj != NULL) {
		(void) fprintf(stderr,
		    gettext("certdb:  Existing token object with ID %s.\n"),
		    object_id);
		return (B_FALSE);
	}

	/*
	 * Re-add public key to keystore if not there.
	 */
	if (add_public &&
	    !public_to_pkcs11(session, cert_key, object_id))
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Write a PKCS#11 certificate.
 */
static boolean_t
write_pkcs11_cert(int f, uint8_t *ber, size_t len, SshX509Certificate cert,
    struct certlib_keys *private)
{
	CK_SESSION_HANDLE session;
	char *object_id = NULL, *cert_type, *pin;
	char newline = '\n';
	char pinstore[PASSPHRASE_MAX] = {'\0'};
	boolean_t add_public;

#define	do_write(f, p, sz)  if (write(f, p, sz) == -1) { \
		perror("certdb: error writing file"); \
		return (B_FALSE); }

	if (private != NULL) {
		(void) strncpy(pkcs11_token_id, private->pkcs11_label,
		    PKCS11_TOKSIZE);
		object_id = private->pkcs11_id;
		pin = (char *)private->pkcs11_pin;
		(void) printf(
		    gettext("Found matching keypair in token \"%.32s\".\n"),
		    pkcs11_token_id);
		add_public = B_FALSE;
	} else {
		pin = pinstore;
		add_public = B_TRUE;
	}

	/* 1. Write some of the bare-bones data to the file. */
	do_write(f, pkcs11_token_id, PKCS11_TOKSIZE);
	do_write(f, &newline, 1);

	/* 2. Opening a session with the appropriate PKCS#11 token. */
	/* 3. Prompt for a PIN and login. */
	if (global_session != NULL) {
		session = global_session;
		pin = global_pinstore;
	} else {
		session = pkcs11_login(pkcs11_token_id, pin);
	}
	if (session == NULL)
		return (B_FALSE);

	if (!write_pin)
		bzero(pin, PASSPHRASE_MAX);
	do_write(f, pin, strlen(pin));
	do_write(f, &newline, 1);

	/*
	 * 4. Find the SubjectName for an object identifier.  (If no
	 *    SubjectName available, use the SubjectAltName instead.)
	 */

	if (object_id == NULL && (object_id = get_cert_id(cert)) == NULL)
		return (B_FALSE);

	/*
	 * 5. Existence-check existing certificate object.
	 */
	if (!cert_reality_checks(cert, session, object_id, &cert_type,
	    add_public))
		return (B_FALSE);

	/* 6. Write out the PKCS#11 object. */

	if (!pkcs11_cert_generate(session, ber, len, object_id, cert))
		return (B_FALSE);

	/* 7. Write out the rest of the bare-bones data. */
	do_write(f, object_id, strlen(object_id));
	do_write(f, &newline, 1);
	do_write(f, cert_type, strlen(cert_type));
	do_write(f, &newline, 1);

#undef do_write

	return (B_TRUE);
}

/*
 * Ugggh, use global state because certlib_iterate_* doesn't take
 * parameters.  :-P
 */
SshX509Certificate cert_to_match;
struct certlib_keys *matched;

static boolean_t
match_pkcs11(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE public,
    CK_OBJECT_HANDLE private)
{
	SshMPIntegerStruct pubval, privval;
	CK_RV pkcs11_rc;
	CK_KEY_TYPE pubtype, privtype;
	CK_ATTRIBUTE type_vec = {CKA_KEY_TYPE, NULL, sizeof (CK_KEY_TYPE)};
	CK_ATTRIBUTE_TYPE bignum_type;
	boolean_t rc = B_FALSE;

	ssh_mp_init(&pubval);
	ssh_mp_init(&privval);

	type_vec.pValue = &pubtype;
	pkcs11_rc = p11f->C_GetAttributeValue(session, public, &type_vec, 1);
	if (pkcs11_rc != CKR_OK) {
		(void) fprintf(stderr,
		    gettext("certdb: Can't get public-key type.\n"));
		goto bail;
	}

	type_vec.pValue = &privtype;
	pkcs11_rc = p11f->C_GetAttributeValue(session, private, &type_vec, 1);
	if (pkcs11_rc != CKR_OK) {
		(void) fprintf(stderr,
		    gettext("certdb: Can't get private-key type.\n"));
		goto bail;
	}

	if (pubtype != privtype) {
		(void) fprintf(stderr,
		    gettext("certdb: Mismatched keypair.\n"));
		goto bail;
	}

	if (pubtype == CKK_DSA) {
		bignum_type = CKA_PRIME;
	} else if (pubtype == CKK_RSA) {
		bignum_type = CKA_MODULUS;
	} else {
		(void) fprintf(stderr,
		    gettext("certdb: Unknown keypair type %d.\n"), pubtype);
		goto bail;
	}

	if (!extract_pkcs11_public(session, public, &pubval, pubtype,
	    bignum_type)) {
		(void) fprintf(stderr,
		    gettext("certdb: Can't extract public value.\n"));
		goto bail;
	}

	if (!extract_pkcs11_public(session, private, &privval, privtype,
	    bignum_type)) {
		(void) fprintf(stderr,
		    gettext("certdb: Can't extract private value.\n"));
		goto bail;
	}

	rc = (ssh_mprz_cmp(&pubval, &privval) == 0);

bail:
	ssh_mp_clear(&pubval);
	ssh_mp_clear(&privval);
	return (rc);
}

/* certlib_key iterator.  Return 1 if I've found it. */
static int
match_pkcs11_key(struct certlib_keys *k)
{
	CK_OBJECT_HANDLE public, private;
	CK_KEY_TYPE type;
	int rc = 0;
	char *key_type;

	if (k->pkcs11_id == NULL)
		return (0);

	if (k->pkcs11_pin[0] == '\0') {
		if (!interactive) {
			/*
			 * If we didn't specify a token and don't have a pin
			 * then bail so we don't go interactive
			 */
			return (0);
		} else {
			/*
			 * If we did specify a token and don't have a pin
			 * then only log into a token that we specified.
			 */
			if (strncmp(k->pkcs11_label, pkcs11_token_id,
			    PKCS11_TOKSIZE) != 0)
				return (0);
		}
	}

	if (global_session == NULL) {
		global_session = pkcs11_login(k->pkcs11_label,
		    (char *)k->pkcs11_pin);
		(void) strlcpy(global_pinstore, (const char *)k->pkcs11_pin,
		    PASSPHRASE_MAX);
	}
	if (global_session == NULL) {
		(void) fprintf(stderr, gettext("\tas specified in slot %s\n"),
		    k->slotname);
		goto bail;
	}

	type = (*(k->type) == 'r' ? CKK_RSA : CKK_DSA);
	public = find_object(global_session, k->pkcs11_id, CKO_PUBLIC_KEY,
	    type);
	if (public == NULL)
		goto bail;
	private = find_object(global_session, k->pkcs11_id, CKO_PRIVATE_KEY,
	    type);
	if (private == NULL)
		goto bail;

	if (match_pkcs11(global_session, public, private) &&
	    verify_key(global_session, public, cert_to_match, &key_type)) {
		rc = 1;
		matched = k;
	}

bail:
	return (rc);
}

/*
 * Iterate all of the loaded private keys to see if I've found a match.
 * If so, return it, making sure that we exploit the PKCS#11 ID data on there.
 */
static struct certlib_keys *
check_matching_private(SshX509Certificate cert)
{
	cert_to_match = cert;

	certlib_iterate_keys(match_pkcs11_key);
	return (matched);
}

static void
close_global_session()
{
	if (global_session != NULL) {
		/*
		 * Belt-and-suspenders move, in case
		 * C_CloseSession() is broken.
		 */
		(void) p11f->C_Logout(global_session);
		(void) p11f->C_CloseSession(global_session);
		global_session = NULL;
		bzero(global_pinstore, PASSPHRASE_MAX);
	}
}

/*
 * Add certificate to the database.  If infile is NULL, the certificate is
 * inputted via stdin, otherwise it reads from a file.  The certificate maybe
 * BER or PEM format.  Not format flag is needed for it will auto-detect.
 */
static int
add_cert(const char *infile)
{
	FILE *f;
	char outfile[MAXPATHLEN];
	unsigned char *b = NULL, *p = NULL;
	size_t size = 0, total_size = 0;
	int i = 0, fout;
	SshX509Certificate cert;

	/* Assign or open input source */
	if (infile == NULL)
		f = stdin;
	else {
		if ((f = fopen(infile, "r")) == NULL) {
			perror(infile);
			return (1);
		}
	}

	/* Load data and write to file */
	while (feof(f) == 0) {
		b = ssh_realloc(b, total_size, 1024 + total_size);
		if (b == NULL)
			memory_bail();
		size = fread(b + total_size, sizeof (unsigned char), 1023, f);
		total_size += size;
	}
	b[total_size] = NULL;
	if (f != stdin)
		(void) fclose(f);

	/* Extract certificate if input was BER */
	cert = extract_from_ber(b, total_size, CERTLIB_CERT);

	/*
	 * Check if extraction was successful, if not, try converting from
	 * PEM and try extractioning again.
	 */
	if (cert == NULL) {
		p = (unsigned char *)pem_to_ber(b, &total_size);
		if (p == NULL) {
			(void) fprintf(stderr, "%s\n",
			    gettext("Input is neither PEM-encoded "
			    "nor BER-encoded; unable to continue."));
			return (1);
		}
		cert = extract_from_ber(p, total_size, CERTLIB_CERT);
		if (cert == NULL) {
			(void) fprintf(stderr, "%s\n",
			    gettext("Unknown format given."));
			return (1);
		} else {
			if (debug) {
				(void) fprintf(stderr, "%s\n",
				    gettext("PEM format detected."));
			}
		}
	} else {
		p = b;
	}

	/* If extraction successful, import data to database */
	if (cert != NULL) {
		struct certlib_keys *private;
		int perms = S_IRUSR|S_IWUSR;

		/*
		 * PKCS#11 hint files can have pins in the clear.
		 * Regular cert files can be publicly readable.
		 */
		if (pkcs11_token_id[0] == '\0')
			perms |= S_IRGRP|S_IROTH;

		/* Find available slot number and open */
		(void) snprintf(outfile, MAXPATHLEN, "%s%d",
		    certlib_certs_dir(), i);
		/* Since fd 0 is already open, this will loop until we break. */
		while ((fout = open(outfile, O_CREAT|O_EXCL|O_WRONLY,
		    perms)) < 0) {
			if (errno == EEXIST) {
				(void) snprintf(outfile, MAXPATHLEN, "%s%d",
				    certlib_certs_dir(), ++i);
			} else {
				perror(gettext("Unable to open public "
				    "certificate database."));
				exit(1);
			}
		}

		private = check_matching_private(cert);

		if (private != NULL || pkcs11_token_id[0] != '\0') {
			if (!write_pkcs11_cert(fout, p, total_size, cert,
			    private)) {
				(void) fprintf(stderr, gettext("certdb: "
				    "Cannot import certificate to token.\n"));
				(void) close(fout);
				(void) unlink(outfile);
				close_global_session();
				exit(1);
			}
		} else {
			if (write(fout, p, total_size) == -1) {
				perror(
				    gettext("certdb: certificate file write"));
				(void) close(fout);
				(void) unlink(outfile);
				close_global_session();
				exit(1);
			}
		}

		(void) close(fout);

	} else {
		(void) fprintf(stderr, "%s\n",
		    gettext("No Certificate detected."));
		close_global_session();
		return (1);
	}
	ssh_free(p);
	ssh_free(b);
	ssh_x509_cert_free(cert);
	close_global_session();
	return (0);
}

/*
 * Associate existing PKCS#11 objects with the IKE database
 */
int
pkcs11_associate_certs(struct certlib_certspec *p)
{
	char pin[256] = {'\0'};
	CK_RV rv;
	int i, error = 0;
	CK_SESSION_HANDLE session = NULL;

	if (certlib_find_local_pkcs11_cert(p) != NULL) {
		/* No need to associate if already have a hint file */
		(void) fprintf(stderr, gettext(
		    "PKCS#11 object linkage with IKE already exists.\n"));
		return (1);
	}

	/*
	 * Get PKCS#11 session - no need to login
	 * unless we want to write the pin
	 */

	session = pkcs11_get_session(pkcs11_token_id, pin, write_pin);
	if (session == NULL)
		return (1);

	for (i = 0; i < p->num_includes; i++) {
		const char *id;
		char pubkeytype[4];
		CK_ATTRIBUTE cert = {CKA_VALUE, NULL, 0};
		char *keytype;
		CK_OBJECT_HANDLE object = NULL;
		SshX509Certificate sshcert;
		SshPublicKey key;

		id = p->includes[i];

		object = find_object(session, (char *)id, CKO_CERTIFICATE,
		    (CK_KEY_TYPE)CKC_X_509);
		if (object == NULL) {
			(void) fprintf(stderr,
			    gettext("No certificate object with ID %s "
			    "found.\n"), id);
			error = 1;
			continue;
		}

		/*
		 * Found object, need to figure out if the key type
		 * is RSA or DSA.  We can't necessarily know whether the
		 * public key object exists on all devices (apparently
		 * some smart cards don't store it as a separate object),
		 * so we extract it right from the cert.
		 */

		/*
		 * First, figure out the size of the ber blob
		 * and allocate a suitable chunk of memory
		 */
		rv = p11f->C_GetAttributeValue(session, object, &cert, 1);
		if (rv == CKR_OK) {
			cert.pValue = ssh_malloc(cert.ulValueLen);
			if (cert.pValue == NULL)
				memory_bail();
		} else {
			(void) fprintf(stderr, gettext("Could not extract "
			    "certificate, rv = %d\n"), rv);
			error = 1;
			goto bail;
		}

		/* Now actually get the ber blob */
		rv = p11f->C_GetAttributeValue(session, object, &cert, 1);
		if (rv != CKR_OK) {
			(void) fprintf(stderr, gettext("Could not extract "
			    "certificate, rv = %d\n"), rv);
			error = 1;
			goto bail;
		}

		/* Now we have the ber blob, but we need the ssh cert struct */
		sshcert = extract_from_ber(cert.pValue, cert.ulValueLen,
		    CERTLIB_CERT);
		if (sshcert == NULL) {
			error = 1;
			goto bail;
		}

		if (!ssh_x509_cert_get_public_key(sshcert, &key)) {
			(void) fprintf(stderr, gettext(
			    "Couldn't get cert public key.\n"));
			error = 1;
			goto bail;
		}

		rv = ssh_public_key_get_info(key, SSH_PKF_SIGN, &keytype,
		    SSH_PKF_END);
		if (rv != SSH_CRYPTO_OK) {
			(void) fprintf(stderr, gettext("Could not get keytype "
			    "for public key, rv = %d\n"), rv);
			error = 1;
			goto bail;
		}

		/*
		 * Now we have everything we need to create
		 * the PKCS#11 hint file.
		 */

		if (*keytype == 'r')
			(void) strcpy(pubkeytype, "rsa");
		else
			(void) strcpy(pubkeytype, "dsa");

		if (!write_pin)
			bzero(pin, PASSPHRASE_MAX);
		if (write_pkcs11_files(pkcs11_token_id, pin, (char *)id,
		    pubkeytype, B_TRUE, B_FALSE, write_pin)) {
			(void) printf(gettext(
			    "PKCS#11 Public key association successful.\n"));
		} else {
			(void) printf(gettext("PKCS#11 hint file could not be "
			    "written to disk.\n"));
			error = 1;
		}
bail:
		/* Can ssh_free() NULL */
		ssh_free(cert.pValue);
	}
	(void) p11f->C_CloseSession(session);
	(void) p11f->C_Finalize(NULL_PTR);
	return (error);
}


int matches = 0;
int errors = 0;
int Uflag = 0;

/*
 * Remove a certificate file from the database
 *
 */

static int
remove_cert_file(const char *path)
{
	uint8_t *buffer, *sofar, *bufcpy, *newbuf;
	struct stat sbuf;
	int fd;
	ssize_t rc, toread;
	SshX509Certificate cert;
	char *token_label = NULL, *token_pin, *key_id, *key_type;
	char pinstore[PASSPHRASE_MAX] = {'\0'};
	CK_SESSION_HANDLE session;
	CK_KEY_TYPE pkcs11_keytype;

	matches = 1;
	/*
	 * Check to see if it's a PKCS#11 object... If so,
	 * remove the object as well as the hint file
	 */
	fd = open(path, O_RDONLY);
	if (fd != -1 && fstat(fd, &sbuf) != -1) {
		buffer = ssh_calloc(1, sbuf.st_size);
		if (buffer == NULL) {
			(void) fprintf(stderr,
			    gettext("Cannot allocate %d bytes for slot.\n"),
			    sbuf.st_size);
			return (1);
		}
		rc = read(fd, buffer, sbuf.st_size);
		sofar = buffer;
		toread = (ssize_t)sbuf.st_size;

		while (rc < toread) {
			if (rc == -1) {
				if (Uflag) {
					(void) fprintf(stderr,
					    gettext("Corrupt slot %s, use -r "
					    "to remove this file.\n"),
					    path);
					return (1);
				} else {
					(void) fprintf(stderr, gettext(
					    "Removing corrupt slot %s.\n"),
					    path);
					goto remove_file;
				}
			}
			toread -= rc;
			sofar += rc;
			rc = read(fd, sofar, toread);
		}

		if ((extract_from_ber(buffer, (size_t)sbuf.st_size,
		    CERTLIB_CERT) == NULL)) {
			parse_pkcs11_file(buffer, sbuf.st_size, &token_label,
			    &token_pin, &key_id, &key_type);

			if (token_label == NULL) {
				if (Uflag) {
					(void) fprintf(stderr,
					    gettext("Corrupt slot %s, use -r "
					    "to remove this file.\n"),
					    path);
					return (1);
				} else {
					(void) fprintf(stderr, gettext(
					    "Removing corrupt slot %s.\n"),
					    path);
					goto remove_file;
				}
			}

			if (Uflag) {
				char padtok[PKCS11_TOKSIZE];
				pkcs11_pad_out(padtok, token_label);
				if (strncmp(pkcs11_token_id, padtok,
				    PKCS11_TOKSIZE) != 0) {
					(void) fprintf(stderr, gettext(
					    "Token \"%s\" not referenced in "
					    "%s.\n"), pkcs11_token_id, path);
					exit(1);
				} else {
					goto remove_file;
				}
			}

			if (p11f == NULL) {
				(void) fprintf(stderr,
				    gettext("Must have PKCS#11 library loaded "
				    "to remove this certificate.\n"));
				exit(1);
			}

			if (token_pin[0] == '\0')
				token_pin = pinstore;
			session = pkcs11_login(token_label, token_pin);
			bzero(pinstore, PASSPHRASE_MAX);
			if (session == NULL) {
				(void) fprintf(stderr,
				    gettext("Could not log into token, use "
				    "-U to unlink without accessing pkcs#11 "
				    "device.\n"));
				exit(1);
			}

			/* Find and nuke the certificate and public key. */
			find_and_nuke(session, key_id, CKO_CERTIFICATE,
			    (CK_KEY_TYPE)CKC_X_509, B_FALSE);
			find_and_nuke(session, key_id, CKO_PUBLIC_KEY,
			    (*key_type == 'd') ? CKK_DSA : CKK_RSA, B_TRUE);
		}
	}

remove_file:
	if (unlink(path) != 0) {
		(void) fprintf(stderr, gettext("certdb: could not remove "
		    "%s: %s\n"), path, strerror(errno));
		errors = 1;
		return (1);
	}
	(void) fprintf(stderr, gettext("certdb: certificate file successfully "
	    "removed.\n"));
	return (0);
}

/*
 * Remove a certificate from the database.
 */
int
remove_cert(struct certlib_cert *p)
{
	matches = 1;
	(void) remove_cert_file(p->slotname);
	return (0);
}

/*
 * Remove certificates from the database.
 */
int
remove_certs(struct certlib_certspec *pattern)
{
	if (chdir(certlib_certs_dir()) != 0) {
		(void) fprintf(stderr, "certdb: %s: %s\n", certlib_certs_dir(),
		    strerror(errno));
		return (1);
	}

	certlib_find_cert_spec(pattern, remove_cert);

	if (matches < 1) {
		if (!Uflag)
			(void) fprintf(stderr, "%s\n",
			    gettext("certdb: no name pattern matches found, "
			    "checking by slot filename."));
		return (1);
	}

	return (errors);
}

const char *extract_outfile;
const char *extract_format;

int
extract_cert(struct certlib_cert *p)
{
	matches = 1;

	export_data(p->data, p->datalen, extract_outfile,
	    extract_format, SSH_PEM_X509_BEGIN, SSH_PEM_X509_END);
	return (1);
}

/*
 * Extract a certificate from the database.  If outfile is NULL, the data
 * will be sent to stdout, otherwise to the given file.  The out_fmt is the
 * format of the output (ber or pem).
 */
int
extract_certs(struct certlib_certspec *pattern,
    const char *outfile, const char *out_fmt)
{
	extract_outfile = outfile;
	extract_format = out_fmt;

	certlib_find_cert_spec(pattern, extract_cert);

	if (!matches) {
		(void) fprintf(stderr, "%s\n",
		    gettext("certdb: no certificate found"));
		return (1);
	}

	return (errors);
}


/*
 * Prints descriptive text (multiple lines) to stdout.
 */
static int
dump_public_key(SshX509Certificate cert, const char *type)
{
	SshPublicKey public_key;

	if (ssh_x509_cert_get_public_key(cert, &public_key) == FALSE ||
	    public_key == NULL) {
		(void) puts("\t\t[Public key invalid.]");
		return (1);
	}

	if (*type == 'r') {
		SshMPIntegerStruct e, n;
		size_t k_len;

		/* Handle RSA keys. */
		ssh_mp_init(&e);
		ssh_mp_init(&n);

		if (ssh_public_key_get_info(public_key,
		    SSH_PKF_MODULO_N, &n,
		    SSH_PKF_PUBLIC_E, &e,
		    SSH_PKF_SIZE, &k_len,
		    SSH_PKF_END) != SSH_CRYPTO_OK) {
			(void) puts("\t\t[Internal error, could not get RSA "
			    "parameters.]");
			ssh_mp_clear(&e);
			ssh_mp_clear(&n);
			ssh_public_key_free(public_key);
			return (1);
		}

		/* Public modulus is equal to key length */

		(void) printf(
		    gettext("\t\tPublic Modulus  (n) (%4u bits): %s\n"),
		    k_len, dump_number(&n));

		(void) printf(
		    gettext("\t\tPublic Exponent (e) (%4u bits): %s\n"),
		    SSH_GET_ROUNDED_SIZE(&e, 2, 8), dump_number(&e));

		ssh_mp_clear(&n);
		ssh_mp_clear(&e);
	} else if (*type == 'd') {
		SshMPIntegerStruct p, q, g, y;
		size_t k_len;

		/* Handle DSA keys. */
		ssh_mp_init(&p);
		ssh_mp_init(&g);
		ssh_mp_init(&q);
		ssh_mp_init(&y);

		if (ssh_public_key_get_info(public_key,
		    SSH_PKF_PRIME_P, &p,
		    SSH_PKF_PRIME_Q, &q,
		    SSH_PKF_GENERATOR_G, &g,
		    SSH_PKF_PUBLIC_Y, &y,
		    SSH_PKF_SIZE, &k_len,
		    SSH_PKF_END) != SSH_CRYPTO_OK) {
			ssh_mp_clear(&p);
			ssh_mp_clear(&q);
			ssh_mp_clear(&g);
			ssh_mp_clear(&y);
			(void) puts("\t\t[Internal error, could not get DSA "
			    "parameters.]");
			ssh_public_key_free(public_key);
			return (1);
		}

		/* Generator, prime, and public key length are equal */

		(void) printf(
		    gettext("\t\tGenerator       (g) (%4u bits): %s\n"),
		    k_len, dump_number(&g));

		(void) printf(
		    gettext("\t\tPrime           (p) (%4u bits): %s\n"),
		    k_len, dump_number(&p));

		/* Technically, this is hardcoded to 160 bits */

		(void) printf(
		    gettext("\t\tGroup order     (q) (%4u bits): %s\n"),
		    SSH_GET_ROUNDED_SIZE(&q, 2, 8), dump_number(&q));

		(void) printf(
		    gettext("\t\tPublic key      (y) (%4u bits): %s\n"),
		    k_len, dump_number(&y));

		ssh_mp_clear(&p);
		ssh_mp_clear(&q);
		ssh_mp_clear(&g);
		ssh_mp_clear(&y);
	} else {
		(void) puts(
		    gettext("\t\tUnsupported key type - corrupt file."));
		ssh_public_key_free(public_key);
		return (1);
	}

	ssh_public_key_free(public_key);

	return (0);
}

/*
 * Returns a static buffer which is overwritten on each call.
 */
char *
dump_keyusage(SshX509UsageFlags flags)
{
	static char buf[512];
	char *p = buf;
	if (flags & SSH_X509_UF_DIGITAL_SIGNATURE)
		p += sprintf(p, "DigitalSignature ");
	if (flags & SSH_X509_UF_NON_REPUDIATION)
		p += sprintf(p, "NonRepudiation ");
	if (flags & SSH_X509_UF_KEY_ENCIPHERMENT)
		p += sprintf(p, "KeyEncipherment ");
	if (flags & SSH_X509_UF_DATA_ENCIPHERMENT)
		p += sprintf(p, "DataEncipherment ");
	if (flags & SSH_X509_UF_KEY_AGREEMENT)
		p += sprintf(p, "KeyAgreement ");
	if (flags & SSH_X509_UF_KEY_CERT_SIGN)
		p += sprintf(p, "KeyCertSign ");
	if (flags & SSH_X509_UF_CRL_SIGN)
		p += sprintf(p, "CRLSign ");
	if (flags & SSH_X509_UF_ENCIPHER_ONLY)
		p += sprintf(p, "EncipherOnly ");
	if (flags & SSH_X509_UF_DECIPHER_ONLY)
		p += sprintf(p, "DecipherOnly ");

	flags &= ~(SSH_X509_UF_DIGITAL_SIGNATURE |
	    SSH_X509_UF_NON_REPUDIATION |
	    SSH_X509_UF_KEY_ENCIPHERMENT |
	    SSH_X509_UF_DATA_ENCIPHERMENT |
	    SSH_X509_UF_KEY_AGREEMENT |
	    SSH_X509_UF_KEY_CERT_SIGN |
	    SSH_X509_UF_CRL_SIGN |
	    SSH_X509_UF_ENCIPHER_ONLY |
	    SSH_X509_UF_DECIPHER_ONLY);
	if (flags != 0 || p == buf)
		p += sprintf(p, "0x%04X\n", flags);

	p[-1] = 0;
	return (buf);
}


static const char *ext_table[] =
{
	NULL, /* authority key identifier */
	NULL, /* subject key identifier */
	NULL, /* key usage */
	NULL, /* private key usage period */
	NULL, /* Certificate Policies */
	"Policy Mappings (not interpreted)",
	NULL, /* subject alternative names */
	NULL, /* issuer alternative names */
	"Subject Directory Attributes (not interpreted)",
	NULL, /* basic constraints */
	"Name Constraints (not interpreted)",
	"Policy Constraints (not interpreted)",
	"Private Internet Extensions (not interpreted)",
	NULL, /* Authority Information Access */
	NULL, /* CRL distribution points */
	NULL, /* extended key usage */
	"Netscape Comment (not interpreted)",
	"Certificate Template Name (not interpreted)",
	"Qualified Certificate Statements (not interpreted)",
	"Subject Information Access (not interpreted)",
	"Freshest CRL Access Location (not interpreted)",
	"Inhibit Any Indicator (not interpreted)",
	"Unknown Non-Critical Extension (not interpreted)",
};
#define	NUMEXTTYPES (sizeof (ext_table)/sizeof (*ext_table))

static int
count_extensions(SshX509Certificate c)
{
	int i, n = 0;
	Boolean critical;

	for (i = 0; i < NUMEXTTYPES; ++i)
		if (ssh_x509_cert_ext_available(c, i, &critical))
			++n;
	return (n);
}

static void
dump_unknown_extensions(SshX509Certificate c)
{
	int i;
	Boolean critical;

	for (i = 0; i < NUMEXTTYPES; ++i) {
		if (ext_table[i] != NULL &&
		    ssh_x509_cert_ext_available(c, i, &critical)) {
			(void) printf("\t    %s%s\n", ext_table[i],
			    (critical ? " [CRITICAL]" : ""));
		}
	}
}

/*
 * Function that certlib_iterate_certs or certlib_find_cert_spec will call to
 * print one certificate.
 */
int
print_cert(struct certlib_cert *p)
{
	SshX509Certificate c = p->cert;
	SshMPIntegerStruct s;
	Boolean rv;
	SshCryptoStatus get_info_rv = SSH_CRYPTO_UNSUPPORTED_IDENTIFIER;
	char *subject;

	ssh_mp_init(&s);

	(void) printf("%s: %s   %s: %s\n",
	    gettext("Certificate Slot Name"), p->slotname,
	    gettext("Key Type"), p->type);

	if (p->keys != NULL)
		(void) printf("\t(%s %s)\n",
		    gettext("Private key in certlocal slot"),
		    p->keys->slotname);

	if (p->crl != NULL)
		(void) printf("\t(%s %s)\n",
		    gettext("CRL in certrldb slot"), p->crl->slotname);

	if (p->pkcs11_label[0] != '\0') {
		(void) printf("\t(%s \"%.32s\")\n",
		    gettext("In PKCS#11 hardware token"), p->pkcs11_label);
		(void) printf("\t(%s\n\t\"%s\")\n",
		    gettext("PKCS#11 object identifier"), p->pkcs11_id);
		if (p->pkcs11_pin[0] != '\0') {
			(void) printf(gettext(
			    "\t(PKCS#11 keystore pin in clear on disk)\n"));
		} else {
			(void) printf(gettext(
			    "\t(PKCS#11 keystore pin not stored on disk)\n"));
		}
	}

	/* subject distinguished name */
	rv = ssh_x509_cert_get_subject_name(c, &subject);
	if (rv) {
		(void) printf(gettext("\tSubject Name: %s\n"),
		    dump_name(subject));
	} else {
		subject = "";
		(void) puts(gettext("\tNo Subject Name"));
	}

	if (verbose) {
		SshBerTimeStruct not_before, not_after;
		char *issuer;
		unsigned char *buf;
		size_t buf_len;
		SshX509Name names;
		Boolean critical;
		Boolean sig_check = TRUE;

		/* issuer distinguished name */
		rv = ssh_x509_cert_get_issuer_name(c, &issuer);
		if (rv == FALSE) {
			(void) puts(gettext("\tNo Issuer Name"));
			issuer = "";
		} else {
			(void) printf(gettext("\tIssuer Name: %s\n"),
			    dump_name(issuer));
		}

		/* Serial number. */
		rv = ssh_x509_cert_get_serial_number(c, &s);
		if (rv == FALSE)
			goto failed;
		(void) printf(gettext("\tSerialNumber: %s\n"),
		    dump_number(&s));

		if (strcmp(subject, issuer) == 0) {
			if (ssh_x509_cert_verify(c,
			    c->subject_pkey.public_key) == FALSE) {
				(void) puts(
				    gettext("\tSelf-signature VERIFICATION "
				    "FAILED\n"));
				sig_check = FALSE;
			} else {
				(void) puts(gettext("\tSelf-signature "
				    "verified successfully"));
			}
		}

		/* Validity times. */
		(void) printf(gettext("\tValidity:\n"));
		rv = ssh_x509_cert_get_validity(c, &not_before, &not_after);
		if (rv == FALSE)
			goto failed;
		(void) printf(gettext("\t\tNot Valid Before: %s\n"),
		    dump_time(&not_before));
		(void) printf(gettext("\t\tNot Valid After:  %s\n"),
		    dump_time(&not_after));

		/* Public key */
		(void) puts(gettext("\tPublic Key Info:"));
		if (dump_public_key(c, p->type))
			goto failed;

		/* Unique identifiers */
		rv = ssh_x509_cert_get_subject_unique_identifier(c,
		    &buf, &buf_len);
		if (rv == TRUE) {
			(void) puts(gettext("\tSubject Unique Identifier:"));
			dump_hex(buf, buf_len);
		}

		rv = ssh_x509_cert_get_issuer_unique_identifier(c,
		    &buf, &buf_len);
		if (rv == TRUE) {
			(void) puts(gettext("\tIssuer Unique Identifier:"));
			dump_hex(buf, buf_len);
		}

		if (count_extensions(c) > 0)
			(void) puts(gettext("\tX509v3 Extensions:"));

		/* Alternative names. */
		if (ssh_x509_cert_ext_available(c,
		    SSH_X509_EXT_SUBJECT_ALT_NAME, &critical)) {
			(void) puts(
			    gettext("\t    Subject Alternative Names:"));
			if (ssh_x509_cert_get_subject_alternative_names(c,
			    &names, &critical)) {
				dump_names(names);
				if (critical)
					(void) puts("\t    [CRITICAL]");
			}
		}

		if (ssh_x509_cert_ext_available(c,
		    SSH_X509_EXT_ISSUER_ALT_NAME, &critical)) {
			(void) puts(gettext("\t    Issuer Alternative Names:"));
			if (ssh_x509_cert_get_issuer_alternative_names(c,
			    &names, &critical)) {
				dump_names(names);
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}

		/* Certificate Policies */
		if (ssh_x509_cert_ext_available(c, SSH_X509_EXT_CERT_POLICIES,
		    &critical)) {
			SshX509ExtPolicyInfo pol_info;
			SshX509ExtPolicyQualifierInfo pq_list;
			size_t len;

			(void) puts(gettext("\t    Certificate Policies:"));
			if (ssh_x509_cert_get_policy_info(c, &pol_info,
			    &critical)) {
				for (; pol_info; pol_info = pol_info->next) {
					pq_list = pol_info->pq_list;
					if (pol_info->oid) {
						/* These are straight OIDs */
						(void) printf("\t\tOID = %s\n",
						    pol_info->oid);
					}
					if (pq_list != NULL) {
						if (pq_list->cpsuri) {
							(void) printf(
							    "\t\tCPS: %s\n",
							    ssh_str_get(
							    pq_list->cpsuri,
							    &len));
						}
						if (pq_list->organization) {
							(void) printf(
							    "\t\t%s\n",
							    ssh_str_get(
							    pq_list->
							    organization,
							    &len));
						}
						if (pq_list->explicit_text) {
							(void) printf(
							    "\t\t%s\n",
							    ssh_str_get(
							    pq_list->
							    explicit_text,
							    &len));
						}
					}
				}
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}

		/* Private key usage period */
		if (ssh_x509_cert_ext_available(c, SSH_X509_EXT_PRV_KEY_UP,
		    &critical)) {
			(void) puts(gettext("\t    Private Key Usage Period:"));
			if (ssh_x509_cert_get_private_key_usage_period(c,
			    &not_before, &not_after, &critical)) {
				if (ssh_ber_time_available(&not_before)) {
					(void) printf(
					    gettext(
					    "\t\tNot Valid Before: %s\n"),
					    dump_time(&not_before));
				}
				if (ssh_ber_time_available(&not_after)) {
					(void) printf(
					    gettext(
					    "\t\tNot Valid After: %s\n"),
					    dump_time(&not_after));
				}
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}

		/* Key usage. */
		if (ssh_x509_cert_ext_available(c, SSH_X509_EXT_KEY_USAGE,
		    &critical)) {
			SshX509UsageFlags usage;
			if (ssh_x509_cert_get_key_usage(c, &usage, &critical)) {
				(void) printf(gettext("\t    Key Usage: %s\n"),
				    dump_keyusage(usage));
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}

		if (ssh_x509_cert_ext_available(c, SSH_X509_EXT_BASIC_CNST,
		    &critical)) {
			size_t path_length;
			Boolean ca;

			(void) puts(gettext("\t    Basic Constraints:"));
			if (ssh_x509_cert_get_basic_constraints(c,
			    &path_length, &ca, &critical)) {
				if (path_length != SSH_X509_MAX_PATH_LEN)
					(void) printf(gettext(
					    "\t\tPathLength: %u\n"),
					    path_length);
				(void) printf("\t\tCA: %s\n",
				    (ca == TRUE ? "TRUE" : "FALSE"));
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}
		if (ssh_x509_cert_ext_available(c,
		    SSH_X509_EXT_AUTH_INFO_ACCESS, &critical)) {
			SshX509ExtInfoAccess access_pts;

			(void) puts(
			    gettext("\t    Authority Information Access:"));
			if (ssh_x509_cert_get_auth_info_access(c, &access_pts,
			    &critical)) {
				for (; access_pts;
				    access_pts = access_pts->next) {
					if (access_pts->access_method) {
						/*
						 * No functions interpret
						 * these OIDs
						 */
						if (strncmp(
						    access_pts->access_method,
						    SSH_OCSP_OID_ID_AD_OCSP,
						    strlen(
						    SSH_OCSP_OID_ID_AD_OCSP))
						    == 0) {
							printf("\t\tOCSP\n");
						} else if (strncmp(
						    access_pts->access_method,
						    ISSUER_OID,
						    strlen(ISSUER_OID)) ==
						    0) {
							printf(
							    "\t\tCA Issuers\n");
						} else {
							printf("\t\tOID = %s\n",
							    access_pts->
							    access_method);
						}
					}
					if (access_pts->access_location) {
						dump_names(access_pts->
						    access_location);
					}
				}
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}

		if (ssh_x509_cert_ext_available(c,
		    SSH_X509_EXT_CRL_DIST_POINTS, &critical)) {
			SshX509ExtCRLDistPoints dist_points;

			(void) puts(gettext("\t    CRL Distribution Points:"));
			if (ssh_x509_cert_get_crl_dist_points(c,
			    &dist_points, &critical)) {
				for (; dist_points;
				    dist_points = dist_points->next) {
					if (dist_points->full_name) {
						(void) puts(gettext(
						    "\t\tFull Name:"));
						dump_names(dist_points->
						    full_name);
					}
					if (dist_points->
					    dn_relative_to_issuer) {
						char *name_buf;

						if (ssh_dn_encode_ldap(
						    dist_points->
						    dn_relative_to_issuer,
						    &name_buf)) {
							(void) printf(
							    gettext(
							    "\t\tDN Relative "
							    "To Issuer: %s\n"),
							    dump_name(
							    name_buf));
						}
						ssh_free(name_buf);
					}
					if (dist_points->reasons) {
						(void) printf(
						    gettext(
						    "\t\tReasons: %s\n"),
						    dump_reason(dist_points->
						    reasons));
					}
					if (dist_points->crl_issuer) {
						(void) puts(
						    gettext(
						    "\t\tCRL Issuer: "));
						dump_names(dist_points->
						    crl_issuer);
					}
				}
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}
		if (ssh_x509_cert_ext_available(c, SSH_X509_EXT_AUTH_KEY_ID,
		    &critical)) {
			SshX509ExtKeyId key_id;
			(void) puts(gettext("\t    Authority Key ID:"));
			if (ssh_x509_cert_get_authority_key_id(c, &key_id,
			    &critical)) {
				if (key_id->key_id && key_id->key_id_len != 0) {
					(void) puts(gettext("\t\tKey ID:"));
					dump_hex(key_id->key_id,
					    key_id->key_id_len);
				}
				if (key_id->auth_cert_issuer) {
					(void) puts(gettext(
					    "\t\tAuthority Certificate "
					    "Issuer:"));
					dump_names(key_id->auth_cert_issuer);
				}
				if (ssh_mp_cmp_ui(
				    &key_id->auth_cert_serial_number, 0) >= 0) {
					(void) printf(
					    gettext("\t\tAuthority Certificate "
					    "Serial Number: %s\n"),
					    dump_number(&key_id->
					    auth_cert_serial_number));
				}
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}

		if (ssh_x509_cert_ext_available(c, SSH_X509_EXT_SUBJECT_KEY_ID,
		    &critical)) {
			unsigned char *key_id;
			size_t key_id_len;
			if (ssh_x509_cert_get_subject_key_id(c, &key_id,
			    &key_id_len, &critical)) {
				(void) puts(gettext("\t    Subject Key ID:"));
				if (key_id && key_id_len != 0) {
					(void) printf(gettext(
					    "\t\tKey ID:\n     "));
					dump_hex(key_id, key_id_len);
					if (critical)
						(void) puts("\t\t[CRITICAL]");
				}
			}
		}

		if (ssh_x509_cert_ext_available(c, SSH_X509_EXT_EXT_KEY_USAGE,
		    &critical)) {
			SshX509OidList oid_list;
			const SshOidStruct *oids;

			if (ssh_x509_cert_get_ext_key_usage(c, &oid_list,
			    &critical)) {
				(void) puts(gettext(
				    "\t    Extended Key Usage:"));
				if (oid_list != NULL) {
					while (oid_list != NULL) {
						oids =
						    ssh_oid_find_by_oid_of_type(
						    (uchar_t *)oid_list->oid,
						    SSH_OID_EXT_KEY_USAGE);
						if (oids == NULL)
							(void) printf(
							    "\t\tOID = %s\n",
							    oid_list->oid);
						else
							(void) printf(
							    "\t\t%s (%s)\n",
							    oids->std_name,
							    oid_list->oid);
						oid_list = oid_list->next;
					}
				}
				if (critical)
					(void) puts("\t\t[CRITICAL]");
			}
		}

		dump_unknown_extensions(c);

		if (!sig_check) {
failed:
			(void) puts(gettext("\t[error in certificate "
			    "contents]"));
		}
	} else {
		SshMPIntegerStruct n;
		size_t key_len;

		ssh_mp_init(&n);

		if (strncmp(p->type, "rsa", 3) == 0) {
			get_info_rv = ssh_public_key_get_info(
			    c->subject_pkey.public_key,
			    SSH_PKF_MODULO_N, &n,
			    SSH_PKF_SIZE, &key_len, SSH_PKF_END);
		} else if (strncmp(p->type, "dsa", 3) == 0) {
			get_info_rv = ssh_public_key_get_info(
			    c->subject_pkey.public_key,
			    SSH_PKF_PUBLIC_Y, &n,
			    SSH_PKF_SIZE, &key_len, SSH_PKF_END);
		} else {
			(void) puts(gettext("\tUnknown key type"));
		}

		if ((get_info_rv == SSH_CRYPTO_OK) &&
		    (key_len > 0)) {
			(void) printf(gettext("\tKey Size: %u\n"), key_len);
			(void) printf(gettext("\tPublic key hash: %s\n"),
			    dump_public_hash(&n));
		} else {
			(void) printf(
			    gettext("\tUnable to get key info - Error %d\n"),
			    get_info_rv);
		}
		ssh_mp_clear(&n);
	}

	ssh_mp_clear(&s);

	(void) putchar('\n');

	return (0);
}

int
print_certs(struct certlib_certspec *pattern)
{
	if (pattern)
		certlib_find_cert_spec(pattern, print_cert);
	else
		certlib_iterate_certs(print_cert);

	return (0);
}

int
migrate_cert_to_token(struct certlib_cert *p)
{
	(void) printf(gettext("Found public cert in slot %s.\n"), p->slotname);
	exit(pkcs11_migrate_keypair(p, B_FALSE, write_pin));
	/* NOTREACHED */
	return (EXIT_SUCCESS);
}

int
main(int argc, char *argv[])
{
	struct certlib_certspec *pattern;
	int help = 0;
	int lflag = 0, aflag = 0, rflag = 0, eflag = 0, Lflag = 0,
	    Tflag = 0, Cflag = 0;
	char c, *infile = NULL, *outfile = NULL, *out_fmt = NULL;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	certlib_mode = CERTLIB_NORMAL;
	if (argc == 1)
		help = 1;

	while ((c = getopt(argc, argv, "h?dvlarepf:i:o:T:CLU")) != EOF) {
		switch (c) {
		case 'h':
		case '?':
			help = 1;
			break;
		case 'T':
			Tflag = 1;
			interactive = B_TRUE;
			pkcs11_pad_out(pkcs11_token_id, optarg);
			break;
		case 'd':
			certlib_mode |= CERTLIB_DEBUG;
			break;
		case 'a': /* Add cert */
			aflag = 1;
			break;
		case 'i': /* input file */
			infile = optarg;
			break;
		case 'r': /* Remove cert */
			rflag = 1;
			break;
		case 'U': /* Unlink certificate */
			Uflag = 1; /* Uflag is global */
			break;
		case 'e': /* Extract certificate */
			eflag = 1;
			break;
		case 'p': /* store pin */
			write_pin = B_TRUE;
			break;
		case 'f': /* certificate out_fmt */
			out_fmt = optarg;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'C': /* Migrate cert to token */
			Cflag = 1;
			break;
		case 'L':
			Lflag = 1;
			break;
		case 'o':
			outfile = optarg;
			break;
		default:
			help = 1;
			break;
		}
	}

	if (help || (Cflag && !Tflag) || (Lflag && !Tflag) ||
	    (Cflag && Lflag) || (Uflag && !Tflag) || (Uflag && Cflag) ||
	    (Uflag && Lflag) || (write_pin && !Tflag)) {
		usage();
	}

	/* Load up PKCS#11, and get some locals set. */
	p11f = pkcs11_setup(find_pkcs11_path());
	if (p11f == NULL) {
		(void) fprintf(stderr, gettext(
		    "PKCS#11 library failed to load.\n"));
		exit(1);
	}

	/* Initialize the libike X.509 library. */
	x509config = &x509config_storage;
	ssh_x509_library_set_default_config(x509config);
	if (!ssh_x509_library_initialize(x509config))
		ssh_fatal("x509_library_initialize failed.");

	/* Let certlib loader prompt for pin if necessary */
	if (lflag || verbose)
		certlib_interactive = B_TRUE;
	if (!certlib_init(certlib_mode, CERTLIB_ALL))
		exit(1);

	if (aflag) {
		exit(add_cert(infile));
	} else if (Uflag || rflag) {
		char slotpath[40];
		char *slotname = argv[optind];

		/*
		 * If we're trying to unlink a PKCS#11 object, we need a
		 * valid pattern, but the certificate won't necessarily
		 * be in the list in memory
		 */
		pattern = gather_certspec(argv+optind, argc-optind);
		if (!pattern) {
			(void) fprintf(stderr, "%s\n",
			    gettext("certdb: missing pattern for remove"));
			exit(1);
		}
		if (remove_certs(pattern) == 0)
			exit(0);

		/* Check for slot number as well */
		if (strncmp(slotname, "SLOT=", 5) == 0)
			slotname += 5;
		(void) snprintf(slotpath, sizeof (slotpath),
		    "/etc/inet/ike/publickeys/%s", slotname);
		exit(remove_cert_file(slotpath));
	} else if (Lflag) {
		pattern = gather_certspec(argv+optind, argc-optind);
		if (!pattern) {
			(void) fprintf(stderr, "%s\n",
			    gettext("certdb: missing pattern for pkcs#11 "
			    "association"));
			exit(1);
		}
		exit(pkcs11_associate_certs(pattern));
	} else if (eflag) {
		pattern = gather_certspec(argv+optind, argc-optind);
		if (!pattern) {
			(void) fprintf(stderr, "%s\n",
			    gettext("certdb: missing pattern for extract"));
			exit(1);
		}
		exit(extract_certs(pattern, outfile, out_fmt));
	} else if (Cflag) {
		/* Copy to PKCS#11 device */
		pattern = gather_certspec(argv+optind, argc-optind);
		if (!pattern) {
			(void) fprintf(stderr, "%s\n",
			    gettext("certdb: missing pattern for extract"));
			exit(1);
		}
		certlib_find_cert_spec(pattern, migrate_cert_to_token);
		/*
		 * The above function calls exit() and just
		 * goes with the first match.  If we get to the
		 * code below, no patterns were matched.
		 */
		(void) fprintf(stderr, gettext(
		    "certdb: no matching certs found to migrate.\n"));
		exit(1);
	} else if (lflag) {
		pattern = gather_certspec(argv+optind, argc-optind);
		exit(print_certs(pattern));
	}

	return (EXIT_SUCCESS);
}
