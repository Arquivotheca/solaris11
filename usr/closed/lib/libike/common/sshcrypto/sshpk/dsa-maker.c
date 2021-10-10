/*
  File: dsa-maker.c

  Authors:
        Mika Kojo <mkojo@ssh.fi>

  Description:
        Description for how to make DSA keys

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved.
*/


#include "sshincludes.h"
#include "sshpk_i.h"
#include "dlglue.h"
#include "sshrgf.h"
#include "sshcrypt.h"

#ifdef SSHDIST_CRYPT_DSA
extern const SshPkSignature ssh_dl_modp_signature_schemes[];
extern const SshPkEncryption ssh_dl_modp_encryption_schemes[];
#ifdef SSHDIST_CRYPT_DH
extern const SshPkDiffieHellman ssh_dl_modp_diffie_hellman_schemes[];
#endif /* SSHDIST_CRYPT_DH */

extern const SshPkAction ssh_pk_dl_modp_actions[];

const SshPkType ssh_pk_dl_modp_generator =
{
  "dl-modp",



  0,
  ssh_pk_dl_modp_actions,
  ssh_dl_modp_signature_schemes,
  ssh_dl_modp_encryption_schemes,
#ifdef SSHDIST_CRYPT_DH
  ssh_dl_modp_diffie_hellman_schemes,
#else /* SSHDIST_CRYPT_DH */
  NULL,
#endif /* SSHDIST_CRYPT_DH */

  /* Basic group operations. */
  ssh_dlp_action_init,
  ssh_dlp_param_action_make,
  ssh_dlp_action_free,

  ssh_dlp_param_import,
  ssh_dlp_param_export,
  ssh_dlp_param_free,
  ssh_dlp_param_copy,
  ssh_dlp_param_get_predefined_groups,

  /* Precomputation. */
  ssh_dlp_param_precompute,

  /* Randomizer generation. */
  ssh_dlp_param_count_randomizers,
  ssh_dlp_param_generate_randomizer,
  ssh_dlp_param_export_randomizer,
  ssh_dlp_param_import_randomizer,

  /* Public key operations. */
  ssh_dlp_action_public_key_init,
  ssh_dlp_public_key_action_make,
  ssh_dlp_action_free,

  ssh_dlp_public_key_import,
  ssh_dlp_public_key_export,
  ssh_dlp_public_key_free,
  ssh_dlp_public_key_copy,
  ssh_dlp_public_key_derive_param,

  /* Precomputation. */
  ssh_dlp_public_key_precompute,

  /* Private key operations. */
  ssh_dlp_action_init,
  ssh_dlp_private_key_action_define,
  ssh_dlp_private_key_action_generate_dsa_fips,
  ssh_dlp_action_free,

  ssh_dlp_private_key_import,
  ssh_dlp_private_key_export,
  ssh_dlp_private_key_free,
  ssh_dlp_private_key_derive_public_key,
  ssh_dlp_private_key_copy,
  ssh_dlp_private_key_derive_param,

  /* Precomputation. */
  ssh_dlp_private_key_precompute,

  /* Private key */
  NULL_FNPTR
};


#if 0
/* The old dl-modp key type, this is no longer used. */

extern const SshPkSignature ssh_dl_modp_signature_schemes_old[];

/* The key type for DSA keys, the subgroup size is 160 bits. */
const SshPkType ssh_pk_dl_modp_generator_old =
{
  "dl-modp-old",
  0,
  ssh_pk_dl_modp_actions,
  ssh_dl_modp_signature_schemes_old,
  ssh_dl_modp_encryption_schemes,
#ifdef SSHDIST_CRYPT_DH
  ssh_dl_modp_diffie_hellman_schemes,
#else /* SSHDIST_CRYPT_DH */
  NULL,
#endif /* SSHDIST_CRYPT_DH */

  /* Basic group operations. */
  ssh_dlp_action_init,
  ssh_dlp_param_action_make,
  ssh_dlp_action_free,

  ssh_dlp_param_import,
  ssh_dlp_param_export,
  ssh_dlp_param_free,
  ssh_dlp_param_copy,
  ssh_dlp_param_get_predefined_groups,

  /* Precomputation. */
  ssh_dlp_param_precompute,

  /* Randomizer generation. */
  ssh_dlp_param_count_randomizers,
  ssh_dlp_param_generate_randomizer,
  ssh_dlp_param_export_randomizer,
  ssh_dlp_param_import_randomizer,

  /* Public key operations. */
  ssh_dlp_action_public_key_init,
  ssh_dlp_public_key_action_make,
  ssh_dlp_action_free,

  ssh_dlp_public_key_import,
  ssh_dlp_public_key_export,
  ssh_dlp_public_key_free,
  ssh_dlp_public_key_copy,
  ssh_dlp_public_key_derive_param,

  /* Precomputation. */
  ssh_dlp_public_key_precompute,

  /* Private key operations. */
  ssh_dlp_action_init,
  ssh_dlp_private_key_action_define,
  ssh_dlp_private_key_action_generate_dsa_std,
  ssh_dlp_action_free,

  ssh_dlp_private_key_import,
  ssh_dlp_private_key_export,
  ssh_dlp_private_key_free,
  ssh_dlp_private_key_derive_public_key,
  ssh_dlp_private_key_copy,
  ssh_dlp_private_key_derive_param,

  /* Precomputation. */
  ssh_dlp_private_key_precompute,

  /* Private key */
  NULL_FNPTR
};

#endif



#endif /* SSHDIST_CRYPT_DSA */
