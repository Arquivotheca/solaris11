/*
 * Authors: Tero Kivinen <kivinen@ssh.fi>
 *          Sami Lehtinen <sjl@ssh.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 *                  All rights reserved
 */
/*
 *        Program: libsocks
 *
 *        Creation          : 18:09 Nov 10 1996 kivinen
 *        Last Modification : 18:05 May 28 2000 kivinen
 *        
 *
 *        Description       : Socks library
 */

#include "sshincludes.h"
#include "sshbuffer.h"
#include "sshmalloc.h"
#include "sshsocks.h"
#include "sshencode.h"
#include "sshinet.h"
#include "sshdsprintf.h"

#define SSH_DEBUG_MODULE "SshSocks"

#define SOCKS4_REPLY_SIZE       8
#define SOCKS4_COMMAND_SIZE     8
#define SOCKS4_MAX_NAME_LEN     128

#define SOCKS_SMALL_BUFFER      64 /* enough to have ip-number as string */

/*
 * Free SocksInfo structure (all fields, and the structure itself).
 * Sets the pointer to socksinfo structure to NULL (NOTE this takes
 * pointer to socksinfo pointer for this purpose).
 */
void ssh_socks_free(SocksInfo *socksinfo)
{
  if (socksinfo == NULL)
    ssh_fatal("ssh_socks_free: socksinfo == NULL");
  if (*socksinfo == NULL)
    ssh_fatal("ssh_socks_free: *socksinfo == NULL");

  ssh_free((*socksinfo)->ip);
  (*socksinfo)->ip = NULL;

  ssh_free((*socksinfo)->port);
  (*socksinfo)->port = NULL;

  ssh_free((*socksinfo)->username);
  (*socksinfo)->username = NULL;

  ssh_free(*socksinfo);
  *socksinfo = NULL;
}

/* Server functions */
/*
 * Parse methods array. This doesn't do anything with SOCKS4.
 */
SocksError ssh_socks_server_parse_methods(SshBuffer buffer,
                                          SocksInfo *socksinfo)
{
  size_t ret = 0L, len;
  unsigned int version, num_methods;
  unsigned char *data;

  data = ssh_buffer_ptr(buffer);
  len = ssh_buffer_len(buffer);

  if (len < 1)
    return SSH_SOCKS_TRY_AGAIN;

  version = *data;

  if (version == 4)
    goto return_success;

  if (len < 2)
    return SSH_SOCKS_TRY_AGAIN;

  ret = ssh_decode_array(data, len,
                         SSH_FORMAT_CHAR, &version,
                         SSH_FORMAT_CHAR, &num_methods,
                         /* XXX We don't do anything with the methods
                            currently.*/
                         SSH_FORMAT_END);
  if (ret == 0)
    {
      SSH_DEBUG(2, ("Decoding methods buffer failed."));
      return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
    }
  if (len < num_methods + 2)
    return SSH_SOCKS_TRY_AGAIN;

  ssh_buffer_consume(buffer, num_methods + 2);

 return_success:
  if (socksinfo)
    {
      *socksinfo = ssh_calloc(1, sizeof(**socksinfo));
      if (*socksinfo == NULL)
        {
          SSH_DEBUG(2, ("Couldn't allocate SshSocksInfo."));
          return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
        }
      (*socksinfo)->socks_version_number = version;
    }
  return SSH_SOCKS_SUCCESS;
}

/*
 * Generate method reply (no authentication required, currently). This
 * doesn't do anything with SOCKS4.
 */
SocksError ssh_socks_server_generate_method(SshBuffer buffer,
                                            SocksInfo socksinfo)
{
  size_t ret = 0L;

  if (socksinfo->socks_version_number == 4)
    return SSH_SOCKS_SUCCESS;

  ret = ssh_encode_buffer(buffer,
                          SSH_FORMAT_CHAR,
                          (unsigned int) socksinfo->socks_version_number,
                          /* XXX We don't currently support any
                             methods. */
                          SSH_FORMAT_CHAR,
                          (unsigned int) SSH_SOCKS5_AUTH_METHOD_NO_AUTH_REQD,
                          SSH_FORMAT_END);
  if (ret == 0)
    {
      SSH_DEBUG(2, ("Encoding return method buffer failed."));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }
  return SSH_SOCKS_SUCCESS;
}

/*
 * Parse incoming socks connection from buffer. Consume the request
 * packet data from buffer. If everything is ok it allocates SocksInfo
 * strcture and store the request fields in it (sets
 * socks_version_number, command_code, ip, port, username). Returns
 * SSH_SOCKS_SUCCESS, SSH_SOCKS_TRY_AGAIN, or SSH_SOCKS_ERROR_*. If
 * anything other than SSH_SOCKS_SUCCESS is returned the socksinfo is
 * set to NULL.  Use ssh_socks_free to free socksinfo data.
 */
SocksError ssh_socks_server_parse_open(SshBuffer buffer, SocksInfo *socksinfo)
{
  unsigned char *data, *ip;
  unsigned long i, port;
  unsigned int version, cmd, ip_addr_len, atyp;
  unsigned char *username = NULL;
  size_t ret, len, bytes = 0;

  *socksinfo = NULL;
  len = ssh_buffer_len(buffer);
  data = ssh_buffer_ptr(buffer);

  if (len < 1)
    return SSH_SOCKS_TRY_AGAIN;

  version = data[0];
  bytes++;

  if (version != 4 && version != 5)
    {
      SSH_DEBUG(2, ("Server gave us version %d.", version));
      return SSH_SOCKS_ERROR_UNSUPPORTED_SOCKS_VERSION;
    }

  if (version == 4)
    {
      /* Check if enough data for header and name */
      if (len < SOCKS4_COMMAND_SIZE + 1)
        {
          return SSH_SOCKS_TRY_AGAIN;
        }


      /* Find the end of username */
      for (i = SOCKS4_COMMAND_SIZE; i < len; i++)
        {
          if (data[i] == '\0')
            break;
        }

      /* End of username not found, return either error or try_again */
      if (i == len || data[i] != '\0')
        {
          if (len > SOCKS4_COMMAND_SIZE + SOCKS4_MAX_NAME_LEN)
            {
              return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
            }
          return SSH_SOCKS_TRY_AGAIN;
        }

      cmd = data[1];

      port = SSH_GET_16BIT(&data[2]);

      ip_addr_len = 4;
      ip = ssh_memdup(&data[4], ip_addr_len);
      atyp = SSH_SOCKS5_ATYP_IPV4;

      if (ip == NULL)
        {
          SSH_DEBUG(2, ("Failed to allocate IP-address buffer."));
          return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
        }

      username = ssh_strdup((char *)(data + SOCKS4_COMMAND_SIZE));
      bytes = SOCKS4_COMMAND_SIZE +
        strlen((char *) data + SOCKS4_COMMAND_SIZE) + 1;
    }
  else
    {
      unsigned char port_buf[2];

      if (len - bytes < 3)
        return SSH_SOCKS_TRY_AGAIN;

      ret = ssh_decode_array(data + bytes, len - bytes,
                             SSH_FORMAT_CHAR, &cmd,
                             SSH_FORMAT_CHAR, NULL, /* RSV */
                             SSH_FORMAT_CHAR, &atyp,
                             SSH_FORMAT_END);
      if (ret <= 0)
        {
          SSH_DEBUG(2, ("Failed to decode command packet."));
          return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
        }
      bytes += ret;
      if (atyp == SSH_SOCKS5_ATYP_IPV4)
        {
          SSH_DEBUG(4, ("SOCKS5 received address type IPV4."));
          ip_addr_len = 4;
        }
      else if (atyp == SSH_SOCKS5_ATYP_IPV6)
        {
          SSH_DEBUG(4, ("SOCKS5 received address type IPV6."));
          ip_addr_len = 16;
        }
      else if (atyp == SSH_SOCKS5_ATYP_FQDN)
        {
          if (len - bytes < 1)
            return SSH_SOCKS_TRY_AGAIN;

          ip_addr_len = *(data + bytes);
          if (ip_addr_len <= 0 || ip_addr_len >= 255)
            {
              SSH_DEBUG(2, ("Invalid FQDN address len %ld.", ip_addr_len));
              return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
            }
          SSH_DEBUG(4, ("SOCKS5 received address type FQDN, len %d.",
                        ip_addr_len));
          bytes++;
        }
      else
        {
          SSH_DEBUG(2, ("Invalid address type %d.", atyp));
          return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
        }
      /* ip addr len + port */
      if (len - bytes < ip_addr_len + 2)
        return SSH_SOCKS_TRY_AGAIN;

      ip = ssh_calloc(ip_addr_len + 1, sizeof(unsigned char));
      if (ip == NULL)
        {
          SSH_DEBUG(2, ("Failed to allocate IP-address buffer."));
          return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
        }
      ret = ssh_decode_array(data + bytes, len - bytes,
                             SSH_FORMAT_DATA, ip, ip_addr_len,
                             SSH_FORMAT_DATA, port_buf, 2,
                             SSH_FORMAT_END);
      if (ret <= 0)
        {
          SSH_DEBUG(2, ("Failed to decode command packet."));
          ssh_free(ip);
          return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
        }
      port = SSH_GET_16BIT(port_buf);
      bytes += ret;
    }

  if ((*socksinfo = ssh_calloc(1, sizeof(struct SocksInfoRec))) == NULL)
    {
      SSH_DEBUG(2, ("Failed to allocate SocksInfo."));
      ssh_free(ip);
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  if (atyp == SSH_SOCKS5_ATYP_FQDN)
    {
      (*socksinfo)->ip = ip;
    }
  else
    {
      SshIpAddrStruct ip_addr;
      unsigned char buf[SOCKS_SMALL_BUFFER];

      SSH_IP_DECODE(&ip_addr, ip, ip_addr_len);

      ssh_ipaddr_print(&ip_addr, buf, sizeof(buf));
      (*socksinfo)->ip = ssh_memdup(buf, strlen(ssh_csstr(buf)));
      ssh_free(ip);
      if ((*socksinfo)->ip == NULL)
        {
          SSH_DEBUG(2, ("Failed to allocate final IP-addr buf."));
          return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
        }
    }

  (*socksinfo)->socks_version_number = version;
  (*socksinfo)->command_code = cmd;
  /* XXX temporary casts until library API is changed XXX */
  ssh_dsprintf((char **) &(*socksinfo)->port, "%lu", port);
  (*socksinfo)->username = username;
  ssh_buffer_consume(buffer, bytes);
  SSH_DEBUG(5, ("Decoded %ld bytes.", bytes));
  return SSH_SOCKS_SUCCESS;
}

/*
 * Make socks reply packet that can be sent to client and store it to buffer.
 * If connection is granted set command_code to SSH_SOCKS_REPLY_GRANTED,
 * otherwise set it to some error code (SSH_SOCKS_REPLY_FAILED_*).
 * The port and ip from the socksinfo are sent along with reply and if
 * the request that was granted was bind they should indicate the port and ip
 * address of the other end of the socket.
 * Does NOT free the SocksInfo structure.
 */
SocksError ssh_socks4_server_generate_reply(SshBuffer buffer,
                                            SocksInfo socksinfo)
{
  unsigned char *data;
  int port;
  SshIpAddrStruct ip_addr;

  port = ssh_inet_get_port_by_service(socksinfo->port, ssh_custr("tcp"));
  if (port >= 65536 || port <= 0)
    return SSH_SOCKS_ERROR_INVALID_ARGUMENT;

  if (!ssh_ipaddr_parse(&ip_addr, socksinfo->ip))
    {
      SSH_DEBUG(2, ("Couldn't parse IP-address `%s'.", socksinfo->ip));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  /* nowadays the ip-addresses returned by ssh functions is more and more
     often in ipv6 format (ipv4 addresses are in ipv6 mapped ipv4 format). */
  ssh_inet_convert_ip6_mapped_ip4_to_ip4(&ip_addr);

  if (!SSH_IP_IS4(&ip_addr))
    {
      SSH_DEBUG(2, ("IP-address `%s' isn't an IPv4 numerical address.",
                    socksinfo->ip));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  if (ssh_buffer_append_space(buffer, &data, SOCKS4_REPLY_SIZE)
      != SSH_BUFFER_OK)
    {
      SSH_DEBUG(2, ("Failed to allocate reply buffer."));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  *data++ = 0; /* SOCKS4 replys must have version number '0'. */
  *data++ = socksinfo->command_code;
  SSH_PUT_16BIT(data, port);
  data += 2;
  SSH_IP4_ENCODE(&ip_addr, data);
  return SSH_SOCKS_SUCCESS;
}

SocksError ssh_socks5_server_generate_reply(SshBuffer buffer,
                                            SocksInfo socksinfo)
{
  unsigned char *data;
  int port, ip_addr_len;
  unsigned int atyp;
  size_t len;
  SshIpAddrStruct ip_addr;

  port = ssh_inet_get_port_by_service(socksinfo->port, ssh_custr("tcp"));
  if (port >= 65536 || port <= 0)
    return SSH_SOCKS_ERROR_INVALID_ARGUMENT;

  if (!ssh_ipaddr_parse(&ip_addr, socksinfo->ip))
    {
      atyp = SSH_SOCKS5_ATYP_FQDN;
      ip_addr_len = strlen(ssh_csstr(socksinfo->ip));
    }
  else if (SSH_IP_IS4(&ip_addr))
    {
      atyp = SSH_SOCKS5_ATYP_IPV4;
      ip_addr_len = 4;
    }
  else if (SSH_IP_IS6(&ip_addr))
    {
      atyp = SSH_SOCKS5_ATYP_IPV6;
      ip_addr_len = 16;
    }
  else
    {
      SSH_DEBUG(2, ("IP-address is of unknown type."));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  len = 6 + ip_addr_len;
  if (atyp == SSH_SOCKS5_ATYP_FQDN)
    /* Length field. */
    len += 1;

  if (ssh_buffer_append_space(buffer, &data, len) != SSH_BUFFER_OK)
    {
      SSH_DEBUG(2, ("Failed to allocate reply buffer."));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  *data++ = socksinfo->socks_version_number;
  *data++ = socksinfo->command_code;
  *data++ = 0; /* RSV. */
  *data++ = atyp;
  if (atyp == SSH_SOCKS5_ATYP_FQDN)
    {
      *data++ = ip_addr_len;
      memmove(data, socksinfo->ip, ip_addr_len);
      data += ip_addr_len;
    }
  else
    {
      int len;
      SSH_IP_ENCODE(&ip_addr, data, len);
      SSH_ASSERT(ip_addr_len == len);
      data += len;
    }
  SSH_PUT_16BIT(data, port);
  return SSH_SOCKS_SUCCESS;
}

SocksError ssh_socks_server_generate_reply(SshBuffer buffer,
                                           SocksInfo socksinfo)
{
  if (socksinfo == NULL)
    ssh_fatal("ssh_socks_server_generate_reply: socksinfo == NULL");

  if (!(socksinfo->socks_version_number == 4 ||
        socksinfo->socks_version_number == 5))
    {
      SSH_DEBUG(1, ("SOCKS version %d not supported.",
                    socksinfo->socks_version_number));
      return SSH_SOCKS_ERROR_UNSUPPORTED_SOCKS_VERSION;
    }
  if ((socksinfo->socks_version_number == 4 &&
       socksinfo->command_code < SSH_SOCKS4_REPLY_GRANTED) ||
      (socksinfo->socks_version_number == 5 &&
       (socksinfo->command_code < SSH_SOCKS5_REPLY_SUCCESS ||
        socksinfo->command_code > SSH_SOCKS5_REPLY_ATYP_NOT_SUPPORTED)))
    {
      SSH_DEBUG(2, ("Invalid command argument %d for version %d.",
                    socksinfo->command_code, socksinfo->socks_version_number));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  if (socksinfo->socks_version_number == 4)
    return ssh_socks4_server_generate_reply(buffer, socksinfo);
  else
    return ssh_socks5_server_generate_reply(buffer, socksinfo);
}

/* Client functions */
/*
 * Send acceptable methods. This doesn't do anything with SOCKS4.
 */
SocksError ssh_socks_client_generate_methods(SshBuffer buffer,
                                              SocksInfo socksinfo)
{
  size_t ret = 0L;

  if (socksinfo->socks_version_number != 5)
    return SSH_SOCKS_SUCCESS;

  ret = ssh_encode_buffer(buffer,
                          SSH_FORMAT_CHAR,
                          (unsigned int) socksinfo->socks_version_number,
                          SSH_FORMAT_CHAR, (unsigned int) 1,
                          SSH_FORMAT_CHAR,
                          (unsigned int) SSH_SOCKS5_AUTH_METHOD_NO_AUTH_REQD,
                          SSH_FORMAT_END);
  if (ret == 0)
    {
      SSH_DEBUG(2, ("Encoding command buffer failed."));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }
  return SSH_SOCKS_SUCCESS;
}

/*
 * Parse reply method. This doesn't do anything with SOCKS4.
 */
SocksError ssh_socks_client_parse_method(SshBuffer buffer,
                                         SocksInfo *socksinfo)
{
  size_t ret = 0L, len;
  unsigned int version, method;
  unsigned char *data;

  data = ssh_buffer_ptr(buffer);
  len = ssh_buffer_len(buffer);

  if (len < 1)
    return SSH_SOCKS_TRY_AGAIN;

  version = *data;
  if (version == 0)
    version = 4;

  if (version == 4)
    return SSH_SOCKS_SUCCESS;

  if (len < 2)
    return SSH_SOCKS_TRY_AGAIN;

  ret = ssh_decode_buffer(buffer,
                          SSH_FORMAT_CHAR, &version,
                          SSH_FORMAT_CHAR, &method,
                          SSH_FORMAT_END);
  if (ret == 0)
    {
      SSH_DEBUG(2, ("Decoding method buffer failed."));
      return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
    }
  if (method != SSH_SOCKS5_AUTH_METHOD_NO_AUTH_REQD)
    {
      SSH_DEBUG(2, ("Server sent method 0x%x."));
      if (method == SSH_SOCKS5_AUTH_METHOD_NO_ACCEPTABLE)
        {
          SSH_DEBUG(2, ("Server doesn't allow use without some authentication "
                        "(we don't implement any methods)."));
        }
      else
        {
          SSH_DEBUG(2, ("Server sent method that we don't support."));
          return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
        }
      return SSH_SOCKS_FAILED_AUTH;
    }
  if (socksinfo)
    {
      *socksinfo = ssh_calloc(1, sizeof(**socksinfo));
      if (*socksinfo == NULL)
        {
          SSH_DEBUG(2, ("Couldn't allocate SshSocksInfo."));
          return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
        }
      (*socksinfo)->socks_version_number = version;
    }
  return SSH_SOCKS_SUCCESS;
}

/*
 * Make socks connect or bind request and store it to buffer.
 * Uses all fields in socksinfo structure. Returns SSH_SOCKS_SUCCESS, or
 * SSH_SOCKS_ERROR. Command_code must be either SSH_SOCKS_COMMAND_CODE_BIND,
 * or SSH_SOCKS_COMMAND_CODE_CONNECT.
 * Does NOT free the SocksInfo structure.
 */
/* For SOCKS4. */
SocksError ssh_socks4_client_generate_open(SshBuffer buffer,
                                           SocksInfo socksinfo)
{
  unsigned char *data;
  const unsigned char *username;
  unsigned long port;
  size_t bytes = 0L, ret = 0;
  SshIpAddrStruct ip_addr;

  port = ssh_inet_get_port_by_service(socksinfo->port, ssh_custr("tcp"));
  if (port >= 65536 || port <= 0)
    return SSH_SOCKS_ERROR_INVALID_ARGUMENT;

  if (socksinfo->username == NULL)
    username = ssh_custr("");
  else
    username = socksinfo->username;

  if (strlen(ssh_csstr(username)) > SOCKS4_MAX_NAME_LEN)
    return SSH_SOCKS_ERROR_INVALID_ARGUMENT;

  if (!ssh_ipaddr_parse(&ip_addr, socksinfo->ip))
    {
      SSH_DEBUG(2, ("IP `%s' could not be parsed.", socksinfo->ip));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  /* nowadays the ip-addresses returned by ssh functions is more and more
     often in ipv6 format (ipv4 addresses are in ipv6 mapped ipv4 format). */
  ssh_inet_convert_ip6_mapped_ip4_to_ip4(&ip_addr);

  if (!SSH_IP_IS4(&ip_addr))
    {
      SSH_DEBUG(2, ("IP `%s' is not a valid IPv4 address.", socksinfo->ip));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  bytes = ssh_encode_buffer(buffer,
                            SSH_FORMAT_CHAR, socksinfo->socks_version_number,
                            SSH_FORMAT_CHAR,
                            (unsigned int) socksinfo->command_code,
                            SSH_FORMAT_CHAR,
                            (unsigned int) ((port & 0xff00U) >> 8),
                            SSH_FORMAT_CHAR, (unsigned int) (port & 0xffU),
                            SSH_FORMAT_END);
  if (bytes == 0)
    {
      SSH_DEBUG(2, ("Encoding command buffer failed."));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  /* Allocate space for the IP-address*/
  if (ssh_buffer_append_space(buffer, &data, 4) != SSH_BUFFER_OK)
    {
      SSH_DEBUG(2, ("Allocating space for the IP-address failed."));
      ssh_buffer_consume_end(buffer, bytes);
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  SSH_IP4_ENCODE(&ip_addr, data);
  data += 4;
  bytes += 4;

  ret = ssh_encode_buffer(buffer,
                          SSH_FORMAT_DATA,
                          username, strlen(ssh_csstr(username)),
                          SSH_FORMAT_DATA, "\0", 1,
                          SSH_FORMAT_END);
  if (ret == 0)
    {
      SSH_DEBUG(2, ("Encoding username to the command buffer failed."));
      ssh_buffer_consume_end(buffer, bytes);
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  SSH_DEBUG(4, ("Command buffer size %ld.", bytes + ret));
  return SSH_SOCKS_SUCCESS;
}

/* For SOCKS5. */
SocksError ssh_socks5_client_generate_open(SshBuffer buffer,
                                           SocksInfo socksinfo)
{
  unsigned char *data;
  unsigned long port;
  size_t bytes = 0L, bytes_needed = 0;
  SshIpAddrStruct ip_addr;
  unsigned int address_type;

  port = ssh_inet_get_port_by_service(socksinfo->port, ssh_custr("tcp"));
  if (port >= 65536 || port <= 0)
    return SSH_SOCKS_ERROR_INVALID_ARGUMENT;

  if (ssh_ipaddr_parse(&ip_addr, socksinfo->ip))
    {
      if (SSH_IP_IS4(&ip_addr))
        address_type = SSH_SOCKS5_ATYP_IPV4;
      else
        address_type = SSH_SOCKS5_ATYP_IPV6;
    }
  else
    {
      SSH_DEBUG(2, ("IP `%s' could not be parsed, assuming it is a hostname.",
                    socksinfo->ip));
      address_type = SSH_SOCKS5_ATYP_FQDN;
    }

  bytes = ssh_encode_buffer(buffer,
                            SSH_FORMAT_CHAR, socksinfo->socks_version_number,
                            SSH_FORMAT_CHAR,
                            (unsigned int) socksinfo->command_code,
                            /* RSV. */
                            SSH_FORMAT_CHAR, (unsigned int) 0,
                            SSH_FORMAT_CHAR, (unsigned int) address_type,
                            SSH_FORMAT_END);
  if (bytes == 0)
    {
      SSH_DEBUG(2, ("Encoding command buffer failed."));
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  if (address_type == SSH_SOCKS5_ATYP_IPV4)
    bytes_needed = 4;
  else if (address_type == SSH_SOCKS5_ATYP_IPV6)
    bytes_needed = 16;
  else if (address_type == SSH_SOCKS5_ATYP_FQDN)
    /* length field + address length */
    bytes_needed = 1 + strlen(ssh_csstr(socksinfo->ip));

  /* port */
  bytes_needed += 2;

  /* Allocate space for the IP-address*/
  if (ssh_buffer_append_space(buffer, &data, bytes_needed) != SSH_BUFFER_OK)
    {
      SSH_DEBUG(2, ("Allocating space for the IP-address failed."));
      ssh_buffer_consume_end(buffer, bytes);
      return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
    }

  if (address_type == SSH_SOCKS5_ATYP_IPV4)
    {
      SSH_IP4_ENCODE(&ip_addr, data);
    }
  else if (address_type == SSH_SOCKS5_ATYP_IPV6)
    {
      SSH_IP6_ENCODE(&ip_addr, data);
    }
  else if (address_type == SSH_SOCKS5_ATYP_FQDN)
    {
      *data = strlen(ssh_csstr(socksinfo->ip));
      strcpy(ssh_sstr(data) + 1, ssh_csstr(socksinfo->ip));
    }
  bytes += bytes_needed - 2;
  data += bytes_needed - 2;
  SSH_PUT_16BIT(data, port);
  SSH_DEBUG(4, ("Command buffer size %ld.", bytes + bytes_needed));
  return SSH_SOCKS_SUCCESS;
}

SocksError ssh_socks_client_generate_open(SshBuffer buffer,
                                          SocksInfo socksinfo)
{
  if (socksinfo == NULL)
    ssh_fatal("ssh_socks_server_generate_reply: socksinfo == NULL");
  if (!(socksinfo->socks_version_number == 4 ||
        socksinfo->socks_version_number == 5))
    {
      SSH_DEBUG(1, ("SOCKS version %d not supported.",
                    socksinfo->socks_version_number));
      return SSH_SOCKS_ERROR_UNSUPPORTED_SOCKS_VERSION;
    }
  if ((socksinfo->socks_version_number == 4 &&
       socksinfo->command_code >= SSH_SOCKS4_REPLY_GRANTED) ||
      (socksinfo->socks_version_number == 5 &&
       (socksinfo->command_code > SSH_SOCKS5_COMMAND_CODE_UDP_ASSOCIATE ||
        socksinfo->command_code < SSH_SOCKS5_COMMAND_CODE_CONNECT)))
    return SSH_SOCKS_ERROR_INVALID_ARGUMENT;

  if (socksinfo->socks_version_number == 4)
    return ssh_socks4_client_generate_open(buffer, socksinfo);
  else
    return ssh_socks5_client_generate_open(buffer, socksinfo);
}

/*
 * Parse socks reply packet. Consume the reply packet data from buffer.
 * If the request was not granted (returns SSH_SOCKS_FAILED_*) the socket can
 * be immediately closed down (there will not be any additional data from the
 * socks server.
 * If the request is granted allocate socksinfo structure and store information
 * from request packet to there (sets socks_version_number, command_code, ip,
 * and port fields).
 * Use ssh_socks_free to free socksinfo data.
 */
SocksError ssh_socks_client_parse_reply(SshBuffer buffer,
                                        SocksInfo *socksinfo)
{
  unsigned char *data, *ip_ptr, *port_ptr;
  unsigned char *username =NULL;
  unsigned long len, port, version;
  size_t bytes = 0;
  unsigned int cmd, atyp, ip_addr_len;

  if (socksinfo)
    *socksinfo = NULL;
  len = ssh_buffer_len(buffer);
  data = ssh_buffer_ptr(buffer);

  /* Check if enough data for version. */
  if (len < 1)
    return SSH_SOCKS_TRY_AGAIN;

  version = data[0];
  /* SOCKS4 replys have version number '0'. Go figure. */
  if (version == 0)
    version = 4;
  bytes++;

  if (version != 4 && version != 5)
    {
      SSH_DEBUG(2, ("Server gave us version %d.", version));
      return SSH_SOCKS_ERROR_UNSUPPORTED_SOCKS_VERSION;
    }

  if (version == 4)
    {
      /* A SOCKS4 command reply is exactly 8 bytes long. */
      if (len < 8)
        return SSH_SOCKS_TRY_AGAIN;
      SSH_DEBUG(2, ("Doing SOCKS4."));
      bytes = 8;

      if (data[1] != SSH_SOCKS4_REPLY_GRANTED)
        {
          SocksError error = SSH_SOCKS_ERROR_PROTOCOL_ERROR;
          switch (data[1])
            {
            case SSH_SOCKS4_REPLY_FAILED_REQUEST:
              error = SSH_SOCKS_FAILED_REQUEST;
              break;
            case SSH_SOCKS4_REPLY_FAILED_IDENTD:
              error = SSH_SOCKS_FAILED_IDENTD;
              break;
            case SSH_SOCKS4_REPLY_FAILED_USERNAME:
              error = SSH_SOCKS_FAILED_USERNAME;
              break;
            default:
              error = SSH_SOCKS_ERROR_PROTOCOL_ERROR;
              break;
            }
          ssh_buffer_consume(buffer, bytes);
          return error;
        }

      cmd = data[1];
      port_ptr = &data[2];
      ip_ptr = &data[4];
      ip_addr_len = 4;
      atyp = SSH_SOCKS5_ATYP_IPV4;
      username = (unsigned char *)&data[8];
    }
  else
    {
      size_t ret = 0L;

      /* SOCKS5. */
      if (len - bytes < 3)
        return SSH_SOCKS_TRY_AGAIN;

      ret = ssh_decode_array(data + bytes, len - bytes,
                             SSH_FORMAT_CHAR, &cmd,
                             SSH_FORMAT_CHAR, NULL, /* RSV */
                             SSH_FORMAT_CHAR, &atyp,
                             SSH_FORMAT_END);
      if (ret == 0)
        {
          SSH_DEBUG(2, ("Decoding reply packet failed."));
          return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
        }
      bytes += ret;
      if (atyp == SSH_SOCKS5_ATYP_IPV4)
        {
          ip_addr_len = 4;
        }
      else if (atyp == SSH_SOCKS5_ATYP_IPV6)
        {
          ip_addr_len = 16;
        }
      else if (atyp == SSH_SOCKS5_ATYP_FQDN)
        {
          if (len - bytes < 1)
            return SSH_SOCKS_TRY_AGAIN;
          ret = ssh_decode_array(data + bytes, len - bytes,
                                 SSH_FORMAT_CHAR, &ip_addr_len,
                                 SSH_FORMAT_END);
          if (ret == 0)
            {
              SSH_DEBUG(2, ("Decoding FQDN hostname len failed."));
              return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
            }
          bytes += ret;
        }
      else
        {
          SSH_DEBUG(2, ("Invalid address type %d.", atyp));
          return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
        }
      /* ip addr len + port (2 bytes) */
      if (len - bytes < ip_addr_len + 2)
        return SSH_SOCKS_TRY_AGAIN;

      ip_ptr = data + bytes;
      if (ret == 0)
        {
          SSH_DEBUG(2, ("Decoding reply packet failed."));
          return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
        }
      bytes += ip_addr_len;

      port_ptr = data + bytes;
      bytes += 2;
      SSH_DEBUG(2, ("Doing SOCKS5."));

      if (cmd != SSH_SOCKS5_REPLY_SUCCESS)
        {
          SSH_DEBUG(2, ("Got reply %d from server.", cmd));
          if (cmd < SSH_SOCKS5_REPLY_SUCCESS ||
              cmd > SSH_SOCKS5_REPLY_ATYP_NOT_SUPPORTED)
            return SSH_SOCKS_ERROR_PROTOCOL_ERROR;
          return SSH_SOCKS_FAILED_REQUEST;
        }
    }

  if (socksinfo)
    {
      SshIpAddrStruct ip_addr;

      if ((*socksinfo = ssh_calloc(1, sizeof(SocksInfoStruct))) == NULL)
        {
          SSH_DEBUG(2, ("Couldn't allocate SocksInfo."));
          return SSH_SOCKS_ERROR_INVALID_ARGUMENT;
        }

      memset(&ip_addr, 0, sizeof(ip_addr));
      (*socksinfo)->socks_version_number = version;
      (*socksinfo)->command_code = cmd;

      port = SSH_GET_16BIT(port_ptr);
      /* XXX temporary casts until library API is changed XXX */
      ssh_dsprintf((char **) &(*socksinfo)->port, "%lu", port);
      if (username)
        (*socksinfo)->username = ssh_strdup(username);
      else
        (*socksinfo)->username = NULL;

      if (atyp == SSH_SOCKS5_ATYP_FQDN)
        {
          (*socksinfo)->ip = ssh_calloc(ip_addr_len + 1, sizeof(char));
          if ((*socksinfo)->ip)
            memmove((*socksinfo)->ip, ip_ptr, ip_addr_len);
        }
      else
        {
          unsigned char buf[SOCKS_SMALL_BUFFER];

          SSH_IP_DECODE(&ip_addr, ip_ptr, ip_addr_len);
          ssh_ipaddr_print(&ip_addr, buf, sizeof(buf) - 1);
          (*socksinfo)->ip = ssh_strdup(buf);
        }
    }

  SSH_DEBUG(2, ("Decoded %ld bytes.", bytes));
  ssh_buffer_consume(buffer, bytes);
  return SSH_SOCKS_SUCCESS;
}
