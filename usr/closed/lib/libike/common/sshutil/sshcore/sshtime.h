/*

Authors: Timo J. Rinne <tri@ssh.fi>
         Tero Kivinen <kivinen@ssh.fi>
         Sami Lehtinen <sjl@ssh.fi> (junior member of the group)

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
All rights reserved.

Calendar time retrieval and manipulation.

*/

#ifndef SSHTIME_H
#define SSHTIME_H

typedef SshInt64 SshTime;

typedef struct SshCalendarTimeRec {
  SshUInt8 second;     /* 0-61 */
  SshUInt8 minute;     /* 0-59 */
  SshUInt8 hour;       /* 0-23 */
  SshUInt8 monthday;   /* 1-31 */
  SshUInt8 month;      /* 0-11 */
  SshInt32 year;       /* Absolute value of year.  1999=1999. */
  SshUInt8 weekday;    /* 0-6, 0=sunday */
  SshUInt16 yearday;   /* 0-365 */
  SshInt32 utc_offset; /* Seconds from UTC (positive=east) */
  Boolean dst;         /* FALSE=non-DST, TRUE=DST */
} *SshCalendarTime, SshCalendarTimeStruct;

/* Returns seconds from epoch "January 1 1970, 00:00:00 UTC".  */
SshTime ssh_time(void);

/* Fills the calendar structure according to ``current_time''. */
void ssh_calendar_time(SshTime current_time,
                       SshCalendarTime calendar_ret,
                       Boolean local_time);

/* Return time string in RFC-2550 compatible format.  Returned string
   is allocated with ssh_malloc and has to be freed with ssh_free by
   the caller. */
char *ssh_time_string(SshTime input_time);

/* Return a time string that is formatted to be more or less human
   readable.  It is somewhat like the one returned by ctime(3) but
   contains no newline in the end.  Returned string is allocated with
   ssh_malloc and has to be freed with ssh_free by the caller. */
char *ssh_readable_time_string(SshTime input_time, Boolean local_time);

/* Convert SshCalendarTime to SshTime. If the dst is set to TRUE then daylight
   saving time is assumed to be set, if dst field is set to FALSE then it is
   assumed to be off. It if it is set to -1 then the function tries to find out
   if the dst was on or off at the time given.

   Weekday and yearday fields are ignored in the conversion, but filled with
   approriate values during the conversion. All other values are normalized to
   their normal range during the conversion.

   If the local_time is set to TRUE then dst and utc_offset values
   are ignored.

   If the time cannot be expressed as SshTime this function returns FALSE,
   otherwise returns TRUE. */
Boolean ssh_make_time(SshCalendarTime calendar_time, SshTime *time_return,
                      Boolean local_time);

#endif /* SSHTIME_H */

/* eof (sshtime.h) */
