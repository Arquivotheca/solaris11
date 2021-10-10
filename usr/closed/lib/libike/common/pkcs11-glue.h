/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_LIBIKE_PKCS11_GLUE_H
#define	_LIBIKE_PKCS11_GLUE_H

#include <ike/sshincludes.h>
#include <ike/sshcrypt.h>
#include <ike/sshpk_i.h>
#include <ike/sshproxykey.h>
#include <ike/sshrgf.h>
#include <ike/sshmp.h>
#include <ike/dlglue.h>

#include <security/cryptoki.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	PKCS11_TOKSIZE 32	/* Fixed length of PKCS#11 token string len. */
#define	PKCS11_OIDSIZE 16	/* Max size oid we have to cope with */

typedef struct pkcs11_inst_s {
	/* NOTE: p11i_session could become a vector indexed by thread id. */
	CK_SESSION_HANDLE p11i_session;
	char p11i_token_label[PKCS11_TOKSIZE];
	char *p11i_pin;
	uint_t p11i_refcnt;
	uint_t p11i_flags;
} pkcs11_inst_t;

/* PKCS#11 instance flags. */
#define	P11F_DH		0x1	/* Native Diffie-Hellman support. */
#define	P11F_DH_RSA	0x2	/* Diffie-Hellman via raw RSA support. */
#define	P11F_RSA	0x4	/* RSA signatures/encryption support. */
#define	P11F_DSA	0x8	/* DSA signatures support. */
#define	P11F_LOGIN	0x10	/* The session is logged in. */
#define	P11F_ECDHP	0x20	/* EC GF[P] Diffie-Hellman support. */

typedef struct pkcs11_key_s {
	pkcs11_inst_t *p11k_p11i;

	union {
		SshPublicKey p11k_su_pubkey;
		SshPrivateKey p11k_su_privkey;
	} p11k_su;
#define	p11k_pubkey p11k_su.p11k_su_pubkey
#define	p11k_privkey p11k_su.p11k_su_privkey
	union {
		SshPublicKey p11k_osu_pubkey;
		SshPrivateKey p11k_osu_privkey;
	} p11k_osu;
#define	p11k_origpub p11k_osu.p11k_osu_pubkey
#define	p11k_origpriv p11k_osu.p11k_osu_privkey
	/* Any PKCS#11 keying info would be good here. */
	CK_OBJECT_HANDLE p11k_p11obj;
#define	p11k_p11pub p11k_p11obj
#define	p11k_p11priv p11k_p11obj
	/* Misc. goodies that should be cached. */
	uint_t p11k_bufsize;	/* For allocating signature storage, etc. */
} pkcs11_key_t;

#define	BIGINT_BUFLEN 8192

typedef struct pkcs11_group_s {
	pkcs11_inst_t *p11g_p11i;
	SshPkGroup p11g_group;
	/* PKCS#11 structures. */
	CK_ATTRIBUTE_PTR p11g_attrs;	/* Attribute list to create RSA key. */
	CK_OBJECT_CLASS p11g_class;	/* Attribute value... */
	CK_KEY_TYPE p11g_keytype;	/* Key type. */
	CK_BBOOL p11g_true;	/* "True" value used for attribute list. */
#define	p11g_use_rsa p11g_true	/*  will be "false" if using native D-H. */
	CK_ULONG p11g_attrcount;
	/* Misc. goodies that should be cached. */
	uint8_t p11g_g[BIGINT_BUFLEN];
	uint8_t p11g_n[BIGINT_BUFLEN]; /* Oakley prime N, currently either. */
				/* 768, 1024, 1536, 2048, 3072, or 4096 bits. */
	uint_t p11g_bufsize;	/* For length of p11g_n. */
	uint_t p11g_gsize;	/* For length of p11g_g. */
} pkcs11_group_t;

typedef struct pkcs11_ecp_group_s {
	pkcs11_inst_t *p11ecpg_p11i;
	CK_ATTRIBUTE_PTR p11ecpg_attrs;
	uint8_t p11ecpg_oid[PKCS11_OIDSIZE];
	uint_t p11ecpg_attrcount;
	uint_t p11ecpg_oidsz;
	uint_t p11ecpg_gsize;
	uint_t p11ecpg_bytes;
} pkcs11_ecp_group_t;

typedef struct pkcs11_state_s {
	pkcs11_inst_t **p11s_p11is;
	int p11s_numinst;
} pkcs11_state_t;

#define	P11I_REFHOLD(p11i) ((p11i)->p11i_refcnt)++

void p11i_free(pkcs11_inst_t *);

#define	P11I_REFRELE(p11i) do { \
		if (--((p11i)->p11i_refcnt) == 0) p11i_free(p11i); } \
		while (0)

/* WARNING:  "attr" should not be something like foo++ */
#define	ATTR_INIT(a, ntype, ptr, sz)  do { (a).type = (ntype); \
	(a).pValue = (ptr); (a).ulValueLen = (sz); } while (0)
#define	MAX_ATTRS 20


CK_FUNCTION_LIST_PTR pkcs11_setup(char *);
void pkcs11_error(CK_RV, char *);
pkcs11_inst_t *find_p11i_slot(char *);
pkcs11_inst_t *find_p11i_flags(uint_t);
void print_pkcs11_slots(void);
CK_ATTRIBUTE_PTR pkcs11_pubkey_attrs(CK_ULONG_PTR,
    SshPublicKey, char *, uint8_t *, uint8_t *, uint8_t *, uint8_t *,
    CK_OBJECT_CLASS *, CK_KEY_TYPE *, CK_BBOOL *);
boolean_t extract_pkcs11_public(CK_SESSION_HANDLE, CK_OBJECT_HANDLE,
    SshMPIntegerStruct *, CK_KEY_TYPE, CK_ATTRIBUTE_TYPE);
CK_ATTRIBUTE_PTR pkcs11_privkey_attrs(CK_ULONG_PTR,
    SshPrivateKey, char *, uint8_t *, uint8_t *,
    uint8_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *,
    uint8_t *, CK_OBJECT_CLASS *, CK_KEY_TYPE *);


CK_SESSION_HANDLE pkcs11_get_session(char *, char *, boolean_t);

/* Put a string into a PKCS#11 token ID. */
void pkcs11_pad_out(char *, char *);

void pkcs11_public_key_free(void *);
void pkcs11_private_key_free(void *);
void pkcs11_dh_free(void *);
void pkcs11_ecp_free(void *);
SshOperationHandle pkcs11_public_key_dispatch(SshProxyOperationId,
    SshProxyRGFId, SshProxyKeyHandle, const uint8_t *, size_t,
    SshProxyReplyCB, void *, void *);
SshOperationHandle pkcs11_private_key_dispatch(SshProxyOperationId,
    SshProxyRGFId, SshProxyKeyHandle, const uint8_t *, size_t,
    SshProxyReplyCB, void *, void *);
SshOperationHandle pkcs11_dh_dispatch(SshProxyOperationId,
    SshProxyRGFId, SshProxyKeyHandle, const uint8_t *, size_t,
    SshProxyReplyCB, void *, void *);
SshOperationHandle pkcs11_ecp_dispatch(SshProxyOperationId,
    SshProxyRGFId, SshProxyKeyHandle, const uint8_t *, size_t,
    SshProxyReplyCB, void *, void *);

/*
 * pkcs11_convert_{public,private}() are subfunctions for certlib.c's
 * pre_accelerate_key(), so they return PKCS#11 objects.
 */
CK_OBJECT_HANDLE pkcs11_convert_public(pkcs11_inst_t *, CK_KEY_TYPE,
    SshPublicKey, char *);
CK_OBJECT_HANDLE pkcs11_convert_private(pkcs11_inst_t *, CK_KEY_TYPE,
    SshPrivateKey, char *);

/*
 * pkcs11_convert_group(), OTOH, is for internal use only, so it returns the
 * proxy-key converted SshPkGroup object.
 */
SshPkGroup pkcs11_convert_group(pkcs11_inst_t *, SshPkGroup);

/*
 * pkcs11_generate_ecp() is for ECP groups, which have no native libike
 * support, but if PKCS#11 supports it, make it happen.
 */
SshPkGroup pkcs11_generate_ecp(pkcs11_inst_t *, int);

/*
 * Take an SshMPInteger and get the size base b rounded up to the next
 * divisor d.  SafeNet functions truncate leading zeros in their
 * calculations.
 * This version assumes d is a power of 2.
 * 'b' is a number-carried-per-unit passed directly into ssh_mprz_get_size().
 * (e.g. b == 256 if you want the size in bytes, b == 2 for bits.)
 */
#define	SSH_GET_ROUNDED_SIZE(i, b, d) \
	(((ssh_mprz_get_size(i, b) - 1) & ~((d) - 1)) + (d))

/*
 * Other return values for pkcs11_get_session.
 * Assume -1, -2, -3 are not == NULL AND are invalid pointers.
 * (NOTE that this is dangerous with PKCS#11.)
 */
#define	PKCS11_OPEN_FAILED ((CK_SESSION_HANDLE)-1)
#define	PKCS11_LOGIN_FAILED ((CK_SESSION_HANDLE)-2)
#define	PKCS11_NO_SUCH_TOKEN ((CK_SESSION_HANDLE)-3)

extern CK_FUNCTION_LIST_PTR p11f;

#define	DSA_BUFSIZE 40	/* 2 times SHA-1 hash length. */

extern const char dl_modp[];
extern const char if_modn[];

extern CK_MECHANISM_PTR rsa_pkcs1;
extern CK_MECHANISM_PTR rsa_raw;
extern CK_MECHANISM_PTR dsa;
extern CK_MECHANISM_PTR dh_generate;
extern CK_MECHANISM_PTR ecp_generate;
/*
 * Missing from the previous list of mechanisms is a dh_agree mechanism.
 * These are done per-agreement, so that's why it's not here.
 */

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBIKE_PKCS11_GLUE_H */
