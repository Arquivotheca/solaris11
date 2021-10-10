/*

  sshfsmstreams.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Thu Sep  2 14:49:34 1999.

  */

#ifndef SSH_FSM_STREAMS_H_INCLUDED
#define SSH_FSM_STREAMS_H_INCLUDED

#include "sshfsm.h"
#include "sshbuffer.h"
#include "sshstream.h"

/* After throwing any one of these two messages, none of the condition
   variables will be accessed and they can be destroyed if so wished. */

/* The `abort' message will, when sent, kill the receiving thread
   immediately AND destroy the underlying stream. */
#define SSH_STREAMSTUB_ABORT ((SshUInt32)0)

/* The `finish' message will, when sent, cause the thread to drain the
   outgoing buffer and then finish the thread, killing the stream. */
#define SSH_STREAMSTUB_FINISH ((SshUInt32)1)

/* The `send_eof' message will, when sent, cause EOF to be sent
   to the underlying stream. */
#define SSH_STREAMSTUB_SEND_EOF ((SshUInt32)2)

/* Flags. */

/* This flag will be set by the stub when eof has been received from
   the stream. At the same time, `stub_has_read_more' will be signaled. */
#define SSH_STREAMSTUB_EOF_RECEIVED     0x0001

/* This flag will be set by the stub when it has finished. At the same
   time, `stub_finished' will be signaled. */
#define SSH_STREAMSTUB_FINISHED         0x0002

/* This flag will be set by the stub when write has failed
   permamently, i.e. when the outside direction has been closed.
   At the same time, `stub_has_written_some' will be signaled. */
#define SSH_STREAMSTUB_OUTPUT_CLOSED    0x0004

/* Spawn a streamstub thread. */
SshFSMThread ssh_streamstub_spawn(SshFSM fsm,
                                  SshStream stream,
                                  SshBuffer in_buf,
                                  SshBuffer out_buf,
                                  SshUInt32 in_buf_limit,
                                  /* stub_has_read_more can be NULL,
                                     in which case it won't get signaled. */
                                  SshFSMCondition stub_has_read_more,
                                  SshFSMCondition in_buf_has_shrunk,
                                  /* stub_has_written_more can be NULL,
                                     in which case it won't get signaled. */
                                  SshFSMCondition stub_has_written_some,
                                  SshFSMCondition out_buf_has_grown,
                                  /* stub_finished can be NULL,
                                     in which case it won't get signaled. */
                                  SshFSMCondition stub_finished,
                                  SshUInt32 *shared_flags);

#endif /* SSH_FSM_STREAMS_H_INCLUDED */
