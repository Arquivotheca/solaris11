/*

  sshadt_array_i.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Tue Oct 19 12:19:23 1999.

  */

#ifndef SSH_ADT_ARRAY_I_H_INCLUDED
#define SSH_ADT_ARRAY_I_H_INCLUDED

#include "sshadt.h"

typedef struct {
  void **array;
  size_t array_size;
} SshADTArrayRoot;

#endif /* SSH_ADT_ARRAY_I_H_INCLUDED */
