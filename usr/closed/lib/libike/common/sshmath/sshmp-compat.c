/*

  sshmp-compat.c

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Thu Mar  8 04:30:42 2001.

  */

#include "sshincludes.h"
#include "sshmp.h"

char *ssh_mprz_get_str_compat(char *str, unsigned int base,
                              SshMPIntegerConst op)
{
  char *out;
  out = ssh_mprz_get_str(op, base);

  if (str && out)
    {
      strcpy(str, out);
      ssh_free(out);
      return str;
    }
  return out;
}
