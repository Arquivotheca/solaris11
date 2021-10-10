/*

  sshbprintf.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Wed Oct 13 15:11:40 1999.

  */

#ifndef SSH_BPRINTF_INCLUDED
#define SSH_BPRINTF_INCLUDED

#include "sshbuffer.h"

/* Same as ssh_snprintf or ssh_dsprintf, but prints to SshBuffer. */
int ssh_bprintf(SshBuffer buf, const char *format, ...);
int ssh_vbprintf(SshBuffer buf, const char *format, va_list ap);
int ssh_xbprintf(SshBuffer buf, const char *format, ...);
int ssh_xvbprintf(SshBuffer buf, const char *format, va_list ap);

#endif /* SSH_BPRINTF_INCLUDED */
