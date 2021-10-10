
/*

genhash.c

Author: Antti Huima <huima@ssh.fi>
        Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

*/

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshcrypt_i.h"
#include "sshhash_i.h"

#define SSH_DEBUG_MODULE "GenHash"

#ifdef SSHDIST_CRYPT_SHA
#include "sha.h"
#include "sha256.h"
#endif /* SSHDIST_CRYPT_SHA */
#ifdef SSHDIST_CRYPT_MD5
#include "md5.h"
#endif /* SSHDIST_CRYPT_MD5 */




#ifndef KERNEL

#ifdef SSHDIST_CRYPT_MD4
#include "md4.h"
#endif /* SSHDIST_CRYPT_MD4 */
#ifdef SSHDIST_CRYPT_MD2
#include "md2.h"
#endif /* SSHDIST_CRYPT_MD2 */







#endif /* !KERNEL */

/* List of supported hash algorithms. */

static const SshHashDefStruct * const ssh_hash_algorithms[] =
{
#ifdef SSHDIST_CRYPT_MD5
  &ssh_hash_md5_def,
#endif /* SSHDIST_CRYPT_MD5 */
#ifdef SSHDIST_CRYPT_SHA
  /* SHA-1 */
  &ssh_hash_sha_def,
  &ssh_hash_sha_96_def,
  &ssh_hash_sha_80_def,
  /* SHA-256. */
  &ssh_hash_sha256_def,
  &ssh_hash_sha256_96_def,
  &ssh_hash_sha256_80_def,
#endif /* SSHDIST_CRYPT_SHA */

#ifdef SUNWIPSEC
  &ssh_hash_sha384_def,
  &ssh_hash_sha512_def,
#endif /* SUNWIPSEC */


#ifndef KERNEL

#ifdef SSHDIST_CRYPT_MD2
  &ssh_hash_md2_def,
#endif /* SSHDIST_CRYPT_MD2 */
#ifdef SSHDIST_CRYPT_MD4
  &ssh_hash_md4_def,
#endif /* SSHDIST_CRYPT_MD4 */











#endif /* KERNEL */

  NULL
};


typedef struct SshHashObjectRec {
  SSH_CRYPTO_OBJECT_HEADER
  const SshHashDefStruct *ops;
  void *context;
  size_t context_size;
  SshCryptoStatus error_status;
} SshHashObjectStruct;



/* Returns the name of the hash function whose encoded oid matches the input
   'encoded_oid'. */
const char *ssh_hash_get_hash_from_oid(const unsigned char *encoded_oid,
				       size_t max_encoded_oid_len,
				       size_t *actual_encoded_oid_len)
{
  unsigned int i;
  size_t len;

  *actual_encoded_oid_len = 0;

  if (encoded_oid == NULL)
    return NULL;

  for (i = 0; ssh_hash_algorithms[i] != NULL; i++)
    {
      if (ssh_hash_algorithms[i]->compare_asn1_oid == NULL)
	continue;

      len = (*ssh_hash_algorithms[i]->compare_asn1_oid)(encoded_oid,
							max_encoded_oid_len);
      if (len != 0)
	{
	  *actual_encoded_oid_len = len;
	  return ssh_hash_algorithms[i]->name;
	}
    }

  return NULL;
}



#if 1
/* XXX */
/* XXX this is used from genmac */
const SshHashDefStruct *
ssh_hash_get_definition_internal(const SshHash handle)
{
  SshHashObject hash = SSH_CRYPTO_HANDLE_TO_HASH(handle);
  return hash->ops;
}
#endif

static const SshHashDefStruct *
ssh_hash_get_hash_def_internal(const char *name)
{
  unsigned int i;





  if (name == NULL)
    return FALSE;

  for (i = 0; ssh_hash_algorithms[i] != NULL; i++)
    {





      if (strcmp(ssh_hash_algorithms[i]->name, name) == 0)
        return ssh_hash_algorithms[i];
    }

  return NULL;
}

/* Returns a comma-separated list of supported hash functions names.
   The caller must free the returned value with ssh_xfree(). */

char *
ssh_hash_get_supported(void)
{
  int i;
  size_t list_len, offset;
  char *list, *tmp;





  list = NULL;
  offset = list_len = 0;

  for (i = 0; ssh_hash_algorithms[i] != NULL; i++)
    {
      size_t newsize;






      newsize = offset + 1 + !!offset + strlen(ssh_hash_algorithms[i]->name);

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

      offset += ssh_snprintf(list + offset, list_len - offset, "%s%s",
                             offset ? "," : "",
                             ssh_hash_algorithms[i]->name);

    }
  return list;
}

/* Check if given hash name belongs to the set of supported ciphers. */

Boolean
ssh_hash_supported(const char *name)
{
  if (ssh_hash_get_hash_def_internal(name) != NULL)
    return TRUE;

  return FALSE;
}

































/* Always returns false in this implementation. */
Boolean
ssh_hash_is_fips_approved(const char *name)
{
  return FALSE;
}



/* Allocates and initializes a hash context. */
SshCryptoStatus
ssh_hash_object_allocate(const char *name, SshHashObject *hash_ret)
{
  const SshHashDefStruct *hash_def;
  SshHashObject hash;

  *hash_ret = NULL;

  hash_def = ssh_hash_get_hash_def_internal(name);

  if (!hash_def)
    return SSH_CRYPTO_UNSUPPORTED;

  if (!(hash = ssh_crypto_malloc_i(sizeof(*hash))))
    return SSH_CRYPTO_NO_MEMORY;

  hash->ops = hash_def;
  hash->error_status = SSH_CRYPTO_OK;
  hash->context_size = (*hash_def->ctxsize)();

  if (!(hash->context = ssh_crypto_malloc_i(hash->context_size)))
    {
      ssh_crypto_free_i(hash);
      return SSH_CRYPTO_NO_MEMORY;
    }

  (*hash_def->reset_context)(hash->context);

  *hash_ret = hash;
  return SSH_CRYPTO_OK;
}


SshCryptoStatus
ssh_hash_allocate(const char *name, SshHash *hash_ret)
{
  SshHashObject hash;
  SshCryptoStatus status;

  *hash_ret = NULL;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  status = ssh_hash_object_allocate(name, &hash);

  if (status != SSH_CRYPTO_OK)
    return status;

  if (!ssh_crypto_library_object_use(hash, SSH_CRYPTO_OBJECT_TYPE_HASH))
    {
      ssh_crypto_free_i(hash->context);
      ssh_crypto_free_i(hash);
      return SSH_CRYPTO_NO_MEMORY;
    }

  *hash_ret = SSH_CRYPTO_HASH_TO_HANDLE(hash);
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_hash_object_allocate_internal(const SshHashDefStruct *hash_def,
                                  SshHashObject *hash_ret)
{
  SshHashObject hash;




  if (hash_def == NULL)
    return SSH_CRYPTO_UNSUPPORTED;








  if (!(hash = ssh_crypto_malloc_i(sizeof(*hash))))
    return SSH_CRYPTO_NO_MEMORY;

  hash->ops = hash_def;
  hash->error_status = SSH_CRYPTO_OK;

  if (!(hash->context = ssh_crypto_malloc_i((hash_def->ctxsize)())))
    {
      ssh_crypto_free_i(hash);
      return SSH_CRYPTO_NO_MEMORY;
    }

  (*hash_def->reset_context)(hash->context);

  *hash_ret = hash;
  return SSH_CRYPTO_OK;
}

/* From a given hash definition allocate a SshHash context. This can be
   used transparently (even though given hash definition need not be
   any "standard" hash function) with this interface. Defined in
   sshcrypti.h for internal usage only. */

SshCryptoStatus
ssh_hash_allocate_internal(const SshHashDefStruct *hash_def, SshHash *hash_ret)
{
  SshHashObject hash;
  SshCryptoStatus status;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  status = ssh_hash_object_allocate_internal(hash_def, &hash);

  if (status != SSH_CRYPTO_OK)
    return status;

  if (!ssh_crypto_library_object_use(hash, SSH_CRYPTO_OBJECT_TYPE_HASH))
    {
      ssh_crypto_free_i(hash->context);
      ssh_crypto_free_i(hash);
      *hash_ret = NULL;
      return SSH_CRYPTO_NO_MEMORY;
    }

  *hash_ret = SSH_CRYPTO_HASH_TO_HANDLE(hash);
  return SSH_CRYPTO_OK;
}



/* Duplicate the given hash with correctly duplicating the internal state
   of the hash function also. */
SshHash
ssh_hash_duplicate(const SshHash handle)
{
  SshHashObject new_hash, hash;

  if (!ssh_crypto_library_object_check_use(NULL))
    return NULL;

  if (handle == NULL)
    return NULL;

  if (!(hash = SSH_CRYPTO_HANDLE_TO_HASH(handle)))
    return NULL;

  if (!(new_hash = ssh_crypto_malloc_i(sizeof(*new_hash))))
    return NULL;

  new_hash->ops = hash->ops;

  /* Copy the state. */
  if (!(new_hash->context = ssh_crypto_malloc_i((*new_hash->ops->ctxsize)())))
    {
      ssh_crypto_free_i(new_hash);
      return NULL;
    }

  memcpy(new_hash->context, hash->context,
         (*new_hash->ops->ctxsize)());

  if (!ssh_crypto_library_object_use(new_hash, SSH_CRYPTO_OBJECT_TYPE_HASH))
    {
      ssh_crypto_free_i(new_hash->context);
      ssh_crypto_free_i(new_hash);
      return NULL;
    }

  return SSH_CRYPTO_HASH_TO_HANDLE(new_hash);
}


/* Free hash context. */
void
ssh_hash_object_free(SshHashObject hash)
{
  if (!hash)
    return;

  ssh_crypto_free_i(hash->context);
  ssh_crypto_free_i(hash);
}


void
ssh_hash_free(SshHash handle)
{
  SshHashObject hash = SSH_CRYPTO_HANDLE_TO_HASH(handle);

  if (!hash)
    return;

  ssh_crypto_library_object_release(hash);
  ssh_crypto_free_i(hash->context);
  ssh_crypto_free_i(hash);
}

const char*
ssh_hash_name(SshHash handle)
{
  SshHashObject hash = SSH_CRYPTO_HANDLE_TO_HASH(handle);

  if (!hash)
    return NULL;

  return hash->ops->name;
}

/* Returns the ASN.1 Object Identifier of the hash function if
   known. Returns NULL if OID is not known. */
const char *
ssh_hash_asn1_oid(const char *name)
{
  const SshHashDefStruct *hash_def;

  if (!(hash_def = ssh_hash_get_hash_def_internal(name)))
    return NULL;

  return hash_def->asn1_oid;
}

size_t
ssh_hash_asn1_oid_compare(const char *name, const unsigned char *oid,
			  size_t max_len)
{
  const SshHashDefStruct *hash_def;

  if (!(hash_def = ssh_hash_get_hash_def_internal(name)) ||
      hash_def->compare_asn1_oid == NULL)
    {
      return 0;
    }

  return (*hash_def->compare_asn1_oid)(oid, max_len);
}

const unsigned char *
ssh_hash_asn1_oid_generate(const char *name, size_t *len)
{
  const SshHashDefStruct *hash_def;

  if (!(hash_def = ssh_hash_get_hash_def_internal(name)) ||
      hash_def->generate_asn1_oid == NULL)
    {
      if (len) *len = 0;
      return NULL;
    }

  return (*hash_def->generate_asn1_oid)(len);
}

/* Returns the ISO/IEC dedicated hash number if available. 0 if not
   known. */
unsigned char
ssh_hash_iso_identifier(const char *name)
{
  const SshHashDefStruct *hash_def;

  if (!(hash_def = ssh_hash_get_hash_def_internal(name)))
    return 0;

  return hash_def->iso_identifier;
}

/* Resets the hash context to its initial state. */

void
ssh_hash_object_reset(SshHashObject hash)
{
  (*hash->ops->reset_context)(hash->context);
  hash->error_status = SSH_CRYPTO_OK;
}

void
ssh_hash_reset(SshHash handle)
{
  SshHashObject hash;

  if (!(hash = SSH_CRYPTO_HANDLE_TO_HASH(handle)))
    return;

  if (!ssh_crypto_library_object_check_use(&hash->error_status))
      return;

  ssh_hash_object_reset(hash);
}

/* Get the digest lenght of the hash. */

size_t
ssh_hash_digest_length(const char *name)
{
  const SshHashDefStruct *hash_def;

  if (!(hash_def = ssh_hash_get_hash_def_internal(name)))
    return 0;

  return hash_def->digest_length;
}

/* Get input block size (used for hmac padding). */

size_t
ssh_hash_input_block_size(const char *name)
{
  const SshHashDefStruct *hash_def;

  if (!(hash_def = ssh_hash_get_hash_def_internal(name)))
    return 0;

  return hash_def->input_block_length;
}

/* Updates the hash context by adding the given text. */
void
ssh_hash_object_update(SshHashObject hash, const void *buf, size_t len)
{
  (*hash->ops->update)(hash->context, buf, len);
}


void
ssh_hash_update(SshHash handle, const unsigned char *buf, size_t len)
{
  SshHashObject hash;

  if (!(hash = SSH_CRYPTO_HANDLE_TO_HASH(handle)))
    return;

  if (!ssh_crypto_library_object_check_use(&hash->error_status))
    return;

  (*hash->ops->update)(hash->context, buf, len);
}


/* Outputs the hash digest. */
SshCryptoStatus
ssh_hash_object_final(SshHashObject hash, unsigned char *digest)
{
  (*hash->ops->final)(hash->context, digest);

  return SSH_CRYPTO_OK;
}


SshCryptoStatus
ssh_hash_final(SshHash handle, unsigned char *digest)
{
  SshHashObject hash;

  if (!(hash = SSH_CRYPTO_HANDLE_TO_HASH(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (!ssh_crypto_library_object_check_use(&hash->error_status))
    return hash->error_status;

  return ssh_hash_object_final(hash, digest);
}
