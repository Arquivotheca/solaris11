/*

  Author: Timo J. Rinne

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created:  Mon Oct 26 18:04:49 1998 tri

  Misc string functions.

*/

#include "sshincludes.h"
#include "sshmiscstring.h"

char *ssh_string_concat_2(const char *s1, const char *s2)
{
  int l1, l2;
  char *r;

  l1 = s1 ? strlen(s1) : 0;
  l2 = s2 ? strlen(s2) : 0;

  r = ssh_xmalloc(l1 + l2 + 1);

  if (l1 > 0)
    strcpy(r, s1);
  else
    *r = '\000';
  if (l2 > 0)
    strcpy(&(r[l1]), s2);

  return r;
}

char *ssh_string_concat_3(const char *s1, const char *s2, const char *s3)
{
  int l1, l2, l3;
  char *r;

  l1 = s1 ? strlen(s1) : 0;
  l2 = s2 ? strlen(s2) : 0;
  l3 = s3 ? strlen(s3) : 0;
  r = ssh_xmalloc(l1 + l2 + l3 + 1);

  if (l1 > 0)
    strcpy(r, s1);
  else
    *r = '\000';
  if (l2 > 0)
    strcpy(&(r[l1]), s2);
  if (l3 > 0)
    strcpy(&(r[l1 + l2]), s3);

  return r;
}

char *ssh_replace_in_string(const char *str, const char *src, const char *dst)
{
  char *hlp1, *hlp2, *hlp3, *strx;

  if (src == NULL)
    src = "";
  if (dst == NULL)
    dst = "";
  strx = ssh_xstrdup(str ? str : "");

  if ((*src == '\000') || ((hlp1 = strstr(strx, src)) == NULL))
    return strx;

  *hlp1 = '\000';
  hlp2 = ssh_string_concat_2(strx, dst);
  hlp1 = ssh_replace_in_string(&(hlp1[strlen(src)]), src, dst);
  hlp3 = ssh_string_concat_2(hlp2, hlp1);
  ssh_xfree(strx);
  ssh_xfree(hlp1);
  ssh_xfree(hlp2);
  return hlp3;
}

size_t ssh_strnlen(const char *str, size_t len)
{
  size_t l;

  for (l = 0; len > 0 && *str != '\0'; l++, len--, str++)
    ;

  return l;
}

/*
 * Pretty print numbers using kilo/mega etc abbrevs to `buffer'. The resulting
 * string is at maximum 3 numbers + letter (kMGTPE) + null, so the buffer must
 * be large enough to hold at least 5 characters. Scale can be either 1024, or
 * 1000, and it will specify if the kMGTPE are for 2^10 or for 10^3 multiples.
 */
char *ssh_format_number(char *buffer, size_t len, SshUInt64 number, int scale)
{
  const char *scale_str = " kMGTPE";
  SshUInt64 num = 0L;
  int d;

  if (scale != 1000 && scale != 1024)
    ssh_fatal("Invalid scale in the ssh_format_number, must be 1024 or 1000");

  if (number < scale)
    {
      ssh_snprintf(buffer, len, "%d", (int) number);
      return buffer;
    }
  while (number >= 1000)
    {
      num = number;
      number /= scale;
      scale_str++;
    }
  if (num < 995 * scale / 100)
    {
      d = (int)(((num * 100 / scale) + 5) / 10);
      ssh_snprintf(buffer, len, "%d.%d%c", d / 10, d % 10, *scale_str);
    }
  else
    {
      d = (int)(((num * 10 / scale) + 5) / 10);
      ssh_snprintf(buffer, len, "%d%c", d, *scale_str);
    }
  return buffer;
}

/*
 * Pretty print time using 23:59:59, 999+23:59, 99999+23, 99999999 format to
 * the `buffer'. Suitable for printing time values from few seconds up to
 * years. The output string at maximum of 9 charcaters, so the buffer must be
 * large enough to hold at least 9 characters.
 */
char *ssh_format_time(char *buffer, size_t len, SshTime t)
{
  if (t < 60 * 60 * 24)
    ssh_snprintf(buffer, len, "%02d:%02d:%02d",
                 (int)(t / 60 / 60), (int)((t / 60) % 60),(int) (t % 60));
  else
    if (t < 60 * 60 * 24 * 100)
      ssh_snprintf(buffer, len, "%d+%02d:%02d",
                   (int) (t / 24 / 60 / 60), (int)((t / 60 / 60) % 24),
                   (int)((t / 60) % 60));
    else
      if (t / (60 * 60 * 24) < 100000)
        ssh_snprintf(buffer, len, "%d+%02d",
                     (int) (t / 24 / 60 / 60), (int)((t / 60 / 60) % 24));
      else
        ssh_snprintf(buffer, len, "%d",
                     (int)(t / 24 / 60 / 60));
  return buffer;
}



/* Returns an item inside brackets. For "(foo(test(bar())))" returns
   "foo(test(bar()))". */
static char *
ssh_mstr_get_next_item(const char *str)
{
  int c = 0;
  char *ptr, *start, *r;

  ptr = start = (char *)str;

  do {
    if (*ptr == '(') c++;
    if (*ptr == ')' && --c == 0) break;
  } while(*(++ptr) && c > 0);

  r = ssh_malloc(ptr - start);
  if (r)
    {
      memcpy(r, start + 1, ptr - start - 1);
      r[ptr - start - 1] = '\0';
    }
  return r;
}


/* Get the data from a string. Component identifies the data which to get.
   The source string is assumed to be in format
   "component1(component1_data), component2(component2_data)".

   Occurance identifies which occurance of the data to get, 0 giving
   the first occurance.

   Returns NULL, if the component is not found in the string and an empty
   string, if the component is empty. */
char *ssh_get_component_data_from_string(const char *source,
                                         const char *component,
                                         SshUInt32 occurance)
{
  const char *s = (char *)source, *c = (char *)component;
  int count = 0, c_len;
  SshUInt32 occ = 0;

  if (source == NULL ||
      component == NULL)
    return NULL;

  c_len = strlen(component);

  while (*s)
    {
      if (*s++ == *c)
        {
          if (++count == c_len)
            {
              if (*s == '(')
                {
                  if (occ == occurance)
                    break;
                  occ++;
                }
              c = (char *)component;
              count = 0;
            }
          else c++;
        }
      else if (count)
        {
          s--;
          c = (char *)component;
          count = 0;
        }
    }

  if (*s == '\0') return NULL;
  /* Now s points to the opening bracket of the compoent. */
  return ssh_mstr_get_next_item(s);
}

/* Free an array of strings. The strings of the array are freed individually
 * using ssh_xfree and the list is freed at last.
 */
void ssh_str_array_free(char **list, SshUInt32 num_items)
{
  for (; num_items > 0; num_items--)
    ssh_xfree(list[num_items - 1]);

  ssh_xfree(list);

}


/* eof (sshmiscstring.c) */
