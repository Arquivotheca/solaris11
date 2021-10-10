/*
 *
 * sshaudit_util.c
 *
 * Author: Markku Rossi <mtr@ssh.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 *               All rights reserved.
 *
 * Help functions for formatting audit events.
 *
 */

#include "sshincludes.h"
#include "sshinet.h"
#include "sshaudit.h"

#define SSH_DEBUG_MODULE "SshAuditUtil"

/* Format the numeric argument `data' with the length `data_len' into
   the buffer `buffer'.  The argument `label' specifies the label of
   the argument. */
static Boolean
ssh_audit_format_number(SshBuffer buffer, const char *label,
                        unsigned char *data, size_t data_len)
{
  char buf[64];
  size_t i;

  if (ssh_buffer_append_cstrs(buffer, label, " ", NULL) != SSH_BUFFER_OK)
    return FALSE;

  switch (data_len)
    {
    case 0:
      SSH_NOTREACHED;
      break;

    case 2:
      ssh_snprintf(buf, sizeof(buf), "%02x",
                   (unsigned int) SSH_GET_16BIT(data));
      break;

    case 4:
      ssh_snprintf(buf, sizeof(buf), "%04x",
                   (unsigned int) SSH_GET_32BIT(data));
      break;

    case 8:
      ssh_snprintf(buf, sizeof(buf), "%04x%04x",
                   (unsigned int) SSH_GET_32BIT(data),
                   (unsigned int) SSH_GET_32BIT(data + 4));
      break;

    default:
      for (i = 0; i < data_len; i++)
        {
          ssh_snprintf(buf, sizeof(buf), "%02x", data[i]);
          if (ssh_buffer_append(buffer, (unsigned char *) buf, 2)
              != SSH_BUFFER_OK)
            return FALSE;
        }

      return TRUE;
      break;
    }

  if (ssh_buffer_append_cstrs(buffer, buf, NULL) != SSH_BUFFER_OK)
    return FALSE;

  return TRUE;
}

/* Get an encoded number from the buffer `data', `data_len'. */
static SshUInt64
ssh_audit_get_number(unsigned char *data, size_t data_len)
{
  if (data_len == 1)
    return data[0];
  else if (data_len == 2)
    return SSH_GET_16BIT(data);
  else if (data_len == 4)
    return SSH_GET_32BIT(data);
  else if (data_len == 8)
    return SSH_GET_64BIT(data);
  else
    return 0;
}

static SshKeywordStruct
ssh_audit_ip4_options[] =
{
  {"nop", 1 },
  {"security", 2},
  {"loose source routing", 3},
  {"record route", 7},
  {"stream-id", 8},
  {"strict source routing", 9},
  {"router alert", 20},
  {"time-stamp", 68},
  {NULL, 0}
};

static SshKeywordStruct
ssh_audit_icmp_typecodes[] =
{
  /* ICMP destination unreachable */
  {"net unreachable", 0x0300},
  {"host unreachable", 0x0301},
  {"protocol unreachable", 0x0302},
  {"port unreachable", 0x0303},
  {"fragmentation needed", 0x0304},
  {"source route failed", 0x0305},

  /* ICMP Time Exceeded */
  {"time exceeded in transit", 0x0b00},
  {"fragment reassembly time exceeded", 0x0b01},

  /* ICMP Parameter Problem */
  {"parameter problem", 0x0c00},

  /* ICMP Source Quench */
  {"source quench", 0x0400},

  /* ICMP Redirect */
  {"redirect datagrams for network", 0x0500},
  {"redirect datagrams for host", 0x0501},
  {"redirect datagrams for network/type-of-service", 0x0502},
  {"redirect datagrams for host/type-of-service", 0x0503},

  /* ICMP Echo */
  {"echo request", 0x0800},
  {"echo response", 0x0000},

  /* ICMP Timestamp */
  {"timestamp", 0x0d00},
  {"timestamp response", 0x0e00},

  /* ICMP Information Request */
  {"information request",   0x0f00},
  {"information resposne",  0x1000},

  {NULL, 0},
};


static SshKeywordStruct
ssh_audit_ipv6icmp_typecodes[] =
{
  { "destination unreachable, no route to destination", 0x0100 },
  { "destination unreachable, communication administratively prohibited",
    0x0101 },
  { "destination unreachable, address unreachable", 0x0103 },
  { "destination unreachable, port unreachable", 0x0104 },

  { "packet too big", 0x0200 },

  { "time exceeded, hop limit exceeded", 0x0300 },
  { "time exceeded, reassembly time exceeded", 0x0301 },

  { "parameter problem, erroneous header field", 0x0400 },
  { "parameter problem, unrecognized NextHeader-type", 0x0401 },
  { "parameter problem, unrecognized IPv6 option", 0x0402 },

  { "echo request", 0x8000 },
  { "echo reply", 0x8100 },

  { "router solicitation", 0x8500 },
  { "router advertisement", 0x8600 },
  { "neighborhood solicitation", 0x8700 },
  { "neighborhood advertisement", 0x8800 },

  { "redirect", 0x8900 },

  { NULL, 0 },
};


Boolean
ssh_audit_format(SshBuffer buffer, SshAuditEvent event, SshUInt32 argc,
                 SshAuditArgument argv)
{
  char *cp, *tmp_ptr;
  const char *name;
  SshBufferStatus status;
  SshUInt32 i;
  unsigned char buf[128];
  SshIpAddrStruct ip;
  SshUInt64 val;

  cp = ssh_time_string(ssh_time());
  if (cp == NULL)
    return FALSE;

  status = ssh_buffer_append_cstrs(buffer, cp, ": ",
                                   ssh_find_keyword_name(ssh_audit_event_names,
                                                         (int) event),
                                   NULL);
  ssh_free(cp);
  if (status != SSH_BUFFER_OK)
    return FALSE;

  /* Add arguments. */
  for (i = 0; i < argc; i++)
    {
      if (argv[i].data_len == 0)
        continue;

      if (ssh_buffer_append_cstrs(buffer, ": ", NULL) != SSH_BUFFER_OK)
        return FALSE;

      switch (argv[i].type)
        {
        case SSH_AUDIT_SPI:
          if (!ssh_audit_format_number(buffer, "SPI", argv[i].data,
                                       argv[i].data_len))
            return FALSE;
          break;

        case SSH_AUDIT_DESTINATION_ADDRESS:
        case SSH_AUDIT_SOURCE_ADDRESS:
        case SSH_AUDIT_SOCKS_SERVER_IP:
        case SSH_AUDIT_TARGET_IP:
          switch (argv[i].data_len)
            {
            case 4:
              SSH_IP4_DECODE(&ip, argv[i].data);
              break;

            case 16:
              SSH_IP6_DECODE(&ip, argv[i].data);
              break;

            default:
              SSH_IP_UNDEFINE(&ip);
              break;
            }

          ssh_ipaddr_print(&ip, buf, sizeof(buf));

          tmp_ptr = "";
          if (argv[i].type == SSH_AUDIT_DESTINATION_ADDRESS)
            tmp_ptr = "Dst IP ";
          else if (argv[i].type == SSH_AUDIT_SOURCE_ADDRESS)
            tmp_ptr = "Src IP ";
          else if (argv[i].type == SSH_AUDIT_SOCKS_SERVER_IP)
            tmp_ptr = "Socks server ";
          else if (argv[i].type == SSH_AUDIT_TARGET_IP)
            tmp_ptr = "Target IP ";
          else
            SSH_NOTREACHED;

          if (ssh_buffer_append_cstrs(buffer, tmp_ptr, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_SOURCE_ADDRESS_STR:
          if (ssh_buffer_append_cstrs(buffer, "Src IP ", NULL)
              != SSH_BUFFER_OK)
            return FALSE;

          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_DESTINATION_ADDRESS_STR:
          if (ssh_buffer_append_cstrs(buffer, "Dst IP ", NULL)
              != SSH_BUFFER_OK)
            return FALSE;

          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_IPV6_FLOW_ID:
          if (!ssh_audit_format_number(buffer, "IPv6 Flow ID", argv[i].data,
                                       argv[i].data_len))
            return FALSE;
          break;

        case SSH_AUDIT_SEQUENCE_NUMBER:
          if (!ssh_audit_format_number(buffer, "SeqNum", argv[i].data,
                                       argv[i].data_len))
            return FALSE;
          break;

        case SSH_AUDIT_PACKET_CORRUPTION:
        case SSH_AUDIT_PACKET_ATTACK:
        case SSH_AUDIT_TXT:
        case SSH_AUDIT_HTTP_METHOD:
        case SSH_AUDIT_REQUEST_URI:
        case SSH_AUDIT_HTTP_VERSION:
        case SSH_AUDIT_RULE_ACTION:
        case SSH_AUDIT_EVENT_SOURCE:
        case SSH_AUDIT_SOURCE_HOST:
        case SSH_AUDIT_DESTINATION_HOST:
        case SSH_AUDIT_CIFS_DOMAIN:
        case SSH_AUDIT_CIFS_ACCOUNT:
        case SSH_AUDIT_CIFS_COMMAND:
        case SSH_AUDIT_CIFS_SUBCOMMAND:
        case SSH_AUDIT_CIFS_DIALECT:
        case SSH_AUDIT_FTP_COMMAND:
          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_USERNAME:
          if (ssh_buffer_append_cstrs(buffer, "User-name ", NULL)
              != SSH_BUFFER_OK)
            return FALSE;

          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;

          /* Append an empty whitespace, so terminals which highlight
             URL's don't include the ending ":" into the URL. */

          if (argv[i].type == SSH_AUDIT_REQUEST_URI)
            {
              if (ssh_buffer_append_cstrs(buffer, " ", NULL) 
                  != SSH_BUFFER_OK)
                return FALSE;
            }

          break;

        case SSH_AUDIT_TOTUNNEL_ID:
        case SSH_AUDIT_FROMTUNNEL_ID:
          if (ssh_buffer_append_cstrs(buffer,
                                      (argv[i].type == SSH_AUDIT_TOTUNNEL_ID
                                       ? "to tunnel " : "from tunnel "),
                                NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          
          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_RULE_NAME:
          if (ssh_buffer_append_cstrs(buffer, "rule ", NULL) != SSH_BUFFER_OK)
            return FALSE;

          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;

          break;

        case SSH_AUDIT_SOURCE_INTERFACE:
        case SSH_AUDIT_DESTINATION_INTERFACE:
          if (ssh_buffer_append_cstrs(buffer,
                                (argv[i].type == SSH_AUDIT_SOURCE_INTERFACE
                                 ? "source " : "destination "),
                                NULL)
              != SSH_BUFFER_OK)
            return FALSE;

          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_IPPROTO:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          cp = (char *) ssh_find_keyword_name(ssh_ip_protocol_id_keywords,
                                              (long) val);
          if (cp)
            if (ssh_buffer_append(buffer, (unsigned char *) cp, strlen(cp))
                != SSH_BUFFER_OK)
              return FALSE;
          break;

        case SSH_AUDIT_SOCKS_VERSION:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          ssh_snprintf(ssh_sstr(buf), sizeof(buf), "SOCKSv%d",(int) val);
          if (ssh_buffer_append_cstrs(buffer, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_DESTINATION_PORT:
        case SSH_AUDIT_SOURCE_PORT:
        case SSH_AUDIT_SOCKS_SERVER_PORT:
        case SSH_AUDIT_TARGET_PORT:
          tmp_ptr = "";
          if (argv[i].type == SSH_AUDIT_DESTINATION_PORT)
            tmp_ptr = "Dst Port ";
          else if (argv[i].type == SSH_AUDIT_SOURCE_PORT)
            tmp_ptr = "Src Port ";
          else if (argv[i].type == SSH_AUDIT_SOCKS_SERVER_PORT)
            tmp_ptr = "Socks Port ";
          else if (argv[i].type == SSH_AUDIT_TARGET_PORT)
            tmp_ptr = "Target Port ";
          else
            SSH_NOTREACHED;

          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          ssh_snprintf(ssh_sstr(buf), sizeof(buf), "%u", (unsigned int) val);
          if (ssh_buffer_append_cstrs(buffer, tmp_ptr, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_IPV4_OPTION:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          name = ssh_find_keyword_name(ssh_audit_ip4_options,
                                       ((unsigned int) val) & 0x7f);
          if (name)
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "%s", name);
          else
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "0x%02x",
                         (unsigned int) val);

          if (ssh_buffer_append_cstrs(buffer, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_ICMP_TYPECODE:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          name = ssh_find_keyword_name(ssh_audit_icmp_typecodes,
                                       ((unsigned int) val) & 0xFFFF);

          if (name)
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "%s", name);
          else
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "type 0x%02x code 0x%02x",
                         (unsigned int)((val >> 8) & 0xFF),
                         (unsigned int)(val & 0xFF));

          if (ssh_buffer_append_cstrs(buffer, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_IPV6ICMP_TYPECODE:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          name = ssh_find_keyword_name(ssh_audit_ipv6icmp_typecodes,
                                       ((unsigned int) val) & 0xFFFF);

          if (name)
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "%s", name);
          else
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "type 0x%02x code 0x%02x",
                         (unsigned int)((val >> 8) & 0xFF),
                         (unsigned int)(val & 0xFF));

          if (ssh_buffer_append_cstrs(buffer, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_TCP_FLAGS:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);

          if (val)
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "flags %s%s%s%s%s%s",
                         (val & SSH_TCPH_FLAG_FIN ? "fin " : ""),
                         (val & SSH_TCPH_FLAG_SYN ? "syn " : ""),
                         (val & SSH_TCPH_FLAG_RST ? "rst " : ""),
                         (val & SSH_TCPH_FLAG_PSH ? "push " : ""),
                         (val & SSH_TCPH_FLAG_ACK ? "ack " : ""),
                         (val & SSH_TCPH_FLAG_ACK ? "urg" : ""));
          else
            ssh_snprintf(ssh_sstr(buf), sizeof(buf), "no tcp flags set");

          if (ssh_buffer_append_cstrs(buffer, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;

          break;

        case SSH_AUDIT_KEY_LENGTH:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          ssh_snprintf(ssh_sstr(buf), sizeof(buf), "key length %u bits",
                       (unsigned int) val);
          if (ssh_buffer_append_cstrs(buffer, buf, NULL)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_NBT_SOURCE_HOST:
        case SSH_AUDIT_NBT_DESTINATION_HOST:
          if (ssh_buffer_append_cstrs(buffer,
                                      argv[i].type == SSH_AUDIT_NBT_SOURCE_HOST
                                      ? "NBT caller " : "NBT callee ",
                                      NULL)
              != SSH_BUFFER_OK)
            return FALSE;

          if (ssh_buffer_append(buffer, argv[i].data, argv[i].data_len)
              != SSH_BUFFER_OK)
            return FALSE;
          break;

        case SSH_AUDIT_TRANSMIT_BYTES:
          val = ssh_audit_get_number(argv[i].data, argv[i].data_len);
          if (val)
            {
              ssh_snprintf(ssh_sstr(buf), sizeof(buf), "%lu bytes",
                           (unsigned long) val);
              if (ssh_buffer_append_cstrs(buffer, buf, NULL) != SSH_BUFFER_OK)
                return FALSE;
            }
          break;

        case SSH_AUDIT_TRANSMIT_DIGEST:
          {
            size_t j;

            buf[0] = '\0';

            for (j = 0;
                 j < argv[i].data_len && j < sizeof(buf) / 2 - 1;
                 j++)
              ssh_snprintf(ssh_sstr(buf) + j * 2, 3, "%02X", argv[i].data[j]);

            if (ssh_buffer_append_cstrs(buffer, "Digest ", buf, NULL)
                != SSH_BUFFER_OK)
              return FALSE;
          }
          break;

          break;

        case SSH_AUDIT_USER:
        case SSH_AUDIT_REMOTE_USER:
        case SSH_AUDIT_SESSION_ID:
        case SSH_AUDIT_SUB_ID:
        case SSH_AUDIT_ERROR_CODE:
        case SSH_AUDIT_FILE_NAME:
        case SSH_AUDIT_COMMAND:
        case SSH_AUDIT_TOTAL_LENGTH:
        case SSH_AUDIT_DATA_WRITTEN:
        case SSH_AUDIT_DATA_READ:




          ssh_fatal("This audit argument type is not implemented yet XXX!");
          break;

        case SSH_AUDIT_ARGUMENT_END:
          SSH_NOTREACHED;
          break;
        }
    }

  /* All done. */
  return TRUE;
}
