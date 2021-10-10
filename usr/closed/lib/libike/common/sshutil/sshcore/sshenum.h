/*

sshenum.h

Author: Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

Created: Wed Aug 21 22:46:35 1996 ylo

Functions for mapping keywords to numbers and vice versa.

*/


#ifndef SSHENUM_H
#define SSHENUM_H

/* Array of keyword - numeric value pairs.  The array is terminated by
   an entry with NULL name. */
typedef struct
{
  const char *name;
  long code;
} *SshKeyword, SshKeywordStruct;


/* Finds the name of a keyword corresponding to the numeric value.
   Returns a pointer to a constant name string, or NULL if there is no
   keyword matching the numeric value. */
const char *ssh_find_keyword_name(const SshKeywordStruct *keywords, long code);

/* Finds the number corresponding to the given keyword.  Returns the number,
   or -1 if there is no matching keyword.  The comparison is case-sensitive. */
long ssh_find_keyword_number(const SshKeywordStruct *names,
                             const char *name);

/* Finds the longist prefix from keyword table. Returns the assisiated number,
   or -1 if there is no matching keyword. The comparison is case-sensitive.
   The `endp' pointer is modifier to points to the end of found keyword if
   it is not NULL. */
long ssh_find_partial_keyword_number(const SshKeywordStruct *names,
                                     const char *name, const char **endp);

/* Finds the number corresponding to the given keyword.  Returns the number,
   or -1 if there is no matching keyword.  The comparison is
   case-insensitive. */
long ssh_find_keyword_number_case_insensitive(const SshKeywordStruct *names,
                                              const char *name);

/* Finds the longist prefix from keyword table. Returns the assisiated number,
   or -1 if there is no matching keyword. The comparison is case-insensitive.
   The `endp' pointer is modifier to points to the end of found keyword if
   it is not NULL. */
long ssh_find_partial_keyword_number_case_insensitive(const
                                                      SshKeywordStruct *names,
                                                      const char *name,
                                                      const char **endp);


#endif /* SSHENUM_H */
