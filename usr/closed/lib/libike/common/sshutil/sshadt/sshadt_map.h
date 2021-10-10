/*

  sshadt_map.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Tue Sep 14 09:26:13 1999.

  */

#ifndef SSH_ADT_MAP_H_INCLUDED
#define SSH_ADT_MAP_H_INCLUDED

#include "sshadt.h"


extern const SshADTContainerType ssh_adt_map_type;

#define SSH_ADT_MAP (ssh_adt_map_type)

/* Use this instead of SshADTHeaderStruct (it's smaller).  */
typedef struct {
  Boolean a; void *b, *c;
} SshADTMapHeaderStruct;


#endif /* SSH_ADT_MAP_H_INCLUDED */
