/*

  Author: Timo J. Rinne

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created:  Mon Oct 26 18:04:49 1998 tri

  Misc string functions.

*/

#ifndef SSHMISCSTRING_H
#define SSHMISCSTRING_H
/*
 * Allocates (ssh_xmalloc) a new string concatenating the NULL
 * terminated strings s1 and s2.  NULL pointer is translated to
 * empty string.
 */
char *ssh_string_concat_2(const char *s1, const char *s2);

/*
 * Allocates (ssh_xmalloc) a new string concatenating the NULL
 * terminated strings s1, s2 and s3.  NULL pointer is translated to
 * empty string.
 */
char *ssh_string_concat_3(const char *s1, const char *s2, const char *s3);

/*
 * Allocates (ssh_xmalloc) a new string where all instances of
 * substring src in string str are replaced with substring dst.
 */
char *ssh_replace_in_string(const char *str, const char *src, const char *dst);

/*
 * Like strlen, but if the string is longer than `len' return len.
 */
size_t ssh_strnlen(const char *str, size_t len);

/*
 * Pretty print numbers using kilo/mega etc abbrevs to `buffer'. The resulting
 * string is at maximum 3 numbers + letter (kMGTPE) + null, so the buffer must
 * be large enough to hold at least 5 characters. Scale can be either 1024, or
 * 1000, and it will specify if the kMGTPE are for 2^10 or for 10^3 multiples.
 */
char *ssh_format_number(char *buffer, size_t len, SshUInt64 number, int scale);

/*
 * Pretty print time using 23:59:59, 999+23:59, 99999+23, 99999999 format to
 * the `buffer'. Suitable for printing time values from few seconds up to
 * years. The output string at maximum of 9 charcaters, so the buffer must be
 * large enough to hold at least 9 characters.
 */
char *ssh_format_time(char *buffer, size_t len, SshTime t);

/* Get mallocated data from a string. Component identifies which part
 * of data to get.  The source string is assumed to be in format
 * "component1(component1_data), component2(component2_data)".  The
 * function handles parentheses correctly inside the component data.
 *
 * Occurance identifies which occurance of the data to get, 0 giving
 * the first occurance.
 *
 * Returns NULL, if the component is not found in the string and an empty
 * string, if the component is empty.  */
char *ssh_get_component_data_from_string(const char *source,
                                         const char *component,
                                         SshUInt32 occurance);


/* Free an array of strings. The strings of the array are freed individually
 * using ssh_xfree and the list is freed at last.
 */
void ssh_str_array_free(char **list, SshUInt32 num_items);


#endif /* SSHMISCSTRING_H */
/* eof (sshmiscstring.h) */
