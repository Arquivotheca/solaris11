/*

  sshbprintf.c

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Wed Oct 13 15:16:58 1999.

  */

#include "sshincludes.h"
#include "sshsnprintf.h"
#include "sshdebug.h"
#include "sshbprintf.h"
#include "sshdsprintf.h"

/* XXX: These are quick hacks to get the idea off the ground. These
   need to be converted into more efficient versions which won't do
   unnecessary allocation and copying.

   This is probably best accomplised by using a "driver" function or
   macro which handles the parsing, and separate auxillary
   functions/macros that handle the actual string/buffer modify
   functions, so that the actual formatting engine is the same for
   both strings and buffers to avoid code forking, but the code itself
   would be duplicated verbatim. */

int ssh_vbprintf(SshBuffer buf, const char *format, va_list ap)
{
  char *str;
  size_t len, size;

  size = 0;
  str = NULL;
  len = ssh_dvsprintf(&str, format, ap);

  if (len > 0 &&
      ssh_buffer_append(buf, (unsigned char *)str, len) == SSH_BUFFER_OK)
    size = len;
  ssh_free(str);
  return size;
}

int ssh_bprintf(SshBuffer buf, const char *format, ...)
{
  int ret;
  va_list ap;
  va_start(ap, format);

  ret = ssh_vbprintf(buf, format, ap);
  va_end(ap);

  return ret;
}

int ssh_xvbprintf(SshBuffer buf, const char *format, va_list ap)
{
  char *str;
  size_t len;

  str = NULL;
  len = ssh_xdvsprintf(&str, format, ap);
  ssh_xbuffer_append(buf, (unsigned char *)str, len);
  ssh_free(str);
  return len;
}

int ssh_xbprintf(SshBuffer buf, const char *format, ...)
{
  int ret;
  va_list ap;
  va_start(ap, format);

  ret = ssh_xvbprintf(buf, format, ap);
  va_end(ap);

  return ret;
}
