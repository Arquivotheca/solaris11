/*

  sshobstack.h

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Sat Feb 15 20:02:47 1997 [mkojo]

  Mallocation to a context, with out possibility to free specific elements.

*/

#ifndef SSHOBSTACK_H
#define SSHOBSTACK_H

typedef struct SshObStackContextRec *SshObStackContext;

/* Initialize the mallocation context. This same context can be used for
   all data, that is rather static i.e. need not to be freed separately.
   Of course this method can be used for allocation in general, but it is
   not recommended. */

SshObStackContext ssh_obstack_create(void);

/* Free all data allocated using this particular context. This function
   makes all allocated space invalid. */

void ssh_obstack_destroy(SshObStackContext context);

/* Allocate byte buffer of length size from the context. If enough
   memory is not available the function will return NULL. */

/* Allocated data is not aligned. */
unsigned char *ssh_obstack_alloc_unaligned(SshObStackContext context,
                                           size_t size);

/* Allocated data is aligned to 8-byte boundary or if item is less than 8 bytes
   then aligned to 4 (4-7 bytes) or 2 (2-3) byte boundary. This should be
   enough for all datatypes. */
void *ssh_obstack_alloc(SshObStackContext context, size_t size);

#endif /* SSHOBSTACK_H */
