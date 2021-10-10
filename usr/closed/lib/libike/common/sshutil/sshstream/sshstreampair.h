/*

sshstreampair.h

Author: Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

A pair of streams connected to each other, like a bidirectional pipe.  This
is mostly used for testing.

*/

#ifndef SSHSTREAMPAIR_H
#define SSHSTREAMPAIR_H

#include "sshstream.h"

/* Creates a pair of streams so that everything written on one stream
   will appear as output from the other stream. */
void ssh_stream_pair_create(SshStream *stream1_return,
                            SshStream *stream2_return);

#endif /* SSHSTREAMPAIR_H */
