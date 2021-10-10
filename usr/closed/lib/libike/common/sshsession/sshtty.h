/*

sshtty.h

Author: Tatu Ylonen <ylo@ssh.com>

Copyright (c) 1997-2000 SSH Communications Security, Finland
              All rights reserved

*/

#ifndef SSHTTY_H
#define SSHTTY_H


/* Returns the terminal to normal mode if it had been put in raw 
   mode.  If fd is negative, assume stdin. */
void ssh_leave_raw_mode(int fd);

/* Puts the terminal in raw mode.  If fd is negative, assume stdin. */
void ssh_enter_raw_mode(int fd, Boolean want_kbd_signals);

/* Puts stdin terminal in non-blocking mode.  If fd is negative, 
   assume stdin.*/
void ssh_leave_non_blocking(int fd);

/* Restores stdin to blocking mode.  If fd is negative, assume stdin. */
void ssh_enter_non_blocking(int fd);

#endif /* !SSHTTY_H */
