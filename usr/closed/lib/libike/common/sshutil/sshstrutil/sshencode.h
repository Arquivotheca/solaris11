/*

sshencode.h

Author: Tero Kivinen <kivinen@ssh.fi>
        Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

Helper functions for encoding/decoding binary data.

Functions with prefix ssh_xencode will call ssh_fatal internally in
case they can not allocate memory for the operations.

*/

#ifndef SSHENCODE_H
#define SSHENCODE_H

#include "sshbuffer.h"

/* Encode object in `datum' into buffer `buf' whose size is `len' bytes.

   Return values:
   [0,len]   number of bytes datums prentation took in the buffer.
   ]len,inf[ amount of space writing datum would require.

   If buf can be extented, this will be done by the driver, and the
   encoder function will be called again. If buf can not be extented
   (either it is static, or allocating space fails at the driver,
   ssh_encode call will return error. */
typedef int (*SshEncodeDatum)(unsigned char *buf, size_t len,
                              void *datum);

/* Decode */
typedef int (*SshDecodeDatum)(const unsigned char *buf, size_t len,
                              void **datum);

/* The packet encoding/decoding functions take a variable number of
   arguments, and decode data from a SshBuffer or a character array as
   specified by a format.  Each element of the format contains a type
   specifier, and arguments depending on the type specifier.  The list
   must end with a SSH_FORMAT_END specifier. */

typedef enum {
  /* Specifies a string with uint32-coded length.  This has two arguments.
     For encoding,
         const unsigned char *data
         size_t len
     For decoding,
         unsigned char **data_return
         size_t *len_return
     When decoding, either or both arguments may be NULL, in which case they
     are not stored.  The returned data is allocated by ssh_xmalloc, and an
     extra nul (\0) character is automatically added at the end to make it
     easier to retrieve strings. */
  SSH_FORMAT_UINT32_STR,        /* Encode unsigned char *, size_t   */
                                /* Decode unsigned char *, size_t * */

  /* This code can only be used while decoding.  This specifies string with
     uint32-coded length.  This has two arguments:
       const unsigned char **data_return
       size_t *len_return
     Either argument may be NULL.  *data_return is set to point to the data
     in the packet, and *len_return is set to the length of the string.
     No null character is stored, and the string remains in the original
     buffer.  This can only be used with ssh_decode_array. */
  SSH_FORMAT_UINT32_STR_NOCOPY, /* char *, size_t */

  /* An 32-bit MSB first integer value. */
  SSH_FORMAT_UINT32,            /* SshUInt32, note that if you encode constant
                                   integer, you still must use (SshUInt32) cast
                                   before it. Also enums must be casted to
                                   SshUInt32 before encoding. */

  /* A boolean value.  For encoding, this has a single "Boolean" argument.
     For decoding, this has a "Boolean *" argument, where the value will
     be stored.  The argument may be NULL in which case the value is not
     stored. */
  SSH_FORMAT_BOOLEAN,           /* Boolean */

  /* Application specific value given as `void *' argument is encoded
     using SshEncodeRenderer function, or decoded using SshDecodeRenderer.
     For information about renderers, see their definitions. */
  SSH_FORMAT_SPECIAL,           /* SshEncodeRenderer, void * */
                                /* SshDecodeRenderer, void ** */

  /* A single one-byte character.  The argument is of type "unsigned int"
     when encoding, and of type "unsigned int *" when decoding.  The value
     may also be NULL when decoding, in which case the value is ignored. */
  SSH_FORMAT_CHAR,              /* unsigned int */

  /* A fixed-length character array, without explicit length.  When
     encoding, the arguments are
         const unsigned char *buf
         size_t len
     and when decoding,
         unsigned char *buf
         size_t len
     The buffer must be preallocated when decoding; data is simply copied
     there.  `buf' may also be NULL, in which the value is ignored. */
  SSH_FORMAT_DATA,              /* char * (fixed length!), size_t */

  /* A 64-bit MSB first integer value.  For encoding, this has a single
     "SshUInt64" argument (the value), and for decoding an
     "SshUInt64 *" argument, where the value will be stored.  The argument
     may be NULL in which case the value is not stored. */
  SSH_FORMAT_UINT64,            /* SshUInt64 */

  /* Marks end of the argument list. */
  SSH_FORMAT_END = 0x0d0e0a0d
} SshEncodingFormat;

/* Appends data at the end of the buffer as specified by the
   variable-length argument list.  Each element must start with a
   SshEncodingFormat type, be followed by arguments of the appropriate
   type, and the list must end with SSH_FORMAT_END.  This returns the
   number of bytes added to the buffer. */

size_t ssh_encode_buffer(SshBuffer buffer,...);
size_t ssh_xencode_buffer(SshBuffer buffer, ...);

/* Appends data at the end of the buffer as specified by the
   variable-length argument list.  Each element must start with a
   SshEncodingFormat type, be followed by arguments of the appropriate
   type, and the list must end with SSH_FORMAT_END.  This returns the
   number of bytes added to the buffer. */
size_t ssh_encode_buffer_va(SshBuffer buffer, va_list ap);
size_t ssh_xencode_buffer_va(SshBuffer buffer, va_list ap);

/* Decodes and consumes data from the given buffer as specified by the
   variable-length argument list.  If all the specified arguments
   could he successfully parsed, returns the number of bytes parsed
   and consumes the parsed data from the buffer.  If parsing results
   in an error, or the buffer does not contain enough data, 0 is
   returned and nothing is consumed from the buffer. */
size_t ssh_decode_buffer(SshBuffer buffer,...);
size_t ssh_xdecode_buffer(SshBuffer buffer,...);

/* Decodes and consumes data from the given buffer as specified by the
   variable-length argument list.  If all the specified arguments
   could he successfully parsed, returns the number of bytes parsed
   and consumes the parsed data from the buffer.  If parsing results
   in an error, or the buffer does not contain enough data, 0 is
   returned and nothing is consumed from the buffer. */
size_t ssh_decode_buffer_va(SshBuffer buffer,va_list ap);
size_t ssh_xdecode_buffer_va(SshBuffer buffer,va_list ap);

/* Encodes the given data to a given buffer as specified by the
   variable-length argument list. If the given buffer cannot hold the
   encoded data, 0 is returned and the given buffer is left in
   undefined state. */
size_t ssh_encode_array(unsigned char *buf, size_t bufsize,...);
size_t ssh_xencode_array(unsigned char *buf, size_t bufsize,...);

/* Encodes the given data to a given buffer as specified by the
   variable-length argument list. If the given buffer cannot hold the
   encoded data, 0 is returned and the given buffer is left in
   undefined state. */
size_t ssh_encode_array_va(unsigned char *buf, size_t bufsize, va_list ap);
size_t ssh_xencode_array_va(unsigned char *buf, size_t bufsize, va_list ap);

/* Encodes the given data.  Returns the length of encoded data in
   bytes, and if `buf_return' is non-NULL, it is set to a memory area
   allocated by ssh_xmalloc that contains the data.  The caller should
   free the data when no longer needed. */
size_t ssh_encode_array_alloc(unsigned char **buf_return, ...);
size_t ssh_xencode_array_alloc(unsigned char **buf_return, ...);

/* Encodes the given data.  Returns the length of encoded data in
   bytes, and if `buf_return' is non-NULL, it is set to a memory area
   allocated by ssh_xmalloc that contains the data.  The caller should
   free the data when no longer needed. */
size_t ssh_encode_array_alloc_va(unsigned char **buf_return, va_list ap);
size_t ssh_xencode_array_alloc_va(unsigned char **buf_return, va_list ap);

/* Decodes data from the given byte array as specified by the
   variable-length argument list.  If all specified arguments could be
   successfully parsed, returns the number of bytes parsed (any
   remaining data can be parsed by first skipping this many bytes).
   If parsing any element results in an error, this returns 0 (and
   frees any already allocated data).  Zero is also returned if the
   specified length would be exceeded. */
size_t ssh_decode_array(const unsigned char *buf, size_t len, ...);
size_t ssh_xdecode_array(const unsigned char *buf, size_t len, ...);

/* Decodes data from the given byte array as specified by the
   variable-length argument list.  If all specified arguments could be
   successfully parsed, returns the number of bytes parsed (any
   remaining data can be parsed by first skipping this many bytes).
   If parsing any element results in an error, this returns 0 (and
   frees any already allocated data).  Zero is also returned if the
   specified length would be exceeded. */
size_t ssh_decode_array_va(const unsigned char *buf, size_t len, va_list ap);
size_t ssh_xdecode_array_va(const unsigned char *buf, size_t len, va_list ap);

#endif /* SSHENCODE_H */
