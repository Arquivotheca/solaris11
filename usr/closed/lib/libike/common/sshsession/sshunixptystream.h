/*

sshunixptystream.h

Author: Tatu Ylonen <ylo@ssh.fi>

Copyright (c) 1997 SSH Communications Security, Finland
                   All rights reserved

*/

/*
 * $Id: sshunixptystream.h,v 1.4 2001/04/24 18:29:53 fis Exp $
 * $Log: sshunixptystream.h,v $
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 * $EndLog$
 */

#ifndef PTYSTREAM_H
#define PTYSTREAM_H

#include "sshstream.h"

/* Max length of pty name, in characters. */
#define SSH_PTY_NAME_SIZE   64

typedef enum {
  SSH_PTY_ERROR,      /* Operation failed (in parent). */
  SSH_PTY_PARENT_OK,  /* Successful, returned on parent side. */
  SSH_PTY_CHILD_OK    /* Successful, returned on child side. */
} SshPtyStatus;

/* Allocates a pty, forks the current process, and returns separately
   in parent and child.  Makes the pty the controlling tty and stdio
   in the child; makes the child a process group leader.  The pty is
   freed when the master side is closed.  It is guaranteed that when
   the child exits, EOF will be received from the stream.  This
   function is unix-specific, and needs to be called as root (calling
   as another user may work partially on some systems).  This will
   arrange for the pty to be cleanly freed when the parent-side stream
   is closed.  The parent side must remain root until is has closed
   the stream.  A stream for the pty will be returned only on master side.
   The uid and gid are only used for setting ownership of the pty; this
   does not switch to be the given user. */
SshPtyStatus ssh_pty_allocate_and_fork(uid_t owner_uid, gid_t owner_gid,
                                       char *namebuf,
                                       SshStream *master_return);

/* Retrieves the name of the pty.  Returns TRUE on success, FALSE if the stream
   is not a pty stream.  This can be used on both parent and child side. */
Boolean ssh_pty_get_name(SshStream stream, char *buf, size_t buflen);

/* Retrieves the process id of the child. */
pid_t ssh_pty_get_pid(SshStream stream);

/* Returns the exit status of the process running on the other side of the
   pty.  It is illegal to call this before EOF has been received from
   the pty stream.  However, it is guaranteed that once EOF has been received,
   this will return a valid value.  The returned value is either the exit
   status of the process (>= 0) or the negated signal number that caused
   it to terminate (< 0). */
int ssh_pty_get_exit_status(SshStream stream);

/* Changes window size on the pty.  This function can only be used on the
   parent side. */
void ssh_pty_change_window_size(SshStream stream,
                                unsigned int width_chars,
                                unsigned int height_chars,
                                unsigned int width_pixels,
                                unsigned int height_pixels);

/* Returns the file descriptor for the pty. This should be used only
   for things that don't change this stream, and care should be taken
   that nothing is destroyed.*/
int ssh_pty_get_fd(SshStream stream);

/* Returns true if the pty is in a mode with C-S/C-Q flow control enabled.
   This can be used to determine whether a client can perform local flow
   control.  This function can only be called for the parent side.
   Note that there is no special notification when the status changes.
   However, a SSH_STREAM_INPUT_AVAILABLE notification will be generated
   whenever there is a status change, even if no real data is available.
   Thus, the read callback handler should read this state and check for change
   before returning.  This returns TRUE if standard C-S/C-Q flow control
   is enabled, and FALSE otherwise. */
Boolean ssh_pty_standard_flow_control(SshStream stream);

#endif /* PTYSTREAM_H */
