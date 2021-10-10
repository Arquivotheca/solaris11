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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/param.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <libintl.h>
#include <locale.h>
#include <time.h>

#include <ike/sshincludes.h>
#include <ike/sshproxykey.h>
#include <ike/sshfileio.h>
#include <ike/certlib.h>
#include <ike/pkcs11-glue.h>
#include <ike/sshprvkey.h>
#include <ike/sshpkcs8.h>
#include "dumputils.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <security/cryptoki.h>
#include <security/pkcs11.h>

/* SafeNet madness (see certdb.c)... */
#undef snprintf
#undef mktime

#define	DATE_FORMAT_FILE	"DATEMSK=/etc/datemsk"

extern char *optarg;
extern int optind;

int certlib_mode;

#define	debug (certlib_mode & CERTLIB_DEBUG)
#define	PASSPHRASE_MAX 256

static boolean_t write_pin = B_FALSE;

extern boolean_t abstime(char *, time_t *);

/* Command-line patterns */
static const char **pattern = NULL;
static int pattern_count = 0;

/* Subject Alternative Name via command-line */
static const char **alt_subj = NULL;
static int alt_subj_count = 0;

static int verbose = 0;			/* verbose mode (print more info) */
static int secret = 0;			/* secret mode (print secrets too) */

static char *pkcs11_path;		/* Initialized to zeros by C. */
static char pkcs11_token_storage[PKCS11_TOKSIZE];
char *pkcs11_token_id;			/* Initialized to zeros by C. */

/* For libike's X.509 routines. */
static struct SshX509ConfigRec x509config_storage;
SshX509Config x509config;

#define	SELF_SIGN_LIFETIME (86400 * 1461)	/* Four years. */
#define	BIGBUFSZ 8192
#define	MAXKEYBUFSZ (BIGBUFSZ * 8)

typedef struct keytype_table_s {
	const char *desc;
	SshSKBType keytype;
} keytype_table_t;

/* Key types, as detected by libike */
static const keytype_table_t keytype_table[] = {
	{ "Unknown", SSH_SKB_UNKNOWN },
	{ "SafeNet proprietary (V1) encrypted", SSH_SKB_SSH_1 },
	{ "SafeNet proprietary (V2) encrypted", SSH_SKB_SSH_2 },
	{ "SSH1 client RSA", SSH_SKB_SECSH_1 },
	{ "SSH2 client RSA/DSA", SSH_SKB_SECSH_2 },
	{ "Solaris proprietary plaintext", SSH_SKB_SSH_X509 },
	{ "PKCS#1 plaintext RSA", SSH_SKB_PKCS1 },
	{ "PKCS#8 plaintext RSA/DSA", SSH_SKB_PKCS8 },
	{ "PKCS#8 encrypted RSA/DSA", SSH_SKB_PKCS8_SHROUDED },
	{ "PKCS#12", SSH_SKB_PKCS12_BROWSER_KEY }
};

static const char *
keytype_to_string(SshSKBType keytype, const keytype_table_t *keytable)
{
	int i;

	for (i = 0; keytable[i].desc; i++)
		if (keytype == keytable[i].keytype)
			return (keytable[i].desc);
	return ("Unknown");
}

static void
usage(void)
{
	/* \t%s\n\t%s\n\n */
	(void) fprintf(stderr,
	    "%s\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t\t%s\n\t\t%s\n\t\t%s\n"
	    "\t%s\n\t%s\n\t%s\n\n\t%s\n\t\t%s\n\t\t%s\n\t\t%s\n\t%s\n\t%s\n\n"
	    "\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n"
	    "\t%s\n\t%s\n\n\t%s\n\n\t%s\n\n",
	    gettext("Usage:"),
	    gettext("certlocal -l [-v] [pattern]"),
	    gettext(" List Certificates."),
	    gettext("certlocal -a [[-p] -T <PKCS#11 token>]"),
	    gettext(" Add private key to keystore from stdin."),
	    gettext("certlocal -kc -m keysize -t keytype -D dname "),
	    gettext("-A altname[...] [-f output_format] "),
	    gettext("[-S start_time] [-F finish_time] "),
	    gettext("[[-p] -T <PKCS#11 token>]"),
	    gettext(" Generate private key and put it in to keystore and "),
	    gettext(" output a CA request for signing and use "),
	    gettext(" stdout by default."),
	    gettext("certlocal -ks -m keysize -t keytype -D dname "),
	    gettext("-A altname[...] [-f output_format] "),
	    gettext("[-S start_time] [-F finish_time] "),
	    gettext("[[-p] -T <PKCS#11 token>]"),
	    gettext(" Generate private key and self-signed certificate, "),
	    gettext(" and install them in keystore."),
	    gettext("certlocal -e slotname [-f output_format]"),
	    gettext(" Extract private key from given certificate."),
	    gettext("certlocal -r slotname"),
	    gettext(" Remove private key from keystore."),
	    gettext("certlocal -T <PKCS#11 token> -U slotname"),
	    gettext(" Unlink PKCS#11 keystore object from IKE database."),
	    gettext("certlocal -T <PKCS#11 token> [-p] -C pattern"),
	    gettext(" Copy key and certificate from disk to keystore."),
	    gettext("certlocal -T <PKCS#11 token> [-p] -L pattern"),
	    gettext(" Link PKCS#11 keystore object with IKE database."),
	    gettext("certlocal -h"),
	    gettext(" This help page."));
	exit(1);
}

static int
match_key(struct certlib_keys *p)
{
	char *s;
	int i = -1;

	while (pattern_count > (++i)) {

		if (strcmp(p->slotname, pattern[i]) == 0) {
			continue;
		}

		s = strchr(pattern[i], '=');
		if (s == NULL)
			return (0);

		s++;
		if ((strstr(pattern[i], "SLOT") != NULL) &&
		    (strcmp(s, p->slotname) == 0)) {
			continue;
		} else {
			return (0);
		}
	}
	return (1);
}

/*
 * Print keys data
 */
static int
print_key(struct certlib_keys *p)
{
	char *s;

	if ((pattern != NULL) && !match_key(p))
		return (0);

	(void) printf("Local ID Slot Name: %s   Key Type: %s\n", p->slotname,
	    p->type);

	if (p->pkcs11_label[0] != '\0') {
		(void) printf(
		    gettext("\t(In PKCS#11 hardware token \"%.32s\")\n"),
		    p->pkcs11_label);
		(void) printf(gettext("\t(PKCS#11 object identifier \"%s\")\n"),
		    p->pkcs11_id);
		if (p->pkcs11_pin[0] != '\0') {
			(void) printf(gettext(
			    "\t(PKCS#11 keystore pin in clear on disk)\n"));
		} else {
			(void) printf(gettext(
			    "\t(PKCS#11 keystore pin not stored on disk)\n"));
		}
		(void) putchar('\n');
		return (0);
	}

	if (verbose) {
		if (strncmp(p->type, "rsa", 3) == 0) {
			s = dump_number(&p->n);
			(void) printf("\tPublic Modulus  (n) (%4u bits): %s\n",
			    SSH_GET_ROUNDED_SIZE(&p->n, 2, 64), s);
			ssh_free(s);

			s = dump_number(&p->e);
			(void) printf("\tPublic Exponent (e) (%4u bits): %s\n",
			    SSH_GET_ROUNDED_SIZE(&p->e, 2, 8), s);
			ssh_free(s);

			if (secret) {
				s = dump_number(&p->d);
				(void) printf(
				    "\tPrivate key     (d) (%4u bits): %s\n",
				    SSH_GET_ROUNDED_SIZE(&p->d, 2, 64), s);
				ssh_free(s);
			}
		} else if (strcmp(p->type, "dsa") == 0) {
			s = dump_number(&p->g);
			(void) printf("\tGenerator       (g) (%4u bits): %s\n",
			    SSH_GET_ROUNDED_SIZE(&p->g, 2, 64), s);
			ssh_free(s);

			s = dump_number(&p->p);
			(void) printf("\tPrime           (p) (%4u bits): %s\n",
			    SSH_GET_ROUNDED_SIZE(&p->p, 2, 64), s);
			ssh_free(s);

			s = dump_number(&p->q);
			(void) printf("\tGroup order     (q) (%4u bits): %s\n",
			    SSH_GET_ROUNDED_SIZE(&p->q, 2, 8), s);
			ssh_free(s);

			s = dump_number(&p->y);
			(void) printf("\tPublic key      (y) (%4u bits): %s\n",
			    SSH_GET_ROUNDED_SIZE(&p->y, 2, 64), s);
			ssh_free(s);

			if (secret) {
				s = dump_number(&p->x);
				(void) printf(
				    "\tPrivate key     (x) (%4u bits): %s\n",
				    SSH_GET_ROUNDED_SIZE(&p->x, 2, 64), s);
				ssh_free(s);
			}
		} else {
			(void) puts("\tUnknown key type");
		}
	} else {
		SshMPIntegerStruct *np;
		if (strncmp(p->type, "rsa", 3) == 0) {
			np = &p->n;
		} else if (strcmp(p->type, "dsa") == 0) {
			np = &p->y;
		} else {
			np = NULL;
		}

		if (np != NULL) {
			(void) printf("\tKey Size: %u\n",
			    SSH_GET_ROUNDED_SIZE(np, 2, 64));
			(void) printf("\tPublic key hash: %s\n",
			    dump_public_hash(np));
		} else {
			(void) puts("\tUnknown key type");
		}
	}

	(void) putchar('\n');

	return (0);
}

/*
 * This is a generic write/copy function.  The first arg is the file you
 * want to read from, the second is where you want it to go.  If the second
 * argument is NULL, then it will write to stdout.
 */
static void
write_file(const char *readfile, const char *outfile)
{
	FILE *f, *fout;
	unsigned char b[1024];
	int size;
	f = fopen(readfile, "r");
	if (f == NULL) {
		(void) fprintf(stderr, "fopen(%s): %s\n", readfile,
		    strerror(errno));
		exit(1);
	}
	if (outfile == NULL) {
		fout = stdout;
	} else {
		fout = fopen(outfile, "w");

		if (fout == NULL) {
			(void) fprintf(stderr, "fopen(%s): %s\n", outfile,
			    strerror(errno));
			exit(1);
		}
	}

	while (feof(f) == 0) {
		size = fread(b, sizeof (char), 1023, f);
		if (fwrite(b, sizeof (char), size, fout) == 0 &&
		    ferror(fout)) {
			perror("fwrite");
			exit(1);
		}
	}
	(void) fclose(f);
	(void) fclose(fout);
}

/*
 * Write a buffer to a file
 */
static boolean_t
write_buffer_to_file(unsigned char *buffer, size_t buflen, const char *outfile,
    boolean_t private)
{
	int perms = (S_IRUSR|S_IWUSR);

	/*
	 * Anything but private keys and PKCS#11 hint files
	 * should be world readable
	 */
	if (!private)
		perms |= (S_IRGRP|S_IROTH);

	if (outfile == NULL) {
		/* just write to stdout */
		if (fwrite(buffer, sizeof (char), buflen, stdout) == 0 &&
		    ferror(stderr)) {
			return (B_FALSE);
		}
	} else {
		int fout;

		fout = open(outfile, O_CREAT|O_WRONLY, perms);
		if (fout == -1) {
			return (B_FALSE);
		}
		if (write(fout, buffer, buflen) == -1) {
			(void) close(fout);
			return (B_FALSE);
		}
		(void) close(fout);
	}
	return (B_TRUE);
}

/*
 * Parser to take alternate subject names from the commandline to certificate
 * form
 */
static void
alt_subj_parser(FILE *f)
{
	int i;

	for (i = 0; i < alt_subj_count; ++i) {
		const char *name = alt_subj[i] + strlen(alt_subj[i]) + 1;
		if (strcmp(alt_subj[i], "DN") == 0)
			(void) fprintf(f, "\t\t\t %s ::= <%s>\n",
			    alt_subj[i], name);
		else
			(void) fprintf(f, "\t\t\t %s ::= %s\n",
			    alt_subj[i], name);
	}
}

/*
 * Verify the alternate subject label is real or invalid.
 */
static int
verify_alt_subj(const char **arg)
{
	char *p;

	p = strchr(*arg, '=');
	if (p == NULL)
		prepare_pattern(arg, 1);

	/* Try again with prepended pattern. */
	p = strchr(*arg, '=');
	if (p == NULL)
		return (0);

	p[0] = '\0';

	if (strcmp(*arg, "IP") == 0)
		return (1);
	else if (strcmp(*arg, "DNS") == 0)
		return (1);
	else if (strcmp(*arg, "EMAIL") == 0)
		return (1);
	else if (strcmp(*arg, "URI") == 0)
		return (1);
	else if (strcmp(*arg, "DN") == 0)
		return (1);
	else if (strcmp(*arg, "RID") == 0)
		return (1);

	return (0);
}

/* Given an object_id, check for existence of a public, then private key. */
static boolean_t
check_pubpriv(CK_SESSION_HANDLE session, char *object_id, CK_KEY_TYPE type)
{
	CK_OBJECT_HANDLE obj;
	boolean_t rc = B_TRUE;

	obj = find_object(session, object_id, CKO_PUBLIC_KEY, type);
	if (obj != NULL) {
		(void) fprintf(stderr,
		    gettext("certlocal: Existing public key object named\n"
		    "\t\"%s\".\n"), object_id);
		rc = B_FALSE;
	}

	obj = find_object(session, object_id, CKO_PRIVATE_KEY, type);
	if (obj != NULL) {
		(void) fprintf(stderr,
		    gettext("certlocal: Existing private key object named\n"
		    "\t\"%s\".\n"), object_id);
		rc = B_FALSE;
	}

	return (rc);
}

/*
 * Create an SSH/SafeNet proxy-key that uses an on-token key.
 *
 * Like the IKE code does, we have to explicitly select the private-key
 * scheme (signature & hash algorithms) to satisfy libike.
 */
static SshPrivateKey
keystore_to_ssh_private(CK_OBJECT_HANDLE private, char *keytype,
    uint32_t key_bits)
{
	SshPrivateKey proxy_key;
	pkcs11_key_t *p11k;
	SshCryptoStatus cret;
	char *scheme;

	p11k = ssh_calloc(sizeof (*p11k), 0);
	if (p11k == NULL)
		memory_bail();

	p11k->p11k_p11i = find_p11i_slot(pkcs11_token_id);
	if (p11k->p11k_p11i == NULL) {
		(void) fprintf(stderr, gettext(
		    "Can't find PKCS#11 slot internally"));
		return (NULL);
	}
	p11k->p11k_p11obj = private;
	p11k->p11k_bufsize = (*keytype == 'r') ? (key_bits >> 3) : DSA_BUFSIZE;

	proxy_key = ssh_private_key_create_proxy((*keytype == 'r') ?
	    SSH_PROXY_RSA : SSH_PROXY_DSA, key_bits,
	    pkcs11_private_key_dispatch, ssh_free, p11k);

	/*
	 * Select schema for key to get appropriate RGF magic to happen, etc.
	 *
	 * Note:  All strcmp() calls are safe because of the constant strings
	 * as the second argument.
	 *
	 * Note #2:  The checks in main() insure the correctness of "keytype".
	 */
	if (strcmp(keytype, "dsa-sha1") == 0)
		scheme = "dsa-nist-sha1";
	else if (strcmp(keytype, "rsa-md5") == 0)
		scheme = "rsa-pkcs1-md5";
	else
		scheme = "rsa-pkcs1-sha1";

	cret = ssh_private_key_select_scheme(proxy_key, SSH_PKF_SIGN, scheme,
	    SSH_PKF_END);
	if (cret != SSH_CRYPTO_OK) {
		ssh_private_key_free(proxy_key);
		(void) fprintf(stderr,
		    gettext("Could not select signature scheme.\n"));
		return (NULL);
	}
	return (proxy_key);
}

/*
 * Create an SSH/SafeNet compatible private-key.
 */
static SshPrivateKey
pkcs11_to_ssh_private(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE private,
    CK_OBJECT_HANDLE public, char *keytype, char *pin, uint32_t key_bits)
{
	SshPrivateKey key;
	SshMPIntegerStruct private_d, prime_p, subprime_q, public_e,
	    modulus_n, base_g, private_x, inverse_u, public_y;
	SshCryptoStatus ssh_rc;
	boolean_t rsa = (*keytype == 'r');

	/* Use on-token key for signing if the key is on-token. */
	if (pin != NULL)
		return (keystore_to_ssh_private(private, keytype, key_bits));

	/* Otherwise, extract and use the libike built-ins. */

	/* Initialize common bignums... */
	ssh_mp_init(&prime_p);
	ssh_mp_init(&subprime_q);

	if (rsa) {
		ssh_mp_init(&modulus_n);
		ssh_mp_init(&public_e);
		ssh_mp_init(&private_d);
		ssh_mp_init(&inverse_u);

		if (!extract_pkcs11_public(session, private, &prime_p,
		    CKK_RSA, CKA_PRIME_2) ||
		    !extract_pkcs11_public(session, private, &subprime_q,
		    CKK_RSA, CKA_PRIME_1) ||
		    !extract_pkcs11_public(session, private, &modulus_n,
		    CKK_RSA, CKA_MODULUS) ||
		    !extract_pkcs11_public(session, private, &public_e,
		    CKK_RSA, CKA_PUBLIC_EXPONENT) ||
		    !extract_pkcs11_public(session, private, &private_d,
		    CKK_RSA, CKA_PRIVATE_EXPONENT) ||
		    !extract_pkcs11_public(session, private, &inverse_u,
		    CKK_RSA, CKA_COEFFICIENT)) {
			(void) fprintf(stderr,
			    gettext("PKCS#11 value extractions failed.\n"));
			return (NULL);
		}

		ssh_rc = ssh_private_key_define(&key, "if-modn",
		    /* Signature algorithm. */
		    SSH_PKF_SIGN,
		    (keytype[4] == 'm') ? "rsa-pkcs1-md5" : "rsa-pkcs1-sha1",
		    /* Encryption algorithm. */
		    SSH_PKF_ENCRYPT, "rsa-pkcs1-none",
		    /* Various values. */
		    SSH_PKF_PRIME_P, &prime_p,
		    SSH_PKF_PRIME_Q, &subprime_q,
		    SSH_PKF_MODULO_N, &modulus_n,
		    SSH_PKF_PUBLIC_E, &public_e,
		    SSH_PKF_SECRET_D, &private_d,
		    SSH_PKF_INVERSE_U, &inverse_u,
		    SSH_PKF_END);
		if (ssh_rc != SSH_CRYPTO_OK) {
			(void) fprintf(stderr, gettext("Private key "
			    "definition failed, %d - %s.\n"), ssh_rc,
			    ssh_crypto_status_message(ssh_rc));
			return (NULL);
		}
	} else {
		ssh_mp_init(&base_g);
		ssh_mp_init(&private_x);
		ssh_mp_init(&public_y);

		if (!extract_pkcs11_public(session, private, &prime_p,
		    CKK_DSA, CKA_PRIME) ||
		    !extract_pkcs11_public(session, private, &subprime_q,
		    CKK_DSA, CKA_SUBPRIME) ||
		    !extract_pkcs11_public(session, private, &base_g,
		    CKK_DSA, CKA_BASE) ||
		    !extract_pkcs11_public(session, private, &private_x,
		    CKK_DSA, CKA_VALUE) ||
		    !extract_pkcs11_public(session, public, &public_y,
		    CKK_DSA, CKA_VALUE)) {
			(void) fprintf(stderr,
			    gettext("PKCS#11 value extractions failed.\n"));
			return (NULL);
		}

		ssh_rc = ssh_private_key_define(&key, "dl-modp",
		    /* Signature algorithm. */
		    SSH_PKF_SIGN, "dsa-nist-sha1",
		    /* Various values. */
		    SSH_PKF_PRIME_P, &prime_p,
		    SSH_PKF_PRIME_Q, &subprime_q,
		    SSH_PKF_GENERATOR_G, &base_g,
		    SSH_PKF_SECRET_X, &private_x,
		    SSH_PKF_PUBLIC_Y, &public_y,
		    SSH_PKF_END);
		if (ssh_rc != SSH_CRYPTO_OK) {
			(void) fprintf(stderr, gettext("Private key "
			    "definition failed, %d - %s.\n"), ssh_rc,
			    ssh_crypto_status_message(ssh_rc));
			return (NULL);
		}
	}

	return (key);
}

/*
 * Create an SSH/SafeNet compatible private-key.
 *
 * GRUMBLE:  SSH/SafeNet DSA "private" keys need the public value, too.
 * So we need the public key for DSA calls to pkcs11_to_ssh_private().
 */
static size_t
generate_ondisk_private(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE private,
    CK_OBJECT_HANDLE public, uint8_t *buffer, size_t buflen, char *keytype)
{
	SshPrivateKey key;
	size_t rc = 0;
	uint8_t *local_buf;

	key = pkcs11_to_ssh_private(session, private, public, keytype, NULL, 0);

	if (key == NULL)
		return (0);

	if (ssh_x509_encode_private_key(key, &local_buf, &rc) != SSH_X509_OK) {
		(void) fprintf(stderr,
		    gettext("Private key conversion failed.\n"));
		return (rc);
	}
	if (rc <= buflen)
		(void) memcpy(buffer, local_buf, rc);
	else
		rc = 0;

	ssh_free(local_buf);
	return (rc);
}

/*
 * Generate a public/private keypair in the PKCS#11 token.  Also store the
 * private key information in the /etc/inet/private/ike.privatekeys/ slot.
 */
static CK_SESSION_HANDLE
pkcs11_generate_keypair(char *keysize, char *keytype, char *dname,
    int slotfd, CK_OBJECT_HANDLE *newpub, CK_OBJECT_HANDLE *newpriv, char *pin)
{
	CK_SESSION_HANDLE session = NULL;
	CK_MECHANISM mech = {0, NULL, 0};
	CK_ULONG key_bits;
	uint32_t public_e;	/* public_e must be in network order. */
	CK_RV pkcs11_rc;
	CK_BBOOL true = TRUE;
	uint8_t bigbuf1[BIGBUFSZ], bigbuf2[BIGBUFSZ];
	/* 20 is SHA-1 output length. */
	uint8_t dsa_subprime[20], *dsa_prime = bigbuf1, *dsa_base = bigbuf2;
	CK_ATTRIBUTE public[MAX_ATTRS], private[MAX_ATTRS];
	int public_len = 0, private_len = 0;  /* Number of attrs... */
	size_t length_to_write;
	char newline = '\n';

	(void) printf(gettext("Creating private key.\n"));

	key_bits = atoi(keysize);

	session = pkcs11_login(pkcs11_token_id, pin);
	if (session == NULL)
		return (NULL);

	/* Attributes that are RSA/DSA dependent. */

	if (*keytype == 'r') {
		/* We know it's RSA because of checks in main() */
		mech.mechanism = CKM_RSA_PKCS_KEY_PAIR_GEN;
		if (!check_pubpriv(session, dname, CKK_RSA))
			return (NULL);
		public_e = htonl(0x10001);	/* Just use 65537. */
		/* LINTED */
		ATTR_INIT(public[public_len], CKA_WRAP, &true, sizeof (true));
		public_len++;
		/* LINTED */
		ATTR_INIT(public[public_len], CKA_MODULUS_BITS, &key_bits,
		    sizeof (CK_ULONG));
		public_len++;
		/* LINTED */
		ATTR_INIT(public[public_len], CKA_PUBLIC_EXPONENT, &public_e,
		    sizeof (uint32_t));
		public_len++;

		/* LINTED */
		ATTR_INIT(private[private_len], CKA_UNWRAP, &true,
		    sizeof (true));
		private_len++;

	} else {
		SshMPIntegerStruct p, q, g;

		/* We know it's DSA because of checks in main(). */
		mech.mechanism = CKM_DSA_KEY_PAIR_GEN;
		if (!check_pubpriv(session, dname, CKK_DSA))
			return (NULL);

		/* Initialize prime, subprime, base? */
		/* Use ssh_mp_* in sshcrypt/ */
		ssh_mp_init(&p);
		ssh_mp_init(&q);
		ssh_mp_init(&g);
		ssh_mprz_random_strong_prime(&p, &q, key_bits, 160);
		if (!ssh_mprz_random_generator(&g, &q, &p))
			return (NULL);

		/* Convert bits to bytes. */
		key_bits >>= 3;
		if (key_bits > sizeof (bigbuf1))
			return (NULL);

		(void) ssh_mp_get_buf(dsa_prime, key_bits, &p);
		(void) ssh_mp_get_buf(dsa_base, key_bits, &g);
		(void) ssh_mp_get_buf(dsa_subprime, sizeof (dsa_subprime), &q);

		/* LINTED */
		ATTR_INIT(public[public_len], CKA_SUBPRIME, dsa_subprime,
		    sizeof (dsa_subprime));
		public_len++;
		/* LINTED */
		ATTR_INIT(public[public_len], CKA_PRIME, dsa_prime, key_bits);
		public_len++;
		/* LINTED */
		ATTR_INIT(public[public_len], CKA_BASE, dsa_base, key_bits);
		public_len++;
	}

	/* Attributes common to both RSA and DSA keys. */

	/* LINTED */
	ATTR_INIT(public[public_len], CKA_ID, dname, strlen(dname));
	public_len++;
	/* LINTED */
	ATTR_INIT(public[public_len], CKA_LABEL, dname, strlen(dname));
	public_len++;
	/* LINTED */
	ATTR_INIT(public[public_len], CKA_VERIFY, &true, sizeof (true));
	public_len++;
	/* LINTED */
	ATTR_INIT(public[public_len], CKA_VERIFY_RECOVER, &true, sizeof (true));
	public_len++;

	/* LINTED */
	ATTR_INIT(private[private_len], CKA_ID, dname, strlen(dname));
	private_len++;
	/* LINTED */
	ATTR_INIT(private[private_len], CKA_LABEL, dname, strlen(dname));
	private_len++;
	/* LINTED */
	ATTR_INIT(private[private_len], CKA_SIGN, &true, sizeof (true));
	private_len++;
	/* LINTED */
	ATTR_INIT(private[private_len], CKA_SIGN_RECOVER, &true, sizeof (true));
	private_len++;


	/*
	 * Attributes dependent on if the keypair is on-filesystem, or in a
	 * PKCS#11 keystore object.  A NULL PIN pointer indicates
	 * on-filesystem.
	 */

	if (pin == NULL) {
		/* LINTED */
		ATTR_INIT(private[private_len], CKA_EXTRACTABLE, &true,
		    sizeof (true));
		private_len++;
	} else {
		/* LINTED */
		ATTR_INIT(public[public_len], CKA_TOKEN, &true, sizeof (true));
		public_len++;

		/* LINTED */
		ATTR_INIT(private[private_len], CKA_TOKEN, &true,
		    sizeof (true));
		private_len++;
		/* LINTED */
		ATTR_INIT(private[private_len], CKA_PRIVATE, &true,
		    sizeof (true));
		private_len++;
		/* LINTED */
		ATTR_INIT(private[private_len], CKA_SENSITIVE, &true,
		    sizeof (true));
		private_len++;
	}

	pkcs11_rc = p11f->C_GenerateKeyPair(session, &mech, public, public_len,
	    private, private_len, newpub, newpriv);

	if (pkcs11_rc != CKR_OK) {
		(void) fprintf(stderr,
		    gettext("certlocal: GenerateKeyPair failed (%d).\n"),
		    pkcs11_rc);
		return (NULL);
	}

#define	do_write(fd, buf, sz) if (write(fd, buf, sz) == -1) { \
			perror("certlocal: error writing file"); \
			return (NULL); }

	if (pin == NULL) {
		length_to_write = generate_ondisk_private(session, *newpriv,
		    *newpub, bigbuf1, BIGBUFSZ, keytype);
		if (length_to_write == 0) {
			/* In case these are token objects... */
			(void) p11f->C_DestroyObject(session, *newpub);
			(void) p11f->C_DestroyObject(session, *newpriv);
			return (NULL);
		}

		/* Scribble SSH/SafeNet-friendly private key to disk. */
		do_write(slotfd, bigbuf1, length_to_write);
	} else if (slotfd >= 0) {
		/* Write information to the private key file if needed. */
		do_write(slotfd, pkcs11_token_id, PKCS11_TOKSIZE);
		do_write(slotfd, &newline, 1);
		if (!write_pin)
			pin[0] = '\0';
		do_write(slotfd, pin, strlen(pin));
		do_write(slotfd, &newline, 1);
		do_write(slotfd, dname, strlen(dname));
		do_write(slotfd, &newline, 1);
		do_write(slotfd, keytype, strlen(keytype));
		do_write(slotfd, &newline, 1);
	}

#undef do_write

	return (session);
}

static SshX509Name
encode_altnames(void)
{
	SshX509Name rc = NULL;
	int i;

	for (i = 0; i < alt_subj_count; i++) {
		const char *type = alt_subj[i];
		const char *name = type + strlen(type) + 1;

		if (strcmp(type, "DN") == 0) {
			if (!ssh_x509_name_push_directory_name(&rc,
			    (uchar_t *)name))
				return (NULL);
		} else if (strcmp(type, "IP") == 0) {
			size_t len;
			in_addr_t v4;
			in6_addr_t v6;
			uint8_t *ptr;

			v4 = inet_addr(name);
			if (v4 == (in_addr_t)-1) {
				ptr = (uint8_t *)&v6;
				if (inet_pton(AF_INET6, name, ptr) != 1)
					return (NULL);
				len = sizeof (v6);
			} else {
				ptr = (uint8_t *)&v4;
				len = sizeof (v4);
			}

			if (!ssh_x509_name_push_ip(&rc, ptr, len))
				return (NULL);
		} else if (strcmp(type, "DNS") == 0) {
			if (!ssh_x509_name_push_dns(&rc, name))
				return (NULL);
		} else if (strcmp(type, "EMAIL") == 0) {
			if (!ssh_x509_name_push_email(&rc, name))
				return (NULL);
		} else if (strcmp(type, "URI") == 0) {
			if (!ssh_x509_name_push_uri(&rc, name))
				return (NULL);
		} else if (strcmp(type, "RID") == 0) {
			if (!ssh_x509_name_push_rid(&rc, name))
				return (NULL);
		}
	}

	return (rc);
}

/*
 * Generate keys and either a self-signed certificate or a certificate request
 * using keystore.  If anything looks like it's not cleaned up here, it's
 * because the program exits if this function fails.
 */
static size_t
pkcs11_generate(SshX509CertType type, uint8_t **buf, CK_SESSION_HANDLE session,
    CK_OBJECT_HANDLE pub, CK_OBJECT_HANDLE priv, char *dname, char *keytype,
    int fd, char *pin, SshBerTimeStruct *not_before,
    SshBerTimeStruct *not_after)
{
	size_t len = 0;
	SshX509Certificate ssh_cert;
	SshMPIntegerStruct mp_scratch, mp_scratch2;
	SshPublicKey ssh_pub;
	SshPrivateKey ssh_priv;
	uint8_t *newbuf;
	SshX509Name names;
	SshX509UsageFlags flags;
	SshMPIntegerStruct dsa_y, dsa_p, dsa_q, dsa_g, rsa_n, rsa_e;
	uint32_t key_bits;

	/*
	 * Extract the public key into an SshPublicKey format.
	 */
	if (*keytype == 'r') {
		ssh_mp_init(&rsa_n);
		ssh_mp_init(&rsa_e);

		if (!extract_pkcs11_public(session, pub, &rsa_e, CKK_RSA,
		    CKA_PUBLIC_EXPONENT) ||
		    !extract_pkcs11_public(session, pub, &rsa_n, CKK_RSA,
		    CKA_MODULUS) ||
		    ssh_public_key_define(&ssh_pub, "if-modn",
		    SSH_PKF_MODULO_N, &rsa_n, SSH_PKF_PUBLIC_E, &rsa_e,
		    SSH_PKF_END) != SSH_CRYPTO_OK)
			return (0);
		key_bits = ssh_mprz_get_size(&rsa_n, 256) << 3;
	} else {
		ssh_mp_init(&dsa_y);
		ssh_mp_init(&dsa_p);
		ssh_mp_init(&dsa_q);
		ssh_mp_init(&dsa_g);

		if (!extract_pkcs11_public(session, pub, &dsa_y, CKK_DSA,
		    CKA_VALUE) ||
		    !extract_pkcs11_public(session, pub, &dsa_p, CKK_DSA,
		    CKA_PRIME) ||
		    !extract_pkcs11_public(session, pub, &dsa_q, CKK_DSA,
		    CKA_SUBPRIME) ||
		    !extract_pkcs11_public(session, pub, &dsa_g, CKK_DSA,
		    CKA_BASE) ||
		    ssh_public_key_define(&ssh_pub, "dl-modp",
		    SSH_PKF_PUBLIC_Y, &dsa_y, SSH_PKF_PRIME_P, &dsa_p,
		    SSH_PKF_PRIME_Q, &dsa_q, SSH_PKF_GENERATOR_G, &dsa_g,
		    SSH_PKF_END) != SSH_CRYPTO_OK)
			return (0);
		key_bits = ssh_mprz_get_size(&dsa_y, 256) << 3;
	}

	/*
	 * Allocate the SSH X.509 cert and generate it!
	 */
	ssh_cert = ssh_x509_cert_allocate(type);

	/* Set all of the fields we can. */
	ssh_mp_init(&mp_scratch);
	ssh_mp_init(&mp_scratch2);
	/*
	 * Serial numbers are treated as signed integers.  In order
	 * to have the SafeNet libraries properly encode an unambiguously
	 * positive signed integer, we must treat the value as signed
	 * and take the absolute value.  We need our serial numbers to
	 * be positive integers in order to be RFC 3280 compliant.
	 */
	ssh_mp_set_si(&mp_scratch2, gethrtime() & 0xffffffff);
	ssh_mprz_abs(&mp_scratch, &mp_scratch2);

	ssh_x509_cert_set_serial_number(ssh_cert, &mp_scratch);

	ssh_mp_clear(&mp_scratch);
	ssh_mp_clear(&mp_scratch2);

	if (!ssh_x509_cert_set_subject_name(ssh_cert, (uchar_t *)dname)) {
		(void) fprintf(stderr,
		    gettext("Setting of subject name \"%s\" failed.\n"),
		    dname);
		return (0);
	}
	if (!ssh_x509_cert_set_issuer_name(ssh_cert, (uchar_t *)dname)) {
		(void) fprintf(stderr,
		    gettext("Setting of issuer name \"%s\" failed.\n"), dname);
		return (0);
	}

	ssh_x509_cert_set_validity(ssh_cert, not_before, not_after);

	if (!ssh_x509_cert_set_public_key(ssh_cert, ssh_pub)) {
		return (0);
	}

	/* SubjectAltName(s). */
	names = encode_altnames();
	if (names != NULL)
		ssh_x509_cert_set_subject_alternative_names(ssh_cert, names,
		    B_FALSE);

	/* Usage for DigitalSignature, (KeyEncipherment if RSA) */
	flags = SSH_X509_UF_DIGITAL_SIGNATURE;
	if (*keytype == 'r')
		flags |= SSH_X509_UF_KEY_ENCIPHERMENT;

	/* Is critical needed? */
	ssh_x509_cert_set_key_usage(ssh_cert, flags, TRUE);

	ssh_priv = pkcs11_to_ssh_private(session, priv, pub, keytype, pin,
	    key_bits);

	/*
	 * Encode certificate.
	 */
	if (ssh_x509_cert_encode(ssh_cert, ssh_priv, &newbuf, &len) !=
	    SSH_X509_OK) {
		return (0);
	}

	*buf = newbuf;

	if (type != SSH_X509_PKIX_CERT)
		return (len);

#define	do_write(fd, buf, sz) if (write(fd, buf, sz) == -1) { \
			perror("certlocal: error writing file"); \
			return (0); }
	if (pin != NULL) {
		char newline = '\n';
		CK_OBJECT_HANDLE obj;

		/*
		 * Scribble hint into the file, and put the cert in the
		 * keystore.
		 */
		obj = find_object(session, dname, CKO_CERTIFICATE, CKC_X_509);
		if (obj != NULL) {
			(void) fprintf(stderr,
			    gettext("certlocal: Existing certificate "
			    "object named\n\t\"%s\".\n"), dname);
			return (0);
		}

		if (!pkcs11_cert_generate(session, *buf, len, dname, ssh_cert))
			return (0);
		do_write(fd, pkcs11_token_id, PKCS11_TOKSIZE);
		do_write(fd, &newline, 1);
		if (!write_pin)
			pin[0] = '\0';
		do_write(fd, pin, strlen(pin));
		do_write(fd, &newline, 1);
		do_write(fd, dname, strlen(dname));
		do_write(fd, &newline, 1);
		do_write(fd, keytype, strlen(keytype));
		do_write(fd, &newline, 1);
	} else {
		/* Put the cert into the file directly. */
		do_write(fd, *buf, len);
	}

#undef do_write
	return (len);
}

/*
 * Create a self signed certificiate.
 */
static void
self_sign(char *keysize, char *keytype, char *dname, char *outfile,
    char *out_fmt, SshBerTimeStruct *not_before, SshBerTimeStruct *not_after)
{
	char file_cert[MAXPATHLEN];
	char file_priv[MAXPATHLEN];
	int priv_fd, pub_fd, i = 0;
	int perms = S_IRUSR|S_IWUSR;
	unsigned char *buf;
	size_t buf_len;
	CK_SESSION_HANDLE session;
	boolean_t on_filesystem = (pkcs11_token_id == NULL);
	CK_OBJECT_HANDLE pub, priv;
	char pin[256] = {'\0'};

	/* Get available slot number for key file */
	(void) snprintf(file_priv, MAXPATHLEN, "%s%d", certlib_keys_dir(), i);

	while ((priv_fd = open(file_priv, O_CREAT|O_EXCL|O_WRONLY,
	    perms)) < 0) {
		if (errno == EEXIST) {
			(void) snprintf(file_priv, MAXPATHLEN, "%s%d",
			    certlib_keys_dir(), ++i);
		} else {
			perror(gettext("Unable to open private-key database."));
			exit(1);
		}
	}

	/* Get available slot number for certificate file */
	i = 0;
	(void) snprintf(file_cert, MAXPATHLEN, "%s%d", certlib_certs_dir(), i);

	/* If not a PKCS#11 object, should be world readable */
	if (on_filesystem)
		perms |= S_IRGRP|S_IROTH;

	while ((pub_fd = open(file_cert, O_CREAT|O_EXCL|O_WRONLY, perms)) < 0) {
		if (errno == EEXIST) {
			(void) snprintf(file_cert, MAXPATHLEN, "%s%d",
			    certlib_certs_dir(), ++i);
		} else {
			perror(gettext("Unable to open public certificate "
			    "database."));
			exit(1);
		}
	}

	/*
	 * Generate public-private keypair using PKCS#11.
	 * As a side-effect, the private key, or private key hint, will be
	 * written to the priv_fd file descriptor.
	 */
	session = pkcs11_generate_keypair(keysize, keytype, dname, priv_fd,
	    &pub, &priv, on_filesystem ? NULL : pin);
	if (session == NULL) {
		(void) fprintf(stderr, gettext("Cannot generate keypair.\n"));
		(void) close(pub_fd);
		(void) close(priv_fd);
		(void) unlink(file_cert);
		(void) unlink(file_priv);
		exit(1);
	}

	/* Generate self-signed certificate BER object and add it. */
	buf_len = pkcs11_generate(SSH_X509_PKIX_CERT, &buf, session, pub, priv,
	    dname, keytype, pub_fd, on_filesystem ? NULL : pin, not_before,
	    not_after);
	if (buf_len == 0) {
		(void) fprintf(stderr,
		    gettext("Cannot generate certificate.\n"));
		(void) close(pub_fd);
		(void) close(priv_fd);
		(void) p11f->C_DestroyObject(session, pub);
		(void) p11f->C_DestroyObject(session, priv);
		(void) unlink(file_cert);
		(void) unlink(file_priv);
		exit(1);
	}

	(void) fprintf(stderr, "%s\n",
	    gettext("Certificate added to database."));
	/* Publish the cert. */
	export_data(buf, buf_len, outfile, out_fmt, SSH_PEM_X509_BEGIN,
	    SSH_PEM_X509_END);
	/*
	 * fds will be closed, because if this function returns, we exit with
	 * 0/success.
	 */
}

/* Create a Certificate Request */
static void
generate_cert_req(char *keysize, char *keytype, char *dname, char *outfile,
    char *out_fmt, char *keyfile, SshBerTimeStruct *not_before,
    SshBerTimeStruct *not_after)
{
	unsigned char *buf;
	size_t buf_len;
	char file_priv[MAXPATHLEN];
	int fd = -1, oflags, i = 0;
	CK_SESSION_HANDLE session;
	boolean_t on_filesystem = (pkcs11_token_id == NULL);
	CK_OBJECT_HANDLE pub, priv;
	char pin[256] = {'\0'};
	boolean_t elfsign_on_token = B_FALSE;

	if (debug)
		(void) fprintf(stderr, "generate_key entered\n");

	if (!(certlib_mode & CERTLIB_SOLARISCRYPTO)) {
		(void) snprintf(file_priv, MAXPATHLEN, "%s%d",
		    certlib_keys_dir(), i);
		oflags = O_CREAT | O_WRONLY | O_EXCL;
	} else if (keyfile == NULL) {
		elfsign_on_token = B_TRUE;
	} else {
		(void) strlcpy(file_priv, keyfile, MAXPATHLEN);
		oflags = O_CREAT | O_WRONLY;  /* Clobber file. */
		/*
		 * Change permissions to match if a newly-created file was
		 * opened.
		 */
		(void) chmod(file_priv, S_IRUSR | S_IWUSR);
	}

	while (!elfsign_on_token &&
	    (fd = open(file_priv, oflags, S_IRUSR | S_IWUSR)) < 0) {
		if (errno == EEXIST &&
		    !(certlib_mode & CERTLIB_SOLARISCRYPTO)) {
			(void) snprintf(file_priv, MAXPATHLEN, "%s%d",
			    certlib_keys_dir(), ++i);
		} else {
			if (certlib_mode & CERTLIB_SOLARISCRYPTO)
				(void) fprintf(stderr, "%s %s: %s",
				    gettext("Cannot write private key file"),
				    keyfile, strerror(errno));
			else {
				perror(gettext("Unable to open private"
				    " key database."));
			}
			exit(1);
		}
	}

	/* Generate public-private keypair using PKCS#11. */
	session = pkcs11_generate_keypair(keysize, keytype, dname, fd, &pub,
	    &priv, on_filesystem ? NULL : pin);
	(void) close(fd);	/* Even works if fd == -1 */
	if (session == NULL) {
		(void) fprintf(stderr,
		    gettext("Cannot generate keypair.\n"));
		(void) unlink(file_priv);
		exit(1);
	}

	/* Generate CR and publish it! */
	buf_len = pkcs11_generate(SSH_X509_PKCS_10, &buf, session, pub, priv,
	    dname, keytype, -1, on_filesystem ? NULL : pin, not_before,
	    not_after);
	if (buf_len == 0) {
		(void) fprintf(stderr,
		    gettext("Cannot generate certificate request.\n"));
		(void) unlink(file_priv);
		exit(1);
	}

	/* Publish the cert. */
	export_data(buf, buf_len, outfile, out_fmt, SSH_PEM_CERT_REQ_BEGIN,
	    SSH_PEM_CERT_REQ_END);
}

/*
 * Given a private key, try to copy it into the keystore
 * We'll need an existing public certificate in order to
 * derive the parameters the label it properly
 */
static boolean_t
private_to_keystore(SshPrivateKey *privkey, unsigned char *b, size_t len)
{
	/* Attempt to copy into the keystore. */
	int rv;
	CK_SESSION_HANDLE session = NULL;
	char pin[256] = {'\0'};
	char *privkeytype, *signature;
	CK_KEY_TYPE privkt;
	SshBerTimeStruct not_before, not_after;
	CK_DATE start, finish;
	CK_BYTE *id, *subject = NULL;
	size_t subject_len;
	SshPublicKey *pubkey;
	struct certlib_keys *kp;

	const char dl_modp[] = "dl-modp";
	const char if_modn[] = "if-modn";
	struct certlib_cert *cert = NULL;
	SshMPIntegerStruct certkey;
	boolean_t foundcert = B_FALSE;


	session = pkcs11_login(pkcs11_token_id, pin);
	if (session == NULL)
		return (B_FALSE);

	/*
	 * We need to get information about the key
	 * and then link it to a certificate in order
	 * to gather enough info to write to a pkcs11 token
	 */

	/* Turn the key into a certlib_key structure */
	/* (Much code taken from certlib_load() and modified) */

	kp = ssh_calloc(1, sizeof (*kp));	/* zeroize */
	if (kp == NULL)
		memory_bail();
	rv = ssh_private_key_get_info(*privkey,
	    SSH_PKF_KEY_TYPE, &privkeytype,
	    SSH_PKF_SIGN, &signature,
	    SSH_PKF_SIZE, &kp->size,
	    SSH_PKF_END);
	if (rv !=  SSH_CRYPTO_OK ||
	    (strcmp(privkeytype, if_modn) != 0 &&
	    strcmp(privkeytype, dl_modp) != 0)) {
		/* unsupported type of key file */
		ssh_free(kp);
		ssh_private_key_free(*privkey);
		fprintf(stderr, gettext("ssh_private_key_get_info() "
		    "failed, rv = %d (%x)\n"), rv, rv);
		return (B_FALSE);
	}
	/* privkeytype and signature are not allocated  */
	if (strstr(signature, "dsa") != NULL)
		kp->type = ssh_strdup("dsa");
	else if (strstr(signature, "rsa") != NULL)
		kp->type = ssh_strdup("rsa");
	else
		kp->type = NULL;

	if (kp->type == NULL) {
		ssh_free(kp);
		ssh_private_key_free(*privkey);
		fprintf(stderr, gettext("Unknown key type.\n"));
		return (B_FALSE);
	}
	kp->data = b;
	kp->datalen = len;
	kp->key = *privkey;

	ssh_mprz_init(&kp->n);
	ssh_mprz_init(&kp->e);
	ssh_mprz_init(&kp->d);
	ssh_mprz_init(&kp->p);
	ssh_mprz_init(&kp->q);
	ssh_mprz_init(&kp->u);
	ssh_mprz_init(&kp->g);
	ssh_mprz_init(&kp->x);
	ssh_mprz_init(&kp->y);

	if (strcmp(privkeytype, if_modn) == 0) {
		rv = ssh_private_key_get_info(*privkey,
		    SSH_PKF_SECRET_D, &kp->d,
		    SSH_PKF_PUBLIC_E, &kp->e,
		    SSH_PKF_MODULO_N, &kp->n,
		    SSH_PKF_PRIME_P, &kp->p,
		    SSH_PKF_PRIME_Q, &kp->q,
		    SSH_PKF_INVERSE_U, &kp->u,
		    SSH_PKF_END);
	} else {
		rv = ssh_private_key_get_info(*privkey,
		    SSH_PKF_GENERATOR_G, &kp->g,
		    SSH_PKF_PRIME_P, &kp->p,
		    SSH_PKF_PRIME_Q, &kp->q,
		    SSH_PKF_SECRET_X, &kp->x,
		    SSH_PKF_PUBLIC_Y, &kp->y,
		    SSH_PKF_END);
	}
	if (rv != SSH_CRYPTO_OK) {
		(void) fprintf(stderr, gettext("Invalid key!"));
		return (B_FALSE);
	}

	/* Find associated certificate, if possible */

	/* Much code taken from link_key_to_cert() and modified */

	ssh_mprz_init(&certkey);
	while (certlib_next_cert(&cert)) {
		switch (cert->cert->subject_pkey.pk_type) {
		case SSH_X509_PKALG_RSA:
			(void) ssh_public_key_get_info(
			    cert->cert->subject_pkey.public_key,
			    SSH_PKF_MODULO_N, &certkey,
			    SSH_PKF_END);

			if (cert->type == NULL)
				cert->type = ssh_strdup("rsa");
			if (ssh_mprz_cmp(&kp->n, &certkey) == 0) {
				foundcert = B_TRUE;
				break;
			}
			break;
		case SSH_X509_PKALG_DSA:
			if (cert->type == NULL)
				cert->type = ssh_strdup("dsa");
			ssh_public_key_get_info(
			    cert->cert->subject_pkey.public_key,
			    SSH_PKF_PUBLIC_Y, &certkey, SSH_PKF_END);
			if (ssh_mprz_cmp(&kp->y, &certkey) == 0) {
				foundcert = B_TRUE;
				break;
			}
			break;
		}
		if (foundcert == B_TRUE)
			break;
	}

	if (foundcert == B_FALSE) {
		fprintf(stderr, gettext("Cannot import "
		    "private key to token without "
		    "corresponding public cert first.\n"));
		return (B_FALSE);
	}

	ssh_x509_name_reset(cert->cert->subject_name);
	if (!ssh_x509_cert_get_subject_name(cert->cert,
	    &cert->subject_name)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert subject name.\n"));
		return (B_FALSE);
	}

	id = (unsigned char *)cert->subject_name;

	if (!ssh_x509_cert_get_subject_name_der(cert->cert,
	    &subject, &subject_len)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert subject name der.\n"));
		return (B_FALSE);
	}


	rv = get_validity(cert, &start, &finish, &not_before,
	    &not_after);
	if (rv != 0) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert validity period.\n"));
		return (B_FALSE);
	}

	rv = pkcs11_migrate_privkey(session, kp, id, subject,
	    subject_len, &privkt, &privkeytype, &start,
	    &finish);
	if (rv != 0) {
		/* Error message already emitted by above function */
		return (B_FALSE);
	}

	ssh_sign_to_keytype(&privkeytype);
	if (!write_pin)
		bzero(pin, PKCS11_TOKSIZE);
	if (!write_pkcs11_files(pkcs11_token_id, pin, (char *)id,
	    privkeytype, B_FALSE, B_TRUE, write_pin)) {
		(void) printf(gettext("PKCS#11 hint file could not "
		    "be written to disk.\n"));
		return (B_FALSE);
	}
	return (B_TRUE);
}


/*
 * Add a key to database.
 */
static void
add_local(char *infile)
{
	FILE *f;
	char filename[MAXPATHLEN];
	unsigned char b[MAXKEYBUFSZ + 1];
	int fout, i = 0;
	size_t len, newlen;
	SshSKBType kind;
	unsigned char *newbuf;

	if (infile == NULL) {
		f = stdin;
	} else {
		f = fopen(infile, "r");

		if (f == NULL) {
			(void) fprintf(stderr, "fopen(%s): %s\n", infile,
			    strerror(errno));
			exit(1);
		}
	}

	/*
	 * Before we blindly write a file, let's do a little validation.
	 * We'll keep the file in a buffer, since we may need it later.
	 * MAXKEYBUFSZ is much bigger than any valid key input.  We'll try
	 * to read up to this maximum conceivable size + 1, and if we can
	 * actually read that much, we know the input is no good.  Further
	 * validation follows.
	 */

	len = fread(b, sizeof (char), MAXKEYBUFSZ + 1, f);
	if (len == MAXKEYBUFSZ + 1) {
		if (f != stdin) {
			(void) fprintf(stderr,
			    gettext("Too much data to be a valid key for file"
			    " %s.\n"), infile);
		} else {
			(void) fprintf(stderr,
			    gettext("Too much data to be a valid key.\n"));
		}
		exit(1);
	}
	kind = buf_to_privkey_blob(b, len, &newbuf, &newlen);

	if (newbuf == NULL) {
		switch (kind) {

		case SSH_SKB_PKCS8:
		case SSH_SKB_SSH_X509:
			if (f != stdin) {
				(void) fprintf(stderr,
				    gettext("Could not import %s key from file"
				    " %s.\n"),
				    keytype_to_string(kind, keytype_table),
				    infile);
			} else {
				(void) fprintf(stderr,
				    gettext("Could not import %s key.\n"),
				    keytype_to_string(kind, keytype_table));
			}
		default:
			if (f != stdin) {
				(void) fprintf(stderr,
				    gettext("Unsupported keyfile format: %s, "
				    "import from file %s aborted.\n"),
				    keytype_to_string(kind, keytype_table),
				    infile);
			} else {
				(void) fprintf(stderr,
				    gettext("Unsupported keyfile format: %s, "
				    "import aborted.\n"),
				    keytype_to_string(kind, keytype_table));
			}
		}
		exit(1);
	}

	/* We've now got data suitable for writing into a file or token */
	if (pkcs11_token_id == NULL) {
		/* Create a private key file */
		(void) snprintf(filename, MAXPATHLEN, "%s%d",
		    certlib_keys_dir(), i);
		while ((fout = open(filename, O_CREAT|O_EXCL|O_WRONLY,
		    S_IRUSR|S_IWUSR)) < 0) {
			if (errno == EEXIST) {
				(void) snprintf(filename, MAXPATHLEN, "%s%d",
				    certlib_keys_dir(), ++i);
			} else {
				int err = errno;
				(void) fprintf(stderr,
				    gettext("Unable to open private-key "
				    "database, error %s\n"), strerror(err));
				ssh_free(newbuf);
				exit(1);
			}
		}
		/* Just copy the file directly into the keystore */
		if (write(fout, newbuf, newlen) == -1) {
			int err = errno;
			(void) fprintf(stderr,
			    gettext("Could not write key file, "
			    "write error %s.\n"), strerror(err));
			(void) unlink(filename);
			ssh_free(newbuf);
			exit(1);
		}
		(void) close(fout);
	} else {
		SshPrivateKey newkey;

		/* Need the raw SafeNet key for PKCS#11 ops */
		newkey = ssh_x509_decode_private_key(
		    (const unsigned char *)newbuf, newlen);
		if (newkey == NULL) {
			(void) fprintf(stderr,
			    gettext("Unable to extract private key from key "
			    "blob.\n"));
			ssh_free(newbuf);
			exit(1);
		}

		if (!private_to_keystore(&newkey, newbuf, newlen)) {
			ssh_free(newbuf);
			exit(1);
		}
	}

	if (f != stdin)
		(void) fclose(f);
	(void) fprintf(stdout, gettext(
	    "Private key import successful.\n"));
	ssh_free(newbuf);
}

/*
 * Remove local private key
 */
static void
remove_local(char *slotname, int unlinkflag)
{
	char filename[MAXPATHLEN];
	uint8_t *buffer, *sofar;
	struct stat sbuf;
	int fd;
	ssize_t rc, toread;
	char *token_label, *token_pin, *key_id, *key_type;
	char pinstore[PASSPHRASE_MAX] = {'\0'};
	CK_SESSION_HANDLE session;
	CK_KEY_TYPE pkcs11_keytype;

	(void) snprintf(filename, MAXPATHLEN, "%s%s", certlib_keys_dir(),
	    slotname);

	/*
	 * Check to see if it's a PKCS#11 object... If so,
	 * remove the object as well as the hint file
	 */
	fd = open(filename, O_RDONLY);
	if (fd != -1 && fstat(fd, &sbuf) != -1) {
		buffer = ssh_calloc(1, sbuf.st_size);
		if (buffer == NULL) {
			(void) fprintf(stderr,
			    gettext("Cannot allocate %d bytes for slot.\n"),
			    sbuf.st_size);
			exit(1);
		}
		rc = read(fd, buffer, sbuf.st_size);
		sofar = buffer;
		toread = (ssize_t)sbuf.st_size;

		while (rc < toread) {
			if (rc == -1) {
				/*
				 * If we're dealing with an on-disk file, we
				 * can just remove it
				 */
				if (unlinkflag) {
					(void) fprintf(stderr,
					    gettext("Corrupt slot %s, use -r "
					    "to remove this file.\n"),
					    slotname);
					exit(1);
				} else {
					(void) fprintf(stderr, gettext(
					    "Removing corrupt slot %s.\n"),
					    slotname);
					goto remove_file;
				}
			}
			toread -= rc;
			sofar += rc;
			rc = read(fd, sofar, toread);
		}

		if (ssh_x509_decode_private_key(buffer, sbuf.st_size) == NULL) {
			/* AHA!  It should be an X.509 key. */
			parse_pkcs11_file(buffer, sbuf.st_size, &token_label,
			    &token_pin, &key_id, &key_type);

			if (token_label == NULL) {
				if (unlinkflag) {
					(void) fprintf(stderr,
					    gettext("Corrupt slot %s, use -r "
					    "to remove this file.\n"),
					    slotname);
					exit(1);
				} else {
					(void) fprintf(stderr, gettext(
					    "Removing corrupt slot %s.\n"),
					    slotname);
					goto remove_file;
				}
			}

			/* We had a valid label, so we can remove the file */
			if (unlinkflag) {
				char padtok[PKCS11_TOKSIZE];

				pkcs11_pad_out(padtok, token_label);
				if (strncmp(pkcs11_token_id, padtok,
				    PKCS11_TOKSIZE) != 0) {
					(void) fprintf(stderr, gettext(
					    "Token \"%s\" not referenced in "
					    "%s.\n"), pkcs11_token_id,
					    slotname);
					exit(1);
				} else {
					goto remove_file;
				}
			}

			if (p11f == NULL) {
				(void) fprintf(stderr,
				    gettext("Must have PKCS#11 library "
				    "loaded to remove this key.\n"));
				exit(1);
			}

			/*
			 * If this file does not have proper permissions
			 * we should not trust it and should not try to
			 * access the token.  It should be owned by root
			 * and only accessible by root.
			 */
			if (INSECURE_PERMS(sbuf)) {
				(void) fprintf(stderr,
				    gettext("PKCS#11 hint file has insecure "
				    "permissions, login to token aborted.\n"));
				close(fd);
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

			pkcs11_keytype = (*key_type == 'd') ? CKK_DSA : CKK_RSA;

			/* Find and nuke PKCS#11 private key. */
			find_and_nuke(session, key_id, CKO_PRIVATE_KEY,
			    pkcs11_keytype, B_FALSE);
		}
	}

remove_file:
	if (unlink(filename)) {
		(void) fprintf(stderr, "%s\n",
		    gettext("Error removing key from that slot."));
		if (debug)
			perror(filename);
		exit(1);
	}
}

/*
 * Extract the raw private key file.
 */
static void
extract_privatekey(char *slotname, char *outfile, char *out_fmt)
{
	int f;
	char filename[MAXPATHLEN];
	char buf[BIGBUFSZ];
	uchar_t *retbuf;
	size_t size;
	size_t retlen = 0;
	struct stat sbuf;

	if ((out_fmt != NULL) && (strncmp(out_fmt, "pkcs8", 6) != 0)) {
		(void) fprintf(stderr, "%s %s\n",
		    gettext("Invalid output format for private key."), out_fmt);
		exit(1);
	}

	(void) snprintf(filename, MAXPATHLEN, "%s%s", certlib_keys_dir(),
	    slotname);
	f = open(filename, O_RDONLY);
	if ((f == -1) || (fstat(f, &sbuf) == -1)) {
		(void) fprintf(stderr, "%s %s\n", gettext("Key not found:"),
		    slotname);
		exit(1);
	}
	/*
	 * File should be owned by root and only accessible by root
	 * or it may have been tampered with.
	 */
	if (INSECURE_PERMS(sbuf)) {
		(void) fprintf(stderr,
		    gettext("Private key %s has insecure permissions and "
		    "will not be trusted.\n"), filename);
		close(f);
		exit(1);
	}

	size = read(f, buf, sizeof (buf));
	/*
	 * keystore files are currently just a few lines of
	 * text.  If we have a big file, it is clearly
	 * not worth checking to see if it is a keystore file.
	 * We do a generous order of magnitude test before
	 * bothering with the keystore sanity check.
	 */
	if (size < (BIGBUFSZ / 4)) {
		char pkcs11_buf[BIGBUFSZ];
		char *token_label, *token_pin, *key_id, *key_type;

		memcpy(pkcs11_buf, buf, size);
		parse_pkcs11_file((uint8_t *)pkcs11_buf, size, &token_label,
		    &token_pin, &key_id, &key_type);
		if (token_label != NULL) {
			(void) fprintf(stderr, gettext("certlocal:  "
			    "Cannot export a key from a PKCS#11 slot.\n"));
			usage();
		}
	}
	(void) close(f);

	(void) buf_to_privkey_blob((uchar_t *)buf, size, &retbuf, &retlen);
	if (retbuf == NULL) {
		(void) fprintf(stderr, gettext("Could not extract "
		    "private key.\n"));
		exit(1);
	} else {
		SshPrivateKey retkey;

		/* Sanity check */
		retkey = ssh_x509_decode_private_key(
		    (const unsigned char *)retbuf, retlen);
		if (retkey == NULL) {
			(void) fprintf(stderr, gettext("Could not extract "
			    "private key file.\n"));
			ssh_free(retbuf);
			exit(1);
		}
	}
	/* We have a buffer in SafeNet private key blob format now */

	if (out_fmt == NULL) {
		/* Just write the raw output in original format */
		if (!write_buffer_to_file(retbuf, retlen, outfile, B_TRUE)) {
			ssh_free(retbuf);
			(void) fprintf(stderr, gettext("Could not write "
			    "private key file.\n"));
			exit(1);
		}
	} else {
		size_t retlen2;
		uchar_t *retbuf2;
		SshPrivateKey retprivkey;

		retprivkey = ssh_x509_decode_private_key(retbuf, retlen);
		if (retprivkey == NULL) {
			(void) fprintf(stderr, gettext("Could not extract "
			    "private key in PKCS#8 format.\n"));
			ssh_free(retbuf);
			exit(1);
		}
		/*
		 * We need to convert our buffer to pkcs#8 format and
		 * write that out instead
		 */
		if (ssh_pkcs8_encode_private_key(retprivkey, &retbuf2,
		    &retlen2) != SSH_X509_OK) {
			(void) fprintf(stderr, gettext("Could not extract "
			    "private key in PKCS#8 format.\n"));
			ssh_free(retbuf);
			exit(1);
		}
		if (!write_buffer_to_file(retbuf2, retlen2, outfile, B_TRUE)) {
			ssh_free(retbuf);
			ssh_free(retbuf2);
			(void) fprintf(stderr, gettext("Could not write "
			    "private key file.\n"));
			exit(1);
		}
		ssh_free(retbuf);
		ssh_free(retbuf2);
	}
}

static boolean_t
set_cert_time(char *timestring, SshBerTimeStruct *certtime)
{
	char c = *timestring;
	time_t inputtime;
	struct tm *caltime;

	ssh_ber_time_zero(certtime);

	/*
	 * If the first character of the string is a sign
	 * then treat this as a relative date.  Otherwise
	 * treat it as an absolute date.
	 */
	if ((c == '+') || (c == '-')) {
		/*
		 * Parse input in relative time and convert to absolute
		 * seconds since the epoch
		 */

		if (!abstime(timestring, &inputtime)) {
			return (B_FALSE);
		}
	} else {
		/*
		 * Check for an absolute time with format described
		 * in getdate(3c).
		 *
		 */
		(void) putenv(DATE_FORMAT_FILE);
		caltime = getdate(timestring);
		if (caltime == NULL)
			return (B_FALSE);
		inputtime = mktime(caltime);
	}
	/*
	 * Set absolute date in cert.  Return value is void, so we have
	 * to assume this works, but we sanity check that it makes sense.
	 */
	ssh_ber_time_set_from_unix_time(certtime, (SshTime) inputtime);
	return (ssh_ber_time_available(certtime));
}

int
pkcs11_associate_private(struct certlib_certspec *p)
{
	char pin[PASSPHRASE_MAX] = {'\0'};
	int i, error = 0;
	CK_SESSION_HANDLE session = NULL;

	if (certlib_find_local_pkcs11_ident(p) != NULL) {
		/* No need to associate if already have a hint file */
		(void) fprintf(stderr, gettext(
		    "PKCS#11 object linkage with IKE already exists.\n"));
		return (1);
	}

	/* Log into token and get PKCS#11 session */

	session = pkcs11_login(pkcs11_token_id, pin);
	if (session == NULL)
		return (1);

	for (i = 0; i < p->num_includes; i++) {
		const char *id;
		char privkeytype[4];
		CK_KEY_TYPE kt;
		CK_OBJECT_HANDLE object = NULL;

		id = p->includes[i];

		/* Try looking for RSA first, then DSA */

		kt = CKK_RSA;
		(void) strcpy(privkeytype, "rsa");
		object = find_object(session, (char *)id, CKO_PRIVATE_KEY, kt);
		if (object == NULL) {
			kt = CKK_DSA;
			(void) strcpy(privkeytype, "dsa");
			object = find_object(session, (char *)id,
			    CKO_PRIVATE_KEY, kt);
			if (object == NULL) {
				(void) fprintf(stderr,
				    gettext("No private key object with ID %s "
				    "found.\n"), id);
				error = 1;
				continue;
			}
		}

		/*
		 * Found object, have everything we need to create
		 * the PKCS#11 hint file.
		 */

		if (!write_pin)
			bzero(pin, PASSPHRASE_MAX);
		if (write_pkcs11_files(pkcs11_token_id, pin, (char *)id,
		    privkeytype, B_FALSE, B_TRUE, write_pin)) {
			(void) printf(gettext(
			    "PKCS#11 Private key association successful.\n"));
		} else {
			(void) printf(gettext("PKCS#11 hint file could not be "
			    "written to disk.\n"));
			error = 1;
		}
	}
	(void) p11f->C_CloseSession(session);
	(void) p11f->C_Finalize(NULL_PTR);
	return (error);
}

int
migrate_to_token(struct certlib_cert *p)
{
	(void) printf(gettext("Found public cert in slot %s.\n"), p->slotname);
	if (p->keys == NULL) {
		(void) printf(gettext("No matching private key found.\n"));
		(void) printf(gettext("Use certdb to copy only the certificate"
		    " and public key object.\n"));
		exit(1);
	}
	(void) printf(gettext("Found private key in slot %s.\n"),
	    p->keys->slotname);
	exit(pkcs11_migrate_keypair(p, B_TRUE, write_pin));
	/* NOTREACHED */
	return (EXIT_SUCCESS);
}

int
main(int argc, char *argv[])
{
	int help = 0, lflag = 0, kflag = 0, aflag = 0, cflag = 0,
	    rflag = 0, eflag = 0, sflag = 0, Lflag = 0, Cflag = 0,
	    Xflag = 0, Tflag = 0, Uflag = 0;
	char c;
	char *outfile = NULL, *infile = NULL,
	    *keysize = NULL, *keytype = NULL, *dname = NULL,
	    *out_fmt = NULL, *this_altname = NULL, *keyfile = NULL,
	    *start_time = NULL, *finish_time = NULL;
	SshBerTimeStruct not_before, not_after;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	certlib_mode = CERTLIB_NORMAL;
	if (argc == 1)
		help = 1;

	while ((c = getopt(argc, argv,
	    "sh?dvlkcpm:t:D:o:ai:ref:A:T:EK:S:F:CXLU")) != EOF)
		switch (c) {
		case 'X':
			Xflag = 1;
			break;
		case 'h':
		case '?':
			help = 1;
			break;
		case 'T':
			Tflag = 1;
			pkcs11_token_id = pkcs11_token_storage;
			pkcs11_pad_out(pkcs11_token_id, optarg);
			break;
		case 'd':
			certlib_mode |= CERTLIB_DEBUG;
			break;
		case 'E':
			certlib_mode |= CERTLIB_SOLARISCRYPTO;
			break;
		case 'K':
			keyfile = optarg;
			break;
		case 'a': /* Add key */
			aflag = 1;
			break;
		case 'i': /* input file */
			infile = optarg;
			break;
		case 'r': /* Remove key */
			rflag = 1;
			break;
		case 'U': /* Unlink */
			Uflag = 1;
			rflag = 1;
			break;
		case 'e': /* Extract private key */
			eflag = 1;
			break;
		case 'p': /* store pin on disk */
			write_pin = B_TRUE;
			break;
		case 'f': /* Output format */
			out_fmt = optarg;
			break;
		case 'c': /* ca request */
			cflag = 1;
			break;
		case 's': /* self-signed cert */
			sflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'l':
			/* List */
			lflag = 1;
			break;
		case 'k':
			/* Generate flag */
			kflag = 1;
			break;
		case 'm':
			/* key size */
			keysize = optarg;
			break;
		case 't':
			/* key type */
			keytype = optarg;
			break;
		case 'C':
			/* Migrate key to token */
			Cflag = 1;
			break;
		case 'L':
			/* Associate keystore object with IKE database */
			Lflag = 1;
			break;
		case 'D':
			/* issuer name */
			dname = ssh_strdup(optarg);
			break;
		case 'A':
			/* Subject Alternative Name */
			this_altname = ssh_strdup(optarg);
			if (this_altname == NULL) {
				(void) fprintf(stderr,
				    gettext("Out of memory (altname)\n"));
				help = 1;
				break;
			}
			if (!verify_alt_subj((const char **)&this_altname)) {
				(void) fprintf(stderr, "%s %s\n",
				    gettext("Invalid Alternative Name type:"),
				    this_altname);
				help = 1;
				break;
			}
			append(&alt_subj, &alt_subj_count, this_altname);
			break;
		case 'o':
			/* output file */
			outfile = optarg;
			break;
		case 'S':
			/* Cert lifetime start */
			start_time = optarg;
			break;
		case 'F':
			/* Cert lifetime finish */
			finish_time = optarg;
			break;
		default:

			help = 1;
		}

	/* Check for valid Key size and cert lifetimes */
	if (kflag) {
		if (keysize != NULL) {
			if ((atoi(keysize) % 512) > 0 || atoi(keysize) > 8192) {
				(void) fprintf(stderr, "%s\n",
				    gettext("Invalid Key Size."));
				help = 1;
			}
		} else {
			(void) fprintf(stderr, "%s\n",
			    gettext("No Key Size type given."));
			help = 1;
		}
		/* Check for valid Key Encryption and Signature type */
		if (keytype != NULL) {
			if ((strcmp(keytype, "rsa-sha1") != 0) &&
			    (strcmp(keytype, "rsa-md5") != 0) &&
			    (strcmp(keytype, "dsa-sha1") != 0)) {
				(void) fprintf(stderr, "%s\n",
				    gettext("Invalid Key Encryption Type."));
				help = 1;
			}
		} else {
			(void) fprintf(stderr, "%s\n",
			    gettext("No Key Encryption type given."));
			help = 1;
		}

		if (start_time != NULL) {
			if (set_cert_time(start_time, &not_before) == B_FALSE) {
				(void) fprintf(stderr, "%s\n",
				    gettext("Invalid Start Time."));
				help = 1;
			}
		} else {
			/* Default to current time as validity start */
			ssh_ber_time_zero(&not_before);
			ssh_ber_time_set_from_unix_time(&not_before,
			    ssh_time());
		}

		if (finish_time != NULL) {
			if (set_cert_time(finish_time, &not_after) == B_FALSE) {
				(void) fprintf(stderr, "%s\n",
				    gettext("Invalid Finish Time."));
				help = 1;
			}
		} else {
			/* Set validity end date relative to start */
			ssh_ber_time_zero(&not_after);
			ssh_ber_time_set(&not_after, &not_before);
			ssh_ber_time_add_secs(&not_after, SELF_SIGN_LIFETIME);
		}

		if (!help && ssh_ber_time_cmp(&not_after,
		    &not_before) != 1) {
			(void) fprintf(stderr, "%s\n",
			    gettext("Finish time must be after Start time."));
			help = 1;
		}
	}

	if (help ||
	    (kflag && !sflag && !cflag) ||
	    (!aflag && !kflag && !rflag && !eflag && !lflag && !Xflag &&
	    !Cflag && !Lflag) || (Cflag && !Tflag) || (Lflag && !Tflag) ||
	    (Cflag && Lflag) || (Uflag && !Tflag) || (Uflag && Cflag) ||
	    (Uflag && Lflag) || (write_pin && !Tflag))
		usage();

	pkcs11_path = find_pkcs11_path();

	/* Load up PKCS#11, and get some locals set. */
	p11f = pkcs11_setup(pkcs11_path);
	if (p11f == NULL) {
		fprintf(stderr, gettext("PKCS#11 library failed to load.\n"));
		exit(1);
	}

	/* Initialize the libike X.509 library. */
	x509config = &x509config_storage;
	ssh_x509_library_set_default_config(x509config);
	if (!ssh_x509_library_initialize(x509config))
		ssh_fatal("x509_library_initialize failed.");

	/* Load certificate database */
	if (!certlib_init(certlib_mode, CERTLIB_KEYS|CERTLIB_CERT))
		exit(1);

	if (Xflag) {
		/* Implement "ikecert tokens" command. */
		(void) printf(
		    gettext("Available tokens with library \"%s\":\n\n"),
		    pkcs11_path);
		print_pkcs11_slots();
		(void) printf(
		    gettext("\nNOTE:  Spaces at end can be filled in by "
		    "ikecert(1m) automatically.\n"));
		exit(0);
	} else if (aflag) {
		/* Add command */
		add_local(infile);
		/* Either succeeds or calls exit(). */
	} else if (kflag && sflag) {
		/* Generate self-signed cert */
		if (keysize == NULL || keytype == NULL || dname == NULL)
			usage();

		self_sign(keysize, keytype, dname, outfile, out_fmt,
		    &not_before, &not_after);
		/* Calls exit() if need be. */
	} else if (kflag && cflag) {
		/* Generate cert request */
		if (keysize == NULL || keytype == NULL || dname == NULL)
			usage();

		generate_cert_req(keysize, keytype, dname, outfile,
		    out_fmt, keyfile, &not_before, &not_after);
		/* Calls exit() if need be. */
	} else if (rflag || Uflag) {
		/* Remove command */
		char *slotname = argv[optind];

		if (slotname == NULL) {
			(void) fprintf(stderr,
			    gettext("certlocal: missing pattern parameter\n"));
			exit(1);
		}
		if (strncmp(slotname, "SLOT=", 5) == 0)
			slotname += 5;
		remove_local(slotname, Uflag);
		/* Calls exit() if need be. */
	} else if (eflag) {
		/* Extract command */
		char *slotname = argv[optind];

		if (slotname == NULL) {
			(void) fprintf(stderr, gettext(
			    "certlocal: missing pattern for extract\n"));
			exit(1);
		}
		if (strncmp(slotname, "SLOT=", 5) == 0)
			slotname += 5;
			extract_privatekey(slotname, outfile, out_fmt);
			/* Calls exit() if need be. */
	} else if (Cflag) {
		/* Copy to PKCS#11 device */
		struct certlib_certspec *certpattern;

		certpattern = gather_certspec(argv+optind, argc-optind);
		if (!certpattern) {
			(void) fprintf(stderr, gettext(
			    "certlocal: missing pattern for migrate\n"));
			exit(1);
		}

		certlib_find_cert_spec(certpattern, migrate_to_token);
		/*
		 * The above function calls exit() and just
		 * goes with the first match.  If we get to the
		 * code below, no patterns were matched.
		 */
		(void) fprintf(stderr, gettext(
		    "certlocal: no matching certificate and key pair found "
		    "to migrate.\n"));
		exit(1);
	} else if (Lflag) {
		/* Look for key in PKCS#11 device and create association */
		struct certlib_certspec *certpattern;

		certpattern = gather_certspec(argv+optind, argc-optind);
		if (!certpattern) {
			(void) fprintf(stderr, gettext(
			    "certlocal: missing pattern for migrate\n"));
			exit(1);
		}
		exit(pkcs11_associate_private(certpattern));
	} else if (lflag) {
		/* List command */
		while (optind < argc)
			append(&pattern, &pattern_count, argv[optind++]);
		certlib_iterate_keys(print_key);
	}

	return (EXIT_SUCCESS);
}
