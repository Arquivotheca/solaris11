/*
    Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
    All rights reserved.

    Created: Mon Oct 28 06:41:24 1996 [mkojo]

    */

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshcrypt_i.h"
#include "sshcipher_i.h"
#include "sshhash_i.h"

#ifdef SSHDIST_CRYPT_MD5
#include "md5.h"
#endif /* SSHDIST_CRYPT_MD5 */

#ifdef SSHDIST_CRYPT_SHA
#include "sha.h"
#endif /* SSHDIST_CRYPT_SHA */

#ifdef SSHDIST_CRYPT_DES
#include "des.h"
#endif /* SSHDIST_CRYPT_DES */

#ifdef SSHDIST_CRYPT_BLOWFISH
#include "blowfish.h"
#endif /* SSHDIST_CRYPT_BLOWFISH */















#ifdef SSHDIST_CRYPT_RC2
#include "rc2.h"
#endif /* SSHDIST_CRYPT_RC2 */













#ifdef SSHDIST_CRYPT_RIJNDAEL
#include "rijndael.h"
#endif /* SSHDIST_CRYPT_RIJNDAEL */






#include "nociph.h"



#ifndef KERNEL
/* These ciphers can only be used in user-mode code, not in the kernel.
   To add a cipher to be used in the kernel, you must add its object
   file to CRYPT_LNOBJS in src/ipsec/engine/Makefile.am, and move it
   outside the #ifndef KERNEL directive both here and later in this file. */

#ifdef SSHDIST_CRYPT_ARCFOUR
#include "arcfour.h"
#endif /* SSHDIST_CRYPT_ARCFOUR */













#endif /* !KERNEL */

#define SSH_DEBUG_MODULE "SshGenCiph"

/* Algorithm definitions */
static const SshCipherDefStruct ssh_cipher_algorithms[] = {
#ifdef SSHDIST_CRYPT_DES
  { "3des-ecb",



    0,
    8, { 24, 24, 24 }, ssh_des3_ctxsize, ssh_des3_init,
    ssh_des3_init_with_key_check, ssh_des3_ecb },
  { "3des-cbc",



    0,
    8, { 24, 24, 24 }, ssh_des3_ctxsize, ssh_des3_init,
    ssh_des3_init_with_key_check, ssh_des3_cbc },
  { "3des-cfb",



    0,
    8, { 24, 24, 24 }, ssh_des3_ctxsize, ssh_des3_init,
    ssh_des3_init_with_key_check, ssh_des3_cfb },
  { "3des-ofb",



    0,
    8, { 24, 24, 24 }, ssh_des3_ctxsize, ssh_des3_init,
    ssh_des3_init_with_key_check, ssh_des3_ofb },
#endif /* SSHDIST_CRYPT_DES */

#ifdef SSHDIST_CRYPT_RIJNDAEL
  /* Rijndael itself is defined in range 128..256 bits, however our
     implementation seems to accept 0 bit keys too. */
  { "rijndael-ecb", 0,
    16, { 0, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_rijndael_init, ssh_rijndael_init, ssh_rijndael_ecb },
  { "rijndael-cbc", 0,
    16, { 0, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_rijndael_init, ssh_rijndael_init, ssh_rijndael_cbc },
  { "rijndael-cfb", 0,
    16, { 0, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_rijndael_init_fb, ssh_rijndael_init_fb, ssh_rijndael_cfb },
  { "rijndael-ofb", 0,
    16, { 0, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_rijndael_init_fb, ssh_rijndael_init_fb, ssh_rijndael_ofb },
  { "rijndael-ctr", 0,
    16, { 0, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_rijndael_init_fb, ssh_rijndael_init_fb, ssh_rijndael_ctr },

  /* AES specifies three key sizes: 128, 192 and 256 bits. We specify
     the min and default as 128 bits, maximum as 256. AES init
     function then verifies that the key length is one of
     128/192/256. */

  { "aes-ecb",



    0,
    16, { 16, 16, 32 }, ssh_rijndael_ctxsize, ssh_aes_init,
    ssh_aes_init, ssh_rijndael_ecb },
  { "aes-cbc",



    0,
    16, { 16, 16, 32 }, ssh_rijndael_ctxsize, ssh_aes_init,
    ssh_aes_init, ssh_rijndael_cbc },
  { "aes-cfb",



    0,
    16, { 16, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_aes_init_fb, ssh_aes_init_fb, ssh_rijndael_cfb },
  { "aes-ofb",



    0,
    16, { 16, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_aes_init_fb, ssh_aes_init_fb, ssh_rijndael_ofb },
  { "aes-ctr",



    0,
    16, { 16, 16, 32 }, ssh_rijndael_ctxsize,
    ssh_aes_init_fb, ssh_aes_init_fb, ssh_rijndael_ctr },
#endif /* SSHDIST_CRYPT_RIJNDAEL */

#ifdef SSHDIST_CRYPT_BLOWFISH
  /* We use a 128 bit default key. */
  { "blowfish-ecb", 0,
    8, { 1, 16, 56 },
    ssh_blowfish_ctxsize, ssh_blowfish_init, ssh_blowfish_init,
    ssh_blowfish_ecb },
  { "blowfish-cbc", 0,
    8, { 1, 16, 56 },
    ssh_blowfish_ctxsize, ssh_blowfish_init, ssh_blowfish_init,
    ssh_blowfish_cbc },
  { "blowfish-cfb", 0,
    8, { 1, 16, 56 },
    ssh_blowfish_ctxsize, ssh_blowfish_init, ssh_blowfish_init,
    ssh_blowfish_cfb },
  { "blowfish-ofb", 0,
    8, { 1, 16, 56 },
    ssh_blowfish_ctxsize, ssh_blowfish_init, ssh_blowfish_init,
    ssh_blowfish_ofb },
#endif /* SSHDIST_CRYPT_BLOWFISH */


















#ifdef SSHDIST_CRYPT_DES
  { "des-ecb",



    0,
    8, { 8, 8, 8 }, ssh_des_ctxsize, ssh_des_init,
    ssh_des_init_with_key_check, ssh_des_ecb },
  { "des-cbc",



    0,
    8, { 8, 8, 8 }, ssh_des_ctxsize, ssh_des_init,
    ssh_des_init_with_key_check, ssh_des_cbc },
  { "des-cfb",



    0,
    8, { 8, 8, 8 }, ssh_des_ctxsize, ssh_des_init,
    ssh_des_init_with_key_check, ssh_des_cfb },
  { "des-ofb",



    0,
    8, { 8, 8, 8 }, ssh_des_ctxsize, ssh_des_init,
    ssh_des_init_with_key_check, ssh_des_ofb },
#endif /* SSHDIST_CRYPT_DES */













































































































#ifdef SSHDIST_CRYPT_RC2
  /* RC2 key size: 8..128 bits, let's use default 128 */
  { "rc2-ecb", 0,
    8, { 1, 16, 16 }, ssh_rc2_ctxsize, ssh_rc2_init,
    ssh_rc2_init, ssh_rc2_ecb },
  { "rc2-cbc", 0,
    8, { 1, 16, 16 }, ssh_rc2_ctxsize, ssh_rc2_init,
    ssh_rc2_init, ssh_rc2_cbc },
  { "rc2-cfb", 0,
    8, { 1, 16, 16 }, ssh_rc2_ctxsize, ssh_rc2_init,
    ssh_rc2_init, ssh_rc2_cfb },
  { "rc2-ofb", 0,
    8, { 1, 16, 16 }, ssh_rc2_ctxsize, ssh_rc2_init,
    ssh_rc2_init, ssh_rc2_ofb },

  { "rc2-128-ecb", 0,
    8, { 1, 16, 16 },
    ssh_rc2_ctxsize, ssh_rc2_128_init, ssh_rc2_128_init, ssh_rc2_ecb },
  { "rc2-128-cbc", 0,
    8, { 1, 16, 16 },
    ssh_rc2_ctxsize, ssh_rc2_128_init, ssh_rc2_128_init, ssh_rc2_cbc },
  { "rc2-128-cfb", 0,
    8, { 1, 16, 16 },
    ssh_rc2_ctxsize, ssh_rc2_128_init, ssh_rc2_128_init, ssh_rc2_cfb },
  { "rc2-128-ofb", 0,
    8, { 1, 16, 16 },
    ssh_rc2_ctxsize, ssh_rc2_128_init, ssh_rc2_128_init, ssh_rc2_ofb },
#endif /* SSHDIST_CRYPT_RC2 */




































#ifndef KERNEL
  /* The ciphers below can only be used in user-level code.  See
     the comments above for adding ciphers to the kernel. */























































#ifdef SSHDIST_CRYPT_ARCFOUR
  /* Key size information for arcfour (rc4 clone) comes mostly from
     source: it asserts that key is at least 1 byte in size, and
     during initialization at most 256 bytes of key material is
     used. So key size seems to be 8..2048 bits. Default 128. */
  { "arcfour", 0,
    1, { 1, 16, 256 },
    ssh_arcfour_ctxsize, ssh_arcfour_init,
    ssh_arcfour_init, ssh_arcfour_transform },
#endif /* SSHDIST_CRYPT_ARCFOUR */























#endif /* !KERNEL */


  { "none", 0,
    1, { 0, 0, 0 }, NULL_FNPTR,
    NULL_FNPTR, NULL_FNPTR, ssh_none_cipher },


  { NULL }
};


typedef struct SshCipherObjectRec {
  SSH_CRYPTO_OBJECT_HEADER
  const SshCipherDefStruct *ops;
  unsigned char iv[SSH_CIPHER_MAX_IV_SIZE];
  void *context;
  size_t context_size;
} SshCipherObjectStruct;


/* Get corresponding cipher def record by cipher name */
static const SshCipherDefStruct *
ssh_cipher_get_cipher_def_internal(const char *name)
{
  int i;




  if (name == NULL)
    return NULL;





  for (i = 0; ssh_cipher_algorithms[i].name; i++)
    {





      if (strcmp(ssh_cipher_algorithms[i].name, name) == 0)
        return &(ssh_cipher_algorithms[i]);
    }

  return NULL;
}

/* Check if given cipher name belongs to the set of supported ciphers
   aliases included. */

Boolean
ssh_cipher_supported(const char *name)
{
  if (ssh_cipher_get_cipher_def_internal(name) != NULL)
    return TRUE;

  return FALSE;
}



















/* Always returns FALSE for this implementation. */
Boolean
ssh_cipher_is_fips_approved(const char *name)
{
  return FALSE;
}


/* Return a comma-separated list of supported cipher algorithm names
   alias names included. */

char *
ssh_cipher_get_supported(void)
{
  int i;
  char *list, *tmp;
  size_t offset, list_len;




  list = NULL;
  offset = list_len = 0;

  list = NULL;
  offset = list_len = 0;





  for (i = 0; ssh_cipher_algorithms[i].name != NULL; i++)
    {
      size_t newsize;







      newsize = offset + 1 + !!offset + strlen(ssh_cipher_algorithms[i].name);

      if (list_len < newsize)
        {
          newsize *= 2;

          if ((tmp = ssh_realloc(list, list_len, newsize)) == NULL)
            {
              ssh_free(list);
              return NULL;
            }

          list = tmp;
          list_len = newsize;
        }

      SSH_ASSERT(list_len > 0);
      SSH_ASSERT(list != NULL);

      offset += ssh_snprintf(list + offset, list_len - offset, "%s%s",
                             offset ? "," : "",
                             ssh_cipher_algorithms[i].name);

    }

  return list;
}

/* The following function checks whether a cipher is a variable-length
   cipher or not. It returns TRUE if the cipher corresponding to 'name'
   has a fixed key length (i.e. the cipher is not a variable-length cipher)
   and returns FALSE otherwise. */
Boolean ssh_cipher_has_fixed_key_length(const char *name)
{
  if (!ssh_cipher_supported(name))
    return FALSE;

  if ((ssh_cipher_get_min_key_length(name) ==
       ssh_cipher_get_max_key_length(name)) &&
      (ssh_cipher_get_min_key_length(name) != 0))
    return TRUE;

  return FALSE;
}






















/* Allocates and initializes a cipher of the specified name. */

SshCryptoStatus
ssh_cipher_object_allocate(const char *name,
                           const unsigned char *key,
                           size_t keylen,
                           Boolean for_encryption,
                           SshCipherObject *cipher_ret)
{
  const SshCipherDefStruct *cipher_def;
  Boolean rv;



  SshCryptoStatus status;
  SshCipherObject cipher;

  *cipher_ret = NULL;






  cipher_def = ssh_cipher_get_cipher_def_internal(name);

  if (cipher_def == NULL)
    return SSH_CRYPTO_UNSUPPORTED;

  /* min<max && (def<max || max == 0) && min <= def */
  SSH_ASSERT(cipher_def->key_lengths.min_key_len
             <= cipher_def->key_lengths.def_key_len);
  SSH_ASSERT(cipher_def->key_lengths.min_key_len
             <= cipher_def->key_lengths.max_key_len);
  SSH_ASSERT(cipher_def->key_lengths.max_key_len == 0 ||
             (cipher_def->key_lengths.def_key_len <=
              cipher_def->key_lengths.max_key_len));

  /* Check for error in key expansion. No keys shorter than the key length
     of the cipher is allowed. Longer are allowed, but only the first
     bytes are used. */
  if (keylen < cipher_def->key_lengths.min_key_len)
    return SSH_CRYPTO_KEY_TOO_SHORT;

  if (keylen > cipher_def->key_lengths.max_key_len)
    return SSH_CRYPTO_KEY_TOO_LONG;

  /* Initialize the cipher. */
  if (!(cipher = ssh_crypto_malloc_i(sizeof(*cipher))))
    return SSH_CRYPTO_NO_MEMORY;

  /* Set up the cipher definition. */
  cipher->ops = cipher_def;
  /* Clean the IV. */
  memset(cipher->iv, 0, sizeof(cipher->iv));

  /* Set return value (rv) to default. */
  rv = TRUE;

  /* The "ctxsize" can be NULL if and only if the cipher is the none cipher. */
  if (cipher_def->ctxsize)
    {
      cipher->context_size = (*cipher_def->ctxsize)();

      /* Allocate the context of the cipher. */
      if (!(cipher->context = ssh_crypto_malloc_i(cipher->context_size)))
        {
          ssh_crypto_free_i(cipher);

          return SSH_CRYPTO_NO_MEMORY;
        }
    }
  else
    {
      cipher->context_size = 0;
      cipher->context = NULL;
    }

  if (cipher_def->init_with_check)
    {
      /* Initialize the cipher with a weak key check performed first.
         Not all ciphers have key classes that are easy or practical to
         test for. For those ciphers this function may perform
         as the plain initialization. */
      status = (*cipher_def->init_with_check)(cipher->context,
                                              key, keylen, for_encryption);
    }
  else if (cipher_def->init)
    {
      /* Initialize the cipher without weak key checks. */
      status = (*cipher_def->init)(cipher->context,
                                   key, keylen, for_encryption);
    }
  else
    status = SSH_CRYPTO_OK;

  if (status != SSH_CRYPTO_OK)
    {
      ssh_crypto_free_i(cipher->context);
      ssh_crypto_free_i(cipher);
      return status;
    }

  *cipher_ret = cipher;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_cipher_allocate(const char *name,
                    const unsigned char *key,
                    size_t keylen,
                    Boolean for_encryption,
                    SshCipher *cipher_ret)
{
  SshCryptoStatus status;
  SshCipherObject cipher;

  *cipher_ret = NULL;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  status = ssh_cipher_object_allocate(name, key, keylen, for_encryption,
                                      &cipher);

  if (status != SSH_CRYPTO_OK)
    return status;

  /* Add a handle ref to the current state */
  if (!ssh_crypto_library_object_use(cipher, SSH_CRYPTO_OBJECT_TYPE_CIPHER))
    {
      ssh_crypto_free_i(cipher->context);
      ssh_crypto_free_i(cipher);
      return SSH_CRYPTO_NO_MEMORY;
    }

  *cipher_ret = SSH_CRYPTO_CIPHER_TO_HANDLE(cipher);
  return SSH_CRYPTO_OK;
}

/* Free the cipher context */
void
ssh_cipher_object_free(SshCipherObject cipher)
{
  if (!cipher)
    return;

  ssh_crypto_free_i(cipher->context);
  ssh_crypto_free_i(cipher);
}


void
ssh_cipher_free(SshCipher handle)
{
  SshCipherObject cipher = SSH_CRYPTO_HANDLE_TO_CIPHER(handle);

  if (!cipher)
    return;

  /* Release cipher object */
  ssh_crypto_library_object_release(cipher);

  ssh_crypto_free_i(cipher->context);
  ssh_crypto_free_i(cipher);
}

const char *
ssh_cipher_name(SshCipher handle)
{
  SshCipherObject cipher = SSH_CRYPTO_HANDLE_TO_CIPHER(handle);

  if (!cipher)
    return NULL;

  return cipher->ops->name;
}

size_t
ssh_cipher_get_key_length(const char *name)
{
  const SshCipherDefStruct *cipher_def;

  cipher_def = ssh_cipher_get_cipher_def_internal(name);

  if (cipher_def == NULL)
    return 0;

  /* XXX XXX XXX This "none" stuff really has to go */
  if (strcmp(name, "none") != 0)
    SSH_ASSERT(cipher_def->key_lengths.def_key_len != 0);

  return cipher_def->key_lengths.def_key_len;
}

size_t
ssh_cipher_get_min_key_length(const char *name)
{
  const SshCipherDefStruct *cipher_def;

  cipher_def = ssh_cipher_get_cipher_def_internal(name);
  if (cipher_def == NULL)
    return 0;

  return cipher_def->key_lengths.min_key_len;
}

size_t
ssh_cipher_get_max_key_length(const char *name)
{
  const SshCipherDefStruct *cipher_def;

  cipher_def = ssh_cipher_get_cipher_def_internal(name);
  if (cipher_def == NULL)
    return 0;

  return cipher_def->key_lengths.max_key_len;
}

size_t
ssh_cipher_get_block_length(const char *name)
{
  const SshCipherDefStruct *cipher_def;

  cipher_def = ssh_cipher_get_cipher_def_internal(name);
  if (cipher_def == NULL)
    return 0;

  return cipher_def->block_length;
}

size_t
ssh_cipher_get_iv_length(const char *name)
{
  /* XXX Currently just returns the block length. */
  return ssh_cipher_get_block_length(name);
}

SshCryptoStatus
ssh_cipher_set_iv(SshCipher handle, const unsigned char *iv)
{
  SshCryptoStatus status;
  SshCipherObject cipher;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(cipher = SSH_CRYPTO_HANDLE_TO_CIPHER(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  memcpy(cipher->iv, iv, cipher->ops->block_length);

  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_cipher_get_iv(SshCipher handle, unsigned char *iv)
{
  SshCryptoStatus status;
  SshCipherObject cipher;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(cipher = SSH_CRYPTO_HANDLE_TO_CIPHER(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  memcpy(iv, cipher->iv, cipher->ops->block_length);
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_cipher_transform(SshCipher handle,
                     unsigned char *dest,
                     const unsigned char *src,
                     size_t len)
{
  SshCryptoStatus status;
  SshCipherObject cipher;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(cipher = SSH_CRYPTO_HANDLE_TO_CIPHER(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  /* Check that the src length is divisible by block length of the cipher. */
  if (len % cipher->ops->block_length == 0)
    (*cipher->ops->transform)(cipher->context, dest, src, len, cipher->iv);
  else
    return SSH_CRYPTO_BLOCK_SIZE_ERROR;

  return SSH_CRYPTO_OK;
}


SshCryptoStatus
ssh_cipher_object_transform_with_iv(SshCipherObject cipher,
                                    unsigned char *dest,
                                    const unsigned char *src,
                                    size_t len,
                                    unsigned char *iv)
{
  /* Check that the src length is divisible by block length of the cipher. */
  if (len % cipher->ops->block_length == 0)
    (*cipher->ops->transform)(cipher->context, dest, src, len, iv);
  else
    return SSH_CRYPTO_BLOCK_SIZE_ERROR;

  return SSH_CRYPTO_OK;
}


SshCryptoStatus
ssh_cipher_transform_with_iv(SshCipher handle,
                             unsigned char *dest,
                             const unsigned char *src,
                             size_t len,
                             unsigned char *iv)
{
  SshCryptoStatus status;
  SshCipherObject cipher;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(cipher = SSH_CRYPTO_HANDLE_TO_CIPHER(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  /* Check that the src length is divisible by block length of the cipher. */
  if (len % cipher->ops->block_length == 0)
    (*cipher->ops->transform)(cipher->context, dest, src, len, iv);
  else
    return SSH_CRYPTO_BLOCK_SIZE_ERROR;

  return SSH_CRYPTO_OK;
}
