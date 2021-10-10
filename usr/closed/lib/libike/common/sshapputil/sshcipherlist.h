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

#ifndef SSHCIPHERLIST_H
#define SSHCIPHERLIST_H

/*
   Return a name list that contains items in list `original'
   so that items in list `excluded' are excluded.
*/
unsigned char *
ssh_cipher_list_exclude(const unsigned char *original,
                        const unsigned char *excluded);

/*
   Convert between canonical cryptolib names and
   names in secsh draft.
 */
unsigned char *ssh_public_key_name_ssh_to_cryptolib(const unsigned char *str);
unsigned char *ssh_public_key_name_cryptolib_to_ssh(const unsigned char *str);

/* When given a list of public key algorithms (ssh-dsa,...)
   constructs an xmallocated list of corresponding X509 versions
   (x509v3-sign-dss,...) and returns it. */
unsigned char *
ssh_cipher_list_x509_from_pk_algorithms(const unsigned char *alglist);

#endif /* SSHCIPHERLIST_H */

/* eof (sshcipherlist.h) */
