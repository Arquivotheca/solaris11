/*

  sshdsprintf.h

  Author:
        Sami Lehtinen <sjl@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.
*/

#ifndef SSHDSPRINTF_H
#define SSHDSPRINTF_H

/* This function is similar to snprintf (indeed, this function, too,
   uses vsnprintf()); it takes a format argument which specifies the
   subsequent arguments, and writes them to a string using the
   format-string. This function differs from snprintf in that this
   allocates the buffer itself, and returns a pointer to the allocated
   string (in str). This function never fails.  (if there is not
   enough memory, ssh_xrealloc() calls ssh_fatal())

   The returned string must be freed by the caller. Returns the number
   of characters written.  */
int ssh_dsprintf(char **str, const char *format, ...);
int ssh_dvsprintf(char **str, const char *format, va_list ap);

/* Same as above, but calls ssh_fatal() if memory allocation fails. */
int ssh_xdsprintf(char **str, const char *format, ...);
int ssh_xdvsprintf(char **str, const char *format, va_list ap);

#endif /* SSHDSPRINTF_H */
