/*

  libmonitor.c

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Thu Jun 12 16:32:48 1997 [mkojo]

  Crypto library monitoring functions.

  */

#include "sshincludes.h"
#include "sshcrypt.h"
#include "libmonitor.h"

/* Global working pointer, that'll indicate the position of applications
   routine for handling these calls. */

static SshCryptoProgressMonitor
  ssh_crypto_progress_monitor_function = NULL_FNPTR;
static void *ssh_crypto_progress_context = NULL;

#ifdef SSHDIST_PLATFORM_VXWORKS
#ifdef VXWORKS
#ifdef ENABLE_VXWORKS_RESTART_WATCHDOG
void ssh_crypto_restart(void)
{
  ssh_crypto_progress_monitor_function = NULL;
  ssh_crypto_progress_context = NULL;
}
#endif /* ENABLE_VXWORKS_RESTART_WATCHDOG */
#endif /* VXWORKS */
#endif /* SSHDIST_PLATFORM_VXWORKS */

/* Interface from library. */

void ssh_crypto_progress_monitor(SshCryptoProgressID id,
                                 unsigned int time_value)
{
  if (ssh_crypto_progress_monitor_function != NULL_FNPTR)
    (*ssh_crypto_progress_monitor_function)(id, time_value,
                                            ssh_crypto_progress_context);
}

/* Interface from application. */

void
ssh_crypto_library_register_progress_func(SshCryptoProgressMonitor
                                          monitor_function,
                                          void *progress_context)
{
  if (monitor_function == NULL_FNPTR)
    {
      ssh_crypto_progress_monitor_function = NULL_FNPTR;
      ssh_crypto_progress_context = NULL;
      return;
    }

  ssh_crypto_progress_monitor_function = monitor_function;
  ssh_crypto_progress_context = progress_context;
  return;
}

/* libmonitor.c */
