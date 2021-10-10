/*

  sshadt_bag.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Fri Sep 24 13:24:19 1999.

  */

#ifndef SSH_ADT_BAG_H_INCLUDED
#define SSH_ADT_BAG_H_INCLUDED

#include "sshadt.h"


extern const SshADTContainerType ssh_adt_bag_type;

#define SSH_ADT_BAG (ssh_adt_bag_type)

/* Use this instead of SshADTHeaderStruct if you want to save 8 bytes
   of memory per header.  */
typedef struct {
  Boolean a; void *b, *c;
} SshADTBagHeaderStruct;


#endif /* SSH_ADT_BAG_H_INCLUDED */
