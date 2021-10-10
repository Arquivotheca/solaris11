/*
  File: sshbuffer.h

  Authors:
        Tatu Ylonen <ylo@cs.hut.fi>

  Description:

        Code for manipulating variable-size buffers where you can
        easily append data and consume it from either end. Routines
        with prefix ssh_xbuffer will call ssh_fatal (thus not
        returning) if they fail to obtain memory.

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
        All rights reserved.
*/

#ifndef SSHBUFFER_H
#define SSHBUFFER_H

typedef enum
{
  SSH_BUFFER_OK = 0,
  SSH_BUFFER_ERROR
} SshBufferStatus;

/* This is buffer record. Even if its contents are visible here, one
   should use the function interface to access them. The content is
   visible here only to allow allocation from the stack. */
typedef struct SshBufferRec
{
  unsigned char *buf;           /* SshBuffer for data. */
  size_t offset;                /* Offset of first byte containing data. */
  size_t end;                   /* Offset of last byte containing data. */
  size_t alloc;                 /* Number of bytes allocated for data. */
  /* The `dynamic' flag tells whether or not this struct is allocated
     by a call to `ssh_malloc', in which case `dynamic' is TRUE, or
     whether this struct is allocated from stack, in a global
     variable, or inside another heap-allocated object, in which case
     the `dynamic' flag is FALSE.  It is used as a sanity check. */
  Boolean dynamic;
  /* The `borrowed' flag is TRUE if and only if the `buf' is memory
     managed by this struct.  This is the default, but it is possible to
     use `ssh_buffer_wrap' to wrap a SshBuffer around given memory, in
     which case we shall not resize the buf. */
  Boolean borrowed;
  SshUInt16 size_index;    /* Index to a table giving the size of the
                              buffer in bytes. */
} *SshBuffer, SshBufferStruct;

/* Allocates and initializes a new buffer structure. */

SshBuffer ssh_buffer_allocate(void);
SshBuffer ssh_xbuffer_allocate(void);

/* Zeroes and frees any memory used by the buffer and its data structures. */

void ssh_buffer_free(SshBuffer buffer);

/* Initializes an already allocated buffer structure. */

void ssh_buffer_init(SshBuffer buffer);

/* Frees any memory used by the buffer, first zeroing the whole area.
   The buffer structure itself is not freed. */

void ssh_buffer_uninit(SshBuffer buffer);

/* Wrap a given memory area `mem' of length `n_bytes' inside the given
   SshBuffer `buf'.  The `buffer' is assumed uninited. */

void ssh_buffer_wrap(SshBuffer buffer, unsigned char *mem, size_t n_bytes);

/* Move the buffer's content to the beginning of the allocated memory,
   realloc it to the current size, and leave the buffer in an uninited
   state. Fill in the returned buffer size into 'len' if it not a NULL
   pointer. */

unsigned char *ssh_buffer_steal(SshBuffer buffer, size_t *len);

/* Clears any data from the buffer, making it empty.  This does not
   zero the memory.  This does not free the memory used by the buffer. */

void ssh_buffer_clear(SshBuffer buffer);

/* Appends data to the buffer, expanding it if necessary. */

SshBufferStatus ssh_buffer_append(SshBuffer buffer,
                                  const unsigned char *data, size_t len);

void ssh_xbuffer_append(SshBuffer buffer,
                        const unsigned char *data, size_t len);

/* Appends space to the buffer, expanding the buffer if necessary.
   This does not actually copy the data into the buffer, but instead
   returns a pointer to the allocated region. */

SshBufferStatus ssh_buffer_append_space(SshBuffer buffer,
                                        unsigned char **datap, size_t len);
void ssh_xbuffer_append_space(SshBuffer buffer,
                              unsigned char **datap, size_t len);

/* Appends NUL-terminated C-strings <...> to the buffer.  The argument
   list must be terminated with a NULL pointer. */

SshBufferStatus ssh_buffer_append_cstrs(SshBuffer buffer, ...);

void ssh_xbuffer_append_cstrs(SshBuffer buffer, ...);


SshBufferStatus ssh_buffer_append_cstrs_va(SshBuffer buffer, va_list ap);


/* Returns the number of bytes of data in the buffer. */

size_t ssh_buffer_len(const SshBuffer buffer);

/* Returns the number of bytes allocated, but not yet in use. */

size_t ssh_buffer_space(const SshBuffer buffer);

/* Consumes the given number of bytes from the beginning of the buffer. */

void ssh_buffer_consume(SshBuffer buffer, size_t bytes);

/* Consumes the given number of bytes from the end of the buffer. */

void ssh_buffer_consume_end(SshBuffer buffer, size_t bytes);

/* Returns a pointer to the first used byte in the buffer. */

unsigned char *ssh_buffer_ptr(const SshBuffer buffer);

#endif /* SSHBUFFER_H */
