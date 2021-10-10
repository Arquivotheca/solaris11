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

/*
 * Based on SSH ssh-certview.c
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <sys/md5.h>

#include <ike/sshincludes.h>
#include <ike/sshmp.h>
#include <ike/sshcrypt.h>
#include <ike/sshasn1.h>
#include <ike/oid.h>
#include <ike/x509.h>
#include <ike/dn.h>
#include <ike/sshpsystem.h>
#include <ike/sshfileio.h>
#include <ike/sshbase64.h>
#include <ike/pkcs11-glue.h>
#include <ike/certlib.h>

#include "dumputils.h"

extern char *pkcs11_token_id;
extern int certlib_mode;
#define	debug (certlib_mode & CERTLIB_DEBUG)

#define	CONFIG_FILE "/etc/inet/ike/config"	/* From "../defs.h". */
#define	BIGBUFSZ 2048
#define	PASSPHRASE_MAX 256

#undef sprintf	/* Avoid the SSH silliness, but use it safely below! */
#undef snprintf	/* Avoid the SSH silliness, but use it safely below! */

static char *default_pkcs11_path = "/usr/lib/libpkcs11.so";

/*
 * Returns a static buffer which is overwritten on each call.
 */
char *
dump_name(const char *name)
{
	static char buf[512];

	(void) snprintf(buf, sizeof (buf), "<%s>", name);

	return (buf);
}

/*
 * Returns a newly allocated buffer which must be freed with ssh_free().
 */
char *
dump_time(SshBerTime ber_time)
{
	char *name;
	ssh_ber_time_to_string(ber_time, &name);
	return (name);
}

/*
 * Return a string representation of an SshMPIntegerStruct in hexadecimal,
 * with no leading 0x, but with a leading zero if needed to form whole octets.
 *
 * Returns a newly allocated buffer which must be freed with ssh_free().
 */
char *
dump_number(const SshMPIntegerStruct *number)
{
	char *s;
	unsigned char *p1, *p2;
	s = ssh_mp_get_str(NULL, 16, number);
	p1 = (unsigned char *)s;
	if (s[0] == '0' && s[1] == 'x') {
		p2 = p1 + 2;
		if ((strlen(s) & 1) != 0)
			*p1++ = '0';
	} else {			/* shouldn't happen */
		p2 = p1;
	}
	do {
	} while ((*p1++ = toupper(*p2++)) != 0);
	return (s);
}

/*
 * Compute a secure hash of a public key for more convenient human-readable
 * display and comparison.  Return it in printable (hex) string form.
 * Returns a static buffer which is overwritten on each call.
 */
char *
dump_public_hash(const SshMPIntegerStruct *key)
{
	static char strbuf[40];
	MD5_CTX md5_context;
	unsigned char md5buf[16];
	char *p;
	int i;
	unsigned char buf[1024];

	/* will silently ignore part of very large (>8192 bits) keys */

	MD5Init(&md5_context);
	(void) ssh_mp_get_buf(buf, sizeof (buf), key);
	MD5Update(&md5_context, buf, sizeof (buf));
	MD5Final(md5buf, &md5_context);

	p = strbuf;
	for (i = 0; i < 16; ++i)
		p += sprintf(p, "%02X", md5buf[i]);
	return (strbuf);
}

/*
 * Returns a static buffer which is overwritten on each call.
 */
char *
dump_reason(SshX509ReasonFlags flags)
{
	static char buf[512];
	char *p = buf;
	if (flags & SSH_X509_RF_UNSPECIFIED)
		p += sprintf(p, "Unspecified ");
	if (flags & SSH_X509_RF_KEY_COMPROMISE)
		p += sprintf(p, "KeyCompromise ");
	if (flags & SSH_X509_RF_CA_COMPROMISE)
		p += sprintf(p, "CACompromise ");
	if (flags & SSH_X509_RF_AFFILIATION_CHANGED)
		p += sprintf(p, "AffiliationChanged ");
	if (flags & SSH_X509_RF_SUPERSEDED)
		p += sprintf(p, "Superseded ");
	if (flags & SSH_X509_RF_CESSATION_OF_OPERATION)
		p += sprintf(p, "CessationOfOperation ");
	if (flags & SSH_X509_RF_CERTIFICATE_HOLD)
		p += sprintf(p, "CertificateHold ");

	flags &= ~(SSH_X509_RF_UNSPECIFIED |
	    SSH_X509_RF_KEY_COMPROMISE |
	    SSH_X509_RF_CA_COMPROMISE |
	    SSH_X509_RF_AFFILIATION_CHANGED |
	    SSH_X509_RF_SUPERSEDED |
	    SSH_X509_RF_CESSATION_OF_OPERATION |
	    SSH_X509_RF_CERTIFICATE_HOLD);
	if (flags != 0 || p == buf)
		p += sprintf(p, "0x%04X\n", flags);

	p[-1] = 0;
	return (buf);
}

/*
 * Prints descriptive text (multiple lines) to stdout.
 */
void
dump_hex(unsigned char *str, size_t len)
{
	size_t i;
	(void) fputs("\t\t", stdout);
	for (i = 0; i < len; ++i) {
		if (i > 0)
			if ((i % 20) == 0)
				(void) fputs("\n\t\t", stdout);
			else
				(void) putchar(' ');
		(void) printf("%02X", str[i]);
	}
	(void) putchar('\n');
}

/*
 * Prints descriptive text (multiple lines) to stdout.
 */
void
dump_names(SshX509Name names)
{
	char *name;
	unsigned char *buf;
	char printbuf[INET6_ADDRSTRLEN];
	size_t buf_len;
	Boolean rv, ret = FALSE;

	do {
		rv = ssh_x509_name_pop_ip(names, &buf, &buf_len);
		if (rv == TRUE) {
			switch (buf_len) {
			case 4:
				(void) inet_ntop(AF_INET, buf,
				    printbuf, sizeof (printbuf));
				break;
			case 16:
				(void) inet_ntop(AF_INET6, buf,
				    printbuf, sizeof (printbuf));
				break;
			default:
				(void) snprintf(printbuf, sizeof (printbuf),
				    "[%u bytes]", buf_len);
				break;
			}
			(void) printf("\t\t\tIP = %s\n", printbuf);
			ret = TRUE;
		}
	} while (rv == TRUE);

	do {
		rv = ssh_x509_name_pop_dns(names, &name);
		if (rv == TRUE) {
			(void) printf("\t\t\tDNS = %s\n", name);
			ret = TRUE;
		}
	} while (rv == TRUE);

	do {
		rv = ssh_x509_name_pop_uri(names, &name);
		if (rv == TRUE) {
			(void) printf("\t\t\tURI = #I%s#i\n", name);
			ret = TRUE;
		}
	} while (rv == TRUE);

	do {
		rv = ssh_x509_name_pop_email(names, &name);
		if (rv == TRUE) {
			(void) printf("\t\t\temail = %s\n", name);
			ret = TRUE;
		}
	} while (rv == TRUE);

	do {
		rv = ssh_x509_name_pop_rid(names, &name);
		if (rv == TRUE) {
			(void) printf("\t\t\tRID = %s\n", name);
			ret = TRUE;
		}
	} while (rv == TRUE);

	do {
		rv = ssh_x509_name_pop_directory_name(names, &name);
		if (rv == TRUE) {
			(void) printf("\t\t\tDN = %s\n", dump_name(name));
			ret = TRUE;
		}
	} while (rv == TRUE);

	if (ret != TRUE)
		(void) puts("\t\t\tNo supported name types detected.");
}

/*
 * While not really a "dump utility", find_pkcs11_path() is shared between
 * certdb and certlocal.
 *
 * Scan the file for pkcs11_path or p1_xform.  If p1_xform, skip {}
 * processing.  If pkcs11_path, pluck out the string and return it.  If I
 * see { or EOF, return NULL.
 */
char *
find_pkcs11_path(void)
{
	enum { FINDING, SKIPPING_P1, GRAB_QUOTE, IN_STRING } state = FINDING;
	char buf[MAXPATHLEN];
	char *bptr;
	int c;
	FILE *cfile;
	boolean_t newline = B_TRUE;
	boolean_t in_comment = B_FALSE;

	cfile = fopen(CONFIG_FILE, "r");
	if (cfile == NULL)
		return (default_pkcs11_path);

	bptr = buf;
	while ((c = getc(cfile)) != EOF) {
		/* Skip whitespace. */
		if ((isspace(c) && bptr == buf) || in_comment) {
			if (c == '\n') {
				in_comment = B_FALSE;
				newline = B_TRUE;
			}
			continue;
		}

		if (newline && c == '#')
			in_comment = B_TRUE;

		newline = B_FALSE;
		if (in_comment)
			continue;

		/* Make sure we don't overstep buf with a single token. */
		if (bptr == &(buf[256]))
			bptr = buf;
		*bptr = '\0';

		switch (state) {
		case FINDING:
			if (c == '{')
				return (default_pkcs11_path);
			if (isspace(c)) {
				if (strcmp(buf, "pkcs11_path") == 0)
					state = GRAB_QUOTE;
				else if (strcmp(buf, "p1_xform") == 0)
					state = SKIPPING_P1;
				bptr = buf;
			} else {
				*bptr = c;
				bptr++;
			}
			break;
		case SKIPPING_P1:
			if (c == '}')
				state = FINDING;
			break;
		case GRAB_QUOTE:
			if (c == '"')
				state = IN_STRING;
			else
				return (default_pkcs11_path);
			break;
		case IN_STRING:
			if (c == '\n')
				return (default_pkcs11_path);
			if (c == '"') {
				bptr = ssh_strdup(buf);
				if (bptr == NULL) {
					(void) fprintf(stderr, gettext(
					    "Out of memory on init, using"
					    " default PKCS#11 path.\n"));
					bptr = default_pkcs11_path;
				}
				return (bptr);
			}
			if (c == '\\') {
				c = getc(cfile);
				if (c == EOF)
					return (default_pkcs11_path);
			}
			*bptr = c;
			bptr++;
			break;
		}
	}

	return (default_pkcs11_path);
}

/*
 * Now that everything has been verified, add a certificate to the PKCS#11
 * keystore.
 */
boolean_t
pkcs11_cert_generate(CK_SESSION_HANDLE session, uint8_t *ber, size_t len,
    char *object_id, SshX509Certificate sshcert)
{
	CK_CERTIFICATE_TYPE ctype = CKC_X_509;
	CK_ATTRIBUTE attrs[MAX_ATTRS];
	CK_OBJECT_HANDLE newobj;
	CK_OBJECT_CLASS class = CKO_CERTIFICATE;
	SshX509Name holder = NULL;
	uint8_t *name_der;
	size_t name_der_len, issuer_len;
	CK_BBOOL true = TRUE;
	CK_ULONG numattrs = 0;
	SshMPIntegerStruct sshserial;
	CK_BYTE *issuer = NULL;
	CK_BYTE *serial = NULL;
	boolean_t status;

	/*
	 * Get the certificate name (object_id) into a DER form.
	 *
	 * NOTE:  If a SubjectAltName was provided in object_id, just
	 * deal.  (We can always prepend CN= to object_id if need be!)
	 *
	 * NOTE2: XXX - if the push/pop doesn't work, then yank the DN
	 *	  out of the BER.
	 */

	if (!ssh_x509_name_push_directory_name(&holder, (uchar_t *)object_id))
		return (B_FALSE);

	if (!ssh_x509_name_pop_directory_name_der(holder, &name_der,
	    &name_der_len))
		return (B_FALSE);

	ssh_mprz_init(&sshserial);
	if (!ssh_x509_cert_get_serial_number(sshcert, &sshserial)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert serial number.\n"));
		return (B_FALSE);
		}
	serial = (uchar_t *)ssh_mp_get_ui(&sshserial);
	ssh_mprz_clear(&sshserial);

	if (!ssh_x509_cert_get_issuer_name_der(sshcert, &issuer, &issuer_len)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert issuer name.\n"));
		return (B_FALSE);
	}

	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_CLASS, &class, sizeof (class));
	numattrs++;
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_CERTIFICATE_TYPE, &ctype,
	    sizeof (CK_CERTIFICATE_TYPE));
	numattrs++;
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_SUBJECT, name_der, name_der_len);
	numattrs++;
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_ISSUER, issuer, issuer_len);
	numattrs++;
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_LABEL, object_id, strlen(object_id));
	numattrs++;
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_ID, object_id, strlen(object_id));
	numattrs++;
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_VALUE, ber, len);
	numattrs++;
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_TOKEN, &true, sizeof (true));
	numattrs++;
	/* Our serial numbers are 4 bytes long */
	/* LINTED */
	ATTR_INIT(attrs[numattrs], CKA_SERIAL_NUMBER, &serial, 4);
	numattrs++;

	status = (p11f->C_CreateObject(session, attrs, numattrs, &newobj) ==
	    CKR_OK);
	ssh_free(issuer);
	return (status);
}

CK_OBJECT_HANDLE
find_object(CK_SESSION_HANDLE session, char *object_id,
    CK_OBJECT_CLASS class, CK_KEY_TYPE type)
{
	CK_RV pkcs11_rc;
	CK_ATTRIBUTE template[4];
	CK_ULONG count = 0;
	CK_OBJECT_HANDLE target;
	CK_OBJECT_CLASS local_class = class;
	CK_KEY_TYPE local_type = type;
	CK_CERTIFICATE_TYPE local_cert_type = (CK_CERTIFICATE_TYPE)type;
	CK_BBOOL true = TRUE;
	CK_ULONG numattrs = 0;

	/* Use "32" because that's what PKCS#11 says label length should be! */
	/* LINTED */
	ATTR_INIT(template[numattrs], CKA_ID, object_id, strlen(object_id));
	numattrs++;
	/* LINTED */
	ATTR_INIT(template[numattrs], CKA_TOKEN, &true, sizeof (true));
	numattrs++;
	/* LINTED */
	ATTR_INIT(template[numattrs], CKA_CLASS, &local_class,
	    sizeof (local_class));
	numattrs++;
	if (class == CKO_CERTIFICATE)
		/* LINTED */
		ATTR_INIT(template[numattrs], CKA_CERTIFICATE_TYPE,
		    &local_cert_type, sizeof (local_cert_type));
	else
		/* LINTED */
		ATTR_INIT(template[numattrs], CKA_KEY_TYPE, &local_type,
		    sizeof (local_type));
	numattrs++;

	pkcs11_rc = p11f->C_FindObjectsInit(session, template, 4);
	if (pkcs11_rc != CKR_OK) {
		(void) fprintf(stderr,
		    gettext("ikecert: C_FindObjectsInit error # %d.\n"),
		    pkcs11_rc);
		return (NULL);
	}

	pkcs11_rc = p11f->C_FindObjects(session, &target, 1, &count);
	if (pkcs11_rc != CKR_OK) {
		(void) fprintf(stderr,
		    gettext("ikecert: C_FindObjects error # %d.\n"),
		    pkcs11_rc);
		goto bail;
	}

	if (count == 0) {
		target = NULL;
	}

bail:
	pkcs11_rc = p11f->C_FindObjectsFinal(session);
	if (pkcs11_rc != CKR_OK) {
		(void) fprintf(stderr,
		    gettext("ikecert: C_FindObjectsFinal error # %d.\n"),
		    pkcs11_rc);
		return (NULL);
	}

	return (target);
}

/*
 * Find and remove a PKCS#11 object identified by the label, class
 * (public/private/cert) and type (RSA/DSA/X.509).
 */
void
find_and_nuke(CK_SESSION_HANDLE session, char *object_id,
    CK_OBJECT_CLASS class, CK_KEY_TYPE type, boolean_t quiet)
{
	CK_RV pkcs11_rc;
	CK_OBJECT_HANDLE target;

	target = find_object(session, object_id, class, type);
	if (target == NULL) {
		if (!quiet)
			(void) fprintf(stderr,
			    gettext("ikecert: %s object \"%s\" missing.\n"),
			    (class == CKO_CERTIFICATE ? "Certificate" :
			    (class == CKO_PUBLIC_KEY ? "Public Key" :
			    "Private Key")), object_id);
		return;
	}

	pkcs11_rc = p11f->C_DestroyObject(session, target);
	if (pkcs11_rc != CKR_OK) {
		(void) fprintf(stderr,
		    gettext("ikecert: C_DestroyObject error # %d.\n"),
		    pkcs11_rc);
		return;
	}
}

/*
 * Convert an SSH public key object into a keystore public key.  Useful if
 * there's insufficient state from previous ikecert(1m) instantiations.
 */
boolean_t
public_to_pkcs11(CK_SESSION_HANDLE session, SshPublicKey key, char *object_id)
{
	CK_ATTRIBUTE_PTR attrs;
	CK_ULONG attrcount;
	uint8_t buf1[BIGBUFSZ], buf2[BIGBUFSZ], buf3[BIGBUFSZ], buf4[BIGBUFSZ];
	CK_OBJECT_CLASS class = CKO_PUBLIC_KEY;
	CK_OBJECT_HANDLE newpub;
	char *keytype;
	CK_KEY_TYPE kt;
	CK_RV pkcs11_rc;
	CK_BBOOL token_object = TRUE;
	int rv;

	rv = ssh_public_key_get_info(key,
	    SSH_PKF_SIGN, &keytype, SSH_PKF_END);
	if (rv != SSH_CRYPTO_OK) {
		(void) fprintf(stderr,
		    gettext("ssh_public_key_info failed, rv = %d\n"), rv);
		return (B_FALSE);
	}
	kt = (*keytype == 'd') ? CKK_DSA : CKK_RSA;

	attrs = pkcs11_pubkey_attrs(&attrcount, key, object_id,
	    buf1, buf2, buf3, buf4, &class, &kt, &token_object);
	if (attrs == NULL)
		return (B_FALSE);
	pkcs11_rc = p11f->C_CreateObject(session, attrs, attrcount, &newpub);

	if (pkcs11_rc == CKR_OK) {
		return (B_TRUE);
	}

	(void) fprintf(stderr,
	    gettext("C_CreateObject failed with %d(0x%x)\n"),
	    pkcs11_rc, pkcs11_rc);
	return (B_FALSE);
}

/*
 * Convert an SSH private key object into a keystore private key.
 */
boolean_t
private_to_pkcs11(CK_SESSION_HANDLE session, SshPrivateKey key, char *object_id)
{
	CK_ATTRIBUTE_PTR attrs;
	CK_ULONG attrcount;
	uint8_t buf1[BIGBUFSZ], buf2[BIGBUFSZ], buf3[BIGBUFSZ], buf4[BIGBUFSZ];
	uint8_t buf5[BIGBUFSZ], buf6[BIGBUFSZ], buf7[BIGBUFSZ], buf8[BIGBUFSZ];
	CK_OBJECT_CLASS class = CKO_PRIVATE_KEY;
	CK_OBJECT_HANDLE newpriv;
	char *keytype;
	CK_KEY_TYPE kt;
	CK_RV pkcs11_rc;
	CK_BBOOL token_object = TRUE;
	int rv;

	rv = ssh_private_key_get_info(key,
	    SSH_PKF_SIGN, &keytype, SSH_PKF_END);
	if (rv != SSH_CRYPTO_OK) {
		(void) fprintf(stderr,
		    gettext("ssh_private_key_info failed, rv = %d\n"), rv);
		return (B_FALSE);
	}
	kt = (*keytype == 'd') ? CKK_DSA : CKK_RSA;

	attrs = pkcs11_privkey_attrs(&attrcount, key, object_id,
	    buf1, buf2, buf3, buf4, buf5, buf6, buf7, buf8, &class, &kt);
	if (attrs == NULL)
		return (B_FALSE);
	/* Add some more attributes */
	/* LINTED */
	ATTR_INIT(attrs[attrcount], CKA_PRIVATE, &token_object,
	    sizeof (CK_BBOOL));
	attrcount++;
	/* LINTED */
	ATTR_INIT(attrs[attrcount], CKA_SENSITIVE, &token_object,
	    sizeof (CK_BBOOL));
	attrcount++;
	/* LINTED */
	ATTR_INIT(attrs[attrcount], CKA_ID, object_id, strlen(object_id));
	attrcount++;
	/* LINTED */
	ATTR_INIT(attrs[attrcount], CKA_TOKEN, &token_object,
	    sizeof (CK_BBOOL));
	attrcount++;

	pkcs11_rc = p11f->C_CreateObject(session, attrs, attrcount, &newpriv);

	if (pkcs11_rc == CKR_OK) {
		return (B_TRUE);
	}

	(void) fprintf(stderr,
	    gettext("C_CreateObject failed with %d(0x%x)\n"),
	    pkcs11_rc, pkcs11_rc);
	return (B_FALSE);
}

/*
 * Log into a PKCS#11 device and establish a session.
 */
CK_SESSION_HANDLE
pkcs11_login(char *pkcs11_token_id, char *pin)
{
	CK_SESSION_HANDLE session;

	session = pkcs11_get_session(pkcs11_token_id, pin, B_TRUE);
	switch (session) {
	case PKCS11_NO_SUCH_TOKEN:
		(void) fprintf(stderr,
		    gettext("token \"%.32s\" not present.\n"), pkcs11_token_id);
		return (NULL);
	case PKCS11_OPEN_FAILED:
		(void) fprintf(stderr, gettext(
		"C_OpenSession failed for token \"%.32s\".\n"),
		    pkcs11_token_id);
		return (NULL);
	case PKCS11_LOGIN_FAILED:
		(void) fprintf(stderr, gettext(
		    "C_Login failed for token \"%.32s\".\n"), pkcs11_token_id);
		return (NULL);
	}
	return (session);
}

void
memory_bail(void)
{
	(void) fprintf(stderr, gettext("Out of memory.\n"));
	exit(1);
}

void
ssh_sign_to_keytype(char **sign_and_type)
{
	if (strstr(*sign_and_type, "dsa") != NULL)
		*sign_and_type = "dsa";
	else if (strstr(*sign_and_type, "rsa") != NULL)
		*sign_and_type = "rsa";
	else
		*sign_and_type = "bogus";
}

/*
 * Extracted from sshfileio.c:
 * This method will extract the header and footer, if any on a base64(PEM)
 * buffer.
 *
 * The difference is the original required the data be loaded from a file.
 *
 * This function assumes that the passed buffer is a valid PEM file.
 * If it is not a valid PEM file, the function frees the buffer and
 * returns NULL.  Otherwise it returns a pointer to a ber buffer and
 * leaves the original buffer intact.
 */
unsigned char *
pem_to_ber(unsigned char *buf, size_t *buf_len)
{
	unsigned char *tmp, *cp;
	size_t len, i, end, start, header, inside, skip;

	tmp = buf;

	/* Remove all before and after headers. */
	i = skip = end = start = header = inside = 0;
	for (len = *buf_len; i < len; i++) {
		switch (tmp[i]) {
		case '-':
			if (skip)
				break;

			if (inside)
				end = i;
			header = 1;
			inside ^= 1;
			skip = 1;
			break;
		case '\n':
		case '\r':
			if (header) {
				header = 0;
				if (inside)
					start = i + 1;
			}
			skip = 0;
			break;
		default:
			break;
		}
	}
	if (end == 0 && start == 0) {
		start = 0;
		end = len;
	}
	if (end == start) {
		ssh_free(tmp);
		return (NULL);
	}
	if (end <= start) {
		ssh_free(tmp);
		return (NULL);
	}

	cp = ssh_base64_remove_whitespace(tmp + start, end - start);
	tmp = ssh_base64_to_buf(cp, buf_len);
	ssh_free(cp);
	return (tmp);
}

/*
 * Export data from a buffer to the outfile.  If outfile is NULL, stdout is
 * used.  The out_fmt is the format of the output, options are ber or pem.
 */
void
export_data(unsigned char *b, size_t len, const char *outfile,
    const char *out_fmt, char *begin, char *end)
{
	if (out_fmt == NULL || (strcmp(out_fmt, "pem") == 0)) {
		if (!ssh_write_file_base64(outfile, begin, end, b, len)) {
			(void) fprintf(stderr, "%s\n",
			    gettext("Failure to extract PEM format."));
			exit(1);
		}
		if (debug)
			(void) fprintf(stderr, "%s\n",
			    gettext("Wrote in PEM format."));
	} else if (strcmp(out_fmt, "ber") == 0) {
		if (!ssh_write_file(outfile, b, len)) {
			(void) fprintf(stderr, "%s\n",
			    gettext("Unable to write to output file."));
			exit(1);
		}
		if (debug)
			(void) fprintf(stderr, "%s\n",
			    gettext("Wrote in BER format."));
	} else {
		(void) fprintf(stderr, "%s\n", gettext("Invalid Format."));
		exit(1);
	}
}



/*
 * This takes commonly used alternative subjects formats and prepends the
 * proper tag in front.  This is for the lazy person in all of us.
 * It check for valid IPv4, IPv6, Email or slotname.
 */
void
prepare_pattern(const char **pat, int count)
{
	char *s, *c;
	int i = 0, len;

	while (count > i) {
		/* LINTED */
		if (strchr(pat[i], '=') != NULL) {
			/* Do nothing here */
		} else if (((atoi(pat[i]) > 0) || (strcmp(pat[i], "0") == 0)) &&
		    (strchr(pat[i], '.') == NULL) &&
		    (strchr(pat[i], ':') == NULL)) {
			len = 6 + strlen(pat[i]);
			s = ssh_malloc(len);
			if (s == NULL)
				memory_bail();
			(void) snprintf(s, len, "SLOT=%s", pat[i]);
			pat[i] = s;
		} else if (inet_addr(pat[i]) != (in_addr_t)-1) {
			len = 4 + strlen(pat[i]);
			s = ssh_malloc(len);
			if (s == NULL)
				memory_bail();
			(void) snprintf(s, len, "IP=%s", pat[i]);
			pat[i] = s;
		} else if (((c = strchr(pat[i], ':')) != NULL) &&
		    (c + 1 != NULL) && (strchr(c + 1, ':')) != NULL) {
			len = 4 + strlen(pat[i]);
			s = ssh_malloc(len);
			if (s == NULL)
				memory_bail();
			(void) snprintf(s, len, "IP=%s", pat[i]);
			pat[i] = s;
		} else if (strchr(pat[i], '@') != NULL) {
			len = 7 + strlen(pat[i]);
			s = ssh_malloc(len);
			if (s == NULL)
				memory_bail();
			(void) snprintf(s, len, "EMAIL=%s", pat[i]);
			pat[i] = s;
		}
		i++;
	}
}


void
append(const char ***arrayp, int *countp, char *value)
{
	const char **array = *arrayp;
	int count = *countp;
	int original;
	int new;

	original = count * sizeof (char *);
	new = (count + 1) * sizeof (char *);
	array = ssh_realloc(array, original, new);
	if (array == NULL)
		memory_bail();
	array[count] = value;

	*countp = count + 1;
	*arrayp = array;
}


struct certlib_certspec *
gather_certspec(char *argv[], int argc)
{
	int i;
	struct certlib_certspec *pattern;

	if (argc == 0)
		return (0);

	pattern = ssh_malloc(sizeof (struct certlib_certspec));
	if (pattern == NULL)
		memory_bail();

	pattern->includes = NULL;
	pattern->num_includes = 0;
	pattern->excludes = NULL;
	pattern->num_excludes = 0;

	for (i = 0; i < argc; ++i) {
		if (argv[i][0] == '!')
			append(&pattern->excludes, &pattern->num_excludes,
			    argv[i]+1);
		else
			append(&pattern->includes, &pattern->num_includes,
			    argv[i]);
	}
	prepare_pattern(pattern->includes, pattern->num_includes);
	prepare_pattern(pattern->excludes, pattern->num_excludes);
	return (pattern);
}

/*
 * This function expects valid pointers to pre-allocated memory
 */
int
get_validity(struct certlib_cert *p, CK_DATE *start, CK_DATE *finish,
    SshBerTime not_before, SshBerTime not_after) {
	char holder[256];

	if (!ssh_x509_cert_get_validity(p->cert, not_before, not_after)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert validity dates.\n"));
		return (1);
	}

	/*
	 * This CK_DATE stuff is an artifact of rolled up prescriptions
	 * that were available in California for some time.  It's an array
	 * of the ASCII representations of year, month, day.
	 * Not strings, though.  My prescription ran out, so I don't
	 * understand why this was a good idea.
	 */
	(void) snprintf(holder, 5, "%4d", not_before->year);
	(void) memcpy(start->year, holder, 4);
	(void) snprintf(holder, 3, "%2d", not_before->month);
	(void) memcpy(start->month, holder, 2);
	(void) snprintf(holder, 3, "%2d", not_before->day);
	(void) memcpy(start->day, holder, 2);

	(void) snprintf(holder, 5, "%4d", not_after->year);
	(void) memcpy(finish->year, holder, 4);
	(void) snprintf(holder, 3, "%2d", not_after->month);
	(void) memcpy(finish->month, holder, 2);
	(void) snprintf(holder, 3, "%2d", not_after->day);
	(void) memcpy(finish->day, holder, 2);
	return (0);
}

int
pkcs11_migrate_cert(CK_SESSION_HANDLE session, struct certlib_cert *p,
    CK_BYTE *id, CK_BYTE *subject, size_t subject_len)
{
	int error = 0;
	CK_RV rv;
	CK_OBJECT_HANDLE object = NULL;
	CK_ATTRIBUTE atts[MAX_ATTRS];
	CK_BBOOL truevalue = TRUE;
	CK_ULONG numattrs = 0;
	CK_OBJECT_CLASS certclass = CKO_CERTIFICATE;
	CK_CERTIFICATE_TYPE certType = CKC_X_509;
	CK_BYTE *issuer = NULL;
	CK_BYTE *certificate = NULL;
	CK_BYTE *serial = NULL;
	size_t issuer_len, buflen;
	SshMPIntegerStruct sshserial;

	object = find_object(session, (char *)id, CKO_CERTIFICATE,
	    (CK_KEY_TYPE)CKC_X_509);
	if (object != NULL) {
		(void) fprintf(stderr,
		    gettext("Certificate token object with ID %s already "
		    "exists.\n"), id);
		error = 1;
		goto bail;
	}

	/* Extract information from cert structure */

	if (!ssh_x509_cert_get_issuer_name_der(p->cert, &issuer, &issuer_len)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert issuer name.\n"));
		goto bail;
	}

	/* Set attributes for the certificate template */
	p->pkcs11_id = (char *)id;

	buflen = p->datalen;
	certificate = ssh_malloc(buflen);
	if (certificate == NULL)
		memory_bail();
	(void) memcpy(certificate, p->data, buflen);

	ssh_mprz_init(&sshserial);
	if (!ssh_x509_cert_get_serial_number(p->cert, &sshserial)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert serial number.\n"));
		goto bail;
	}
	serial = (uchar_t *)ssh_mp_get_ui(&sshserial);
	ssh_mprz_clear(&sshserial);

	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_CLASS, &certclass, sizeof (certclass));
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_CERTIFICATE_TYPE, &certType,
	    sizeof (certType));
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_SUBJECT, subject,
	    subject_len);
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_ISSUER, issuer, issuer_len);
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_LABEL, id, strlen((char *)id));
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_ID, id, strlen((const char *)id));
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_VALUE, certificate, buflen);
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_TOKEN, &truevalue,
	    sizeof (truevalue));
	numattrs++;
	/* Our serial numbers are 4 bytes long */
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_SERIAL_NUMBER, &serial, 4);
	numattrs++;

	/* Stick the certificate in the pkcs11 object */

	(void) printf(gettext(
	    "Creating certificate object in PKCS#11 keystore...\n"));

	rv = p11f->C_CreateObject(session, atts, numattrs, &object);

	if (rv != CKR_OK) {
		pkcs11_error(rv, "C_CreateObject failed for cert");
		(void) fprintf(stderr, "C_CreateObject: rv = 0x%.8lX\n", rv);
		error = 1;
		goto bail;
	}

#ifdef DEBUG
	object = find_object(session, (char *)id, CKO_CERTIFICATE,
	    (CK_KEY_TYPE)CKC_X_509);
	if (object == NULL) {
		(void) fprintf(stderr,
		    gettext("Certificate token object with ID %s apparently "
		    "created but not found!\n"), id);
		error = 1;
		goto bail;
	}
#endif

bail:
	/* Can ssh_free() NULL */
	ssh_free(certificate);
	ssh_free(issuer);

	return (error);
}

int
pkcs11_migrate_pubkey(CK_SESSION_HANDLE session, struct certlib_cert *p,
    CK_BYTE *id, CK_BYTE *subject, size_t subject_len, CK_KEY_TYPE *kt,
    char **keytype, CK_DATE *start, CK_DATE *finish)
{
	int error = 0;
	CK_RV rv;
	CK_OBJECT_HANDLE object = NULL;
	CK_ATTRIBUTE atts[MAX_ATTRS];
	CK_ULONG numattrs = 0;

	if (!ssh_x509_cert_get_public_key(p->cert, &p->key)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert public key.\n"));
		error = 1;
		goto bail;
	}

	rv = ssh_public_key_get_info(p->key, SSH_PKF_SIGN, keytype,
	    SSH_PKF_END);
	if (rv != SSH_CRYPTO_OK) {
		(void) fprintf(stderr, gettext("Could not get keytype for "
		    "public key, rv = %d\n"), rv);
		error = 1;
		goto bail;
	}
	*kt = (**keytype == 'd') ? CKK_DSA : CKK_RSA;

	object = find_object(session, (char *)id, CKO_PUBLIC_KEY, *kt);
	if (object != NULL) {
		(void) fprintf(stderr,
		    gettext("Public key object with ID %s already exists.\n"),
		    id);
		error = 1;
		goto bail;
	}

	/* Create the pkcs11 public key object */

	(void) printf(gettext(
	    "Creating public key object in PKCS#11 keystore...\n"));

	if (!public_to_pkcs11(session, p->key, (char *)id)) {
		(void) fprintf(stderr, gettext("public to pkcs11 failed\n"));
		error = 1;
		goto bail;
	}

	object = find_object(session, (char *)id, CKO_PUBLIC_KEY, *kt);
	if (object == NULL) {
		(void) fprintf(stderr,
		    gettext("Public key object with ID %s apparently "
		    "created but not found!\n"), id);
		error = 1;
		goto bail;
	}

	/* Add more optional attributes */

	numattrs = 0;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_SUBJECT, subject,
	    subject_len);
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_START_DATE, start, sizeof (CK_DATE));
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_END_DATE, finish, sizeof (CK_DATE));
	numattrs++;

	rv = p11f->C_SetAttributeValue(session, object, atts, numattrs);
	if (rv != CKR_OK) {
		pkcs11_error(rv, "Non fatal error adding unrequired values");
		(void) fprintf(stderr, "Non fatal error adding optional "
		    "values: C_SetAttributeValue: rv = 0x%.8lX\n", rv);
	}
bail:
	/* Can ssh_free() NULL */
	ssh_free(p->key);

	return (error);
}

int
pkcs11_migrate_privkey(CK_SESSION_HANDLE session, struct certlib_keys *k,
    CK_BYTE *id, CK_BYTE *subject, size_t subject_len, CK_KEY_TYPE *kt,
    char **keytype, CK_DATE *start, CK_DATE *finish)
{
	int rc, error = 0;
	CK_RV rv;
	CK_OBJECT_HANDLE object = NULL;
	CK_ATTRIBUTE atts[MAX_ATTRS];
	CK_ULONG numattrs = 0;

	rc = ssh_private_key_get_info(k->key, SSH_PKF_SIGN, keytype,
	    SSH_PKF_END);
	if (rc != SSH_CRYPTO_OK) {
		(void) fprintf(stderr, gettext("Could not get keytype for "
		    "private key, rc = %d\n"), rc);
		error = 1;
		goto bail;
	}

	*kt = (**keytype == 'd') ? CKK_DSA : CKK_RSA;

	object = find_object(session, (char *)id, CKO_PRIVATE_KEY, *kt);
	if (object != NULL) {
		(void) fprintf(stderr,
		    gettext("Private key object with ID %s already exists.\n"),
		    id);
		error = 1;
		goto bail;
	}

	(void) printf(gettext(
	    "Creating private key object in PKCS#11 keystore...\n"));

	if (!private_to_pkcs11(session, k->key, (char *)id)) {
		(void) fprintf(stderr, gettext("private to pkcs11 failed\n"));
		error = 1;
		goto bail;
	}

	object = find_object(session, (char *)id, CKO_PRIVATE_KEY, *kt);
	if (object == NULL) {
		(void) fprintf(stderr,
		    gettext("Private key object with ID %s apparently "
		    "created but not found!\n"), id);
		error = 1;
		goto bail;
	}

	/* Add more optional attributes */

	numattrs = 0;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_SUBJECT, subject, subject_len);
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_START_DATE, start, sizeof (CK_DATE));
	numattrs++;
	/* LINTED */
	ATTR_INIT(atts[numattrs], CKA_END_DATE, finish, sizeof (CK_DATE));
	numattrs++;
	rv = p11f->C_SetAttributeValue(session, object, atts, numattrs);
	if (rv != CKR_OK) {
		pkcs11_error(rv, "Non fatal error adding unrequired values");
		(void) fprintf(stderr, "Non fatal error adding optional "
		    "values: C_SetAttributeValue: rv = 0x%.8lX\n", rv);
		error = 1;
	}
bail:
	return (error);
}

int
pkcs11_migrate_keypair(struct certlib_cert *p, boolean_t hasprivate,
    boolean_t write_pin)
{
	char pin[256] = {'\0'};
	int error = 0;
	CK_RV rv;
	CK_SESSION_HANDLE session = NULL;
	CK_BYTE *subject = NULL;
	CK_BYTE *id;
	char *pubkeytype;
	char *privkeytype;
	CK_KEY_TYPE pubkt;
	CK_KEY_TYPE privkt;
	size_t subject_len;
	SshBerTimeStruct not_before, not_after;
	CK_DATE start;
	CK_DATE finish;
	boolean_t haspublic = B_FALSE;

	/* Log into token and get PKCS#11 session */

	session = pkcs11_login(pkcs11_token_id, pin);
	if (session == NULL)
		return (1);

	/* This id will be the PKCS#11 identifier for all objects */
	id = (unsigned char *)p->subject_name;

	/* Extract necessary common elements */
	if (!ssh_x509_cert_get_subject_name_der(p->cert, &subject,
	    &subject_len)) {
		(void) fprintf(stderr, gettext(
		    "Couldn't get cert subject name.\n"));
		error = 1;
		goto exit_session;
	}

	rv = get_validity(p, &start, &finish, &not_before, &not_after);
	if (rv != 0) {
		error = 1;
		goto exit_session;
	}

	rv = pkcs11_migrate_cert(session, p, id, subject, subject_len);
	if (rv != 0)
		error = 1;
	else
		haspublic = B_TRUE;

	/*
	 * Only try to migrate the public key if the cert object
	 * migration was successful
	 */
	if (error == 0) {
		rv = pkcs11_migrate_pubkey(session, p, id, subject,
		    subject_len, &pubkt, &pubkeytype, &start, &finish);
		if (rv != 0)
			error = 1;
		/*
		 * We need to at least have the keytype to be able
		 * to write a keyfile.  We should have this from going
		 * through the motions, even if the actual migration
		 * failed.  If not, don't try to write the file and
		 * attempt to remove the certificate we just migrated.
		 */
		if (haspublic && (*pubkeytype != 'r') && (*pubkeytype != 'd')) {
			CK_OBJECT_HANDLE object = NULL;

			haspublic = B_FALSE;
			object = find_object(session, (char *)id,
			    CKO_CERTIFICATE, (CK_KEY_TYPE)CKC_X_509);
			if (object != NULL) {
				(void) p11f->C_DestroyObject(session, object);
			}
		}
	}

	/* If there is a private key, migrate it */

	if (hasprivate) {
		rv = pkcs11_migrate_privkey(session, p->keys, id, subject,
		    subject_len, &privkt, &privkeytype, &start, &finish);
		if (rv != 0) {
			error = 1;
			/* Don't write a hint later file if this failed */
			hasprivate = B_FALSE;
		} else {
			if (!haspublic)
				pubkeytype = privkeytype;
		}

	}

	if (!haspublic && !hasprivate)
		goto exit_session;

	/* Get a scheme suitable for the hint file */
	ssh_sign_to_keytype(&pubkeytype);

	/*
	 * For this particular mode of migration, we can assume the same
	 * keytype for public and private keys and thus the same scheme.
	 */

	if (write_pkcs11_files(pkcs11_token_id, pin, (char *)id, pubkeytype,
	    haspublic, hasprivate, write_pin)) {
		(void) printf(gettext("To remove the originals, run the "
		    "following:\n"));
		if (haspublic) {
			(void) printf(gettext("Certificate:\n"));
			(void) printf("\t# ikecert certdb -r SLOT=%s\n",
			    p->slotname);
		}
		if (hasprivate) {
			(void) printf(gettext("Private key:\n"));
			(void) printf("\t# ikecert certlocal -r SLOT=%s\n",
			    p->keys->slotname);
		}
	} else {
		(void) printf(gettext("PKCS#11 hint file(s) could not be "
		    "written to disk.\n"));
		error = 1;
	}

exit_session:
	(void) p11f->C_CloseSession(session);

exit_function:
	(void) p11f->C_Finalize(NULL_PTR);

	/* Can ssh_free() NULL */
	ssh_free(subject);

	return (error);
}


/*
 * Write a PKCS#11 hint file.
 * Public and private key hint files have the same format
 */
boolean_t
write_pkcs11_hint(int slotfd, char *token_id, char *pin, char *dname,
    char *keytype, boolean_t write_pin) {
	char newline = '\n';

#define	do_write(fd, buf, sz) if (write(fd, buf, sz) == -1) { \
	    perror(gettext("error writing pkcs#11 hint file")); \
	    return (B_FALSE); }

	if (slotfd >= 0) {
		/* Write information to the private key file if needed. */
		do_write(slotfd, token_id, PKCS11_TOKSIZE);
		do_write(slotfd, &newline, 1);
		if (write_pin)
			do_write(slotfd, pin, strlen(pin));
		do_write(slotfd, &newline, 1);
		do_write(slotfd, dname, strlen(dname));
		do_write(slotfd, &newline, 1);
		do_write(slotfd, keytype, strlen(keytype));
		do_write(slotfd, &newline, 1);
	}
#undef do_write
	return (B_TRUE);
}

/*
 * Create PKCS#11 IKE placeholder files
 *
 */
boolean_t
write_pkcs11_files(char *token_id, char *pin, char *dname,
    char *keytype, boolean_t public, boolean_t private, boolean_t write_pin)
{
	char file_cert[MAXPATHLEN];
	char file_priv[MAXPATHLEN];
	int priv_fd, pub_fd, i = 0;
	boolean_t success_status = B_TRUE;

	if (public) {
		/* Get available slot number for certificate file */
		(void) snprintf(file_cert, MAXPATHLEN, "%s%d",
		    certlib_certs_dir(), i);

		/*
		 * These files are public, but have cleartext pins.
		 * Keep permissions locked down.
		 */
		while ((pub_fd = open(file_cert, O_CREAT|O_EXCL|O_WRONLY,
		    S_IRUSR|S_IWUSR)) < 0) {
			if (errno == EEXIST) {
				(void) snprintf(file_cert, MAXPATHLEN, "%s%d",
				    certlib_certs_dir(), ++i);
			} else {
				perror(gettext("Unable to open public "
				    "certificate database."));
			exit(1);
			}
		}
		if (!write_pkcs11_hint(pub_fd, token_id, pin, dname, keytype,
		    write_pin))
			success_status = B_FALSE;
		(void) close(pub_fd);
	}

	if (private) {
		/* Get available slot number for key file */
		i = 0;
		(void) snprintf(file_priv, MAXPATHLEN, "%s%d",
		    certlib_keys_dir(), i);
		while ((priv_fd = open(file_priv, O_CREAT|O_EXCL|O_WRONLY,
		    S_IRUSR|S_IWUSR)) < 0) {
			if (errno == EEXIST) {
				(void) snprintf(file_priv, MAXPATHLEN, "%s%d",
				    certlib_keys_dir(), ++i);
			} else {
				perror(gettext(
				    "Unable to open private-key database."));
				exit(1);
			}
		}
		if (!write_pkcs11_hint(priv_fd, token_id, pin, dname, keytype,
		    write_pin))
			success_status = B_FALSE;
		(void) close(priv_fd);
	}
	return (success_status);
}
