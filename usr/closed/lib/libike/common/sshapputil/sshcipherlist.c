/*

  sshcipherlist.c

  Authors:
        Tatu Ylonen <ylo@ssh.fi>
        Markku-Juhani Saarinen <mjos@ssh.fi>
        Timo J. Rinne <tri@ssh.fi>
        Sami Lehtinen <sjl@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Canonialize comma-separated cipher lists.

*/

#include "sshincludes.h"
#include "sshcipherlist.h"
#include "ssh2pubkeyencode.h"
#include "sshcrypt.h"
#include "sshsnlist.h"
#include "sshcryptoaux.h"

unsigned char *ssh_public_key_name_ssh_to_cryptolib(const unsigned char *str)
{
  unsigned char *r;

  r = NULL;
  if (str == NULL)
    r = NULL;
  else if (strcmp(ssh_csstr(str), SSH_SSH_DSS) == 0)
    r = ssh_xstrdup(SSH_CRYPTO_DSS);
#ifdef SSHDIST_CRYPT_RSA
#ifdef WITH_RSA
  else if (strcmp(ssh_csstr(str), SSH_SSH_RSA) == 0)
    r = ssh_xstrdup(SSH_CRYPTO_RSA);
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */

  return r;
}

unsigned char *ssh_public_key_name_cryptolib_to_ssh(const unsigned char *str)
{
  unsigned char *r;

  r = NULL;
  if (str == NULL)
    return NULL;
  else if (strcmp(ssh_csstr(str), SSH_SSH_DSS) == 0)
    r = ssh_xstrdup(SSH_SSH_DSS);
  else if (strcmp(ssh_csstr(str), SSH_CRYPTO_DSS) == 0)
    r = ssh_xstrdup(SSH_SSH_DSS);
#ifdef SSHDIST_CRYPT_RSA
#ifdef WITH_RSA
  else if (strcmp(ssh_csstr(str), SSH_SSH_RSA) == 0)
    r = ssh_xstrdup(SSH_SSH_RSA);
  else if (strcmp(ssh_csstr(str), SSH_CRYPTO_RSA) == 0)
    r = ssh_xstrdup(SSH_SSH_RSA);
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */
  else
    r = NULL;

  return r;
}

/* When given a list of public key algorithms (ssh-dss,...)
   constructs an xmallocated list of corresponding X509 versions
   (x509v3-sign-dss,...) and returns it. */
unsigned char *
ssh_cipher_list_x509_from_pk_algorithms(const unsigned char *alglist)
{
  unsigned char *result = NULL;

  if (ssh_snlist_contains(alglist, ssh_custr(SSH_SSH_DSS)))
    {
      ssh_snlist_append(&result, ssh_custr(SSH_SSH_X509_DSS));
    }
  if (ssh_snlist_contains(alglist, ssh_custr(SSH_SSH_RSA)))
    {
      ssh_snlist_append(&result, ssh_custr(SSH_SSH_X509_RSA));
    }

  return result;
}
