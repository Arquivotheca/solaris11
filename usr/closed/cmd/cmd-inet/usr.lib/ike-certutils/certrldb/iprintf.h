/*
  iprintf.h

  Author:
        Mika Kojo <mkojo@ssh.fi>

  Copyright (c) 1999, 2000, 2001 SSH Communications Security Corp, Finland
  All rights reserved.

*/
#ifndef IPRINTF_H
#define IPRINTF_H
#include <ike/sshmp.h>
void iprintf_set(int line_width, int indent_level, int indent_step);
void iprintf(const char *str, ...);
Boolean cu_dump_number(SshMPInt number);
Boolean cu_dump_pub(SshPublicKey pub);
Boolean cu_dump_prv(SshPrivateKey prv);

#endif /* IPRINTF_H */
