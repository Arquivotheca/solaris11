/*

  Author: Tomi Salo <ttsalo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Mon Jul  8 17:40:06 1996 [ttsalo]

  sshsignals.h

  Derived straight from signals.c

  */

#ifndef SSHSIGNALS_H
#define SSHSIGNALS_H

/* Prevent signals for dumping core. This should be called when we have
   decrypted private key or some other secret in memory, which we don't want to
   be written to core dump file. This also sets the RLIMIT_CORE to zero if
   supported by system. */
/* XXX Remove use_eloop and ctx arguments */
void
ssh_signals_prevent_core(Boolean use_eloop, void *ctx);

/* Reset all signal handlers back to default state. This also restores the
   RLIMIT_CORE. */
void
ssh_signals_reset(void);

#endif /* SSHSIGNALS_H */
