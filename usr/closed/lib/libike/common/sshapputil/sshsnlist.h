/*

  Authors: Timo J. Rinne <tri@ssh.com>
           Sami Lehtinen <sjl@ssh.com>
           
  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                               Helsinki, Finland
  All rights reserved.

  Simple Namelist. Compute the section between two namelists, SSHv2
  style. Doesn't handle trees (see namelist.h in
  lib/sshcrypto/sshcryptocore).

  Inspired by namelist in sshcrypto.

*/
#ifndef _SSHSNLIST_H_
#define _SSHSNLIST_H_

/* Get the name following to 'namelist' pointer and ending with the
   next comma separator. Name string returned is zero terminated and
   is to be freed by caller with ssh_xfree.  Returns NULL if there are
   no more names or namelist is NULL. */
unsigned char *ssh_snlist_get_name(const unsigned char *namelist);

/* Step over to the next name. Returns the pointer to the next name, or NULL
   if there are no more names in the list. */
const unsigned char *ssh_snlist_step_forward(const unsigned char *namelist);

/* Compute the intersection between string `src1' and `src2'.
   Format for inputs and output is "name1,name2,...,namen".
   The caller must free the returned string with ssh_xfree.
   The output list will contain the names in the order in which they
   are listed in the first list. */
unsigned char *ssh_snlist_intersection(const unsigned char *src1,
                                       const unsigned char *src2);

/*
   True if list `list' contains item `item'.
*/
Boolean ssh_snlist_contains(const unsigned char *namelist,
                            const unsigned char *item);

/* Appends the second list to the first list. */
void ssh_snlist_append(unsigned char **list, const unsigned char *item);

/*
   Return a name list that contains items in list `original'
   so that items in list `excluded' are excluded.
*/
unsigned char *ssh_snlist_exclude(const unsigned char *original,
                                  const unsigned char *excluded);

#endif /* _SSHSNLIST_H_ */
