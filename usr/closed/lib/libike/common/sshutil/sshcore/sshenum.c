/*

sshenum.c

Author: Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

Created: Wed Aug 21 22:46:35 1996 ylo

Functions for mapping keywords to numbers and vice versa.

*/

#include "sshincludes.h"
#include "sshenum.h"

/* Finds the name of a keyword corresponding to the numeric value.
   Returns a pointer to a constant name string, or NULL if there is no
   keyword matching the numeric value. */

const char *ssh_find_keyword_name(const SshKeywordStruct *keywords, long code)
{
  int i;

  for (i = 0; keywords[i].name; i++)
    if (keywords[i].code == code)
      return keywords[i].name;
  return NULL;
}

/* Finds the number corresponding to the given keyword.  Returns the number,
   or -1 if there is no matching keyword.  The comparison is case-sensitive. */

long ssh_find_keyword_number(const SshKeywordStruct *keywords,
                             const char *name)
{
  int i;

  for (i = 0; keywords[i].name; i++)
    if (strcmp(keywords[i].name, name) == 0)
      return keywords[i].code;
  return -1;
}

/* Finds the number corresponding to the given keyword.  Returns the number,
   or -1 if there is no matching keyword.  The comparison is
   incase-sensitive. */

long ssh_find_keyword_number_case_insensitive(const SshKeywordStruct *keywords,
                                              const char *name)
{
  int i;

  for (i = 0; keywords[i].name; i++)
    if (strcasecmp(keywords[i].name, name) == 0)
      return keywords[i].code;
  return -1;
}

/* Finds the longist prefix from keyword table. Returns the assisiated number,
   or -1 if there is no matching keyword. The comparison is case-sensitive.
   The `endp' pointer is modifier to points to the end of found keyword if
   it is not NULL. */
long ssh_find_partial_keyword_number(const SshKeywordStruct * keywords,
                                     const char *name, const char **endp)
{
  int i, len, max_len;
  long ret;

  if (endp)
    *endp = name;
  max_len = 0;
  ret = -1;
  for (i = 0; keywords[i].name; i++)
    {
      len = strlen(keywords[i].name);
      if (strncmp(keywords[i].name, name, len) == 0)
        {
          if (len > max_len)
            {
              max_len = len;
              if (endp)
                *endp = name + max_len;
              ret = keywords[i].code;
            }
        }
    }
  return ret;
}

/* Finds the longist prefix from keyword table. Returns the assisiated number,
   or -1 if there is no matching keyword. The comparison is incase-sensitive.
   The `endp' pointer is modifier to points to the end of found keyword if
   it is not NULL. */
long ssh_find_partial_keyword_number_case_insensitive(const
                                                      SshKeywordStruct *
                                                      keywords,
                                                      const char *name,
                                                      const char **endp)
{
  int i, len, max_len;
  long ret;

  if (endp)
    *endp = name;
  max_len = 0;
  ret = -1;
  for (i = 0; keywords[i].name; i++)
    {
      len = strlen(keywords[i].name);
      if (strncasecmp(keywords[i].name, name, len) == 0)
        {
          if (len > max_len)
            {
              max_len = len;
              if (endp)
                *endp = name + max_len;
              ret = keywords[i].code;
            }
        }
    }
  return ret;
}
