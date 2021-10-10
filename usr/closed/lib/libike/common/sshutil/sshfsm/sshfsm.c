/*

  sshfsm.c

  Author: Antti Huima <huima@ssh.fi>
          Markku Rossi <mtr@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Thu Aug 26 14:27:14 1999.

  */

#include "sshincludes.h"
#include "sshfsm.h"
#include "sshdebug.h"
#include "sshtimeouts.h"

/************************** Types and definitions ***************************/

#define SSH_DEBUG_MODULE "SshFSM"

/* Thread flags. */

#define SSH_FSM_RUNNING                 1 /* Control inside a step function */
#define SSH_FSM_IN_MESSAGE_QUEUE        2 /* Waiting for message handler call*/
#define SSH_FSM_CALLBACK_FLAG           4
#define SSH_FSM_DYNAMIC_THREAD          8 /* Thread malloc()ated. */

/* FSM flags. */
#define SSH_FSM_IN_SCHEDULER            1  /* Control inside scheduler. */
#define SSH_FSM_SCHEDULER_SCHEDULED     2  /* Scheduler scheduled for running*/


/***************************** Ring functions ******************************/

static void ring_add(SshFSMThread *root_ptr,
                     SshFSMThread object)
{
  if ((*root_ptr) == NULL)
    {
      *root_ptr = object;
      object->next = object->prev = object;
    }
  else
    {
      object->prev = (*root_ptr)->prev;
      (*root_ptr)->prev = object;
      object->prev->next = object;
      object->next = *root_ptr;
    }
}

static void ring_remove(SshFSMThread *root_ptr,
                        SshFSMThread object)
{
  if (object->next == object)
    {
      *root_ptr = NULL;
    }
  else
    {
      object->next->prev = object->prev;
      object->prev->next = object->next;
      if (*root_ptr == object)
        *root_ptr = object->next;
    }
}


/************************ Create and destroying FSMs ************************/

SshFSM ssh_fsm_create(void *context)
{
  SshFSM fsm;

  fsm = ssh_malloc(sizeof(*fsm));
  if (fsm == NULL)
    return NULL;

  ssh_fsm_init(fsm, context);

  return fsm;
}

void ssh_fsm_init(SshFSM fsm, void *context)
{
  memset(fsm, 0, sizeof(*fsm));
  fsm->context_data = context;
}

static void destroy_callback(void *ctx)
{
  SshFSM fsm = (SshFSM)ctx;

#ifdef DEBUG_LIGHT
  if (fsm->num_threads > 0)
    ssh_fatal("Tried to destroy a FSM that has %d thread(s) left",
              fsm->num_threads);
#endif /* DEBUG_LIGHT */

  /* Cancel all callbacks from the FSM. */
  ssh_cancel_timeouts(SSH_ALL_CALLBACKS, fsm);

  ssh_free(fsm);

  SSH_DEBUG(8, ("FSM context destroyed"));
}

void ssh_fsm_destroy(SshFSM fsm)
{
  if (fsm->flags & SSH_FSM_SCHEDULER_SCHEDULED)
    ssh_cancel_timeout(&fsm->fsm_timeout);

  ssh_register_timeout(&fsm->fsm_timeout,
                       0L, 0L, destroy_callback, (void *)fsm);
}

void ssh_fsm_uninit(SshFSM fsm)
{
#ifdef DEBUG_LIGHT
  if (fsm->num_threads > 0)
    ssh_fatal("Tried to destroy a FSM that has %d thread(s) left",
              fsm->num_threads);
#endif /* DEBUG_LIGHT */

  /* Cancel all callbacks from the FSM. */
  ssh_cancel_timeouts(SSH_ALL_CALLBACKS, fsm);

  SSH_DEBUG(8, ("FSM context uninitialized"));
}

void ssh_fsm_register_debug_names(SshFSM fsm, SshFSMStateDebug states,
                                  int num_states)
{
#ifdef DEBUG_LIGHT
  fsm->states = states;
  fsm->num_states = num_states;
#endif /* DEBUG_LIGHT */
}


/**************************** Thread operations *****************************/

/* Move threads. */
static void move_thread(SshFSMThread *from_ring,
                        SshFSMThread *to_ring,
                        SshFSMThread thread)
{
  ring_remove(from_ring, thread);
  ring_add(to_ring, thread);
}

/* Delete a thread. */
static void delete_thread(SshFSMThread thread)
{
  Boolean dynamic = FALSE;

  /* Store the info about dynamic threads into a local variable since
     the `thread' argument can be invalidated at the thread's
     destructor. */
  if (thread->flags & SSH_FSM_DYNAMIC_THREAD)
    dynamic = TRUE;

#ifdef DEBUG_LIGHT
  thread->fsm->num_threads--;
#endif /* DEBUG_LIGHT */

  /* Wake up all waiters.  We are dying now. */
  while (thread->waiting)
    {
      SSH_ASSERT(thread->waiting->status == SSH_FSM_T_WAITING_THREAD);
      thread->waiting->status = SSH_FSM_T_ACTIVE;
      move_thread(&(thread->waiting), (&thread->fsm->active), thread->waiting);
    }

  if (thread->destructor)
    (*thread->destructor)(thread->fsm, thread->context_data);

  if (dynamic)
    ssh_free(thread);
}

#ifdef DEBUG_LIGHT
static char *
ssh_fsm_state_name(SshFSM fsm, SshFSMStepCB step, char *buf, size_t buflen)
{
  int i;

  if (fsm->num_states)
    {
      for (i = 0; i < fsm->num_states; i++)
        if (fsm->states[i].func == step)
          {
            if (fsm->states[i].state_id)
              return fsm->states[i].state_id;

            break;
          }
    }

  ssh_snprintf(buf, buflen, "%p", step);
  return buf;
}

static char *
ssh_fsm_state_descr(SshFSM fsm, SshFSMStepCB step, char *buf, size_t buflen)
{
  int i;

  if (fsm->num_states)
    {
      for (i = 0; i < fsm->num_states; i++)
        if (fsm->states[i].func == step)
          {
            if (fsm->states[i].descr)
              return fsm->states[i].descr;

            break;
          }
    }

  ssh_snprintf(buf, buflen, "%p", step);
  return buf;
}
#endif /* DEBUG_LIGHT */

/* Internal dispatcher, scheduler, whatever. */
static void scheduler(SshFSM fsm)
{
  /* No recursive invocations! */
  if (fsm->flags & SSH_FSM_IN_SCHEDULER)
    return;

  SSH_DEBUG(8, ("Entering the scheduler"));
  SSH_DEBUG_INDENT;

  fsm->flags |= SSH_FSM_IN_SCHEDULER;

  while (1)
    {
      SshFSMThread thread;
      SshFSMStepStatus status;
#ifdef DEBUG_LIGHT
      char buf[128];
#endif /* DEBUG_LIGHT */

      if (fsm->active == NULL)
        {
          SSH_DEBUG_UNINDENT;
          SSH_DEBUG(6, ("No active threads so return from scheduler"));
          fsm->flags &= ~SSH_FSM_IN_SCHEDULER;
          break;
        }

      thread = fsm->active;
      ring_remove(&(fsm->active), thread);
      SSH_ASSERT(thread->status == SSH_FSM_T_ACTIVE);

      SSH_ASSERT(!(thread->flags & SSH_FSM_RUNNING));
      thread->flags |= SSH_FSM_RUNNING;

      SSH_DEBUG(8, ("Thread continuing from state `%s' (%s)",
                    ssh_fsm_state_name(fsm, thread->current_state,
                                       buf, sizeof(buf)),
                    ssh_fsm_state_descr(fsm, thread->current_state,
                                        buf, sizeof(buf))));

      /* Continue as long as it is possible. */
      do
        {
          status = (*thread->current_state)(fsm, thread,
                                            thread->context_data,
                                            fsm->context_data);

          /* Pass messages. */
          while (fsm->waiting_message_handler)
            {
              SshFSMThread msg_thr = fsm->waiting_message_handler;

              ring_remove(&(fsm->waiting_message_handler), msg_thr);

              SSH_ASSERT(msg_thr->message_handler != NULL_FNPTR);
              SSH_ASSERT(msg_thr->flags & SSH_FSM_IN_MESSAGE_QUEUE);

              SSH_DEBUG(8, ("Delivering the message %u to thread `%s'",
                            msg_thr->message,
                            (msg_thr->name
                             ? msg_thr->name : "unknown")));

              (*msg_thr->message_handler)(msg_thr, msg_thr->message);

              /* And put thread back to correct list. */
              msg_thr->flags &= ~SSH_FSM_IN_MESSAGE_QUEUE;
              switch (msg_thr->status)
                {
                case SSH_FSM_T_ACTIVE:
                  ring_add(&(fsm->active), msg_thr);
                  break;

                case SSH_FSM_T_SUSPENDED:
                  ring_add(&(fsm->waiting_external), msg_thr);
                  break;

                case SSH_FSM_T_WAITING_CONDITION:
                  SSH_ASSERT(msg_thr->waited.condition != NULL);
                  ring_add(&(msg_thr->waited.condition->waiting), msg_thr);
                  break;

                case SSH_FSM_T_WAITING_THREAD:
                  SSH_ASSERT(msg_thr->waited.thread != NULL);
                  ring_add(&(msg_thr->waited.thread->waiting), msg_thr);
                  break;
                }
            }
        }
      while (status == SSH_FSM_CONTINUE);

      thread->flags &= ~SSH_FSM_RUNNING;

      switch (status)
        {
        case SSH_FSM_FINISH:
          SSH_DEBUG(8, ("Thread finished in state `%s'",
                        ssh_fsm_state_name(fsm, thread->current_state,
                                           buf, sizeof(buf))));
          delete_thread(thread);
          break;

        case SSH_FSM_SUSPENDED:
          SSH_DEBUG(8, ("Thread suspended in state `%s'",
                        ssh_fsm_state_name(fsm, thread->current_state,
                                           buf, sizeof(buf))));
          thread->status = SSH_FSM_T_SUSPENDED;
          ring_add(&(fsm->waiting_external), thread);
          break;

        case SSH_FSM_WAIT_CONDITION:
          SSH_DEBUG(8, ("Thread waiting for a condition variable in "
                        "state `%s'",
                        ssh_fsm_state_name(fsm, thread->current_state,
                                           buf, sizeof(buf))));
          /* Already added to the condition variable's ring. */
          break;

        case SSH_FSM_WAIT_THREAD:
          SSH_DEBUG(8, ("Thread waiting for a thread to terminate in "
                        "state `%s'",
                        ssh_fsm_state_name(fsm, thread->current_state,
                                           buf, sizeof(buf))));
          /* Already added to the thread's ring. */
          break;

        case SSH_FSM_CONTINUE:
        case SSH_FSM_YIELD:
          ring_add(&(fsm->active), thread);
          break;
        }
    }
}

static void scheduler_callback(void *ctx)
{
  ((SshFSM)ctx)->flags &= ~SSH_FSM_SCHEDULER_SCHEDULED;
  scheduler((SshFSM)ctx);
}

static void schedule_scheduler(SshFSM fsm)
{
  if (!(fsm->flags & (SSH_FSM_IN_SCHEDULER |
                      SSH_FSM_SCHEDULER_SCHEDULED)))
    {
      fsm->flags |= SSH_FSM_SCHEDULER_SCHEDULED;
      ssh_register_timeout(&fsm->fsm_timeout,
                           0L, 0L, scheduler_callback, (void *)fsm);
    }
}

SshFSMThread ssh_fsm_thread_create(SshFSM fsm,
                                   SshFSMStepCB first_state,
                                   SshFSMMessageHandler ehandler,
                                   SshFSMDestructor destructor,
                                   void *context)
{
  SshFSMThread thread;

  thread = ssh_malloc(sizeof(*thread));
  if (thread == NULL)
    return NULL;

  ssh_fsm_thread_init(fsm, thread, first_state, ehandler, destructor, context);
  thread->flags |= SSH_FSM_DYNAMIC_THREAD;

  return thread;
}

void ssh_fsm_thread_init(SshFSM fsm, SshFSMThread thread,
                         SshFSMStepCB first_state,
                         SshFSMMessageHandler message_handler,
                         SshFSMDestructor destructor,
                         void *context)
{
#ifdef DEBUG_LIGHT
  char buf[128];
#endif /* DEBUG_LIGHT */

  SSH_DEBUG(8, ("Starting a new thread starting from `%s'",
                ssh_fsm_state_name(fsm, first_state,
                                   buf, sizeof(buf))));

  memset(thread, 0, sizeof(*thread));

  thread->fsm = fsm;
  thread->current_state = first_state;
  thread->message_handler = message_handler;
  thread->destructor = destructor;
  thread->context_data = context;

#ifdef DEBUG_LIGHT
  fsm->num_threads++;
#endif /* DEBUG_LIGHT */

  ring_add(&(fsm->active), thread);
  thread->status = SSH_FSM_T_ACTIVE;

  schedule_scheduler(fsm);
}

void ssh_fsm_set_next(SshFSMThread thread, SshFSMStepCB next_state)
{
  thread->current_state = next_state;
}

SshFSMStepCB ssh_fsm_get_thread_current_state(SshFSMThread thread)
{
  return thread->current_state;
}


void ssh_fsm_continue(SshFSMThread thread)
{
  SSH_DEBUG(8, ("Continue called for thread `%s'.",
                thread->name ? thread->name : "unknown"));

  /* Check if the call comes from a message handler. */
  if (thread->flags & SSH_FSM_IN_MESSAGE_QUEUE)
    {
      SSH_DEBUG(8, ("Continue called from a message handler"));
      /* We simply make the thread active. */
      thread->status = SSH_FSM_T_ACTIVE;
      return;
    }

  if (thread->status == SSH_FSM_T_SUSPENDED)
    {
      SSH_DEBUG(8, ("Reactivating a suspended thread"));
      thread->status = SSH_FSM_T_ACTIVE;
      move_thread(&(thread->fsm->waiting_external),
                  &(thread->fsm->active),
                  thread);
      schedule_scheduler(thread->fsm);
      return;
    }

  if (thread->status == SSH_FSM_T_WAITING_CONDITION)
    {
      SSH_DEBUG(8, ("Reactivating a thread waiting for a condition variable "
                    "(detaching from the condition)"));
      thread->status = SSH_FSM_T_ACTIVE;
      move_thread(&(thread->waited.condition->waiting),
                  &(thread->fsm->active),
                  thread);
      schedule_scheduler(thread->fsm);
      return;
    }

  if (thread->status == SSH_FSM_T_WAITING_THREAD)
    {
      SSH_DEBUG(8, ("Reactivating a thread waiting for a thread to terminate "
                    "(detaching from the thread)"));
      thread->status = SSH_FSM_T_ACTIVE;
      move_thread(&(thread->waited.thread->waiting),
                  &(thread->fsm->active),
                  thread);
      schedule_scheduler(thread->fsm);
      return;
    }

  if (thread->status == SSH_FSM_T_ACTIVE)
    {
      SSH_DEBUG(8, ("Reactivating an already active thread (do nothing)"));
      return;
    }

  SSH_NOTREACHED;
}

void ssh_fsm_kill_thread(SshFSMThread thread)
{
  SSH_ASSERT(!(thread->flags & SSH_FSM_RUNNING));

  /* Remove the thread from the appropriate ring. */
  switch (thread->status)
    {
    case SSH_FSM_T_ACTIVE:
      ring_remove(&(thread->fsm->active), thread);
      break;

    case SSH_FSM_T_SUSPENDED:
      ring_remove(&(thread->fsm->waiting_external), thread);
      break;

    case SSH_FSM_T_WAITING_CONDITION:
      ring_remove(&(thread->waited.condition->waiting), thread);
      break;

    case SSH_FSM_T_WAITING_THREAD:
      ring_remove(&(thread->waited.thread->waiting), thread);
      break;
    }

  delete_thread(thread);
}

void ssh_fsm_wait_thread(SshFSMThread thread, SshFSMThread waited)
{
  /* A thread can start to wait a thread only when it is running. */
  SSH_ASSERT(thread->flags & SSH_FSM_RUNNING);
  SSH_ASSERT(thread->status == SSH_FSM_T_ACTIVE);
  ring_add(&(waited->waiting), thread);
  thread->status = SSH_FSM_T_WAITING_THREAD;
  thread->waited.thread = waited;
}


void ssh_fsm_set_thread_name(SshFSMThread thread, const char *name)
{
#ifdef DEBUG_LIGHT
  thread->name = (char *) name;
#endif /* DEBUG_LIGHT */
}


const char *ssh_fsm_get_thread_name(SshFSMThread thread)
{
#ifdef DEBUG_LIGHT
  return thread->name;
#else /* not DEBUG_LIGHT */
  return "???";
#endif /* not DEBUG_LIGHT */
}


/************************** Accessing context data **************************/

void *ssh_fsm_get_gdata(SshFSMThread thread)
{
  return thread->fsm->context_data;
}

void *ssh_fsm_get_gdata_fsm(SshFSM fsm)
{
  return fsm->context_data;
}

void *ssh_fsm_get_tdata(SshFSMThread thread)
{
  return thread->context_data;
}

SshFSM ssh_fsm_get_fsm(SshFSMThread thread)
{
  return thread->fsm;
}


/*************************** Condition variables ****************************/

SshFSMCondition ssh_fsm_condition_create(SshFSM fsm)
{
  SshFSMCondition condition;

  condition = ssh_malloc(sizeof(*condition));
  if (condition == NULL)
    return NULL;

  ssh_fsm_condition_init(fsm, condition);

  return condition;
}

void ssh_fsm_condition_init(SshFSM fsm, SshFSMCondition condition)
{
  memset(condition, 0, sizeof(*condition));
}

void ssh_fsm_condition_destroy(SshFSMCondition condition)
{
  SSH_ASSERT(condition->waiting == NULL);
  ssh_free(condition);
}

void ssh_fsm_condition_uninit(SshFSMCondition condition)
{
  SSH_ASSERT(condition->waiting == NULL);
}

void ssh_fsm_condition_signal(SshFSM fsm, SshFSMCondition condition)
{
  SSH_DEBUG(8, ("Signalling a condition variable"));

  if (condition->waiting == NULL)
    {
      SSH_DEBUG(8, ("Waiting list empty"));
      return;
    }

  SSH_ASSERT(condition->waiting->status == SSH_FSM_T_WAITING_CONDITION);

  SSH_DEBUG(8, ("Ok, activating one of the waiting threads"));

  condition->waiting->status = SSH_FSM_T_ACTIVE;

  move_thread(&(condition->waiting), &(fsm->active), condition->waiting);
  schedule_scheduler(fsm);
}

void ssh_fsm_condition_broadcast(SshFSM fsm, SshFSMCondition condition)
{
  while (condition->waiting != NULL)
    ssh_fsm_condition_signal(fsm, condition);
}

void ssh_fsm_condition_wait(SshFSMThread thread,
                            SshFSMCondition condition)
{
  /* A thread can start to wait a condition only when it is running. */
  SSH_ASSERT(thread->flags & SSH_FSM_RUNNING);
  SSH_ASSERT(thread->status == SSH_FSM_T_ACTIVE);
  ring_add(&(condition->waiting), thread);
  thread->status = SSH_FSM_T_WAITING_CONDITION;
  thread->waited.condition = condition;
}


/**************************** Asynchronous calls ****************************/

void ssh_fsm_set_callback_flag(SshFSMThread thread)
{
  thread->flags |= SSH_FSM_CALLBACK_FLAG;
}

void ssh_fsm_drop_callback_flag(SshFSMThread thread)
{
  thread->flags &= ~SSH_FSM_CALLBACK_FLAG;
}

Boolean ssh_fsm_get_callback_flag(SshFSMThread thread)
{
  return ((thread->flags & SSH_FSM_CALLBACK_FLAG) != 0);
}


/********************************* Messages *********************************/

void ssh_fsm_throw(SshFSMThread thread,
                   SshFSMThread recipient,
                   SshUInt32 message)
{
  /* Message throwing is not allowed outside the execution of the
     state machine. */
  SSH_ASSERT(thread->fsm->flags & SSH_FSM_IN_SCHEDULER);
  SSH_ASSERT(thread != recipient);

  if (recipient->message_handler == NULL_FNPTR)
    /* Nothing to do. */
    return;

  /* Check the state of the recipient and remove it from its ring. */
  switch (recipient->status)
    {
    case SSH_FSM_T_ACTIVE:
      ring_remove(&(recipient->fsm->active), recipient);
      break;

    case SSH_FSM_T_SUSPENDED:
      ring_remove(&(recipient->fsm->waiting_external), recipient);
      break;

    case SSH_FSM_T_WAITING_CONDITION:
      ring_remove(&(recipient->waited.condition->waiting), recipient);
      break;

    case SSH_FSM_T_WAITING_THREAD:
      ring_remove(&(recipient->waited.thread->waiting), recipient);
      break;
    }

  /* Add the thread to the list of threads, waiting for message
     handler call. */
  recipient->flags |= SSH_FSM_IN_MESSAGE_QUEUE;
  recipient->message = message;
  ring_add(&(thread->fsm->waiting_message_handler), recipient);
}
