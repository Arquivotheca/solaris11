/*

  sshpem.h

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Sat Mar 25 00:19:34 2000.

  */

#ifndef SSHPEM_H
#define SSHPEM_H

/* Parse the contents of the 'data'. Attempt to decrypt encrypted
   contents if key available. */
unsigned char *ssh_pem_decode_with_key(const unsigned char *data,
                                       size_t data_len,
                                       const unsigned char *key, 
                                       size_t key_len,
                                       size_t *ret_len);


#endif /* SSHPEM_H */
