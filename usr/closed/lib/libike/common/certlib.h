/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_LIBIKECERT_CERTLIB_H
#define	_LIBIKECERT_CERTLIB_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is a private library for internal use by in.iked(1M), certlocal(1M),
 * certdb(1M), certrldb(1M), and SunScreen.
 */

#ifdef	LIBIKE_MAKE
#include <sshincludes.h>
#include <sshmp-types.h>
#include <sshmp-integer.h>
#include <sshmp.h>
#include <x509.h>
#include <pkcs11-glue.h>
#include <sshprvkey.h>
#include <sshpkcs8.h>
#else
#include <ike/sshincludes.h>
#include <ike/sshmp-types.h>
#include <ike/sshmp-integer.h>
#include <ike/sshmp.h>
#include <ike/x509.h>
#include <ike/pkcs11-glue.h>
#include <ike/sshprvkey.h>
#include <ike/sshpkcs8.h>
#endif	/* LIBIKE_MAKE */

#define	CERTLIB_NORMAL		0
#define	CERTLIB_SUNSCREEN	1
#define	CERTLIB_DEBUG		2
#define	CERTLIB_SOLARISCRYPTO	4

#define	CERTLIB_KEYS		1
#define	CERTLIB_CRL		2
#define	CERTLIB_CERT		4
#define	CERTLIB_ALL		7
#define	CERTLIB_REQ		8

/*
 * Test for insecure permissions.  Redundant with ipsec_util.h
 * definition for now due to open source/closed source separation.
 */
#ifndef	INSECURE_PERMS
#define	INSECURE_PERMS(sbuf)	(((sbuf).st_uid != 0) || \
	((sbuf).st_mode & S_IRWXG) || ((sbuf).st_mode & S_IRWXO))
#endif

extern boolean_t certlib_interactive;
extern uchar_t *certlib_token_pin;

struct certlib_keys {
	unsigned char *data;		/* the raw file contents (BER format) */
	size_t datalen;			/* the raw file length */
	const char *slotname;		/* the slotname (aka file name) */
	struct certlib_cert *cert;	/* certificate info or NULL */
	SshPrivateKey key;		/* SSH private keys structure */
	char pkcs11_label[PKCS11_TOKSIZE];	/* PKCS#11 token label. */
	uchar_t *pkcs11_pin;		/* PKCS#11 token PIN. */
	char *pkcs11_id;		/* PKCS#11 object ID. */
	const char *type;		/* key type */
	unsigned int size;		/* key size in bits */
	/*
	 * p, q, n, e, d, u, g, x, y are the actual keys (both the public and
	 * private ones) for convenient access by the certlib user, and for
	 * use by the library in connecting keys files with certificates.
	 */
	SshMPIntegerStruct p, q;		/* values of RSA/DSA keys */
	SshMPIntegerStruct n, e, d, u;		/* values of RSA keys */
	SshMPIntegerStruct g, x, y;		/* values of DSA keys */
	struct certlib_keys *next, *prev;
};

struct certlib_crl {
	unsigned char *data;		/* the raw file contents (BER format) */
	size_t datalen;			/* the raw file length */
	const char *slotname;		/* the slotname (aka file name) */
	SshX509Crl crl;			/* SSH CRL structure */
	char *issuer_name;		/* Issuer name of CRL */
	struct certlib_crl *next, *prev;
};

struct certlib_cert {
	unsigned char *data;		/* the raw file contents (BER format) */
	size_t datalen;			/* the raw file length */
	const char *slotname;		/* the slotname (aka file name) */
	struct certlib_keys *keys;	/* private key info or NULL */
	struct certlib_crl *crl;	/* issuer's CRL info or NULL */
	void *appdata;			/* application data */
	char pkcs11_label[PKCS11_TOKSIZE];	/* PKCS#11 token label. */
	uchar_t *pkcs11_pin;		/* PKCS#11 token PIN. */
	char *pkcs11_id;		/* PKCS#11 object ID. */
	SshPublicKey key;		/* Pre-accelerated PKCS#11 key. */
	void *orig_data;		/* Parsed-in file, for keystore. */
	size_t orig_data_len;		/* Parsed-in file length. */
	SshX509Certificate cert;	/* SSH certificate structure */
	const char *type;		/* key type */
	char *subject_name;		/* Subject name of cert */
	char *issuer_name;		/* Issuer name of cert */
	struct certlib_cert *next, *prev;
};

struct certlib_certspec {
	const char **includes;
	int num_includes;
	const char **excludes;
	int num_excludes;
};

/*
 * Obtain certificate pathnames for the filesystem.
 */
extern const char *certlib_keys_dir(void);	/* for certutils use only */
extern const char *certlib_crls_dir(void);	/* for certutils use only */
extern const char *certlib_certs_dir(void);	/* for certutils use only */

/*
 * Initialize the library.  This is required before any other function is used.
 * The implementation will load all objects into memory for rapid access.
 */
extern boolean_t certlib_init(int type, int param);

/*
 * Reload the in-memory copies of the filesystem databases in case of external
 * modification (e.g. the daemon receives a SIGHUP).  Call the callcack function
 * for each deleted/modified/new certificate.
 */
extern void certlib_refresh(int (*func)(struct certlib_cert *certp));

/*
 * Iterate through the entire list of public certificates, calling the callback
 * function for each one.
 */
extern void certlib_iterate_certs(int (*func)(struct certlib_cert *certp));

/*
 * Iterate through the entire list of public certificates, calling the callback
 * function for each one.  Returns number of successes.
 * Does not exit on intermediary failure.
 */
extern int certlib_iterate_certs_count(int (*func)(struct certlib_cert *certp));

/*
 * Iterate through the list of public certificates, calling the callback
 * function for each one.  Return on first success with the exit status
 * of the callback function.  A callback function is considered a non-match
 * if it returns 0.
 */
extern int
certlib_iterate_certs_first_match(int (*func)(struct certlib_cert *));

/*
 * Get the next cert, if it exists
 */

extern boolean_t certlib_next_cert(struct certlib_cert **certp);

/*
 * Iterate through the entire list of key pairs, calling the callback function
 * for each one.
 */
extern void certlib_iterate_keys(int (*func)(struct certlib_keys *keysp));

/*
 * Iterate through the entire list of key pairs, calling the callback function
 * for each one.  Returns number of successes.
 * Does not exit on intermediary failure.
 */
extern int certlib_iterate_keys_count(int (*func)(struct certlib_keys *keysp));

/*
 * Iterate through the list of private keys, calling the callback
 * function for each one.  Return on first success with the exit status
 * of the callback function.  A callback function is considered a non-match
 * if it returns 0.
 */
extern int
certlib_iterate_keys_first_match(int (*func)(struct certlib_keys *));

/*
 * Iterate through the entire list of CRLs, calling the callback function for
 * each one.
 */
extern void certlib_iterate_crls(int (*func)(struct certlib_crl *crlp));

/*
 * Test whether the given certificate matches the given certspec.
 */
extern int certlib_match_cert(const struct certlib_cert *certp,
    const struct certlib_certspec *certspec);

/*
 * Find all certificates that match the given certspec, calling the callback
 * function for each one.  Stop early if func returns non-zero.
 */
extern void certlib_find_cert_spec(const struct certlib_certspec *certspec,
    int (*func)(struct certlib_cert *certp));

/*
 * Find all locally stored (never from LDAP) certificates that match the given
 * certspec, calling the callback function for each one.  This is not  limited
 * to those that represent local identities; the term local in this function's
 * name refers to local filesystem storage.  Stop early if func returns
 * non-zero.
 */
extern void certlib_find_local_cert_spec(
    const struct certlib_certspec *certspec,
    int (*func)(struct certlib_cert *certp));

/*
 * Find a certificate that matches the given certspec and that represents a
 * local identity (has a corresponding entry in the private key database).
 */
extern struct certlib_cert *certlib_find_local_ident(
    const struct certlib_certspec *certspec);

/*
 * Find a pkcs11 certificate that matches the given certspec and that
 * represents a local pkcs11 identity (has a corresponding entry in the
 * private key database).
 */
extern struct certlib_cert *certlib_find_local_pkcs11_ident(
    const struct certlib_certspec *certspec);

/*
 * Find a local pkcs11 certificate that matches the given certspec
 */
extern struct certlib_cert *certlib_find_local_pkcs11_cert(
    const struct certlib_certspec *certspec);

/*
 * Find all CRLs that match the given certspec, calling the callback function
 * for each one.  Stop early if func returns non-zero.
 */
extern void certlib_find_crl_spec(const struct certlib_certspec *certspec,
    int (*func)(struct certlib_crl *crlp));

/*
 * Certspec support functions for in.iked's use as well as certlib's.
 */

/* Get a certspec-style pattern from an X.509 certificate. */
int certlib_get_x509_pattern(SshX509Certificate cert, char ***cert_array);

/* Publicly expose the certspec-style clear function. */
void certlib_clear_cert_pattern(char **cert_pattern, int cert_count);

/* Read a "keystore" hint file's contents into easy-to-chew tokens. */
void parse_pkcs11_file(uchar_t *, size_t, char **, char **, char **, char **);

/* Pre-accelerate a "keystore" key. */
boolean_t pre_accelerate_key(void *, CK_OBJECT_CLASS);

/* Convert a key buffer to an SSH proprietary key buffer */
extern SshSKBType buf_to_privkey_blob(uchar_t *, size_t, uchar_t **, size_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBIKECERT_CERTLIB_H */
