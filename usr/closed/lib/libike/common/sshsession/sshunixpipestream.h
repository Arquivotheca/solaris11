/*

sshunixpipestream.h

Author: Tatu Ylonen <ylo@ssh.fi>

Copyright (c) 1997 SSH Communications Security, Finland
                   All rights reserved

Functions for creating a pipe to a child process.  The functions in this
module essentially fork and set up stdin/stdout/stderr in the child process,
and return streams for them in the parent.

*/

#ifndef PIPESTREAM_H
#define PIPESTREAM_H

#include "sshstream.h"

typedef enum {
  SSH_PIPE_ERROR,     /* Operation failed (in parent). */
  SSH_PIPE_PARENT_OK, /* Successful, returned on parent side. */
  SSH_PIPE_CHILD_OK   /* Successful, returned on child side. */
} SshPipeStatus;

/* Forks the current process, creates pipes for its stdin/stdout/stderr,
   and returns separately in the parent and child.  In the parent,
   SshStreams are returned for stdin/stdout and separately for stderr,
   and in the child stdin/stdout/stderr are set to the pipes.
     `stdio_return'    set to stdin/stdout stream in parent
     `stderr_return'   set to stderr if non-NULL; otherwise stderr left
                       to be the parent's stderr. */
SshPipeStatus ssh_pipe_create_and_fork(SshStream *stdio_return,
                                       SshStream *stderr_return);

/* Retrieves the process id of the child. */
pid_t ssh_pipe_get_pid(SshStream stream);

/* Returns the exit status of the process running on the other side of the
   pipe.  It is illegal to call this before EOF has been received from
   the pipe stream.  However, it is guaranteed that once EOF has been received,
   this will return a valid value.  The returned value is either the exit
   status of the process (>= 0) or the negated signal number that caused
   it to terminate (< 0).
     `stream'    the stdio stream returned by ssh_pipe_create_and_fork. */
int ssh_pipe_get_exit_status(SshStream stream);

#endif /* PIPESTREAM_H */
