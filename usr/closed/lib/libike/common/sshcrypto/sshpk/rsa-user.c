/*
  File: rsa-user.c

  Authors:
        Mika Kojo <mkojo@ssh.fi>

  Description:
        Description for how to make RSA keys

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved.
*/

#include "sshincludes.h"
#include "sshpk_i.h"
#include "sshhash_i.h"
#include "rsa.h"
#include "sshrgf.h"
#include "sshcrypt.h"

#ifdef WITH_RSA
const SshPkSignature ssh_if_modn_signature_schemes[] =
  {
#ifdef SSHDIST_CRYPT_SHA
    { "rsa-pkcs1-sha1",



      0,
      &ssh_rgf_pkcs1_sha1_def,
      ssh_rsa_private_key_max_signature_input_len,
      ssh_rsa_private_key_max_signature_output_len,
      ssh_rsa_public_key_verify,
      NULL_FNPTR,
      ssh_rsa_private_key_sign,
      NULL_FNPTR },
#endif /* SSHDIST_CRYPT_SHA */
#ifdef SSHDIST_CRYPT_MD5
    { "rsa-pkcs1-md5",
      0,
      &ssh_rgf_pkcs1_md5_def,
      ssh_rsa_private_key_max_signature_input_len,
      ssh_rsa_private_key_max_signature_output_len,
      ssh_rsa_public_key_verify,
      NULL_FNPTR,
      ssh_rsa_private_key_sign,
      NULL_FNPTR },
#endif /* SSHDIST_CRYPT_MD5 */
#ifdef SSHDIST_CRYPT_MD2
    { "rsa-pkcs1-md2",
      0,
      &ssh_rgf_pkcs1_md2_def,
      ssh_rsa_private_key_max_signature_input_len,
      ssh_rsa_private_key_max_signature_output_len,
      ssh_rsa_public_key_verify,
      NULL_FNPTR,
      ssh_rsa_private_key_sign,
      NULL_FNPTR },
#endif /* SSHDIST_CRYPT_MD2 */
    { "rsa-pkcs1-none",
      0,
      &ssh_rgf_pkcs1_none_def,
      ssh_rsa_private_key_max_signature_unhash_input_len,
      ssh_rsa_private_key_max_signature_output_len,
      ssh_rsa_public_key_verify,
      NULL_FNPTR,
      ssh_rsa_private_key_sign,
      NULL_FNPTR },
    { NULL }
  };

/* Table of all supported encryption schemes for if-modn keys. */

/* XXX FIPS rsa encryption schemes need to be checked whether they
   conform to the FIPS requirements and set the certification status
   flag accordingly */

const SshPkEncryption ssh_if_modn_encryption_schemes[] =
  {
#ifdef SSHDIST_CRYPT_SHA
    { "rsa-pkcs1v2-oaep",
      0,
      &ssh_rgf_pkcs1v2_sha1_def,
      ssh_rsa_private_key_max_decrypt_input_len,
      ssh_rsa_private_key_max_decrypt_output_len,
      ssh_rsa_private_key_decrypt,
      NULL_FNPTR,
      ssh_rsa_public_key_max_oaep_encrypt_input_len,
      ssh_rsa_public_key_max_encrypt_output_len,
      ssh_rsa_public_key_encrypt,
      NULL_FNPTR
    },
    { "rsa-pkcs1-none",
      0,
      &ssh_rgf_pkcs1_sha1_def,
      ssh_rsa_private_key_max_decrypt_input_len,
      ssh_rsa_private_key_max_decrypt_output_len,
      ssh_rsa_private_key_decrypt,
      NULL_FNPTR,
      ssh_rsa_public_key_max_encrypt_input_len,
      ssh_rsa_public_key_max_encrypt_output_len,
      ssh_rsa_public_key_encrypt,
      NULL_FNPTR
    },
#endif /* SSHDIST_CRYPT_SHA */
    { "rsa-none-none",
      0,
      &ssh_rgf_dummy_def,
      ssh_rsa_private_key_max_decrypt_input_len,
      ssh_rsa_private_key_max_decrypt_output_len,
      ssh_rsa_private_key_decrypt,
      NULL_FNPTR,
      ssh_rsa_public_key_max_none_encrypt_input_len,
      ssh_rsa_public_key_max_encrypt_output_len,
      ssh_rsa_public_key_encrypt,
      NULL_FNPTR
    },
    { NULL }
  };

/* RSA special actions. */
const SshPkAction ssh_pk_if_modn_actions[] =
  {
    /* key type */
    { SSH_PKF_KEY_TYPE,
      SSH_PK_ACTION_FLAG_KEY_TYPE | SSH_PK_ACTION_FLAG_PRIVATE_KEY |
      SSH_PK_ACTION_FLAG_PUBLIC_KEY | SSH_PK_ACTION_FLAG_PK_GROUP,
      0, NULL_FNPTR, 0, NULL_FNPTR },

    /* Handling of RSA parameters. Assuming SshMPInteger input. */
    { SSH_PKF_PRIME_P,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY,
      0, ssh_rsa_action_private_key_put,





      0, ssh_rsa_action_private_key_get },

    { SSH_PKF_PRIME_Q,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY,
      0, ssh_rsa_action_private_key_put,





      0, ssh_rsa_action_private_key_get },

    { SSH_PKF_MODULO_N,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY,
      0, ssh_rsa_action_private_key_put,
      0, ssh_rsa_action_private_key_get },

    { SSH_PKF_MODULO_N,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PUBLIC_KEY,
      0, ssh_rsa_action_public_key_put,
      0, ssh_rsa_action_public_key_get },

    { SSH_PKF_SECRET_D,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY,
      0, ssh_rsa_action_private_key_put,





      0, ssh_rsa_action_private_key_get },

    { SSH_PKF_INVERSE_U,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY,
      0, ssh_rsa_action_private_key_put,





      0, ssh_rsa_action_private_key_get },

    { SSH_PKF_PUBLIC_E,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY,
      0, ssh_rsa_action_private_key_put,
      0, ssh_rsa_action_private_key_get },

    { SSH_PKF_PUBLIC_E,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PUBLIC_KEY,
      0, ssh_rsa_action_public_key_put,
      0, ssh_rsa_action_public_key_get },

    /* Use some explicit size. Also get size (in bits) from private and
       public keys. */
    { SSH_PKF_SIZE,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY,
      0, ssh_rsa_action_private_key_put,
      0, ssh_rsa_action_private_key_get },

    { SSH_PKF_SIZE,
      SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PUBLIC_KEY,
      0, NULL_FNPTR,
      0, ssh_rsa_action_public_key_get },

    /* End of list. */
    { SSH_PKF_END }
  };


const SshPkType ssh_pk_if_modn =
  {
    "if-modn",



    0,
    ssh_pk_if_modn_actions,
    ssh_if_modn_signature_schemes,
    ssh_if_modn_encryption_schemes,
    NULL_FNPTR,

    /* No group operations available. */
    NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR,
    NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR,
    NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR,
    NULL_FNPTR,

    /* Basic public key operations. */
    ssh_rsa_public_key_init_action,
    ssh_rsa_public_key_make_action,
    ssh_rsa_private_key_init_ctx_free,

    ssh_rsa_public_key_import,
    ssh_rsa_public_key_export,
    ssh_rsa_public_key_free,
    ssh_rsa_public_key_copy,
    NULL_FNPTR, NULL_FNPTR,

    /* Basic private key operations. */
    ssh_rsa_private_key_init_action,
    ssh_rsa_private_key_define_action,
    NULL_FNPTR,
    ssh_rsa_private_key_init_ctx_free,

    ssh_rsa_private_key_import,
    ssh_rsa_private_key_export,
    ssh_rsa_private_key_free,
    ssh_rsa_private_key_derive_public_key,
    ssh_rsa_private_key_copy,
    NULL_FNPTR, NULL_FNPTR, NULL_FNPTR
  };
#endif /* WITH_RSA */
