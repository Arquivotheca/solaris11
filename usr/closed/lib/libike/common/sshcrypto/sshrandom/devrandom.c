/*

  devrandom.c

  (P)RNG, relies on system /dev/random to get the data.

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshcrypt_i.h"
#include "sshrandom_i.h"

#define SSH_DEBUG_MODULE "SshRandomDev"

#ifdef VXWORKS
#define open(x,y) open((x),(y), 0777)
#endif /* VXWORKS */

typedef struct SshRandomDevStateRec {
  int fd;
} *SshRandomDevState, SshRandomDevStateStruct;

static SshCryptoStatus
ssh_random_devrandom_get_bytes(void *context,
                               unsigned char *buf, size_t buflen)
{
  SshRandomDevState state = (SshRandomDevState) context;
  int offset, got;

  /* Read until buflen bytes are in or an error occurs */
  offset = 0;

  while (offset < buflen)
    {
      got = read(state->fd, buf + offset, buflen - offset);

      if (got == 0 || got == -1)
        return SSH_CRYPTO_OPERATION_FAILED;

      offset += got;
    }

  SSH_ASSERT(offset == buflen);

  return SSH_CRYPTO_OK;
}

static SshCryptoStatus
ssh_random_devrandom_init(void **context_ret)
{
  int fd;
  SshRandomDevState state;
  const char *dev = "/dev/random";

  SSH_DEBUG(2, ("Using device `%s'", dev));

  if (!(state = ssh_calloc(1, sizeof(*state))))
    return SSH_CRYPTO_NO_MEMORY;

  if ((fd = open(dev, O_RDONLY)) == -1)
    {
      ssh_free(state);
      return SSH_CRYPTO_UNSUPPORTED;
    }

  state->fd = fd;
  *context_ret = state;

  return SSH_CRYPTO_OK;
}

static void
ssh_random_devrandom_uninit(void *context)
{
  SshRandomDevState state = (SshRandomDevState) context;
  close(state->fd);
  ssh_free(state);
}

const SshRandomDefStruct ssh_random_devrandom = {
  "device",
  0,
  ssh_random_devrandom_init, ssh_random_devrandom_uninit,
  NULL_FNPTR, ssh_random_devrandom_get_bytes
};
