/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 * 
 * All rights reserved.
 * 
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "k5-int.h"
#include "hash_provider.h"
#include "keyhash_provider.h"
#include "cksumtypes.h"

/* Solaris Kerbros */
struct krb5_cksumtypes krb5int_cksumtypes_list[] = {
    { CKSUMTYPE_CRC32, CKSUM_NOT_COLL_PROOF,
      "crc32", "CRC-32",
      NULL, NULL, &krb5int_hash_crc32, 0,
#ifdef _KERNEL
      NULL,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
},

    /* Solaris Kerberos: RSA-MD4 not supported */
#if 0 /************** Begin IFDEF'ed OUT *******************************/
    { CKSUMTYPE_RSA_MD4,
      "md4", { 0 }, "RSA-MD4",
      NULL, &krb5int_hash_md4,
      krb5int_unkeyed_checksum, NULL,
      16, 16, KRB5_CKSUMFLAG_DERIVE },

    { CKSUMTYPE_RSA_MD4_DES,
      "md4-des", { 0 }, "RSA-MD4 with DES cbc mode",
      &krb5int_enc_des, &krb5int_hash_md4,
      krb5int_confounder_checksum, krb5int_confounder_verify,
      24, 24, 0 },
#endif /**************** END IFDEF'ed OUT *******************************/

    { CKSUMTYPE_DESCBC, 0,
      "des-cbc", "DES cbc mode",
      ENCTYPE_DES_CBC_CRC, &krb5int_keyhash_descbc,
      NULL,  NULL,
#ifdef _KERNEL
      NULL,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
},

    { CKSUMTYPE_RSA_MD5, 0,
      "md5", "RSA-MD5",
      NULL, NULL, &krb5int_hash_md5, 0,
#ifdef _KERNEL
      SUN_CKM_MD5,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
},
    { CKSUMTYPE_RSA_MD5_DES, 0,
      "md5-des", "RSA-MD5 with DES cbc mode",
      ENCTYPE_DES_CBC_CRC, &krb5int_keyhash_md5des,
      NULL, NULL,
#ifdef _KERNEL
      SUN_CKM_MD5,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
},

    { CKSUMTYPE_NIST_SHA, 0,
      "sha", "NIST-SHA",
      NULL, NULL, &krb5int_hash_sha1, 0,
#ifdef _KERNEL
      SUN_CKM_SHA1,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
},

    { CKSUMTYPE_HMAC_SHA1_DES3, KRB5_CKSUMFLAG_DERIVE,
      "hmac-sha1-des3", "HMAC-SHA1 DES3 key",
      NULL, NULL, &krb5int_hash_sha1, 0,
#ifdef _KERNEL
      SUN_CKM_SHA1_HMAC,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
 },
    { CKSUMTYPE_HMAC_SHA1_DES3, KRB5_CKSUMFLAG_DERIVE,
      "hmac-sha1-des3-kd", "HMAC-SHA1 DES3 key", /* alias */
      NULL, NULL, &krb5int_hash_sha1, 0,
#ifdef _KERNEL
      SUN_CKM_SHA1_HMAC,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
},
    { CKSUMTYPE_HMAC_MD5_ARCFOUR, 0,
      "hmac-md5-rc4", "Microsoft HMAC MD5 (RC4 key)", 
      ENCTYPE_ARCFOUR_HMAC, &krb5int_keyhash_hmac_md5,
      NULL, 0,
#ifdef _KERNEL
      SUN_CKM_MD5,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
    },
    { CKSUMTYPE_HMAC_MD5_ARCFOUR, 0,
      "hmac-md5-enc", "Microsoft HMAC MD5 (RC4 key)",  /*Heimdal alias*/
      ENCTYPE_ARCFOUR_HMAC, &krb5int_keyhash_hmac_md5,
      NULL, 0,
#ifdef _KERNEL
      SUN_CKM_MD5,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
    },
    { CKSUMTYPE_HMAC_MD5_ARCFOUR, 0,
      "hmac-md5-earcfour", "Microsoft HMAC MD5 (RC4 key)",  /* alias*/
      ENCTYPE_ARCFOUR_HMAC, &krb5int_keyhash_hmac_md5,
      NULL, 0,
#ifdef _KERNEL
      SUN_CKM_MD5,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
    },

    { CKSUMTYPE_HMAC_SHA1_96_AES128, KRB5_CKSUMFLAG_DERIVE,
      "hmac-sha1-96-aes128", "HMAC-SHA1 AES128 key",
	NULL, NULL, &krb5int_hash_sha1, 12,
#ifdef _KERNEL
      SUN_CKM_SHA1_HMAC,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
    },
    { CKSUMTYPE_HMAC_SHA1_96_AES256, KRB5_CKSUMFLAG_DERIVE,
      "hmac-sha1-96-aes256", "HMAC-SHA1 AES256 key",
	0, NULL, &krb5int_hash_sha1, 12,
#ifdef _KERNEL
      SUN_CKM_SHA1_HMAC,
      CRYPTO_MECH_INVALID
#endif /* _KERNEL */
    }

};

const int krb5int_cksumtypes_length =
    sizeof(krb5int_cksumtypes_list)/sizeof(struct krb5_cksumtypes);

/* Solaris Kerberos */
#ifdef _KERNEL
void
setup_kef_cksumtypes()
{
	int i;
	struct krb5_cksumtypes *ck;

	for (i=0; i<krb5int_cksumtypes_length; i++) {
		ck = (struct krb5_cksumtypes *)&krb5int_cksumtypes_list[i];
		if (ck != NULL &&
		    ck->mt_c_name != NULL &&
		    ck->kef_cksum_mt == CRYPTO_MECH_INVALID) {

			ck->kef_cksum_mt = crypto_mech2id(ck->mt_c_name);
		}
	}
}
#endif /* _KERNEL */
