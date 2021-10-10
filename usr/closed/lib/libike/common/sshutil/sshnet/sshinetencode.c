/*

  sshinetencode.c

  Author:
        Santeri Paavolainen <santtu@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved

*/

#include "sshincludes.h"
#include "sshencode.h"
#include "sshinetencode.h"

#define SSH_DEBUG_MODULE "SshInetEncode"

size_t ssh_encode_ipaddr_array(unsigned char *buf, size_t bufsize,
                               const SshIpAddr ip)
{
  size_t got;
  size_t desired_len;

  if (!ip || ip->type == SSH_IP_TYPE_NONE)
    return ssh_encode_array(buf, bufsize,
                            SSH_FORMAT_CHAR, (unsigned int) SSH_IP_TYPE_NONE,
                            SSH_FORMAT_END);
  desired_len = 1 + 4 + SSH_IP_ADDR_LEN(ip);
  SSH_ASSERT(desired_len <= SSH_MAX_IPADDR_ENCODED_LENGTH);
  if ((got = ssh_encode_array(buf, bufsize,
                                SSH_FORMAT_CHAR, (unsigned int) ip->type,
                                SSH_FORMAT_UINT32, ip->mask_len,
                                SSH_FORMAT_DATA,
                                ip->addr_data, (size_t)(SSH_IP_ADDR_LEN(ip)),
                              SSH_FORMAT_END)) != desired_len)
    return 0;
  return desired_len;
}

size_t ssh_encode_ipaddr_array_alloc(unsigned char **buf_return,
                                     const SshIpAddr ip)
{
  size_t req, got;

  if (ip->type == SSH_IP_TYPE_NONE)
    req = 1;
  else
    req = 1 + 4 + SSH_IP_ADDR_LEN(ip);

  if (buf_return == NULL)
    return req;

  if ((*buf_return = ssh_malloc(req)) == NULL)
    return 0;

  got = ssh_encode_ipaddr_array(*buf_return, req, ip);

  if (got != req)
    {
      ssh_free(*buf_return);
      *buf_return = NULL;
      return 0;
    }

  return got;
}

size_t ssh_decode_ipaddr_array(const unsigned char *buf, size_t len,
                               SshIpAddr ip)
{
  size_t point, got;
  SshUInt32 mask_len;
  unsigned int type;

  point = 0;

  if ((got = ssh_decode_array(buf + point, len - point,
                              SSH_FORMAT_CHAR, &type,
                              SSH_FORMAT_END)) != 1)
      return 0;

  ip->type = type;

  point += got;

  if (ip->type == SSH_IP_TYPE_NONE)
    return point;

  if ((got = ssh_decode_array(buf + point, len - point,
                              SSH_FORMAT_UINT32, &mask_len,
                              SSH_FORMAT_DATA,
                                ip->addr_data, (size_t)(SSH_IP_ADDR_LEN(ip)),
                              SSH_FORMAT_END)) != (4 + SSH_IP_ADDR_LEN(ip)))
      return 0;

  ip->mask_len = mask_len;

  point += got;

  /* Sanity check */
  if (!SSH_IP_IS4(ip) && !SSH_IP_IS6(ip))
    return 0;

  return point;
}

#if !(defined(_KERNEL) || defined(KERNEL))
size_t ssh_encode_ipaddr_buffer(SshBuffer buffer, const SshIpAddr ip)
{
  size_t got;
  unsigned char tmpbuf[1 + 4 + 16];

  if ((got = ssh_encode_ipaddr_array(tmpbuf, sizeof(tmpbuf), ip)) == 0)
    return FALSE;

  if (ssh_buffer_append(buffer, tmpbuf, got) == SSH_BUFFER_OK)
    return TRUE;
  else
    return FALSE;

}

size_t ssh_ipaddr_decode_buffer(SshBuffer buffer, SshIpAddr ip)
{
  size_t got;

  if ((got = ssh_decode_ipaddr_array(ssh_buffer_ptr(buffer),
                                     ssh_buffer_len(buffer),
                                     ip)) == 0)
    return 0;

  ssh_buffer_consume(buffer, got);

  return got;
}
#endif
