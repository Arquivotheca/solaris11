/*

sigchld.h

Author: Tatu Ylonen <ylo@ssh.fi>

Copyright (c) 1997 SSH Communications Security, Finland
                   All rights reserved

*/

#ifndef SIGCHLD_H
#define SIGCHLD_H

/* This function is called when the process for which it is registered
   terminates.  `status' is the exit status of the process (>= 0) or the
   negated number of the signal that caused it to terminate (< 0).
   `pid' is its process id, and context is the argument given when the
   handler for the process was registered. */
typedef void (*SshSigChldHandler)(pid_t pid, int status, void *context);

/* Initializes the sigchld handler subsystem.  It is permissible to call
   this multiple times; only one initialization will be performed.
   It is guaranteed that after this has been called, it is safe to fork and
   call ssh_sigchld_register (in the parent) for the new process as long
   as the process does not return to the event loop in the meanwhile. */
void ssh_sigchld_initialize(void);

/* Registers the given function to be called when the specified
   process terminates.  Only one callback can be registered for any
   process; any older callbacks for the process are erased when a new
   one is registered. */
void ssh_sigchld_register(pid_t pid, SshSigChldHandler callback,
                          void *context);

/* Unregisters the given SIGCHLD callback. */
void ssh_sigchld_unregister(pid_t pid);

#endif /* SIGCHLD_H */
