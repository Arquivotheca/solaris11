/*

pty-int.h

Author: Tatu Ylonen <ylo@ssh.fi>

Copyright (c) 1997 SSH Communications Security, Finland
                   All rights reserved

Internal header for pty manipulation.

*/

#ifndef PTY_INT_H
#define PTY_INT_H

/* Make sure this is defined. */
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

/* Allocates a pty using a machine-specific method, and returns the
   master side pty in *ptyfd, the child side in *ttyfd, and the name of the
   device in namebuf.  Returns TRUE if successful. */
Boolean ssh_pty_internal_allocate(int *ptyfd, int *ttyfd, char *namebuf);

/* Makes the given tty the controlling tty of the current process.
   This may close and reopen the original file descriptor.  When called,
   *ttyfd should be a valid file descriptor for the slave side, and ttyname
   should contain its name (e.g., "/dev/ttyp3").  Returns FALSE if the
   controlling tty could not be set. */
Boolean ssh_pty_internal_make_ctty(int *ttyfd, const char *ttyname);

#endif /* PTY_INT_H */
