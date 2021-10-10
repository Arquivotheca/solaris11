/*

  sshobstack.c

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Helsinki, Finland
  All rights reserved.

  Created: Sat Feb 15 19:37:35 1997 [mkojo]

  Memory allocation from a context. These routines allocate data to a
  context, to be freed by one call to ssh_obstack_free. There is no
  other way of freeing data, than freeing it all. */

#include "sshincludes.h"
#include "sshobstack.h"

#define SSH_DEBUG_MODULE "SshObstack"

typedef struct SshObStackDataRec
{
  struct SshObStackDataRec *next;

  unsigned char *ptr;
  size_t free_bytes;
} SshObStackData;

/* Main context for all allocated data through obstack. Uses buckets
   of different sizes. Reason for this is to minimize the space needed
   and to make it more probable that there exists already enough
   allocated memory in the obstack context. */

/* Minimum amount of allocation is 1024 (2^10) bytes. */
#define SSH_OBSTACK_BUCKET_START 10

/* Maximum amount of allocation is
     2^(SSH_OBSTACK_BUCKET_START-1) * (2^SSH_OBSTACK_BUCKET_COUNT)
   = 2^(SSH_OBSTACK_BUCKET_START+SSH_OBSTACK_BUCKET_COUNT-1)
   = 2^24 = 16Mb
 */
#define SSH_OBSTACK_BUCKET_COUNT 15

struct SshObStackContextRec
{
  SshObStackData *bucket[SSH_OBSTACK_BUCKET_COUNT];
};

/* Initialize the obstack context. Clear all buckets. */

SshObStackContext ssh_obstack_create(void)
{
  SshObStackContext created = ssh_malloc(sizeof(*created));
  int i;

  if (created)
    {
      for (i = 0; i < SSH_OBSTACK_BUCKET_COUNT; i++)
        created->bucket[i] = NULL;
    }
  return created;
}

void ssh_obstack_destroy(SshObStackContext context)
{
  SshObStackData *temp, *next;
  int i;

  /* Free all data in buckets. */
  for (i = 0; i < SSH_OBSTACK_BUCKET_COUNT; i++)
    {
      temp = context->bucket[i];
      context->bucket[i] = NULL;
      while (temp)
        {
          next = temp->next;
          ssh_free(temp);
          temp = next;
        }
    }
  /* Free the context also. */
  ssh_free(context);
}

static unsigned char *
ssh_obstack_internal(SshObStackContext context, size_t size, size_t align)
{
  unsigned char *ptr;
  SshObStackData *data;
  unsigned int i;
  size_t bucket_size;
  unsigned int alignment;

  if (size == 0)
    {
      SSH_DEBUG(SSH_D_ERROR, ("Tried to allocate ZERO bytes"));
      return NULL;
    }

  if (size > (1 << (SSH_OBSTACK_BUCKET_COUNT + SSH_OBSTACK_BUCKET_START - 1)))
    {
      SSH_DEBUG(SSH_D_ERROR,
                ("Tried to allocate too much (%d bytes).", size));
      return NULL;
    }

  /* Select bucket. */
  for (bucket_size = (1 << SSH_OBSTACK_BUCKET_START), i = 0;
       bucket_size < size;
       bucket_size <<= 1, i++)
    ;

  if (context->bucket[i] != NULL)
    {
      /* Compute align_ptr */
      alignment = (unsigned long)(context->bucket[i]->ptr) & (align - 1);
      if (alignment != 0x0)
        {
          if (context->bucket[i]->free_bytes >= size + (align - alignment))
            {
              ptr = context->bucket[i]->ptr + (align - alignment);
              context->bucket[i]->ptr += size + (align - alignment);
              context->bucket[i]->free_bytes -= (size + (align - alignment));
              return ptr;
            }
        }
      else
        /* Check if enough data is available. */
        if (context->bucket[i]->free_bytes >= size)
          {
            ptr = context->bucket[i]->ptr;
            context->bucket[i]->ptr += size;
            context->bucket[i]->free_bytes -= size;
            return ptr;
          }
    }

  /* Not enough space. */

  /* Allocate just one new small block of data and link it as first of
     the particular bucket list.

     Here we can skip the alignment checking because ssh_malloc
     always aligns correctly (and we don't want to align more than
     what is needed). */
  data =
    (SshObStackData *)ssh_malloc(bucket_size + size + sizeof(SshObStackData));
  if (data)
    {
      data->next = context->bucket[i];
      context->bucket[i] = data;
      context->bucket[i]->free_bytes = bucket_size + size;

      /* However here we need to do some checking (to make the data ptr
         aligned). */
      alignment = sizeof(SshObStackData) & (align - 1);
      if (alignment != 0x0)
        {
          context->bucket[i]->ptr =
            ((unsigned char *)data) + sizeof(SshObStackData) +
            (align - alignment);
          context->bucket[i]->free_bytes -= (align - alignment);
        }
      else
        context->bucket[i]->ptr = ((unsigned char *)data) +
          sizeof(SshObStackData);

      /* Give the caller requested amount of data. */

      ptr = context->bucket[i]->ptr;
      context->bucket[i]->ptr += size;
      context->bucket[i]->free_bytes -= size;
    }
  else
    ptr = NULL;

  return ptr;
}

unsigned char *
ssh_obstack_alloc_unaligned(SshObStackContext context, size_t size)
{
  return ssh_obstack_internal(context, size, 1);
}

void *ssh_obstack_alloc(SshObStackContext context, size_t size)
{
  return (void *)ssh_obstack_internal(context, size, size >= 8 ? 8 :
                                      (size >= 4 ? 4 :
                                       (size >= 2 ? 2 : 1)));
}

/* sshobstack.c */
