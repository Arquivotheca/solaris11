/*

  sshinetencode.h

  Author:
        Santeri Paavolainen <santtu@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved

*/

#ifndef SSHINETENCODE_H
#define SSHIINETENCODE_H

#include "sshinet.h"

/* Encode and decode SshIpAddr addresses */

#ifndef _KERNEL
#include "sshbuffer.h"
size_t  ssh_encode_ipaddr_buffer (SshBuffer buffer, const SshIpAddr ip);
size_t  ssh_decode_ipaddr_buffer (SshBuffer buffer, SshIpAddr ip);
#endif  /* _KERNEL */

size_t  ssh_decode_ipaddr_array (const unsigned char * buf, size_t bufsize,
                                 SshIpAddr ip);

size_t  ssh_encode_ipaddr_array (unsigned char * buf, size_t bufsize,
                                 const SshIpAddr ip);
size_t  ssh_encode_ipaddr_array_alloc (unsigned char ** buf_return,
                                       const SshIpAddr ip);

/* type+mask+content */
#define SSH_MAX_IPADDR_ENCODED_LENGTH 1+4+16

#endif
