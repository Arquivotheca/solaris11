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

#ifndef	_DUMPUTILS_H
#define	_DUMPUTILS_H

#ifdef	__cplusplus
extern "C" {
#endif

char *dump_name(const char *);
char *dump_time(SshBerTime);
char *dump_number(const SshMPIntegerStruct *);
char *dump_public_hash(const SshMPIntegerStruct *);
char *dump_reason(SshX509ReasonFlags);
void dump_hex(unsigned char *, size_t);
void dump_names(SshX509Name);
void memory_bail(void);
void ssh_sign_to_keytype(char **);
unsigned char *pem_to_ber(unsigned char *, size_t *);
void export_data(unsigned char *, size_t, const char *, const char *, char *,
    char *);
void prepare_pattern(const char **, int);
void append(const char ***, int *, char *);
struct certlib_certspec *gather_certspec(char **, int);

/* Define PKCS#11 goodies only if the user included ike/pkcs11.h beforehand. */
#ifdef CKM_RSA_PKCS

/* PKCS#11 goodies. */
char *find_pkcs11_path(void);
boolean_t pkcs11_cert_generate(CK_SESSION_HANDLE, uint8_t *, size_t, char *,
    SshX509Certificate);
CK_OBJECT_HANDLE find_object(CK_SESSION_HANDLE, char *, CK_OBJECT_CLASS,
    CK_KEY_TYPE);
void find_and_nuke(CK_SESSION_HANDLE, char *, CK_OBJECT_CLASS, CK_KEY_TYPE,
    boolean_t);
boolean_t public_to_pkcs11(CK_SESSION_HANDLE, SshPublicKey, char *);
boolean_t private_to_pkcs11(CK_SESSION_HANDLE, SshPrivateKey, char *);
CK_SESSION_HANDLE pkcs11_login(char *pkcs11_token_id, char *pin);
int get_validity(struct certlib_cert *, CK_DATE *, CK_DATE *, SshBerTime,
    SshBerTime);
int pkcs11_migrate_cert(CK_SESSION_HANDLE, struct certlib_cert *, CK_BYTE *,
    CK_BYTE *, size_t);
int pkcs11_migrate_pubkey(CK_SESSION_HANDLE, struct certlib_cert *, CK_BYTE *,
    CK_BYTE *, size_t, CK_KEY_TYPE *, char **, CK_DATE *, CK_DATE *);
int pkcs11_migrate_privkey(CK_SESSION_HANDLE, struct certlib_keys *, CK_BYTE *,
    CK_BYTE *, size_t, CK_KEY_TYPE *, char **, CK_DATE *, CK_DATE *);
int pkcs11_migrate_keypair(struct certlib_cert *, boolean_t, boolean_t);
boolean_t write_pkcs11_hint(int, char *, char *, char *, char *, boolean_t);
boolean_t write_pkcs11_files(char *, char *, char *, char *, boolean_t,
    boolean_t, boolean_t);

extern CK_FUNCTION_LIST_PTR p11f;

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _DUMPUTILS_H */
