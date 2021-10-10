/*

  Author: Antti Huima <huima@ssh.fi>
          Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Espoo, Finland
  All rights reserved.

  Created: Thu Jul 18 17:28:10 1996 [huima]

  The implementation of the generic event loop.

*/

#include "sshincludes.h"
#include "sshtimeoutsi.h"
#include "ssheloop.h"
#include "sshglobals.h"

#ifdef HAVE_SIGNAL
#include <signal.h>
#endif /* HAVE_SIGNAL */

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */

#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif /* HAVE_SYS_POLL_H */

#include "sshadt.h"
#include "sshadt_map.h"
#include "sshadt_priority_queue.h"

/*
 * Locking declarations, which are used to block execution of the door
 * functions while the event loop is handling io in a callback.
 */
#include <thread.h>
#include <synch.h>

mutex_t	door_lock = DEFAULTMUTEX;

#define SSH_DEBUG_MODULE "SshEventLoop"

/* The ssh_register_timeout() is not thread-safe (global ADT container
   ssheloop.to.map_by_identifier/ph_by_firing_time), and on VxWorks
   (unified-kernel mode of QuickSec) this is called both from the
   "kernel-mode" tNetTask network TASK (so called zero timeout upcall)
   and normally from the policymanager TASK. The required locking has
   now been added with the macros below. On other platforms and
   distributions these macros do nothing. */
#define SSH_ELOOP_MUTEX_INITIALIZE()
#define SSH_ELOOP_MUTEX_UNINITIALIZE()
#define SSH_ELOOP_MUTEX_LOCK()
#define SSH_ELOOP_MUTEX_UNLOCK()

#ifdef SSHDIST_PLATFORM_VXWORKS
#ifdef SSHDIST_QUICKSEC
#ifdef VXWORKS
#undef SSH_ELOOP_MUTEX_INITIALIZE
#undef SSH_ELOOP_MUTEX_UNINITIALIZE
#undef SSH_ELOOP_MUTEX_LOCK
#undef SSH_ELOOP_MUTEX_UNLOCK

/* On VxWorks SSH Quicksec utilizes the unified-address space
   compilation option for communication between the SSH Quicksec
   engine (normally executed by VxWorks tNetTask) and the SSH Quicksec
   policymanager (we spawn a new task for PM, called
   tSshIpsecpm). This means that direct function calls are possible
   from the PM to the engine, as the engine code is properly locked,
   thread safe and re-entrant.

   However to the other direction from engine to PM direct function
   calls are not possible, since the PM code is always executed from
   the same context, that is, the same VxWorks task, and events are
   initiated always through the event-loop, where the PM task sleeps
   in "select()" if it has nothing to do. This makes the PM code
   clean, more efficient and we can avoid a high number of possible
   bugs, since we can make sure that absolutely no locking is required
   in any parts of the code executed by the PM TASK, all of the PM
   code, including the SSH libraries, like IKE.

   This means that communication from engine to PM must be wrapped to
   the right context, that is, calls from the tNetTask must be wrapped
   to the PM task in the event-loop before the actual operation is
   executed in the PM.  This is done by registering a so called
   zero-timeout in the event-loop, which simply is a timeout, which
   will expire immediately. So far, everything is fine. The context
   switch happens correctly from tNetTask to PM task, the timeout
   structures (ADT containers) in the event-loop are properly locked,
   and the actual event is executed safely in the right context.

   What SSH_VX_WAKEUP code below does is that it wakes up the PM task
   sleeping in select(), in the case the timeout is registered from a
   different context than the PM task. This is done by a VxWorks pipe,
   the message sent is insignificant, the sole purpose of this pipe is
   to wake up select(), so that the zero-timeout event would be truly
   executed immediately. Without this code the PM peridically checks
   the timeouts and everything seems to work fine, however the delay
   could be up to 5 seconds, normally on the average about 2 seconds.

   Without this code, problems that are hard to predict can occure
   during events from the engine to the PM, for example, during IKE
   rekey packets could drop, or entire ALG session setup negotiations
   could fail, or we could observe general slowness. */
#include <ioLib.h>
#include <iosLib.h>
#include <semLib.h>

#define SSH_VX_WAKEUP_NAME "/ssh/wakeup"
static int ssh_vx_wakeup_fd = 0;
static int ssh_vx_wakeup_context_id = -1;

/* This structure manages the dummy wakeup device */
struct SshWakeupDevRec
{
  DEV_HDR               dev;
  SEL_WAKEUP_LIST       select_list;
  BOOL                  wakeup;
};

typedef struct SshWakeupDevRec SshWakeupDev;

/* Local variables */
static int ssh_vx_wakeup_drv = 0; /* driver number assigned to the driver */

void ssh_vx_wakeup_cb(unsigned int events, void *context)
{ 
  read(ssh_vx_wakeup_fd, 0, 0);
};

/* Implements the dummy wakeup device open(), returns a pointer to the
   device record */
static int ssh_vx_wakeup_open(SshWakeupDev *ssh_vx_dev, char *name, int mode)
{ 
  ssh_vx_dev->wakeup = FALSE;
  selWakeupListInit(&ssh_vx_dev->select_list);
  return((int)ssh_vx_dev);
}
static int ssh_vx_wakeup_close(SshWakeupDev *ssh_vx_dev) { return(OK); }
static int ssh_vx_wakeup_read(SshWakeupDev *ssh_vx_dev, char *buf, int max)
{ 
  ssh_vx_dev->wakeup = FALSE;
  errno = EAGAIN;
  return -1;
}

static int ssh_vx_wakeup_write(SshWakeupDev *ssh_vx_dev, char *buf, int n)
{
  /* wake up select */
  ssh_vx_dev->wakeup = TRUE;
  selWakeupAll(&ssh_vx_dev->select_list, SELREAD);
  return 1;
}

static int ssh_vx_wakeup_ioctl(SshWakeupDev *ssh_vx_dev, int request, int arg)
{
  switch(request)
    {
    case FIOSELECT:
      {
        SEL_WAKEUP_NODE *node = (SEL_WAKEUP_NODE *)arg;
        selNodeAdd(&ssh_vx_dev->select_list, node);
        
        if (selWakeupType(node) == SELREAD &&
            ssh_vx_dev->wakeup == TRUE)
          {
            selWakeup(node);
          }
        break;
      }
    case FIOUNSELECT:
      {
        SEL_WAKEUP_NODE* node = (SEL_WAKEUP_NODE *)arg;
        selNodeDelete(&ssh_vx_dev->select_list, node);
        break;
      }
    }
  return OK;
}

/* Locking */
SEMAPHORE *ssh_eloop_mutex;
#define SSH_ELOOP_MUTEX_INITIALIZE() \
  do { ssh_eloop_mutex = semMCreate(SEM_Q_FIFO); } while (0)
#define SSH_ELOOP_MUTEX_UNINITIALIZE() \
  do { semDelete(ssh_eloop_mutex); } while (0)
#define SSH_ELOOP_MUTEX_LOCK() \
  do { semTake(ssh_eloop_mutex, WAIT_FOREVER); } while (0)
#define SSH_ELOOP_MUTEX_UNLOCK() \
  do { semGive(ssh_eloop_mutex); } while (0)
#endif /* VXWORKS */
#endif /* SSHDIST_QUICKSEC */
#endif /* SSHDIST_PLATFORM_VXWORKS */


/* Determine whether to use poll() or select() */

#ifdef USE_POLL
#undef USE_POLL
#endif /* USE_POLL */

#ifdef USE_SELECT
#undef USE_SELECT
#endif /* USE_SELECT */

#ifdef HAVE_POLL
#ifdef ENABLE_SELECT
#ifdef HAVE_SELECT
#define USE_SELECT
#else
#define USE_POLL
#endif /* HAVE_SELECT */
#else
#define USE_POLL
#endif /* ENABLE_SELECT */
#else
#ifdef HAVE_SELECT
#define USE_SELECT
#endif /* HAVE_SELECT */
#endif /* HAVE_POLL */

/* The USE_POLL and USE_SELECT are mutually exclusive */

#ifndef USE_POLL
#ifndef USE_SELECT
#error Can not compile without select or poll
#endif
#endif

/* Set defaults according to this choice */

#ifdef USE_SELECT
#ifdef FD_SETSIZE
#define SSH_ELOOP_INITIAL_REQS_ARRAY_SIZE (FD_SETSIZE)
#else
#define SSH_ELOOP_INITIAL_REQS_ARRAY_SIZE 1024
#endif /* FD_SETSIZE */
#else
#define SSH_ELOOP_INITIAL_REQS_ARRAY_SIZE 16
#endif /* USE_SELECT */


#define SSH_ELOOP_REQS_ARRAY_SIZE_STEP    16

/* The timeouts are kept in a priority heap. The file descriptors are
   kept in an array indexed by the descriptors. Signals are indexed by
   the signal numbers. Signals are put into queue too. */

#ifdef HAVE_SIGNAL

#ifndef NSIG
#define NSIG 32
#endif

typedef struct SshEloopSignalRec
{
  SshSignalCallback callback;
  void *context;
} *SshEloopSignal, SshEloopSignalStruct;
#endif /* HAVE_SIGNAL */

typedef struct SshEloopIORec
{
  int fd;
  Boolean was_nonblocking;
  SshIoCallback callback;
  void *context;
  struct SshEloopIORec *next;
  Boolean killed;
  int request;
#ifdef USE_POLL
  int poll_idx;
#endif /* USE_POLL */
} *SshEloopIO, SshEloopIOStruct;

typedef struct SshEloopRec
{
  Boolean running;

  SshEloopIO io_records;
  SshEloopIO io_records_tail;
  SshEloopIO *fd_to_record_map;
  int fd_map_size;
  SshTimeoutContainerStruct to;
  struct timeval *select_timeout_ptr;
  Boolean in_select;
  Boolean is_clean_necessary;
  Boolean is_pollcache_invalid;

#ifdef HAVE_SIGNAL
  sigset_t used_signals;
  SshEloopSignal signal_records;
  Boolean fired_signals[NSIG];
  Boolean signal_fired;
#endif /* HAVE_SIGNAL */

#ifdef USE_POLL
  struct pollfd *pfds;
  unsigned int pfd_size;
#endif /* USE_POLL */

  struct timeval select_timeout_no_wait;
} *SshEloop, SshEloopStruct;

SSH_GLOBAL_DECLARE(SshEloopStruct, ssheloop);
#define ssheloop SSH_GLOBAL_USE(ssheloop)
SSH_GLOBAL_DEFINE(SshEloopStruct, ssheloop);

/* Initializes the event loop.  This must be called before any other
   event loop, timeout, or stream function.  The IO records list
   contains no items.  The fd_to_record_map array contains initially
   SSH_ELOOP_INITIAL_REQS_ARRAY_SIZE items. The array is mallocated
   here. The signal records array contains exactly NSIG items. The
   size of the array never changes, contrary to the requests
   array. Timeouts records list contains no items, neither the list of
   fired signals. */

void ssh_event_loop_initialize(void)
{
  SshEloopStruct ssheloop_init;

  memset(&ssheloop_init, 0, sizeof(ssheloop_init));
  SSH_GLOBAL_INIT(ssheloop, ssheloop_init);

  ssheloop.select_timeout_no_wait.tv_sec = 0L;
  ssheloop.select_timeout_no_wait.tv_usec = 0L;

#ifdef HAVE_SIGNAL
  sigemptyset(&ssheloop.used_signals);
  ssheloop.signal_records = ssh_calloc(NSIG, sizeof(SshEloopSignalStruct));
  if (ssheloop.signal_records == NULL)
    ssh_fatal("Insufficient memory available to initialize event loop "
              "signal handlers.");
#endif /* HAVE_SIGNAL */

  ssh_timeout_container_initialize(&ssheloop.to);

  ssheloop.fd_map_size = SSH_ELOOP_INITIAL_REQS_ARRAY_SIZE;
  ssheloop.fd_to_record_map = ssh_malloc(sizeof(ssheloop.fd_to_record_map[0])
                                         * ssheloop.fd_map_size);
  if (ssheloop.fd_to_record_map == NULL)
    ssh_fatal("Insufficient memory available to initialize event loop "
              "request queue.");

#ifdef USE_POLL
  ssheloop.pfds = ssh_malloc(sizeof(ssheloop.pfds[0])
                             * ssheloop.fd_map_size);
  if (ssheloop.pfds == NULL)
    ssh_fatal("Insufficient memory available to initialize event loop "
              "polling descriptors.");
#endif

  /* Start the unified address space locks, and select wakeup
     mechanism */
  SSH_ELOOP_MUTEX_INITIALIZE();

#ifdef SSHDIST_PLATFORM_VXWORKS
#ifdef SSHDIST_QUICKSEC
#ifdef VXWORKS
  /* Add the device to system, this needs to be done only once */
  if (!ssh_vx_wakeup_drv)
    {
      SshWakeupDev *ssh_vx_dev;
      
      /* Store the task id of the thread sleeping in select() */
      ssh_vx_wakeup_context_id = taskIdSelf();
      
      if ((ssh_vx_dev = (SshWakeupDev *)ssh_calloc(1, sizeof(SshWakeupDev)))
          == NULL)
        ssh_fatal("calloc");
      
      /* Create the pipe device, file and register it */
      ssh_vx_wakeup_drv = 
        iosDrvInstall(ssh_vx_wakeup_open, (FUNCPTR) NULL,
                      ssh_vx_wakeup_open, ssh_vx_wakeup_close,
                      ssh_vx_wakeup_read, ssh_vx_wakeup_write,
                      ssh_vx_wakeup_ioctl);
      
      if (iosDevAdd((DEV_HDR *)ssh_vx_dev, SSH_VX_WAKEUP_NAME, 
                    ssh_vx_wakeup_drv) != OK)
        ssh_fatal("iosDevAdd");
    }
  if ((ssh_vx_wakeup_fd = open(SSH_VX_WAKEUP_NAME, O_RDWR, 0)) == ERROR)
    ssh_fatal("open");
  if (!ssh_io_register_fd(ssh_vx_wakeup_fd, ssh_vx_wakeup_cb, 0))
    ssh_fatal("malloc");
  ssh_io_set_fd_request(ssh_vx_wakeup_fd, SSH_IO_READ);
#endif /* VXWORKS */
#endif /* SSHDIST_QUICKSEC */
#endif /* SSHDIST_PLATFORM_VXWORKS */

  ssheloop.running = FALSE;

  SSH_DEBUG(SSH_D_HIGHOK, ("Initialized the event loop."));
}

/* Abort the event loop. This causes the event loop to exit before
   the next select(). */

void ssh_event_loop_abort(void)
{
  if (ssheloop.running == TRUE)
    ssheloop.running = FALSE;
}

/* XXX: get rid of the event-loop-lock functionality when we know for
   sure that the library is completely re-entrant. */
void ssh_event_loop_lock(void)
{
  return;
}

void ssh_event_loop_unlock(void)
{
  return;
}

static void ssh_event_loop_delete_all_fds(void)
{
  SshEloopIO temp;

  ssheloop.is_pollcache_invalid = TRUE;
  ssheloop.is_clean_necessary = TRUE;

  while (ssheloop.io_records != NULL)
    {
      temp = ssheloop.io_records;
      ssheloop.io_records = temp->next;

#ifdef SSHDIST_PLATFORM_VXWORKS
#ifdef VXWORKS
#ifdef ENABLE_VXWORKS_RESTART_WATCHDOG
      close(temp->fd);
#endif /* ENABLE_VXWORKS_RESTART_WATCHDOG */
#endif /* VXWORKS */
#endif /* SSHDIST_PLATFORM_VXWORKS */

      ssh_free(temp);
    }
  ssheloop.io_records = NULL;
  ssheloop.io_records_tail = NULL;
}

#ifdef HAVE_SIGNAL
static void ssh_event_loop_delete_all_signals(void)
{
  int sig;

  for (sig = 1; sig <= NSIG; sig++)
    {
      if (sigismember((&(ssheloop.used_signals)), sig))
        ssh_unregister_signal(sig);
    }
}
#endif /* HAVE_SIGNAL */

/* Uninitialize the event loop after it has returned.
   Delete all timeouts etc. left and free the structures. */

void ssh_event_loop_uninitialize(void)
{
  ssh_cancel_timeouts(SSH_ALL_CALLBACKS, SSH_ALL_CONTEXTS);

  ssh_timeout_container_uninitialize(&ssheloop.to);

  ssh_event_loop_delete_all_fds();

#ifdef HAVE_SIGNAL
  ssh_event_loop_delete_all_signals();
#endif /* HAVE_SIGNAL */

  ssh_free(ssheloop.fd_to_record_map);

#ifdef USE_POLL
  ssh_free(ssheloop.pfds);
#endif

#ifdef HAVE_SIGNAL
  ssh_free(ssheloop.signal_records);
#endif /* HAVE_SIGNAL */

  SSH_ELOOP_MUTEX_UNINITIALIZE();

#ifdef SSHDIST_PLATFORM_VXWORKS
#ifdef SSHDIST_QUICKSEC
#ifdef VXWORKS
  /* Close the VxWorks pipe */
  close(ssh_vx_wakeup_fd);
#endif /* VXWORKS */
#endif /* SSHDIST_QUICKSEC */
#endif /* SSHDIST_PLATFORM_VXWORKS */


  SSH_DEBUG(SSH_D_HIGHOK, ("Uninitialized the event loop."));
}

/* The signal handler. Insert a new fired signal structure to the
   list of fired signals. Block signals until the insertion has
   finished so that other catched signals don't mess the list up. */

#ifdef HAVE_SIGNAL
static RETSIGTYPE ssh_event_loop_signal_handler(int sig)
{
  sigset_t old_set;

  SSH_DEBUG(SSH_D_MIDOK, ("Got signal number: %d", sig));
  SSH_ASSERT(sig > 0 && sig <= NSIG);

  /* Signals are blocked during the execution of this call. */
  sigprocmask(SIG_BLOCK, &ssheloop.used_signals, &old_set);

  if (ssheloop.in_select)
    {
      /* We were in select(), deliver the callback immediately. */
      if (ssheloop.signal_records[sig - 1].callback)
        (*ssheloop.signal_records[sig - 1].callback)(sig,
           ssheloop.signal_records[sig - 1].context);
    }
  else
    {
      /* We are currently processing a callback; deliver the signal callback
         when the current callback returns. */
      ssheloop.signal_fired = TRUE;
      ssheloop.fired_signals[sig - 1] = TRUE;
    }

  sigprocmask(SIG_SETMASK, &old_set, NULL);
}
#endif /* HAVE_SIGNAL */

/*****************************************************************************
 * Timeouts
 */

/* Get current time. This system also handles backward jumps at the
   wall clock time (e.g. current time being less than reference time
   records at previous call to this routine. */
static int
ssh_eloop_gettimeofday(struct timeval *tp)
{
#ifndef HAVE_GETTIMEOFDAY
  if (tp)
    {
      tp->tv_sec = ssh_time();
      tp->tv_usec = 0;
    }
#else
  gettimeofday(tp, NULL);
#endif

  if (tp->tv_usec > 1000000L)
    {
      tp->tv_usec -= 1000000L;
      tp->tv_sec  += 1;
    }
  /* Check the clock adjust */
  ssh_timeout_container_check_clock_jump(&ssheloop.to, tp);

  return 0;
}

/* Compare two struct timevals. */
static int
ssh_event_loop_compare_time(struct timeval *first,
                            struct timeval *second)
{
  return
    (first->tv_sec  < second->tv_sec)  ? -1 :
    (first->tv_sec  > second->tv_sec)  ?  1 :
    (first->tv_usec < second->tv_usec) ? -1 :
    (first->tv_usec > second->tv_usec) ?  1 : 0;
}

/* Convert relative timeout to absolute firing time. */
static void
ssh_eloop_convert_relative_to_absolute(long seconds,
                                       long microseconds,
                                       struct timeval *timeval,
                                       struct timeval *reference_time)
{
  SSH_ASSERT(microseconds >= 0 && microseconds < 1000000L);

  ssh_eloop_gettimeofday(timeval);
  timeval->tv_sec += seconds;
  timeval->tv_usec += microseconds;

  if (timeval->tv_usec > 999999L)
    {
      timeval->tv_usec -= 1000000L;
      timeval->tv_sec  += 1L;
    }
}

SshTimeout
ssh_register_timeout_internal(SshTimeout state,
                              long seconds,
                              long microseconds,
                              SshTimeoutCallback callback,
                              void *context)
{
  SshTimeout created, p;
  SshADTHandle handle;

  created = state;

  if (seconds > 1000000000)
    {
      seconds = 1000000000; microseconds = 0;
    }
  else
    {
      seconds += microseconds / 1000000L; microseconds %= 1000000L;
    }

  /* Convert to absolute time and initialize timeout record. */
  ssh_eloop_convert_relative_to_absolute(seconds, microseconds,
                                         &created->firing_time,
                                         &ssheloop.to.reference_time);
  created->callback = callback;
  created->context = context;
  created->identifier = ssheloop.to.next_identifier++;

  SSH_ELOOP_MUTEX_LOCK();

  ssh_adt_insert(ssheloop.to.map_by_identifier, created);
  ssh_adt_insert(ssheloop.to.ph_by_firing_time, created);

  if ((handle =
       ssh_adt_get_handle_to_equal(ssheloop.to.map_by_context, created))
      != SSH_ADT_INVALID)
    {
      p = ssh_adt_get(ssheloop.to.map_by_context, handle);
      created->next = p->next;
      created->prev = p;
      if (p->next)
        p->next->prev = created;
      p->next       = created;
    }
  else
    {
      created->next = NULL;
      created->prev = NULL;
      ssh_adt_insert(ssheloop.to.map_by_context, created);
    }

  SSH_ELOOP_MUTEX_UNLOCK();

  SSH_DEBUG(SSH_D_MIDOK,
            ("timeout %qd at %ld:%ld",
             created->identifier,
             created->firing_time.tv_sec,
             created->firing_time.tv_usec));

#ifdef SSHDIST_PLATFORM_VXWORKS
#ifdef SSHDIST_QUICKSEC
#ifdef VXWORKS
  /* Wake up the select if calling from a different context, in order
     to recalculate the next timeout. This is especially important for
     low latency for the so called zero timeouts */
  if (ssh_vx_wakeup_context_id != taskIdSelf())
    write(ssh_vx_wakeup_fd, "", 1);
#endif /* VXWORKS */
#endif /* SSHDIST_QUICKSEC */
#endif /* SSHDIST_PLATFORM_VXWORKS */


  return created;
}

SshTimeout
ssh_xregister_timeout(long seconds,
                      long microseconds,
                      SshTimeoutCallback callback,
                      void *context)
{
  SshTimeout created = ssh_calloc(1,sizeof(*created));

  if (created == NULL)
    ssh_fatal("Out of memory while trying to register timeout with call to "
              "ssh_xregister_timeout().");

  created->is_dynamic = TRUE;
  return ssh_register_timeout_internal(created, seconds, microseconds,
                                       callback, context);
}

SshTimeout
ssh_register_timeout(SshTimeout state,
                     long seconds,
                     long microseconds,
                     SshTimeoutCallback callback,
                     void *context)
{
  if (state != NULL)
    {
      memset(state, 0, sizeof(*state));
      state->is_dynamic = FALSE;
    }
  else
    {
      state = ssh_calloc(1,sizeof(*state));
      if (state == NULL)
        {
          SSH_DEBUG(SSH_D_FAIL,
                    ("Insufficient memory to instantiate timeout!"));
          return NULL;
        }
      state->is_dynamic = TRUE;
    }

  return ssh_register_timeout_internal(state, seconds, microseconds,
                                       callback, context);
}

void
ssh_cancel_timeout(SshTimeout timeout)
{
  SshTimeout p;
  SshADTHandle mh, ph, cmh;

  if (timeout == NULL)
    return;

  SSH_ELOOP_MUTEX_LOCK();

  if ((mh =
       ssh_adt_get_handle_to_equal(ssheloop.to.map_by_identifier, timeout))
      != SSH_ADT_INVALID)
    {
      p = ssh_adt_get(ssheloop.to.map_by_identifier, mh);

      SSH_DEBUG(SSH_D_MIDOK, ("cancelled %qd", p->identifier));

      ph = &p->adt_ft_ph_hdr;

      ssh_adt_detach(ssheloop.to.ph_by_firing_time, ph);
      ssh_adt_detach(ssheloop.to.map_by_identifier, mh);

      if (p->prev == NULL)
        {
          cmh = &p->adt_ctx_map_hdr;
          ssh_adt_detach(ssheloop.to.map_by_context, cmh);
          if (p->next)
            {
              p->next->prev = NULL;
              ssh_adt_insert(ssheloop.to.map_by_context, p->next);
            }
        }
      else
        {
          p->prev->next = p->next;
          if (p->next)
            p->next->prev = p->prev;
        }

      if (p->is_dynamic)
        ssh_free(p);
      else
        memset(p, 0, sizeof(*p));

      SSH_ELOOP_MUTEX_UNLOCK();

      return;
    }

  SSH_ELOOP_MUTEX_UNLOCK();

}

/* Cancel all timeouts that call `callback' with context `context'.
   SSH_ALL_CALLBACKS and SSH_ALL_CONTEXTS can be used as wildcards. */
void ssh_cancel_timeouts(SshTimeoutCallback callback, void *context)
{
  SshADTHandle nmh, mh, cmh;
  SshTimeoutStruct probe;

  SSH_ELOOP_MUTEX_LOCK();

  if (context != SSH_ALL_CONTEXTS)
    {
      /* Cancel with given context. */
      probe.context = context;
      if ((cmh =
           ssh_adt_get_handle_to_equal(ssheloop.to.map_by_context, &probe))
          != SSH_ADT_INVALID)
        {
          ssh_to_remove_from_contextmap(&ssheloop.to, callback, context, cmh);
        }
    }
  else
    {
      /* Cancel with wildcard context. Enumerates context map and
         traverses its lists. */
      for (mh = ssh_adt_enumerate_start(ssheloop.to.map_by_context);
           mh != SSH_ADT_INVALID;
           mh = nmh)
        {
          nmh = ssh_adt_enumerate_next(ssheloop.to.map_by_context, mh);
          ssh_to_remove_from_contextmap(&ssheloop.to, callback, context, mh);
        }
    }

  SSH_ELOOP_MUTEX_UNLOCK();

}

/*****************************************************************************
 * Idle Timeouts
 * It is legal to never call idle timeouts, and that's right we exercise.
 */
SshTimeout
ssh_xregister_idle_timeout(long seconds,
                           long microseconds,
                           SshTimeoutCallback callback,
                           void *context)
{
  SSH_TRACE(1, ("Idle timeouts not yet implemented."));
  return NULL;
}

#ifdef HAVE_SIGNAL
/*****************************************************************************
 * Signals
 */

/* Register a new signal. Add the signal action with the sigaction()
   system call. Also insert the callback and context information to
   the static array of signal callbacks, indexed by the signal
   number. */

void ssh_register_signal(int sig, SshSignalCallback callback,
                         void *context)
{
  struct sigaction action;
  sigset_t mask, old_mask;

  if (sig <= 0 || sig > NSIG)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Registering bad signal %d ignored.", sig));
      return;
    }

  sigemptyset(&mask);
  sigaddset(&mask, SIGALRM);
  sigprocmask(SIG_BLOCK, &mask, &old_mask);

  sigaddset(&(ssheloop.used_signals), sig);
  ssheloop.signal_records[sig - 1].callback = callback;
  ssheloop.signal_records[sig - 1].context = context;
  action.sa_handler = ssh_event_loop_signal_handler;
  action.sa_flags = 0;
  sigemptyset(&action.sa_mask);
  sigaction(sig, &action, NULL);

  sigprocmask(SIG_SETMASK, &old_mask, (sigset_t *) NULL);

  SSH_DEBUG(SSH_D_MIDOK, ("Registered signal %d.", sig));
}

/* Unregister a signal. Set the signal action to its system default
   with the sigaction() system call. Also set the callback and context
   information of the signal to NULLs. */

void ssh_unregister_signal(int sig)
{
  struct sigaction action;
  sigset_t mask, old_mask;
  Boolean previously_fired;

  if (sig <= 0 || sig > NSIG)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Unregistering bad signal %d ignored.", sig));
      return;
    }
  sigemptyset(&mask);
  sigaddset(&mask, SIGALRM);
  sigprocmask(SIG_BLOCK, &mask, &old_mask);

  action.sa_handler = SIG_DFL;
  action.sa_flags = 0;
  sigemptyset(&action.sa_mask);
  sigaction(sig, &action, NULL);
  sigdelset(&ssheloop.used_signals, sig);

  /* Save the signal status. */
  previously_fired = ssheloop.fired_signals[sig - 1];
  ssheloop.fired_signals[sig - 1] = FALSE;

  ssheloop.signal_records[sig - 1].callback = NULL_FNPTR;
  ssheloop.signal_records[sig - 1].context = NULL;

  sigprocmask(SIG_SETMASK, &old_mask, (sigset_t *) NULL);

  if (previously_fired)
    {
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("Reissuing signal "
                 "for which callback was not yet delivered."));
      kill(getpid(), sig);
    }

  SSH_DEBUG(SSH_D_MIDOK, ("Unregistered signal %d.", sig));
}
#endif /* HAVE_SIGNAL */

/*****************************************************************************
 * File IO
 */

/* Register a file descriptor. Create a structure and add it to the
   beginning of the list of IO records. Arrays are expanded if
   necessary. */
Boolean
ssh_io_register_fd(int fd, SshIoCallback callback, void *context)
{
  SshEloopIO created;
#ifdef USE_POLL
  struct pollfd *pfds;
  int nrequests;
  SshEloopIO *requests;
#endif /* USE_POLL */

  SSH_DEBUG(SSH_D_NICETOKNOW, ("register fd=%d", fd));

  /* First make sure a sufficient amount of store exists. */

#ifdef USE_POLL
  requests = NULL;
  pfds = NULL;
#endif /*USE_POLL */

  created = ssh_malloc(sizeof(*created));

  if (created == NULL)
    goto fail;

  if (fd >= ssheloop.fd_map_size)
    {
#ifdef USE_SELECT
      SSH_DEBUG(SSH_D_FAIL,
                ("Can not register file descriptor %d (fd_set limit %d)",
                 fd,ssheloop.fd_map_size));
      return FALSE;
#endif

#ifdef USE_POLL
      nrequests = ssheloop.fd_map_size;

      nrequests += SSH_ELOOP_REQS_ARRAY_SIZE_STEP;

      if (fd >= nrequests)
        nrequests = fd +1;

      requests =
        ssh_realloc(ssheloop.fd_to_record_map,
                    ssheloop.fd_map_size
                    * sizeof(ssheloop.fd_to_record_map[0]),
                    nrequests
                    * sizeof(ssheloop.fd_to_record_map[0]));

      if (requests == NULL)
        goto fail;

      pfds = ssh_realloc(ssheloop.pfds,
                         ssheloop.fd_map_size
                         * sizeof(ssheloop.pfds[0]),
                         nrequests
                         * sizeof(ssheloop.pfds[0]));

      if (pfds == NULL)
        goto fail;

      ssheloop.pfds = pfds;
      pfds = NULL;

      ssheloop.fd_map_size = nrequests;
      ssheloop.fd_to_record_map = requests;
      requests = NULL;
#endif
    }
  /* Then initialize the state pertaining to the file descriptor */
  created->callback = callback;
  created->context = context;
  created->fd = fd;
  created->killed = FALSE;
  created->request = 0;
  created->was_nonblocking =
#ifdef VXWORKS
    1; /* cannot query if it was or not in VxWorks */
#else /* VXWORKS */
    (fcntl(fd, F_GETFL, 0) & (O_NONBLOCK|O_NDELAY)) != 0;
#endif /* VXWORKS */

#ifdef VXWORKS
      {
        int tmp = 1;
        ioctl(fd, FIONBIO, (int)&tmp);
      }
#else /* VXWORKS */
  /* Make the file descriptor use non-blocking I/O. */
#if defined(O_NONBLOCK) && !defined(O_NONBLOCK_BROKEN)
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#else /* O_NONBLOCK && !O_NONBLOCK_BROKEN */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NDELAY);
#endif /* O_NONBLOCK && !O_NONBLOCK_BROKEN */
#endif  /* VXWORKS */

  SSH_HEAVY_DEBUG(99, ("fd %d is %sin non-blocking mode.", fd,
                       (fcntl(fd, F_GETFL, 0) & (O_NONBLOCK|O_NDELAY)) != 0 ?
                       "" : "not "));

  /* Add the newly created structure to the END of the list. */
  created->next = NULL;
  if (ssheloop.io_records_tail)
    ssheloop.io_records_tail->next = created;
  else
    ssheloop.io_records = created;
  ssheloop.io_records_tail = created;

  ssheloop.fd_to_record_map[created->fd] = created;
#ifdef USE_POLL
  created->poll_idx = -1;
#endif /* USE_POLL */

  SSH_DEBUG(SSH_D_MIDOK, ("Registered file descriptor %d.", fd));
  return TRUE;
 fail:


  if (created != NULL)
    ssh_free(created);

#ifdef USE_POLL
  if (requests != NULL)
    ssh_free(requests);


  if (pfds != NULL)
    ssh_free(pfds);
#endif

  return FALSE;
}

void
ssh_io_xregister_fd(int fd, SshIoCallback callback, void *context)
{
  if (ssh_io_register_fd(fd, callback,context) == FALSE)
    ssh_fatal("ssh_io_register_fd failed, could not register file descriptor");
}

/* Unregister a file descriptor. */

void ssh_io_unregister_fd(int fd, Boolean keep_nonblocking)
{
  SshEloopIO item;

  SSH_DEBUG(SSH_D_NICETOKNOW, ("unregister fd=%d", fd));

  ssheloop.is_pollcache_invalid = TRUE;
  ssheloop.is_clean_necessary = TRUE;

  item = ssheloop.fd_to_record_map[fd];
  if (item != NULL && item->killed == FALSE)
    {
      SSH_ASSERT(item->fd == fd);

      if (!item->was_nonblocking && !keep_nonblocking)
        {
#ifdef VXWORKS
          /* nothing, we cannot know of it was blocking */
#else /* VXWORKS */
#if defined(O_NONBLOCK) && !defined(O_NONBLOCK_BROKEN)
          fcntl(item->fd, F_SETFL,
                fcntl(item->fd, F_GETFL, 0) & ~O_NONBLOCK);
#else /* O_NONBLOCK && !O_NONBLOCK_BROKEN */
          fcntl(item->fd, F_SETFL,
                fcntl(item->fd, F_GETFL, 0) & ~O_NDELAY);
#endif /* O_NONBLOCK && !O_NONBLOCK_BROKEN */
#endif  /* VXWORKS */
        }
      SSH_ASSERT(ssheloop.fd_to_record_map[item->fd] == item);
      ssheloop.fd_to_record_map[item->fd] = NULL;
      item->killed = TRUE;
      SSH_DEBUG(SSH_D_MIDOK,
                ("Killed the file descriptor %d, waiting for removal",
                 fd));
      return;
    }
  /* File descriptor was not found. */
  ssh_warning("ssh_io_unregister_fd: file descriptor %d was not found.", fd);
}

/* Set the IO request(s) for a file descriptor. The file descriptor
   must have been registered previously to the event loop; otherwise
   the requests table might have less items than `fd'.
   ssh_fatal() is called if this happens. */

void ssh_io_set_fd_request(int fd, unsigned int request)
{
  SshEloopIO iorec;

  if (fd >= ssheloop.fd_map_size)
    {
      ssh_fatal("File descriptor %d exceeded the array size in "
                "ssh_io_set_fd_request.",
                fd);
    }

  iorec = ssheloop.fd_to_record_map[fd];
  SSH_ASSERT(iorec != NULL);
  SSH_ASSERT(iorec->fd == fd);

  iorec->request = request;

#ifdef USE_POLL
  if (ssheloop.is_pollcache_invalid == FALSE && iorec->poll_idx != -1
      && (iorec->request & (SSH_IO_READ|SSH_IO_WRITE)))
    {
      SSH_DEBUG(SSH_D_MY, ("optimized set fd=%d request=0x%08x", fd, request));

      SSH_ASSERT(ssheloop.pfds[iorec->poll_idx].fd == iorec->fd);

      ssheloop.pfds[iorec->poll_idx].events = 0;

      if (iorec->request & SSH_IO_READ)
        ssheloop.pfds[iorec->poll_idx].events |= POLLIN | POLLPRI;

      if (iorec->request & SSH_IO_WRITE)
        ssheloop.pfds[iorec->poll_idx].events |= POLLOUT;
    }
  else
#endif /* USE_POLL */
    {
      SSH_DEBUG(SSH_D_MY, ("invalidating set fd=%d request=0x%08x",
                           fd, request));
      ssheloop.is_pollcache_invalid = TRUE;
    }
}

/*****************************************************************************
 * Run the event loop.
 */

static void
ssh_event_loop_clean_fds(void)
{
  SshEloopIO iorec_temp, iorec_prev;
  SshEloopIO *iorec_ptr;

  if (ssheloop.is_clean_necessary == FALSE)
    return;

  SSH_DEBUG(SSH_D_NICETOKNOW, ("clean fds!"));

  iorec_temp = ssheloop.io_records;
  iorec_ptr = &(ssheloop.io_records);
  iorec_prev = NULL;

  while (iorec_temp != NULL)
    {
      if (iorec_temp->killed == TRUE)
        {
          SSH_DEBUG(SSH_D_MIDOK, ("Removed a killed IO callback."));
          /* First set the pointer to point to the next item in the list. */

          *iorec_ptr = iorec_temp->next;
          if (iorec_temp->next == NULL)
            ssheloop.io_records_tail = iorec_prev;

          /* Then free the killed structure. */
          ssh_free(iorec_temp);

          /* Finally set the iteration pointer to the next item
             in the list. */
          iorec_temp = *iorec_ptr;
        }
      else
        {
          iorec_ptr = &(iorec_temp->next);
          iorec_prev = iorec_temp;
          iorec_temp = iorec_temp->next;
        }
    }
  ssheloop.is_clean_necessary = FALSE;
}

void
ssh_event_loop_run(void)
{
  struct timeval current_time, prev_time;
  struct timeval select_timeout;
  SshTimeout time_temp;
  SshADTHandle ph;
  SshEloopIO iorec_temp;
  Boolean done_something;
  unsigned int nfds;
  int poll_return_value;
#ifdef USE_POLL
  int poll_nopoll_counter;
  int poll_timeout;
  int idx;
#endif
#ifdef USE_SELECT
  fd_set readfds, writefds;
  int max_fd;
#endif

#ifdef HAVE_SIGNAL
  sigset_t old_set;
#endif /* HAVE_SIGNAL */

  SSH_DEBUG(SSH_D_HIGHOK, ("Starting the event loop."));

  ssheloop.running = TRUE;
  ssheloop.is_clean_necessary = TRUE;
  ssheloop.is_pollcache_invalid = TRUE;
  ssheloop.in_select = FALSE;

#ifdef USE_POLL
  poll_nopoll_counter = 0;
#endif /* USE_POLL */

  ssh_eloop_gettimeofday(&prev_time);
  while (1)
    {
      done_something = FALSE;

#ifdef HAVE_SIGNAL
      /* Handle signals. */
      while (ssheloop.signal_fired)
        {
          int i;

          /* We don't want to get signals during this because we're
             modifying the signals list. */
          sigprocmask(SIG_BLOCK, &ssheloop.used_signals, &old_set);
          for (i = 1; i <= NSIG; i++)
            {
              if (ssheloop.fired_signals[i - 1])
                {
                  ssheloop.fired_signals[i - 1] = FALSE;
                  SSH_DEBUG(SSH_D_MIDOK, ("Calling a signal handler."));
                  if (ssheloop.signal_records[i - 1].callback)
                    (*ssheloop.signal_records[i - 1].callback)(i,
                        ssheloop.signal_records[i - 1].context);
                  done_something = TRUE;
                }
            }
          ssheloop.signal_fired = FALSE;

          /* Turn the mask off so that signals that have arrived during
             the iteration get into the queue. Then start the iteration
             again if the queue is not empty.
             XXX - shouldn't we call sigpending() to find out which signals
             were raised while blocked? */
          sigprocmask(SIG_SETMASK, &old_set, NULL);
        }
#endif /* HAVE_SIGNAL */

      ssheloop.select_timeout_ptr = NULL;

      /* Get current time */
      ssh_eloop_gettimeofday(&current_time);

      /* If there are any timeouts to be fired fire them now.  If
         there are any timeouts waiting set the timeout of the
         select() call to match the earliest of the timeouts. */
      while (1)
        {
          SshTimeoutCallback callback;
          void *callback_context;

          SSH_ELOOP_MUTEX_LOCK();

          if ((ph = ssh_adt_enumerate_start(ssheloop.to.ph_by_firing_time))
              == SSH_ADT_INVALID)
            {
              SSH_ELOOP_MUTEX_UNLOCK();
              break;
            }

          time_temp = ssh_adt_get(ssheloop.to.ph_by_firing_time, ph);

          if (ssh_event_loop_compare_time(&(time_temp->firing_time),
                                          &current_time) > 0)
            {
              SSH_ELOOP_MUTEX_UNLOCK();
              break;
            }

          SSH_ELOOP_MUTEX_UNLOCK();

          callback = time_temp->callback;
          callback_context = time_temp->context;

          SSH_DEBUG(SSH_D_MIDOK, ("firing timeout %qd",
                                  time_temp->identifier));

          ssh_cancel_timeout(time_temp);

          if (callback)
            {
              (*callback)(callback_context);
            }
          done_something = TRUE;
        }

      /* Determine the amount of time until the next timeout.  This
         can be in the past, because we run expire queue only once. */

      ssh_eloop_gettimeofday(&current_time);

      SSH_ELOOP_MUTEX_LOCK();

      if ((ph = ssh_adt_enumerate_start(ssheloop.to.ph_by_firing_time))
          != SSH_ADT_INVALID)
        {
          long sec, usec;

          time_temp = ssh_adt_get(ssheloop.to.ph_by_firing_time, ph);

          sec = time_temp->firing_time.tv_sec;
          usec = time_temp->firing_time.tv_usec;

          if (sec < current_time.tv_sec ||
              (sec == current_time.tv_sec && usec < current_time.tv_usec))
            {
              sec = 0;
              usec = 0;
            }
          else
            {
              sec = sec - current_time.tv_sec;
              if (usec < current_time.tv_usec)
                {
                  sec--;
                  usec = usec + 1000000 - current_time.tv_usec;
                }
              else
                usec = usec - current_time.tv_usec;
            }

          select_timeout.tv_sec = sec;
          select_timeout.tv_usec = usec;

          ssheloop.select_timeout_ptr = &select_timeout;
          SSH_DEBUG(SSH_D_LOWOK,
                    ("Select/Poll timeout: %ld seconds, %ld usec.",
                     ssheloop.select_timeout_ptr->tv_sec,
                     ssheloop.select_timeout_ptr->tv_usec));
        }

      SSH_ELOOP_MUTEX_UNLOCK();

#ifdef USE_SELECT
      max_fd = -1;
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);
#endif
      /* Remove killed filedescriptors */
      ssh_event_loop_clean_fds();
      nfds = 0;

      iorec_temp = ssheloop.io_records;
#ifdef USE_POLL
      if (ssheloop.is_pollcache_invalid == TRUE)
        {
          SSH_DEBUG(SSH_D_MY, ("poll cache has been invalidated!"));
#endif /* USE_POLL */

          while (iorec_temp != NULL)
            {
              SSH_ASSERT(iorec_temp->killed == FALSE);

              if ((iorec_temp->request & (SSH_IO_READ | SSH_IO_WRITE)) != 0)
                {
#ifdef USE_POLL
                  ssheloop.pfds[nfds].fd = iorec_temp->fd;
                  ssheloop.pfds[nfds].events = 0;;
                  ssheloop.pfds[nfds].revents = 0;

                  if (iorec_temp->request & SSH_IO_READ)
                    ssheloop.pfds[nfds].events |= POLLIN | POLLPRI;

                  if (iorec_temp->request & SSH_IO_WRITE)
                    ssheloop.pfds[nfds].events |= POLLOUT;

                  iorec_temp->poll_idx = nfds;

#else
                  if (iorec_temp->request & SSH_IO_READ)
                    FD_SET(iorec_temp->fd, &readfds);

                  if (iorec_temp->request & SSH_IO_WRITE)
                    FD_SET(iorec_temp->fd, &writefds);

                  if (max_fd < iorec_temp->fd)
                    max_fd = iorec_temp->fd;
#endif
                  nfds++;
                }
#ifdef USE_POLL
              else
                {
                  iorec_temp->poll_idx = -1;
                }
#endif /* USE_POLL */
              iorec_temp = iorec_temp->next;
            }
#ifdef USE_POLL
          ssheloop.pfd_size = nfds;
          ssheloop.is_pollcache_invalid = FALSE;
        }
      else
        nfds = ssheloop.pfd_size;
#endif /* USE_POLL */

      if (nfds < 1 && ssheloop.select_timeout_ptr == NULL
          && done_something == FALSE)
        break;

      /* Exit now if the event loop has been aborted. */
      if (!ssheloop.running)
        break;

      /* If we had done something (other than running idle timeouts) copy the
         current time to prev time */
      if (done_something)
        prev_time = current_time;

      if (ssheloop.select_timeout_ptr != NULL &&
          ssheloop.select_timeout_ptr->tv_sec == 0 &&
          ssheloop.select_timeout_ptr->tv_usec != 0)
        SSH_DEBUG(SSH_D_LOWOK,
                  ("select/poll timeout: %ld %ld",
                   (long)ssheloop.select_timeout_ptr->tv_sec,
                   (long)ssheloop.select_timeout_ptr->tv_usec));

      /* Check if a signal was received after the last time they were checked.
         If so, use a zero timeout instead of whatever we have scheduled
         now, so we don't end up waiting for the select() to return until the
         signal handler callback is called. */
      if (ssheloop.signal_fired)
        ssheloop.select_timeout_ptr = &ssheloop.select_timeout_no_wait;

      if (ssheloop.select_timeout_ptr != NULL || nfds > 0)
        {
          /* Raise the in_select flag. If signals arrive during the
             select() function call, the signal handler notices that and
             calls the callback for the signal immediately. */
          ssheloop.in_select = TRUE;

#ifdef USE_POLL
          poll_timeout = -1;

          if (ssheloop.select_timeout_ptr != NULL)
            {
              long sec, usec;

              sec = ssheloop.select_timeout_ptr->tv_sec;
              usec = ssheloop.select_timeout_ptr->tv_usec;

              if (sec >= ((1 << 30)/1000))
                poll_timeout = (int)(1 << 30);
              else
                poll_timeout = (sec * 1000) + (usec / 1000 );
            }


          SSH_DEBUG(SSH_D_LOWOK, ("Poll fds=%d timeout=%d.",
                                  nfds, poll_timeout));

          /* poll() is Very expensive, especially with large
             amounts of filedescriptors. So if we have a zero-timeout
             ready to go, then skip poll(). For fairness reasons
             we skip poll only a predefined amount of times before
             running a poll(). */
          if (done_something == TRUE && poll_timeout == 0
              && poll_nopoll_counter < 1000)
            {
              poll_nopoll_counter++;
              continue;
            }

          poll_nopoll_counter = 0;
          poll_return_value = poll(ssheloop.pfds, nfds, poll_timeout);
#else

          SSH_DEBUG(SSH_D_LOWOK, ("Select."));

          poll_return_value = select(max_fd + 1, &readfds, &writefds, NULL,
                                     ssheloop.select_timeout_ptr);

#endif

          ssheloop.in_select = FALSE;

          switch (poll_return_value)
            {
            case 0: /* Timeout */
              break;
            case -1: /* Error */
              switch (errno)
                {
                case ENOMEM:
                  SSH_DEBUG(SSH_D_NICETOKNOW,
                            ("poll() exited due to insufficient resources."));
                  break;
                case EBADF: /* Bad file descriptor. */
                  ssh_fatal("Bad file descriptor in the event loop.");
                  break;
                case EINTR: /* Caught a signal. */
                  SSH_DEBUG(SSH_D_NICETOKNOW,
                            ("poll() exited because of a caught signal."));
                  break;
                case EINVAL: /* Invalid time limit. */
                  ssh_fatal("Bad time limit in the event loop.");
                  break;
                }
              break;

            default: /* Some IO is ready */
              done_something = TRUE;
#ifdef USE_POLL
              idx = 0;

              /*
               * Enter the door lock, so that door calls won't
               * interrupt us!
               */
              mutex_lock(&door_lock);

              while (idx < nfds && poll_return_value > 0)
                {
                  short revents;
                  int reqs;

                  revents = ssheloop.pfds[idx].revents;
                  if (revents)
                    {
                      ssheloop.pfds[idx].revents = 0;
                      poll_return_value--;

                      iorec_temp =
                        ssheloop.fd_to_record_map[ssheloop.pfds[idx].fd];
                      SSH_ASSERT(iorec_temp != NULL);
                      SSH_ASSERT(iorec_temp->fd == ssheloop.pfds[idx].fd);

                      if (iorec_temp->killed == TRUE)
                        revents = 0;

                      reqs = iorec_temp->request;

                      if ((reqs & SSH_IO_READ) != 0)
                        {
                          if (revents & (POLLERR|POLLHUP|POLLNVAL))
                            {
                              /* If an error occurs, call a callback
                                 only once. */
                              revents = 0;
                              SSH_DEBUG(99, ("pollnval fd=%d!",
                                             iorec_temp->fd));
                              (*iorec_temp->callback)(SSH_IO_READ,
                                                      iorec_temp->context);
                            }
                          else if (revents & (POLLIN|POLLPRI))
                            {
                              SSH_DEBUG(99, ("pollin fd=%d!", iorec_temp->fd));
                              (*iorec_temp->callback)(SSH_IO_READ,
                                                      iorec_temp->context);
                            }
                        }

                      if (iorec_temp->killed == TRUE)
                        revents = 0;

                      reqs = iorec_temp->request;

                      if ((reqs & SSH_IO_WRITE) != 0)
                        {
                          if (revents & (POLLERR|POLLHUP|POLLNVAL))
                            {
                              revents = 0;
                              SSH_DEBUG(99, ("pollnvalfd=%d!",
                                             iorec_temp->fd));
                              (*iorec_temp->callback)(SSH_IO_WRITE,
                                                      iorec_temp->context);
                            }
                          else if (revents & POLLOUT)
                            {
                              SSH_DEBUG(99, ("pollout fd=%d", iorec_temp->fd));
                              (*iorec_temp->callback)(SSH_IO_WRITE,
                                                      iorec_temp->context);
                            }
                        }
                    }
                  idx++;
                }
              SSH_ASSERT(poll_return_value == 0);
#else
              iorec_temp = ssheloop.io_records;

              /*
               * Enter the door lock, so that door calls won't
               * interrupt us!
               */
              mutex_lock(&door_lock);

              while (iorec_temp != NULL)
                {
                  SshEloopIO *iorec_ptr;

                  if ((FD_ISSET(iorec_temp->fd, &readfds)) &&
                      (iorec_temp->killed == FALSE) &&
                      (iorec_temp->request & SSH_IO_READ))
                    (*iorec_temp->callback)(SSH_IO_READ, iorec_temp->context);

                  if ((FD_ISSET(iorec_temp->fd, &writefds)) &&
                      (iorec_temp->killed == FALSE) &&
                      (iorec_temp->request & SSH_IO_WRITE))
                    (*iorec_temp->callback)(SSH_IO_WRITE,
                                            iorec_temp->context);

                  iorec_ptr = &(iorec_temp->next);
                  iorec_temp = iorec_temp->next;
                }
#endif /* !USE_POLL */

              /* This unlock works for both POLL/non-POLL cases. */
	      mutex_unlock(&door_lock);
              break;
            }
        }
    }
}
