/*

keyencode.c

Author: Hannu K. Napari <Hannu.Napari@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
All rights reserved

Encode and decode keyblobs present in GAFP protocol.

*/

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshgafp.h"
#include "sshpkcs1.h"
#include "sshpubkey.h"
#include "sshprvkey.h"

#define SSH_DEBUG_MODULE "SshGafpKeyEncode"

SshCryptoStatus
ssh_gafp_decode_public_key_blob(const char *public_key_encoding,
                                const unsigned char *public_key_blob,
                                size_t public_key_blob_len,
                                SshPublicKey *key_return)
{
  SshCryptoStatus status;

  *key_return = NULL;
  if (public_key_encoding)
    {
      status = ssh_pkb_decode(ssh_pkb_name_to_type(public_key_encoding),
                              public_key_blob, public_key_blob_len,
                              NULL, 0,
                              key_return);
    }
  else
    {
      status = SSH_CRYPTO_UNSUPPORTED;
    }
  return status;
}

SshCryptoStatus
ssh_gafp_decode_private_key_blob(const char *private_key_encoding,
                                 const unsigned char *private_key_blob,
                                 size_t private_key_blob_len,
                                 const unsigned char *cipher_key,
                                 size_t cipher_key_len,
                                 SshPrivateKey *key_return)
{
  SshCryptoStatus status;

  *key_return = NULL;
  if (private_key_encoding)
    {
      status = ssh_skb_decode(ssh_skb_name_to_type(private_key_encoding),
                              private_key_blob, private_key_blob_len,
                              NULL, NULL,
                              cipher_key, cipher_key_len,
                              key_return);
    }
  else
    {
      status = SSH_CRYPTO_UNSUPPORTED;
    }
  return status;
}

/* eof (keyencode.c) */
