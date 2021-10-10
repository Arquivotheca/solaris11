/*
  File: ssh-rsa-maker.c

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

extern const SshPkSignature ssh_if_modn_signature_schemes[];
extern const SshPkEncryption ssh_if_modn_encryption_schemes[];
extern const SshPkAction ssh_pk_if_modn_actions[];

const SshPkType ssh_pk_if_modn_generator =
  {
    "if-modn",



    0,
    ssh_pk_if_modn_actions,
    ssh_if_modn_signature_schemes,
    ssh_if_modn_encryption_schemes,
    NULL,

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
    ssh_rsa_private_key_generate_action,
    ssh_rsa_private_key_init_ctx_free,

    ssh_rsa_private_key_import,
    ssh_rsa_private_key_export,
    ssh_rsa_private_key_free,
    ssh_rsa_private_key_derive_public_key,
    ssh_rsa_private_key_copy,
    NULL_FNPTR, NULL_FNPTR, NULL_FNPTR
  };
#endif /* WITH_RSA */

/* eof */
