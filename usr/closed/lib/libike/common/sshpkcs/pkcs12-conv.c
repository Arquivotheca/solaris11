/*
  pkcs12-conv.c

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved

  Convenience functions for using PKSC#12.
*/

#include "sshincludes.h"
#include "sshpkcs12-conv.h"

#define SSH_DEBUG_MODULE "SshPKCS12Conv"

SshPrivateKey
ssh_pkcs12_conv_get_key_from_bag(SshPkcs12Safe safe,
                                 SshStr passwd,
                                 int index)
{
  SshPrivateKey key;
  SshPkcs12BagType bag_type;
  SshPkcs12Bag bag;

  ssh_pkcs12_safe_get_bag(safe, index, &bag_type, &bag);

  switch (bag_type)
    {
    case SSH_PKCS12_BAG_SHROUDED_KEY:
      /* Bag contains a shrouded private key key. We must use password
         to decrypt the key. */
      if (!ssh_pkcs12_bag_get_shrouded_key(bag, passwd, &key))
        {
          SSH_DEBUG(SSH_D_MIDOK, ("Got shrouded key from bag %d.", index));
          return key;
        }
      else
        {
          SSH_DEBUG(SSH_D_ERROR,
                    ("Error getting shrouded key, bag %d.", index));
          return NULL;
        }
      break;
    case SSH_PKCS12_BAG_KEY:
      /* Bag contains plaintext private key. */
      if (!ssh_pkcs12_bag_get_key(bag, &key))
        {
          SSH_DEBUG(SSH_D_MIDOK, ("Got plaintext key from bag %d.", index));
          return key;
        }
      else
        {
          SSH_DEBUG(SSH_D_ERROR,
                    ("Error getting plaintext key, bag %d.", index));
          return NULL;
        }
      break;
    case SSH_PKCS12_BAG_CERT:
    default:
      SSH_DEBUG(SSH_D_MIDOK, ("No key in bag %d.", index));
      return NULL;
    }
}


Boolean
ssh_pkcs12_conv_get_cert_from_bag(SshPkcs12Safe safe,
                                  SshStr passwd,
                                  int index,
                                  unsigned char **cert,
                                  size_t *cert_len)
{
  SshPkcs12BagType bag_type;
  SshPkcs12Bag bag;
  const unsigned char *tmp_cert;

  ssh_pkcs12_safe_get_bag(safe, index, &bag_type, &bag);

  if (bag == NULL)
    return FALSE;

  switch (bag_type)
    {
    case SSH_PKCS12_BAG_CERT:
      if (ssh_pkcs12_bag_get_cert(bag, &tmp_cert, cert_len) == SSH_PKCS12_OK)
        {
          if ((*cert = ssh_memdup(tmp_cert, *cert_len)) != NULL)
            return TRUE;
          else
            return FALSE;
        }
    default:
      SSH_DEBUG(SSH_D_MIDOK, ("No cert in bag %d.", index));
      return FALSE;
    }
}


/* Decode the n:th public key from the PKSC#12 block. Use the
   passphrase for both integrity checks and the encryption (like the
   browser does.)  */
SshPkcs12Status ssh_pkcs12_conv_decode_public_key(const unsigned char *data,
                                                  size_t len,
                                                  SshStr passwd,
                                                  SshUInt32 n,
                                                  SshPublicKey *key_ret)
{
  SshPkcs12PFX pfx;
  SshPkcs12Safe safe;
  SshPkcs12IntegrityMode type;
  int i, j, num_safes, num_bags;
  SshPrivateKey key = NULL;
  SshPublicKey pub = NULL;
  SshPkcs12SafeProtectionType prot;
  SshPkcs12Status status = SSH_PKCS12_OK;
  SshUInt32 occurance = 0;

  /* Decode data */
  if (ssh_pkcs12_pfx_decode(data, len, &type, &pfx))
    {
      SSH_DEBUG(SSH_D_FAIL, ("Decoding of PKCS12 blob failed."));
      return SSH_PKCS12_FORMAT_ERROR;
    }

  if (type == SSH_PKCS12_INTEGRITY_PASSWORD)
    {
      if (ssh_pkcs12_pfx_verify_hmac(pfx, passwd))
        {
          status = SSH_PKCS12_FORMAT_ERROR;
          goto done;
        }
    }

  num_safes = ssh_pkcs12_pfx_get_num_safe(pfx);

  for (i = 0; i < num_safes; i++)
    {
      ssh_pkcs12_pfx_get_safe(pfx, i, &prot, &safe);
      switch(prot)
        {
        case SSH_PKCS12_SAFE_ENCRYPT_NONE:
          /* Safe is not encrypted, we can traverse bags immediately */
          num_bags = ssh_pkcs12_safe_get_num_bags(safe);
          for (j = 0; j < num_bags; j++)
            {
              key = ssh_pkcs12_conv_get_key_from_bag(safe, passwd, j);
              if (key)
                {
                  if (occurance == n)
                    goto done;
                  occurance++;
                }
            }
          break;

        case SSH_PKCS12_SAFE_ENCRYPT_PASSWORD:
          /* Safe is encrypted with password. WE must first decrypt the
             safe before we can access the bags. */
          if (!ssh_pkcs12_safe_decrypt_password(safe, passwd))
            {
              SSH_DEBUG(SSH_D_MIDOK, ("Safe decrypted succesfully."));
              /* Traverse the bags */
              num_bags = ssh_pkcs12_safe_get_num_bags(safe);
              for (j = 0; j < num_bags; j++)
                {
                  key = ssh_pkcs12_conv_get_key_from_bag(safe, passwd, j);
                  if (key)
                    {
                      if (occurance == n)
                        goto done;
                      occurance++;
                    }
                }
            }
          else
            {
              SSH_DEBUG(SSH_D_ERROR, ("Invalid password"));
              status = SSH_PKCS12_FORMAT_ERROR;
              goto done;
            }
          break;
        default:
          SSH_DEBUG(SSH_D_ERROR, ("Unkown protection type"));
          break;
        }
    }
  if (key == NULL)
    status = SSH_PKCS12_FORMAT_ERROR;

done:
  ssh_pkcs12_pfx_free(pfx);
  if (key && ssh_private_key_derive_public_key(key, &pub) == SSH_CRYPTO_OK)
   *key_ret = pub;
  else
   *key_ret = NULL;

  if (key)
    ssh_private_key_free(key);
  return status;
}

/* Decode the n:th private key from the PKSC#12 block. Use the
   passphrase for both integrity checks and the encryption (like the
   browser does.)  */
SshPkcs12Status ssh_pkcs12_conv_decode_private_key(const unsigned char *data,
                                                   size_t len,
                                                   SshStr passwd,
                                                   SshUInt32 n,
                                                   SshPrivateKey *key_ret)
{
  SshPkcs12PFX pfx;
  SshPkcs12Safe safe;
  SshPkcs12IntegrityMode type;
  int i, j, num_safes, num_bags;
  SshPrivateKey key = NULL;
  SshPkcs12SafeProtectionType prot;
  SshPkcs12Status status = SSH_PKCS12_OK;
  SshUInt32 occurance = 0;

  /* Decode data */
  if (ssh_pkcs12_pfx_decode(data, len, &type, &pfx))
    {
      SSH_DEBUG(SSH_D_FAIL, ("Decoding of PKCS12 blob failed."));
      return SSH_PKCS12_FORMAT_ERROR;
    }

  if (type == SSH_PKCS12_INTEGRITY_PASSWORD)
    {
      if (ssh_pkcs12_pfx_verify_hmac(pfx, passwd))
        {
          status = SSH_PKCS12_FORMAT_ERROR;
          goto done;
        }
    }

  num_safes = ssh_pkcs12_pfx_get_num_safe(pfx);

  for (i = 0; i < num_safes; i++)
    {
      ssh_pkcs12_pfx_get_safe(pfx, i, &prot, &safe);
      switch(prot)
        {
        case SSH_PKCS12_SAFE_ENCRYPT_NONE:
          /* Safe is not encrypted, we can traverse bags immediately */
          num_bags = ssh_pkcs12_safe_get_num_bags(safe);
          for (j = 0; j < num_bags; j++)
            {
              key = ssh_pkcs12_conv_get_key_from_bag(safe, passwd, j);
              if (key)
                {
                  if (occurance == n)
                    goto done;
                  occurance++;
                }
            }
          break;
        case SSH_PKCS12_SAFE_ENCRYPT_PASSWORD:
          /* Safe is encrypted with password. WE must first decrypt the
             safe before we can access the bags. */
          if (!ssh_pkcs12_safe_decrypt_password(safe, passwd))
            {
              SSH_DEBUG(SSH_D_MIDOK, ("Safe decrypted succesfully."));
              /* Traverse the bags */
              num_bags = ssh_pkcs12_safe_get_num_bags(safe);
              for (j = 0; j < num_bags; j++)
                {
                  key = ssh_pkcs12_conv_get_key_from_bag(safe, passwd, j);
                  if (key)
                    {
                      if (occurance == n)
                        goto done;
                      occurance++;
                    }
                }
            }
          else
            {
              SSH_DEBUG(SSH_D_ERROR, ("Invalid password"));
              status = SSH_PKCS12_FORMAT_ERROR;
              goto done;
            }
          break;
        default:
          SSH_DEBUG(SSH_D_ERROR, ("Unkown protection type"));
          break;
        }
    }
  if (key == NULL)
    status = SSH_PKCS12_FORMAT_ERROR;

done:
  ssh_pkcs12_pfx_free(pfx);
  *key_ret = key;

  return status;
}


/* Decode the n:th certificate from the PKSC#12 block. Use the
   passphrase for both integrity checks and the encryption (like the
   browser does.)  */
SshPkcs12Status ssh_pkcs12_conv_decode_cert(const unsigned char *data,
                                            size_t len,
                                            SshStr passwd,
                                            SshUInt32 n,
                                            unsigned char **cert_buf,
                                            size_t *cert_buf_len)
{
  SshPkcs12PFX pfx;
  SshPkcs12Safe safe;
  SshPkcs12IntegrityMode type;
  int i, j, num_safes, num_bags;
  SshPkcs12SafeProtectionType prot;
  SshPkcs12Status status = SSH_PKCS12_OK;
  SshUInt32 occurance = 0;
  Boolean success;

  *cert_buf = NULL;
  /* Decode data */
  if (ssh_pkcs12_pfx_decode(data, len, &type, &pfx))
    {
      SSH_DEBUG(SSH_D_FAIL, ("Decoding of PKCS12 blob failed."));
      return SSH_PKCS12_FORMAT_ERROR;
    }

  if (type == SSH_PKCS12_INTEGRITY_PASSWORD)
    {
      if (ssh_pkcs12_pfx_verify_hmac(pfx, passwd))
        {
          status = SSH_PKCS12_FORMAT_ERROR;
          goto done;
        }
    }

  num_safes = ssh_pkcs12_pfx_get_num_safe(pfx);

  for (i = 0; i < num_safes; i++)
    {
      ssh_pkcs12_pfx_get_safe(pfx, i, &prot, &safe);
      switch(prot)
        {
        case SSH_PKCS12_SAFE_ENCRYPT_NONE:
          /* Safe is not encrypted, we can traverse bags immediately */
          num_bags = ssh_pkcs12_safe_get_num_bags(safe);
          for (j = 0; j < num_bags; j++)
            {
              success = ssh_pkcs12_conv_get_cert_from_bag(safe, passwd, j,
                                                          cert_buf,
                                                          cert_buf_len);
              if (success)
                {
                  if (occurance == n)
                    goto done;
                  occurance++;
                  ssh_free(*cert_buf);
                  *cert_buf = NULL;
                }
            }
          break;
        case SSH_PKCS12_SAFE_ENCRYPT_PASSWORD:
          /* Safe is encrypted with password. We must first decrypt the
             safe before we can access the bags. */
          if (!ssh_pkcs12_safe_decrypt_password(safe, passwd))
            {
              SSH_DEBUG(SSH_D_MIDOK, ("Safe decrypted succesfully."));
              /* Traverse the bags */
              num_bags = ssh_pkcs12_safe_get_num_bags(safe);
              for (j = 0; j < num_bags; j++)
                {
                  success = ssh_pkcs12_conv_get_cert_from_bag(safe,
                                                              passwd,
                                                              j,
                                                              cert_buf,
                                                              cert_buf_len);
                  if (success)
                    {
                      if (occurance == n)
                        goto done;
                      occurance++;
                      ssh_free(*cert_buf);
                      *cert_buf = NULL;
                    }
                }
            }
          else
            {
              SSH_DEBUG(SSH_D_ERROR, ("Invalid password"));
              status = SSH_PKCS12_FORMAT_ERROR;
              goto done;
            }
          break;
        default:
          SSH_DEBUG(SSH_D_ERROR, ("Unknown protection type"));
          status = SSH_PKCS12_FORMAT_ERROR;
          break;
        }
    }
  if (*cert_buf == NULL && status == SSH_PKCS12_OK)
    status = SSH_PKCS12_INVALID_INDEX;

done:
  ssh_pkcs12_pfx_free(pfx);

  return status;
}
