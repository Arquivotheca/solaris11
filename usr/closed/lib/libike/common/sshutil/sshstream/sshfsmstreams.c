/*

  sshfsmstreams.c

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Thu Sep  2 15:21:26 1999.

  */

#include "sshincludes.h"

#include "sshdebug.h"
#include "sshfsmstreams.h"
#include "sshfsm.h"
#include "sshstream.h"
#include "sshbuffer.h"

#define SSH_DEBUG_MODULE "SshStreamstub"

#define SSH_STREAMSTUB_EXPECT_READ_NOTIFY       0x0001
#define SSH_STREAMSTUB_EXPECT_WRITE_NOTIFY      0x0002
#define SSH_STREAMSTUB_SENDING_EOF              0x0004
#define SSH_STREAMSTUB_EOF_SENT                 0x0008
#define SSH_STREAMSTUB_DRAINING                 0x0010

#define SSH_STREAMSTUB_READER_DIED              0xf000
#define SSH_STREAMSTUB_WRITER_DIED              0xf001

/* Prototypes for state functions. */
SSH_FSM_STEP(ssh_streamstub_abort_reader);
SSH_FSM_STEP(ssh_streamstub_read);
SSH_FSM_STEP(ssh_streamstub_abort_writer);
SSH_FSM_STEP(ssh_streamstub_write);
SSH_FSM_STEP(ssh_streamstub_die);
SSH_FSM_STEP(ssh_streamstub_abort);
SSH_FSM_STEP(ssh_streamstub_finish);
SSH_FSM_STEP(ssh_streamstub_send_eof);
SSH_FSM_STEP(ssh_streamstub_parent);

#define SSH_FSM_TDATA(type) \
type tdata = ssh_fsm_get_tdata(thread)

typedef struct {
  SshFSMThread reader;
  SshFSMThread writer;
  SshStream stream;
  SshUInt32 *flags;
  SshFSMCondition finished_condition;
} SshFSMStreamParentDataRec;

typedef struct {
  SshFSMThread parent;

  SshStream stream;             /* The stream to access. */

  SshBuffer in_buf;            /* Buffer where data will be read to. */

  SshUInt32 in_buf_limit;       /* `in_buf' won't grow below this limit. */

  SshUInt32 *flags;             /* shared flags */

  SshFSMCondition got_more;     /* Condition the stub will signal when
                                   more data has been got in `in_buf'. */
  SshFSMCondition in_buf_shrunk;
                                /* Condition that outside must signal when
                                   `in_buf' has shrunk. */

  SshUInt32 own_flags;          /* Own flags. */
} SshFSMStreamReaderDataRec;

typedef struct {
  SshFSMThread parent;

  SshStream stream;             /* The stream to access. */

  SshBuffer out_buf;           /* Buffer where data will be written from. */

  SshUInt32 *flags;             /* shared flags */

  SshFSMCondition out_buf_shrunk;
                                /* Condition the stub will signal when
                                   data has been consumed from `out_buf'. */
  SshFSMCondition data_present;
                                /* Condition that outside must signal when
                                   `out_buf' has got some [more] data. */
  SshUInt32 own_flags;          /* Own flags. */
} SshFSMStreamWriterDataRec;

/* The reader thread. */

static void reader_message_handler(SshFSMThread thread, SshUInt32 message)
{
  switch (message)
    {
      case SSH_STREAMSTUB_ABORT:
        ssh_fsm_set_next(thread, ssh_streamstub_abort_reader);
        ssh_fsm_continue(thread);
        break;

    default:
      SSH_NOTREACHED;
    }
}

SSH_FSM_STEP(ssh_streamstub_abort_reader)
{
  SSH_FSM_TDATA(SshFSMStreamReaderDataRec *);
  SSH_FSM_THROW(tdata->parent, SSH_STREAMSTUB_READER_DIED);
  return SSH_FSM_FINISH;
}

SSH_FSM_STEP(ssh_streamstub_read)
{
  SSH_FSM_TDATA(SshFSMStreamReaderDataRec *);
  int result;
  unsigned char *ptr;
  size_t room;

  SSH_ASSERT(tdata->stream != NULL);
  SSH_ASSERT(tdata->in_buf != NULL);

  room = tdata->in_buf_limit - ssh_buffer_len(tdata->in_buf);

  SSH_ASSERT(room > 0);

  ssh_buffer_append_space(tdata->in_buf, &ptr, room);

  result = ssh_stream_read(tdata->stream, ptr, room);

  if (result < 0)
    {
      SSH_DEBUG(8, ("Read blocks."));
      ssh_buffer_consume_end(tdata->in_buf, room);
      /* Blocking. */
      tdata->own_flags |= SSH_STREAMSTUB_EXPECT_READ_NOTIFY;
      return SSH_FSM_SUSPENDED;
    }

  if (result == 0)
    {
      SSH_DEBUG(8, ("Read returned EOF."));
      /* EOF got. */
      ssh_buffer_consume_end(tdata->in_buf, room);
      *(tdata->flags) |= SSH_STREAMSTUB_EOF_RECEIVED;
      if (tdata->got_more != NULL)
        SSH_FSM_CONDITION_SIGNAL(tdata->got_more);
      SSH_FSM_SET_NEXT(ssh_streamstub_abort_reader);
      return SSH_FSM_CONTINUE;
    }

  /* result > 0 */

  if (room > result)
    {
      SSH_DEBUG(8, ("Read in %d bytes, continuing.", result));
      ssh_buffer_consume_end(tdata->in_buf, room - result);
      if (tdata->got_more != NULL)
        SSH_FSM_CONDITION_SIGNAL(tdata->got_more);
      return SSH_FSM_CONTINUE;
    }
  else
    {
      SSH_DEBUG(8, ("Read in %d bytes, buffer full.", result));
      if (tdata->got_more != NULL)
        SSH_FSM_CONDITION_SIGNAL(tdata->got_more);
      SSH_FSM_CONDITION_WAIT(tdata->in_buf_shrunk);
    }
}

/* The writer thread. */

static void writer_message_handler(SshFSMThread thread, SshUInt32 message)
{
  SSH_FSM_TDATA(SshFSMStreamWriterDataRec *);

  switch (message)
    {
    case SSH_STREAMSTUB_ABORT:
      ssh_fsm_set_next(thread, ssh_streamstub_abort_writer);
      ssh_fsm_continue(thread);
      break;

    case SSH_STREAMSTUB_SEND_EOF:
    case SSH_STREAMSTUB_FINISH:
      tdata->own_flags |= SSH_STREAMSTUB_SENDING_EOF;
      ssh_fsm_continue(thread);
      break;

    default:
      SSH_NOTREACHED;
    }
}

SSH_FSM_STEP(ssh_streamstub_abort_writer)
{
  SSH_FSM_TDATA(SshFSMStreamWriterDataRec *);
  SSH_FSM_THROW(tdata->parent, SSH_STREAMSTUB_WRITER_DIED);
  return SSH_FSM_FINISH;
}

SSH_FSM_STEP(ssh_streamstub_write)
{
  SSH_FSM_TDATA(SshFSMStreamWriterDataRec *);
  int result;
  unsigned char *ptr;

  SSH_ASSERT(tdata->stream != NULL);
  SSH_ASSERT(tdata->out_buf != NULL);

  ptr = ssh_buffer_ptr(tdata->out_buf);

  if (ssh_buffer_len(tdata->out_buf) == 0 &&
      ((tdata->own_flags) & SSH_STREAMSTUB_SENDING_EOF))
    {
      SSH_ASSERT(!(tdata->own_flags & SSH_STREAMSTUB_EOF_SENT));
      SSH_DEBUG(8, ("Sending eof."));
      tdata->own_flags |= SSH_STREAMSTUB_EOF_SENT;
      ssh_stream_output_eof(tdata->stream);
      SSH_FSM_SET_NEXT(ssh_streamstub_abort_writer);
      return SSH_FSM_CONTINUE;
    }

  if (ssh_buffer_len(tdata->out_buf) == 0)
    {
      SSH_DEBUG(8, ("Nothing to write."));
      SSH_FSM_CONDITION_WAIT(tdata->data_present);
    }

  result = ssh_stream_write(tdata->stream, ptr,
                            ssh_buffer_len(tdata->out_buf));

  if (result < 0)
    {
      /* Blocking. */
      SSH_DEBUG(8, ("Write blocks."));
      tdata->own_flags |= SSH_STREAMSTUB_EXPECT_WRITE_NOTIFY;
      return SSH_FSM_SUSPENDED;
    }

  if (result == 0)
    {
      /* EOF got. */
      SSH_DEBUG(8, ("Write fails."));
      *(tdata->flags) |= SSH_STREAMSTUB_OUTPUT_CLOSED;
      if (tdata->out_buf_shrunk != NULL)
        SSH_FSM_CONDITION_SIGNAL(tdata->out_buf_shrunk);
      SSH_FSM_SET_NEXT(ssh_streamstub_abort_writer);
      return SSH_FSM_CONTINUE;
    }

  /* result > 0 */
  SSH_DEBUG(8, ("Wrote %d bytes, continuing.", result));
  ssh_buffer_consume(tdata->out_buf, result);
  if (tdata->out_buf_shrunk != NULL)
    SSH_FSM_CONDITION_SIGNAL(tdata->out_buf_shrunk);
  return SSH_FSM_CONTINUE;
}

/* The parent thread. */

static void parent_message_handler(SshFSMThread thread, SshUInt32 message)
{
  SSH_FSM_TDATA(SshFSMStreamParentDataRec *);

  switch (message)
    {
      case SSH_STREAMSTUB_SEND_EOF:
        ssh_fsm_set_next(thread, ssh_streamstub_send_eof);
        ssh_fsm_continue(thread);
        return;

      case SSH_STREAMSTUB_FINISH:
        ssh_fsm_set_next(thread, ssh_streamstub_finish);
        ssh_fsm_continue(thread);
        return;

      case SSH_STREAMSTUB_ABORT:
        ssh_fsm_set_next(thread, ssh_streamstub_abort);
        ssh_fsm_continue(thread);
        return;

      case SSH_STREAMSTUB_READER_DIED:
        SSH_DEBUG(8, ("Child `reader' has died."));
        tdata->reader = NULL;
        break;

      case SSH_STREAMSTUB_WRITER_DIED:
        SSH_DEBUG(8, ("Child `writer' has died."));
        tdata->writer = NULL;
        break;

    default:
      SSH_NOTREACHED;
    }

  /* When both children are dead, prepare to die. */
  if (tdata->reader == NULL && tdata->writer == NULL)
    {
      SSH_DEBUG(8, ("Both children dead, let the parent die, too."));
      ssh_fsm_set_next(thread, ssh_streamstub_die);
      ssh_fsm_continue(thread);
    }
}

SSH_FSM_STEP(ssh_streamstub_die)
{
  SSH_FSM_TDATA(SshFSMStreamParentDataRec *);
  *(tdata->flags) |= SSH_STREAMSTUB_FINISHED;
  if (tdata->finished_condition != NULL)
    SSH_FSM_CONDITION_SIGNAL(tdata->finished_condition);
  SSH_DEBUG(4, ("Dying..."));
  ssh_stream_destroy(tdata->stream);
  return SSH_FSM_FINISH;
}

SSH_FSM_STEP(ssh_streamstub_abort)
{
  SSH_FSM_TDATA(SshFSMStreamParentDataRec *);
  if (tdata->writer != NULL)
    {
      SSH_DEBUG(10, ("Throwing ABORT message to `writer' thread."));
      SSH_FSM_THROW(tdata->writer, SSH_STREAMSTUB_ABORT);
    }
  if (tdata->reader != NULL)
    {
      SSH_DEBUG(10, ("Throwing ABORT message to `reader' thread."));
      SSH_FSM_THROW(tdata->reader, SSH_STREAMSTUB_ABORT);
    }
  return SSH_FSM_SUSPENDED;
}

SSH_FSM_STEP(ssh_streamstub_finish)
{
  SSH_FSM_TDATA(SshFSMStreamParentDataRec *);
  if (tdata->writer != NULL)
    SSH_FSM_THROW(tdata->writer, SSH_STREAMSTUB_FINISH);
  if (tdata->reader != NULL)
    SSH_FSM_THROW(tdata->reader, SSH_STREAMSTUB_ABORT);
  return SSH_FSM_SUSPENDED;
}

SSH_FSM_STEP(ssh_streamstub_send_eof)
{
  SSH_FSM_TDATA(SshFSMStreamParentDataRec *);
  if (tdata->writer != NULL)
    SSH_FSM_THROW(tdata->writer, SSH_STREAMSTUB_SEND_EOF);
  return SSH_FSM_SUSPENDED;
}

static void got_read_notify(SshFSMThread thread)
{
  SSH_FSM_TDATA(SshFSMStreamReaderDataRec *);
  if (tdata->own_flags & SSH_STREAMSTUB_EXPECT_READ_NOTIFY)
    {
      tdata->own_flags &= ~SSH_STREAMSTUB_EXPECT_READ_NOTIFY;
      ssh_fsm_continue(thread);
    }
}

static void got_write_notify(SshFSMThread thread)
{
  SSH_FSM_TDATA(SshFSMStreamWriterDataRec *);
  if (tdata->own_flags & SSH_STREAMSTUB_EXPECT_WRITE_NOTIFY)
    {
      tdata->own_flags &= ~SSH_STREAMSTUB_EXPECT_WRITE_NOTIFY;
      ssh_fsm_continue(thread);
    }
}

static void got_disconnect(SshFSMThread thread)
{
  ssh_fsm_throw(thread, thread, SSH_STREAMSTUB_ABORT);
}

static void stream_callback(SshStreamNotification notification,
                            void *context)
{
  SshFSMThread thread = context;
  SshFSMStreamParentDataRec *d = ssh_fsm_get_tdata(thread);

  switch (notification)
    {
    case SSH_STREAM_INPUT_AVAILABLE:
      SSH_DEBUG(8, ("Got input available notification."));
      if (d->reader != NULL)
        got_read_notify(d->reader);
      break;

    case SSH_STREAM_CAN_OUTPUT:
      SSH_DEBUG(8, ("Got can output notification."));
      if (d->writer != NULL)
        got_write_notify(d->writer);
      break;

    case SSH_STREAM_DISCONNECTED:
      SSH_DEBUG(8, ("Got stream disconnected notification."));
      if (d->writer != NULL)
        got_disconnect(d->writer);
      if (d->reader != NULL)
        got_disconnect(d->reader);
      break;
    }
}

SSH_FSM_STEP(ssh_streamstub_parent)
{
  return SSH_FSM_SUSPENDED;
}

void ssh_fsmstream_thread_destroy(SshFSM fsm, void *context)
{
  ssh_xfree(context);
}

SshFSMThread ssh_streamstub_spawn(SshFSM fsm,
                                  SshStream stream,
                                  SshBuffer in_buf,
                                  SshBuffer out_buf,
                                  SshUInt32 in_buf_limit,
                                  SshFSMCondition stub_has_read_more,
                                  SshFSMCondition in_buf_has_shrunk,
                                  SshFSMCondition stub_has_written_some,
                                  SshFSMCondition out_buf_has_grown,
                                  SshFSMCondition stub_finished,
                                  SshUInt32 *shared_flags)
{
  SshFSMStreamParentDataRec *p;
  SshFSMStreamReaderDataRec *r;
  SshFSMStreamWriterDataRec *w;
  SshFSMThread thread;

  p = ssh_xcalloc(1, sizeof(*p));
  r = ssh_xcalloc(1, sizeof(*r));
  w = ssh_xcalloc(1, sizeof(*w));

  thread = ssh_fsm_thread_create(fsm,
                                 ssh_streamstub_parent,
                                 parent_message_handler,
                                 ssh_fsmstream_thread_destroy,
                                 p);

  p->finished_condition = stub_finished;


  p->reader = ssh_fsm_thread_create(fsm,
                                    ssh_streamstub_read,
                                    reader_message_handler,
                                    ssh_fsmstream_thread_destroy,
                                    r);

  p->writer = ssh_fsm_thread_create(fsm,
                                    ssh_streamstub_write,
                                    writer_message_handler,
                                    ssh_fsmstream_thread_destroy,
                                    w);

  ssh_fsm_set_thread_name(thread, "streamstub_parent");
  ssh_fsm_set_thread_name(p->reader, "streamstub_reader");
  ssh_fsm_set_thread_name(p->writer, "streamstub_writer");

  p->stream = stream;

  p->flags = shared_flags;

  r = ssh_fsm_get_tdata(p->reader);
  w = ssh_fsm_get_tdata(p->writer);

  r->stream = stream;
  r->in_buf = in_buf;
  r->in_buf_limit = in_buf_limit;
  r->flags = shared_flags;
  r->got_more = stub_has_read_more;
  r->in_buf_shrunk = in_buf_has_shrunk;
  r->own_flags = 0;

  w->stream = stream;
  w->out_buf = out_buf;
  w->flags = shared_flags;
  w->out_buf_shrunk = stub_has_written_some;
  w->data_present = out_buf_has_grown;
  w->own_flags = 0;

  r->parent = w->parent = thread;

  SSH_DEBUG(8, ("Setting the stream callback."));

  ssh_stream_set_callback(stream, stream_callback, thread);

  return thread;
}
