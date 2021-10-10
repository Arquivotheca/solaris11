/*

  sshadt_map_i.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Thu Sep  9 15:39:16 1999.

  */

#ifndef SSHADT_MAP_I_H_INCLUDED
#define SSHADT_MAP_I_H_INCLUDED

#include "sshadt.h"

struct ssh_adt_map_node;

typedef struct ssh_adt_map_node {
  Boolean is_last_in_rib;
  union
  {
    struct ssh_adt_map_node *next;
    struct ssh_adt_map_node **rib_start;
  } u;
  void *image;
} SshADTMapNode;

typedef struct ssh_adt_map_enode {
  void *object;
  SshADTMapNode n;
} SshADTMapENode;

typedef struct {
  SshADTMapNode **nodes;
  size_t array_size;
  size_t prev_array_size;
  int    num_objects;
} SshADTMapRoot;

extern const SshADTStaticData ssh_adt_map_static_data;

#endif /* SSHADT_MAP_I_H_INCLUDED */
