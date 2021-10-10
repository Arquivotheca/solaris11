/*

  sshadt_priority_heap_i.h

  Author: Kenneth Oksanen <cessu@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  */

#ifndef SSH_ADT_PRIORITY_HEAP_I_H_INCLUDED
#define SSH_ADT_PRIORITY_HEAP_I_H_INCLUDED

#include "sshadt.h"

typedef struct SshADTPriorityHeapNodeRec {
  SshUInt32 height;
  struct SshADTPriorityHeapNodeRec *left, *right, *parent;
} SshADTPriorityHeapNodeStruct, *SshADTPriorityHeapNode;

typedef struct {
  void *object;
  SshADTPriorityHeapNodeStruct n;
} SshADTPriorityHeapENodeStruct, *SshADTPriorityHeapENode;

typedef struct {
  SshADTPriorityHeapNode min;
} SshADTPriorityHeapRootStruct;


#endif /* SSH_ADT_PRIORITY_HEAP_I_H_INCLUDED */
