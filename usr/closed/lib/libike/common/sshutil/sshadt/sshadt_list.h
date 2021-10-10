/*

  sshadt_list.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Tue Sep 14 09:21:41 1999.

  */

#ifndef SSH_ADT_LIST_H_INCLUDED
#define SSH_ADT_LIST_H_INCLUDED

#include "sshadt.h"

extern const SshADTContainerType ssh_adt_list_type;

#define SSH_ADT_LIST (ssh_adt_list_type)

/* Sort a list destructively in ascending order (smallest objects
   first). */
void ssh_adt_list_sort(SshADTContainer c);

/* Type for inlined list headers.  (Users only need to know the type
   of this; it is only provided so that one doesn't need to store the
   20 bytes of SshADTHeader.)  */
typedef struct {
  void *a, *b;
} SshADTListHeaderStruct;

#endif /* SSH_ADT_LIST_H_INCLUDED */
