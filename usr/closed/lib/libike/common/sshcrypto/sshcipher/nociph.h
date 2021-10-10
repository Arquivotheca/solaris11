/*

  nociph.h

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Sat Nov  2 04:22:51 1996 [mkojo]

  Cipher 'none'.

  */

#ifndef NOCIPH_H
#define NOCIPH_H

void ssh_none_cipher(void *context, unsigned char *dest,
                     const unsigned char *src, size_t len,
                     unsigned char *iv);

#endif /* NOCIPH_H */
