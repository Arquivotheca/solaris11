/*
  sshpkcs12_conv.h

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved

  Convenience functions for PKCS#12.
*/

#ifndef SSHPKCS12_CONV_H_INCLUDED
#define SSHPKCS12_CONV_H_INCLUDED

#include "sshpkcs12.h"


/* Decode the n:th private key from the PKSC#12 block. Use the
   passphrase for both integrity checks and the encryption (like the
   browser does.)  */
SshPkcs12Status ssh_pkcs12_conv_decode_private_key(const unsigned char *data,
                                                   size_t len,
                                                   SshStr passphrase,
                                                   SshUInt32 n,
                                                   SshPrivateKey *key_ret);

/* Decode the n:th public key from the PKSC#12 block. Use the
   passphrase for both integrity checks and the encryption (like the
   browser does.)  */
SshPkcs12Status ssh_pkcs12_conv_decode_public_key(const unsigned char *data,
                                                  size_t len,
                                                  SshStr passphrase,
                                                  SshUInt32 n,
                                                  SshPublicKey *key_ret);

/* Decode the n:th certificate from the PKSC#12 block. Use the
   passphrase for both integrity checks and the encryption (like the
   browser does.)  */
SshPkcs12Status ssh_pkcs12_conv_decode_cert(const unsigned char *data,
                                            size_t len,
                                            SshStr passphrase,
                                            SshUInt32 n,
                                            unsigned char **cert_buf,
                                            size_t *cert_buf_len);


#endif /* SSHPKCS12_CONV_H_INCLUDED */
