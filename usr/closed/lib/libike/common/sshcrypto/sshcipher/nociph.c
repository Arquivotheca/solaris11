/*

  nociph.c

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Sat Nov  2 04:25:01 1996 [mkojo]

  Cipher 'none'.

  */

#include "sshincludes.h"
#include "nociph.h"

void ssh_none_cipher(void *context, unsigned char *dest,
                     const unsigned char *src, size_t len,
                     unsigned char *iv)
{
  if (src != dest)
    memcpy(dest, src, len);
}

/* nociph.c */
