/*

  sshrandompoll.c

  Author: Patrick Irwin <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Periodically poll the system for noise and add it to the random number 
  generator.

*/

#include "sshincludes.h"
#include "sshglobals.h"
#include "sshtimeouts.h"
#include "sshcrypt.h"

#define SSH_DEBUG_MODULE "CryptoRandomPoll"

typedef struct SshRandomPollRec {
  Boolean started;
  SshTimeoutStruct timeout;
  SshUInt32 seconds;
  SshUInt32 microseconds;
} SshRandomPollStruct, *SshRandomPoll;

SSH_GLOBAL_DEFINE(SshRandomPollStruct, ssh_random_poll_state);
SSH_GLOBAL_DECLARE(SshRandomPollStruct, ssh_random_poll_state);
#define ssh_random_poll_state SSH_GLOBAL_USE(ssh_random_poll_state)


/* Forward declaration */
static void crypto_add_light_noise(void);


static void random_poll_timeout(void *context)
{
  ssh_cancel_timeout(&ssh_random_poll_state.timeout);
  
  SSH_DEBUG(SSH_D_NICETOKNOW, 
            ("In the random poll timeout, now searching for noise"));
  
  /* Get some system noise and add it to the default RNG. */
  crypto_add_light_noise();
  
  /* Schedule the next poll for randomness */
  ssh_register_timeout(&ssh_random_poll_state.timeout,
                       ssh_random_poll_state.seconds,
                       ssh_random_poll_state.microseconds,
                       random_poll_timeout, NULL);
}


void
ssh_start_random_noise_polling(void)
{
  SshRandomPollStruct poll;

  if (ssh_random_poll_state.started)
    return;

  SSH_DEBUG(SSH_D_HIGHOK, ("Begin random noise polling"));

  memset(&poll, 0, sizeof(poll));

  /* This appears to be a resonable interval to poll the system */
  poll.seconds = 10;
  poll.microseconds = 0;
  poll.started = TRUE;

  SSH_GLOBAL_INIT(ssh_random_poll_state, poll);

  ssh_register_timeout(&ssh_random_poll_state.timeout, 0, 0, 
                       random_poll_timeout, NULL);

  return;
}

void
ssh_stop_random_noise_polling(void)
{
  if (!ssh_random_poll_state.started)
    return;
  ssh_random_poll_state.started = FALSE;

  SSH_DEBUG(SSH_D_HIGHOK, ("Stop random noise polling"));

  ssh_cancel_timeouts(random_poll_timeout, SSH_ALL_CONTEXTS);
  
  memset(&ssh_random_poll_state, 0, sizeof(ssh_random_poll_state));
}



/******************* Retrieve noise from operating env ******************/

/* Utility macros */
#define noise_add_byte(B)                                       \
  do {                                                          \
    noise_bytes[noise_index++ % sizeof(noise_bytes)] = (B);     \
  } while (0)

#define noise_add_word(W)                       \
  do {                                          \
    SshUInt32 __w = (W);                        \
    noise_add_byte(__w & 0xff);                 \
    noise_add_byte((__w & 0xff00) >> 8);        \
    noise_add_byte((__w & 0xff0000) >> 16);     \
    noise_add_byte((__w & 0xff000000) >> 24);   \
  } while (0)


static void crypto_add_light_noise(void)
{
  unsigned char noise_bytes[512];
  int noise_index = 0;
  
#if !defined(WINDOWS) && !defined(DOS) && \
        !defined(macintosh) && !defined(VXWORKS)
  {
    int f;
    SSH_DEBUG(SSH_D_MIDOK, ("Starting read from /dev/random."));
    /* If /dev/random is available, read some data from there in non-blocking
       mode and mix it into the pool. */
    f = open("/dev/random", O_RDONLY);

    if (f != -1)
      {
        unsigned char buf[32];
        int len;
        /* Set the descriptor into non-blocking mode. */
#if defined(O_NONBLOCK) && !defined(O_NONBLOCK_BROKEN)
        fcntl(f, F_SETFL, O_NONBLOCK);
#else /* O_NONBLOCK && !O_NONBLOCK_BROKEN */
        fcntl(f, F_SETFL, O_NDELAY);
#endif /* O_NONBLOCK && !O_NONBLOCK_BROKEN */
        len = read(f, buf, sizeof(buf));
        close(f);
        SSH_DEBUG(SSH_D_NICETOKNOW, ("Read %ld bytes from /dev/random.", len));
        if (len > 0)
          ssh_random_add_noise(buf, len);
      }
    else
      {
        SSH_DEBUG(SSH_D_FAIL, ("Opening /dev/random failed."));
      }
  }
#endif /* !WINDOWS, !DOS, !macintosh, !VXWORKS */

  /* Get miscellaneous noise from various system parameters and statistics. */
  noise_add_word((SshUInt32) ssh_time());
#ifdef HAVE_CLOCK
  noise_add_word((SshUInt32)clock());
#endif /* HAVE_CLOCK */
#ifdef HAVE_GETTIMEOFDAY
  {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    noise_add_word((SshUInt32)tv.tv_usec);
    noise_add_word((SshUInt32)tv.tv_sec);
  }
#endif /* HAVE_GETTIMEOFDAY */
#ifdef HAVE_TIMES
  {
    struct tms tm;
    noise_add_word((SshUInt32)times(&tm));
    noise_add_word((SshUInt32)(tm.tms_utime ^
                               (tm.tms_stime << 8) ^
                               (tm.tms_cutime << 16) ^
                               (tm.tms_cstime << 24)));
  }
#endif /* HAVE_TIMES */
#ifdef HAVE_GETRUSAGE
  {
    struct rusage ru, cru;
    getrusage(RUSAGE_SELF, &ru);
    getrusage(RUSAGE_CHILDREN, &cru);

    noise_add_word((SshUInt32)(ru.ru_utime.tv_usec +
                               cru.ru_utime.tv_usec));
    noise_add_word((SshUInt32)(ru.ru_stime.tv_usec +
                               cru.ru_stime.tv_usec));
    noise_add_word((SshUInt32)(ru.ru_maxrss + cru.ru_maxrss));
    noise_add_word((SshUInt32)(ru.ru_ixrss + cru.ru_ixrss));
    noise_add_word((SshUInt32)(ru.ru_idrss + cru.ru_idrss));
    noise_add_word((SshUInt32)(ru.ru_minflt + cru.ru_minflt));
    noise_add_word((SshUInt32)(ru.ru_majflt + cru.ru_majflt));
    noise_add_word((SshUInt32)(ru.ru_nswap + cru.ru_nswap));
    noise_add_word((SshUInt32)(ru.ru_inblock + cru.ru_inblock));
    noise_add_word((SshUInt32)(ru.ru_oublock + cru.ru_oublock));
    noise_add_word((SshUInt32)((ru.ru_msgsnd ^ ru.ru_msgrcv ^
                                ru.ru_nsignals) +
                               (cru.ru_msgsnd ^ cru.ru_msgrcv ^
                                cru.ru_nsignals)));
    noise_add_word((SshUInt32)(ru.ru_nvcsw + cru.ru_nvcsw));
    noise_add_word((SshUInt32)(ru.ru_nivcsw + cru.ru_nivcsw));
  }
#endif /* HAVE_GETRUSAGE */
#if !defined(WINDOWS) && !defined(DOS)
#ifdef HAVE_GETPID
  noise_add_word((SshUInt32)getpid());
#endif /* HAVE_GETPID */
#ifdef HAVE_GETPPID
  noise_add_word((SshUInt32)getppid());
#endif /* HAVE_GETPPID */
#ifdef HAVE_GETUID
  noise_add_word((SshUInt32)getuid());
#endif /* HAVE_GETUID */
#ifdef HAVE_GETGID
  noise_add_word((SshUInt32)(getgid() << 16));
#endif /* HAVE_GETGID */
#ifdef HAVE_GETPGRP
  noise_add_word((SshUInt32)getpgrp());
#endif /* HAVE_GETPGRP */
#endif /* WINDOWS */
#ifdef _POSIX_CHILD_MAX
  noise_add_word((SshUInt32)(_POSIX_CHILD_MAX << 16));
#endif /* _POSIX_CHILD_MAX */
#if defined(CLK_TCK) && !defined(WINDOWS) && !defined(DOS)
  noise_add_word((SshUInt32)(CLK_TCK << 16));
#endif /* CLK_TCK && !WINDOWS */
#ifdef VXWORKS
  /* XXX - Get better noise from cipher chips RNG unit (HiFN, MPC180,...) */
#endif /* VXWORKS */

#ifdef SSH_TICKS_READ64
  {
    SshUInt64 tick;
    SSH_TICKS_READ64(tick);
    noise_add_word((tick >> 32) & 0xfffffff);
    noise_add_word(tick & 0xffffff);
  }
#else /* !SSH_TICKS_READ64 */
#ifdef SSH_TICKS_READ32
  {
    SshUInt32 tick;
    SSH_TICKS_READ32(tick);
    noise_add_word(tick);
  }
#endif /* SSH_TICKS_READ32 */
#endif /* SSH_TICKS_READ64 */

#ifdef WIN32
  /* additional noice on Windows */
  {
    noise_add_word((SshUInt32)GetTickCount());
    noise_add_word((SshUInt32)_getpid());
    noise_add_word((SshUInt32)GetCurrentThreadId());
  }
#endif /* WIN32 */

  SSH_DEBUG(SSH_D_LOWSTART,
            ("Adding %d bytes (out of %d collected) of noise to random pool",
             noise_index > sizeof(noise_bytes)
             ? sizeof(noise_bytes) : noise_index,
             noise_index));

  /* Add collected entropic (hopefully) bytes */
  ssh_random_add_noise(noise_bytes,
                       noise_index > sizeof(noise_bytes)
                       ? sizeof(noise_bytes) : noise_index);
}


#undef noise_add_byte
#undef noise_add_word





