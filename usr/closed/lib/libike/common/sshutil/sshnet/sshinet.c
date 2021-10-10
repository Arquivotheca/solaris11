/*
  File: sshinet.c

  Authors:
        Tero Kivinen <kivinen@ssh.fi>
        Tatu Ylonen <ylo@ssh.fi>

  Description:
        IP related functions and definitions.

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved
*/

#include "sshincludes.h"
#include "sshinet.h"

#define SSH_DEBUG_MODULE "SshInet"

#define MAX_IP_ADDR_LEN 16

/* The address string of the SSH_IPADDR_ANY. */
const char *const ssh_ipaddr_any = "*** SSH_IPADDR_ANY ***";
const char *const ssh_ipaddr_any_ipv4 = "0.0.0.0";
const char *const ssh_ipaddr_any_ipv6 = "0::0";

/* Mapping between protocol name and doi protocol number */
const SshKeywordStruct ssh_ip_protocol_id_keywords[] =
{
  { "any", SSH_IPPROTO_ANY },
  { "icmp", SSH_IPPROTO_ICMP },
  { "igmp", SSH_IPPROTO_IGMP },
  { "ggp", SSH_IPPROTO_GGP },
  { "ipip", SSH_IPPROTO_IPIP },
  { "st", SSH_IPPROTO_ST },
  { "tcp", SSH_IPPROTO_TCP },
  { "cbt", SSH_IPPROTO_CBT },
  { "egp", SSH_IPPROTO_EGP },
  { "igp", SSH_IPPROTO_IGP },
  { "bbn", SSH_IPPROTO_BBN },
  { "nvp", SSH_IPPROTO_NVP },
  { "pup", SSH_IPPROTO_PUP },
  { "argus", SSH_IPPROTO_ARGUS },
  { "emcon", SSH_IPPROTO_EMCON },
  { "xnet", SSH_IPPROTO_XNET },
  { "chaos", SSH_IPPROTO_CHAOS },
  { "udp", SSH_IPPROTO_UDP },
  { "mux", SSH_IPPROTO_MUX },
  { "dcn", SSH_IPPROTO_DCN },
  { "hmp", SSH_IPPROTO_HMP },
  { "prm", SSH_IPPROTO_PRM },
  { "xns", SSH_IPPROTO_XNS },
  { "trunk1", SSH_IPPROTO_TRUNK1 },
  { "trunk2", SSH_IPPROTO_TRUNK2 },
  { "leaf1", SSH_IPPROTO_LEAF1 },
  { "leaf2", SSH_IPPROTO_LEAF2 },
  { "rdp", SSH_IPPROTO_RDP },
  { "irtp", SSH_IPPROTO_IRTP },
  { "isotp4", SSH_IPPROTO_ISOTP4 },
  { "netblt", SSH_IPPROTO_NETBLT },
  { "mfe", SSH_IPPROTO_MFE },
  { "merit", SSH_IPPROTO_MERIT },
  { "sep", SSH_IPPROTO_SEP },
  { "3pc", SSH_IPPROTO_3PC },
  { "idpr", SSH_IPPROTO_IDPR },
  { "xtp", SSH_IPPROTO_XTP },
  { "ddp", SSH_IPPROTO_DDP },
  { "idprc", SSH_IPPROTO_IDPRC },
  { "tp", SSH_IPPROTO_TP },
  { "il", SSH_IPPROTO_IL },
  { "ipv6", SSH_IPPROTO_IPV6 },
  { "sdrp", SSH_IPPROTO_SDRP },
  { "ipv6route", SSH_IPPROTO_IPV6ROUTE },
  { "ipv6frag", SSH_IPPROTO_IPV6FRAG },
  { "idrp", SSH_IPPROTO_IDRP },
  { "rsvp", SSH_IPPROTO_RSVP },
  { "gre", SSH_IPPROTO_GRE },
  { "mhrp", SSH_IPPROTO_MHRP },
  { "bna", SSH_IPPROTO_BNA },
  { "esp", SSH_IPPROTO_ESP },
  { "ah", SSH_IPPROTO_AH },
  { "inlsp", SSH_IPPROTO_INLSP },
  { "swipe", SSH_IPPROTO_SWIPE },
  { "narp", SSH_IPPROTO_NARP },
  { "mobile", SSH_IPPROTO_MOBILE },
  { "tlsp", SSH_IPPROTO_TLSP },
  { "skip", SSH_IPPROTO_SKIP },
  { "ipv6icmp", SSH_IPPROTO_IPV6ICMP },
  { "ipv6nonxt", SSH_IPPROTO_IPV6NONXT },
  { "ipv6opts", SSH_IPPROTO_IPV6OPTS },
  { "cftp", SSH_IPPROTO_CFTP },
  { "local", SSH_IPPROTO_LOCAL },
  { "sat", SSH_IPPROTO_SAT },
  { "kryptolan", SSH_IPPROTO_KRYPTOLAN },
  { "rvd", SSH_IPPROTO_RVD },
  { "ippc", SSH_IPPROTO_IPPC },
  { "distfs", SSH_IPPROTO_DISTFS },
  { "satmon", SSH_IPPROTO_SATMON },
  { "visa", SSH_IPPROTO_VISA },
  { "ipcv", SSH_IPPROTO_IPCV },
  { "cpnx", SSH_IPPROTO_CPNX },
  { "cphb", SSH_IPPROTO_CPHB },
  { "wsn", SSH_IPPROTO_WSN },
  { "pvp", SSH_IPPROTO_PVP },
  { "brsatmon", SSH_IPPROTO_BRSATMON },
  { "sunnd", SSH_IPPROTO_SUNND },
  { "wbmon", SSH_IPPROTO_WBMON },
  { "wbexpak", SSH_IPPROTO_WBEXPAK },
  { "isoip", SSH_IPPROTO_ISOIP },
  { "vmtp", SSH_IPPROTO_VMTP },
  { "securevmtp", SSH_IPPROTO_SECUREVMTP },
  { "vines", SSH_IPPROTO_VINES },
  { "ttp", SSH_IPPROTO_TTP },
  { "nsfnet", SSH_IPPROTO_NSFNET },
  { "dgp", SSH_IPPROTO_DGP },
  { "tcf", SSH_IPPROTO_TCF },
  { "eigrp", SSH_IPPROTO_EIGRP },
  { "ospfigp", SSH_IPPROTO_OSPFIGP },
  { "sprite", SSH_IPPROTO_SPRITE },
  { "larp", SSH_IPPROTO_LARP },
  { "mtp", SSH_IPPROTO_MTP },
  { "ax25", SSH_IPPROTO_AX25 },
  { "ipwip", SSH_IPPROTO_IPWIP },
  { "micp", SSH_IPPROTO_MICP },
  { "scc", SSH_IPPROTO_SCC },
  { "etherip", SSH_IPPROTO_ETHERIP },
  { "encap", SSH_IPPROTO_ENCAP },
  { "encrypt", SSH_IPPROTO_ENCRYPT },
  { "gmtp", SSH_IPPROTO_GMTP },
  { "ifmp", SSH_IPPROTO_IFMP },
  { "pnni", SSH_IPPROTO_PNNI },
  { "pim", SSH_IPPROTO_PIM },
  { "aris", SSH_IPPROTO_ARIS },
  { "scps", SSH_IPPROTO_SCPS },
  { "qnx", SSH_IPPROTO_QNX },
  { "an", SSH_IPPROTO_AN },
  { "ippcp", SSH_IPPROTO_IPPCP },
  { "snp", SSH_IPPROTO_SNP },
  { "compaq", SSH_IPPROTO_COMPAQ },
  { "ipxip", SSH_IPPROTO_IPXIP },
  { "vrrp", SSH_IPPROTO_VRRP },
  { "pgm", SSH_IPPROTO_PGM },
  { "0hop", SSH_IPPROTO_0HOP },
  { "l2tp", SSH_IPPROTO_L2TP },
  { "ddx", SSH_IPPROTO_DDX },
  { "iatp", SSH_IPPROTO_IATP },
  { "stp", SSH_IPPROTO_STP },
  { "srp", SSH_IPPROTO_SRP },
  { "uti", SSH_IPPROTO_UTI },
  { "smp", SSH_IPPROTO_SMP },
  { "sm", SSH_IPPROTO_SM },
  { "ptp", SSH_IPPROTO_PTP},
  { "isis over ipv4", SSH_IPPROTO_ISISIPV4 },
  { "fire", SSH_IPPROTO_FIRE },
  { "crtp", SSH_IPPROTO_CRTP },
  { "crudp", SSH_IPPROTO_CRUDP },
  { "sscopmce", SSH_IPPROTO_SSCOPMCE },
  { "iplt", SSH_IPPROTO_IPLT },
  { "sps", SSH_IPPROTO_SPS },
  { "pipe", SSH_IPPROTO_PIPE },
  { "sctp", SSH_IPPROTO_SCTP },
  { "fc", SSH_IPPROTO_FC },
  { "rsvp-e2e-ignore", SSH_IPPROTO_RSVP_E2E_IGNORE },
  { "reserved", SSH_IPPROTO_RESERVED },
  { NULL, 0 }
};

/* Convert ip number string to binary format. The binary format is
   unsigned character array containing the ip address in network byte
   order. If the ip address is ipv4 address then this fills 4 bytes to
   the buffer, if it is ipv6 address then this will fills 16 bytes to
   the buffer. The buffer length is modified accordingly. This returns
   TRUE if the address is valid and conversion is successful (the
   buffer is large enough) and FALSE otherwise.  */

Boolean ssh_inet_strtobin(const unsigned char *ip_address,
                          unsigned char *out_buffer,
                          size_t *out_buffer_len_in_out)
{
  SshIpAddrStruct ipaddr;

  /* Parse the IP address.  Return FALSE on error.*/
  if (!ssh_ipaddr_parse(&ipaddr, ip_address))
    return FALSE;

  /* Convert the IP address to binary. */
  if (SSH_IP_IS6(&ipaddr))
    {
      if (*out_buffer_len_in_out < 16)
        return FALSE;
      SSH_IP6_ENCODE(&ipaddr, out_buffer);
      *out_buffer_len_in_out = 16;
    }
  else
    {
      if (*out_buffer_len_in_out < 4)
        return FALSE;
      SSH_IP4_ENCODE(&ipaddr, out_buffer);
      *out_buffer_len_in_out = 4;
    }
  return TRUE;
}

/* Compares comma separated list of ip nets and ip-address. Returns
   TRUE if ip-address is inside one of the nets given in
   net-address/netmask-bits format. */

Boolean ssh_inet_compare_netmask(const unsigned char *netmask,
                                 const unsigned char *ip_in)
{
  unsigned char net[MAX_IP_ADDR_LEN], mask[MAX_IP_ADDR_LEN],
    ip[MAX_IP_ADDR_LEN];
  size_t len;
  unsigned char temp_buffer[256], *p, *p2, *next;
  int mask_bits;

  memset(net, 0, MAX_IP_ADDR_LEN);
  memset(ip, 0, MAX_IP_ADDR_LEN);

  len = MAX_IP_ADDR_LEN;
  if (!ssh_inet_strtobin(ip_in, ip, &len))
    return FALSE;

  if (len == 4)
    {
      memmove(ip + 12, ip, 4);
      memset(ip, 0, 4);
    }
  do {
    p = ssh_ustr(strchr(ssh_csstr(netmask), ','));
    if (p != NULL)
      {
        next = p + 1;
        if (p - netmask < (int)sizeof(temp_buffer))
          {
            strncpy(ssh_sstr(temp_buffer), ssh_csstr(netmask),
                    (size_t)(p - netmask));
            temp_buffer[p - netmask] = '\0';
          }
        else
          {
            strncpy(ssh_sstr(temp_buffer), ssh_csstr(netmask),
                    sizeof(temp_buffer));
            temp_buffer[sizeof(temp_buffer) - 1] = '\0';
          }
      }
    else
      {
        next = NULL;
        strncpy(ssh_sstr(temp_buffer), ssh_csstr(netmask),
                sizeof(temp_buffer));
        temp_buffer[sizeof(temp_buffer) - 1] = '\0';
      }

    /* Basically this is strrchr. */
    for (p = NULL, p2 = temp_buffer; *p2; p2++)
      if (*p2 == (unsigned char)'/')
        p = p2;

    if (p == NULL)
      {
        mask_bits = MAX_IP_ADDR_LEN * 8;
      }
    else
      {
        *p++ = '\0';
        if (*p < '0' || *p > '9')
          mask_bits = -1;
        else
          {
            for (mask_bits = 0; *p >= '0' && *p <= '9'; p++)
              mask_bits = 10 * mask_bits + *p - '0';
          }
      }
    len = MAX_IP_ADDR_LEN;
    if (ssh_inet_strtobin(temp_buffer, net, &len) && mask_bits != -1)
      {
        if (len == 4)
          {
            memmove(net + 12, net, 4);
            memset(net, 0, 4);
            mask_bits += 96;
          }
        if (mask_bits > 128)
          mask_bits = 128;

        memset(mask, 0, MAX_IP_ADDR_LEN);
        memset(mask, 255, (size_t)(mask_bits / 8));
        if (mask_bits % 8 != 0)
          mask[mask_bits / 8] =
            "\000\200\300\340\360\370\374\376"[mask_bits % 8];
        for (len = 0; len < MAX_IP_ADDR_LEN; len++)
          {
            if ((ip[len] & mask[len]) != (net[len] & mask[len]))
              break;
          }
        if (len == MAX_IP_ADDR_LEN)
          return TRUE;
      }
    netmask = next;
  } while (netmask != NULL);
  return FALSE;
}

/* Compares two IP addresses, and returns <0 if address1 is smaller
   (in some implementation-defined sense, usually numerically), 0 if
   they denote the same address (though possibly written differently),
   and >0 if address2 is smaller (in the implementation-defined
   sense).  The result is zero if either address is invalid. */

int ssh_inet_ip_address_compare(const unsigned char *address1,
                                const unsigned char *address2)
{
  unsigned char a1[MAX_IP_ADDR_LEN], a2[MAX_IP_ADDR_LEN];
  size_t len;
  int ret;

  len = MAX_IP_ADDR_LEN;
  if (!ssh_inet_strtobin(address1, a1, &len))
    return 0;

  if (len == 4)
    {
      memmove(a1 + 12, a1, 4);
      memset(a1, 0, 12);
    }

  len = MAX_IP_ADDR_LEN;
  if (!ssh_inet_strtobin(address2, a2, &len))
    return 0;

  if (len == 4)
    {
      memmove(a2 + 12, a2, 4);
      memset(a2, 0, 12);
    }

  ret = memcmp(a1, a2, 16);
  if (ret < 0)
    return -1;
  else if (ret > 0)
    return 1;
  else
    return 0;
}

/* Produces a value that can (modulo a prime) be used as a hash value for
   the ip address.  The value is suitable for use with a prime-sized hash
   table. */

unsigned long ssh_ipaddr_hash(SshIpAddr ip)
{
  unsigned long value;
  size_t len;
  unsigned int i;

  len = SSH_IP_IS6(ip) ? 16 : 4;
  for (i = 0, value = 0; i < len; i++)
    value = 257 * value + ip->addr_data[i] + 3 * (value >> 23);
  return value;
}

/* Sets all rightmost bits after keeping `keep_bits' bits on the left to
   the value specified by `value'. */

void ssh_ipaddr_set_bits(SshIpAddr result, SshIpAddr ip,
                         unsigned int keep_bits, unsigned int value)
{
  size_t len;
  unsigned int i;

  len = SSH_IP_IS6(ip) ? 16 : 4;

  *result = *ip;
  for (i = keep_bits / 8; i < len; i++)
    {
      if (8 * i >= keep_bits)
        result->addr_data[i] = value ? 0xff : 0;
      else
        {
          SSH_ASSERT(keep_bits - 8 * i < 8);
          result->addr_data[i] &= (0xff << (8 - (keep_bits - 8 * i)));
          if (value)
            result->addr_data[i] |= (0xff >> (keep_bits - 8 * i));
        }
    }
}

/* Merges two ip addresses (left_ip and right_ip) so that leftmost
   keep_bits are from left_ip and rightmost bits are from right_ip.

   Mask length of the result is set to 0.
   XXX: Or should it be left/right ip mask, or keep_bits??
*/

void ssh_ipaddr_merge_bits(SshIpAddr result, SshIpAddr left_ip,
                           unsigned int left_bits, SshIpAddr right_ip)
{
  unsigned int total_bits, i;

  total_bits = (SSH_IP_IS6(left_ip) ? 128 : 32);

  SSH_ASSERT(left_bits <= (SSH_IP_IS6(left_ip) ? 128 : 32));
  SSH_ASSERT(left_ip->type == right_ip->type);

  result->type = left_ip->type;
  result->mask_len = 0;

  /* Copy whole left bytes */
  for (i = 0; (i + 7) < left_bits; i += 8)
    result->addr_data[i / 8] = left_ip->addr_data[i / 8];

  /* If on non-byte boundary, do bit fiddling */
  if ((left_bits - i) != 0) {
    result->addr_data[i / 8] =
      (left_ip->addr_data[i / 8] & (0xff << (8 - left_bits % 8))) |
      (right_ip->addr_data[i / 8] & ~(0xff << (8 - left_bits % 8)));

#if 0
    fprintf(stderr,
            "i=%d left_bits=%d left_bytes=%d total_bits=%d shift=%d left=%d "
            "right=%d\n",
            i, left_bits, left_bits / 8, total_bits,
            (8 - left_bits % 8),
            (left_ip->addr_data[i / 8] & (0xff << (8 - left_bits % 8))),
            (right_ip->addr_data[i / 8] & ~(0xff << (8 - left_bits % 8))));
#endif
    i += 8;
  }

  /* Copy whole right bytes */
  for (; i < total_bits; i += 8)
    result->addr_data[i / 8] = right_ip->addr_data[i / 8];
}

/* Parses an IP address from the string to the internal representation. */

static Boolean ssh_ipaddr_ipv4_parse(unsigned char *data,
                                     const unsigned char *str)
{
  int i, value;

  for (i = 0; i < 4; i++)
    {
      if (i != 0)
        {
          if (!*str)
            {
              switch (i)
                {
                case 1:
                  /* Single zero means 0.0.0.0.
                     Other single digit address is invalid. */
                  if (data[0] == 0)
                    {
                      data[1] = data[2] = data[3] = 0;
                      return TRUE;
                    }
                  return FALSE;

                case 2:
                  /* 1.2 -> 1.0.0.2 */
                  data[3] = data[1];
                  data[1] = data[2] = 0;
                  return TRUE;

                case 3:
                  /* 1.2.3 -> 1.2.0.3 */
                  data[3] = data[2];
                  data[2] = 0;
                  return TRUE;

                default:
                  SSH_NOTREACHED;
                }
            }
          else if (*str == '.' && *(str + 1) != '.')
            {
              str++;
            }
          else
            {
              return FALSE;
            }
        }
      for (value = 0; *str >= '0' && *str <= '9'; str++)
        {
          value = 10 * value + *str - '0';
          if (value > 255)
            return FALSE;
        }

      if ((*str && *str != '.' && !(*str >= '0' && *str <= '9')) ||
          (!*str && i == 0))
          return FALSE;

      data[i] = value;
    }

  if (*str)
    return FALSE;

  return TRUE;
}

#if !defined(WITH_IPV6)
/* ARGSUSED0 */
static Boolean ssh_ipaddr_ipv6_parse(unsigned char *addr,
                                     const unsigned char *str)
{
  return FALSE;
}
#else /* WITH_IPV6 */

#define h2i(CH) (((CH) >= '0' && (CH) <= '9') ? ((CH) - '0') : \
                  (((CH) >= 'a' && (CH) <= 'f') ? ((CH) - 'a' + 10) : \
                   (((CH) >= 'A' && (CH) <= 'F') ? ((CH) - 'A' + 10) : (-1))))

static Boolean ssh_ipaddr_ipv6_parse(unsigned char *addr,
                                     const unsigned char *str)
{
  const unsigned char *cp, *start, *next;
  int                 len, right, i;
  unsigned char       out_bytes[4];
  unsigned long       tmp, need_bytes, right_ptr, left_ptr;

  if (addr)
    {
      /* Zero addr */
      memset(addr, 0, 16);
    }

  /* Have we seen a "::" yet? */
  right = 0;
  left_ptr = 0;
  right_ptr = 16;

  start = cp = str;

  /* Look for next ':' delimiter */
  while (*start)
    {
      if ((cp = ssh_custr(strchr(ssh_csstr(start), ':'))) != NULL)
        {
          next = cp + 1;
        }
      else
        {
          cp = ssh_custr(strchr(ssh_csstr(start), '\0'));
          next = cp;
        }

      len = cp - start;

      if (len == 0)
        {
          if (*next != ':')
            {
              /* printf("ERROR: Empty element\n"); */
              return FALSE;
            }
          need_bytes = 0;
        }

      /* ipv6 'x', 'xx', 'xxx' or 'xxxx' part? */
      else if (len <= 4)
        {
          for (tmp = i = 0; i < len; i++)
            {
              if (h2i(start[i]) == -1)
                {
                  /* printf("ERROR: Invalid character in address\n"); */
                  return FALSE;
                }
              tmp = (tmp << 4) | h2i(start[i]);
            }

          out_bytes[0] = (unsigned char)((tmp >>  8) & 0xff);
          out_bytes[1] = (unsigned char)((tmp >>  0) & 0xff);

          need_bytes = 2;
        }
      else if (memchr(start, '.', len) != NULL && (len <= 15))
        {
          unsigned char buf[16];

          memcpy(buf, start, len);
          buf[len] = '\0';

          if (ssh_ipaddr_ipv4_parse(out_bytes, buf) == FALSE)
            return FALSE;

          need_bytes = 4;
        }

      else
        {
          /* printf("ERROR: Unrecognized address part\n"); */
          return FALSE;
        }

      if ((right_ptr - left_ptr) < need_bytes)
        {
#if 0
          printf("ERROR: Not enough space in output address "
                 "(have %d, required %d)\n",
                 right_ptr - left_ptr, need_bytes);
#endif
          return FALSE;
        }

      if (right)
        {
          if (addr)
            {
              memmove(addr + right_ptr - need_bytes,
                      addr + right_ptr,
                      16 - right_ptr);
              memcpy(addr + 16 - need_bytes, out_bytes, need_bytes);
            }
          right_ptr -= need_bytes;
        }
      else
        {
          if (addr)
            memcpy(addr + left_ptr, out_bytes, need_bytes);
          left_ptr += need_bytes;
        }

      if (*next == ':')
        {
          if (right)
            {
              /* printf("ERROR: Already seen '::'\n"); */
              return FALSE;
            }

          right = 1;
          next++;
        }

      /* Move on to next iteration */
      start = next;
    }

  if ((right_ptr - left_ptr) > 0 && !right)
    {
      /* printf("ERROR: %d unresolved address bytes\n",
         right_ptr - left_ptr); */
      return FALSE;
    }

  return TRUE;
}
#endif /* !WITH_IPV6 */

/* Determines whether the given string is a valid numeric IP address.
   (This currently only works for IPv4 addresses, but might be changed
   in future to accept also IPv6 addresses on systems that support
   them. */

Boolean ssh_inet_is_valid_ip_address(const unsigned char *address)
{
  unsigned char tmp[16];

  if (ssh_ipaddr_ipv4_parse(tmp, address) ||
      ssh_ipaddr_ipv6_parse(tmp, address))
    return TRUE;
  return FALSE;
}

Boolean ssh_ipaddr_parse(SshIpAddr ip, const unsigned char *str)
{
  unsigned char buf[64];
  const unsigned char *cp;

  /* Is the scope ID part given? */
  cp = ssh_ustr(strchr(ssh_csstr(str), '%'));
  if (cp)
    {
      /* Yes it is.  Let's ignore it. */
      if (cp - str + 1 > sizeof(buf))
        /* This can not be a valid IP address since all decimal IP
           addresses fit into 64 bytes. */
        return FALSE;

      memcpy(buf, str, cp - str);
      buf[cp - str] = '\0';
    }
  else
    {
      /* No it isn't.  Store the address into our buffer. */
      if (strlen(ssh_csstr(str)) + 1 > sizeof(buf))
        /* This can not be a valid IP address since all decimal IP
           addresses fit into 64 bytes. */
        return FALSE;

      strncpy(ssh_sstr(buf), ssh_csstr(str), sizeof(buf));
    }

  /* Try to parse it first as ipv4 address, then as ipv6 */
  if (ssh_ipaddr_ipv4_parse(ip->addr_data, buf))
    {
      ip->type = SSH_IP_TYPE_IPV4;
      ip->mask_len = 32;
      return TRUE;
    }

  if (ssh_ipaddr_ipv6_parse(ip->addr_data, buf))
    {
      ip->type = SSH_IP_TYPE_IPV6;
      ip->mask_len = 128;
      return TRUE;
    }

  SSH_IP_UNDEFINE(ip);
  return FALSE;
}

/* ssh_ipaddr_parse_with_mask

   If mask == NULL, we expect that str is in format "a.b.c.d/masklen"
   instead. */

Boolean ssh_ipaddr_parse_with_mask(SshIpAddr ip, const unsigned char *str,
                                   const unsigned char *mask)
{
  unsigned char *dup, *cp;
  Boolean     ret;

  dup = NULL;
  ret = FALSE;

  SSH_IP_UNDEFINE(ip);

  if (mask == NULL)
    {
      dup = ssh_strdup(str);
      if (!dup)
        return FALSE;

      if ((cp = ssh_ustr(strchr(ssh_csstr(dup), '/'))) == NULL)
        {
          ssh_free(dup);
          return FALSE;
        }
      str = dup;
      mask = cp + 1;

      *cp = '\0';
    }

  /* Try to parse as ipv4 address */
  if (ssh_ipaddr_ipv4_parse(ip->addr_data, str) == TRUE)
    {
      ip->type = SSH_IP_TYPE_IPV4;

      ret = FALSE;

      /* x.x.x.x/y.y.y.y type netmask? Dang. Parse and count the bits. */
      if (strchr(ssh_csstr(mask), '.') != NULL)
        {
          SshIpAddrStruct mask_ip;

          if (ssh_ipaddr_ipv4_parse(mask_ip.addr_data, mask))
            {
              SshUInt32 mask_bits, mask_len;

              mask_bits = SSH_IP4_TO_INT(&mask_ip);
              mask_len = 0;

              while (mask_len < 32 && ((mask_bits >> 31) & 0x1))
                {
                  mask_bits <<= 1;
                  mask_len++;
                }

              ip->mask_len = mask_len;

              ret = TRUE;
            }
        }
      else
        {
          ip->mask_len = atoi(ssh_csstr(mask));
          ret = TRUE;
        }
    }
  else if (ssh_ipaddr_ipv6_parse(ip->addr_data,str) == TRUE)
    {
      ip->type = SSH_IP_TYPE_IPV6;
      ip->mask_len = atoi(ssh_csstr(mask));
      ret = TRUE;
    }

  if (dup != NULL)
    ssh_free(dup);

  return ret;
}

/* Parses an IP address with an optional IPv6 link-local address scope
   ID.  The addresses with a scope ID are given as `ADDR%SCOPEID'.  On
   success, the function returns a pointer to the scope ID part of the
   address in `scope_id_return'.  The value returned in
   `scope_id_return' will point into the original input string `str'.
   If the string `str' does not contain the scope ID part, the
   `scope_id_return' is set to NULL. */

Boolean ssh_ipaddr_parse_with_scope_id(SshIpAddr ip, const unsigned char *str,
                                       unsigned char **scope_id_return)
{
  unsigned char *cp;

  /* Is the scope ID part given? */
  /* XXX tzimmo: is it safe to convert this const char pointer to a
     non-const char pointer? XXX */
  cp = ssh_ustr(strchr(ssh_csstr(str), '%'));
  if (cp)
    /* Yes it was. */
    *scope_id_return = (cp + 1);
  else
    /* No it wasn't. */
    *scope_id_return = NULL;

  /* Parse the IP address part.  Note that the ssh_ipaddr_parse
     function ingnores the possible scope ID part from the string. */
  return ssh_ipaddr_parse(ip, str);
}

/* Prints the IP address into the buffer in string format.  If the buffer
   is too short, the address is truncated.  This returns `buf'. */

static void ssh_ipaddr_ipv4_print(const unsigned char *data,
                                  unsigned char *buf, size_t buflen)
{
  char largebuf[64];

  ssh_snprintf(largebuf, sizeof(largebuf), "%d.%d.%d.%d",
               data[0], data[1], data[2], data[3]);

  strncpy(ssh_sstr(buf), largebuf, buflen);
  buf[buflen - 1] = '\0';
}

static void ssh_ipaddr_ipv6_print(const unsigned char *data,
                                  unsigned char *buf, size_t buflen)
{
  int i, j;
  char largebuf[64], *cp;
  int opt_start = 8;
  int opt_len = 0;
  SshUInt16 val;

  /* Optimize the largest zero-block from the address. */
  for (i = 0; i < 8; i++)
    if (SSH_GET_16BIT(data + i * 2) == 0)
      {
        for (j = i + 1; j < 8; j++)
          {
            val = SSH_GET_16BIT(data + j * 2);
            if (val != 0)
              break;
          }
        if (j - i > opt_len)
          {
            opt_start = i;
            opt_len = j - i;
          }
        i = j;
      }

  if (opt_len <= 1)
    /* Disable optimization. */
    opt_start = 8;

  cp = largebuf;

  /* Format the result. */
  for (i = 0; i < 8; i++)
    {
      if (i == opt_start)
        {
          if (i == 0)
            *cp++ = ':';

          *cp++ = ':';
          i += opt_len - 1;
        }
      else
        {
          ssh_snprintf(cp, 20, "%x",
                       (unsigned int) SSH_GET_16BIT(data + i * 2));
          cp += strlen(cp);

          if (i + 1 < 8)
            {
              *cp = ':';
              cp++;
            }
        }
    }
  *cp = '\0';

  strncpy(ssh_sstr(buf), largebuf, buflen);
  buf[buflen - 1] = '\0';
}

unsigned char *ssh_ipaddr_print(const SshIpAddr ip, unsigned char *buf,
                                size_t buflen)
{
  if (SSH_IP_IS4(ip))
    ssh_ipaddr_ipv4_print(ip->addr_data, buf, buflen);
  else if (SSH_IP_IS6(ip))
    ssh_ipaddr_ipv6_print(ip->addr_data, buf, buflen);
  else if (buflen > 0)
    buf[0] = '\0';

  return buf;
}

unsigned char *ssh_ipaddr_print_with_mask(const SshIpAddr ip,
                                          unsigned char *buf, size_t buflen)
{
  if (SSH_IP_IS4(ip))
    ssh_ipaddr_ipv4_print(ip->addr_data, buf, buflen);
  else
    ssh_ipaddr_ipv6_print(ip->addr_data, buf, buflen);

  /* XXX temporary casts until library API is changed XXX */
  ssh_snprintf(ssh_sstr(buf) + strlen(ssh_csstr(buf)),
               buflen - strlen(ssh_csstr(buf)) - 1, "/%d",
               (unsigned int) ip->mask_len);

  return buf;
}

/* Compares two IP addresses in the internal representation and returns
   TRUE if they are equal. */

Boolean ssh_ipaddr_with_mask_equal(SshIpAddr ip1, SshIpAddr ip2,
                                   SshIpAddr mask)
{
  unsigned int i;
  unsigned char i1[MAX_IP_ADDR_LEN], i2[MAX_IP_ADDR_LEN], m[MAX_IP_ADDR_LEN];

  if ((ip1->type != ip2->type) || (ip2->type != mask->type))
    return FALSE;

  memset(i1, 0, 16);
  memset(i2, 0, 16);
  memset(m, 255, 16);

  if (SSH_IP_IS4(ip1))
    memcpy(i1 + 12, ip1->addr_data, 4);
  else
    memcpy(i1, ip1->addr_data, 16);

  if (SSH_IP_IS4(ip2))
    memcpy(i2 + 12, ip2->addr_data, 4);
  else
    memcpy(i2, ip2->addr_data, 16);

  if (SSH_IP_IS4(mask))
    memcpy(m + 12, mask->addr_data, 4);
  else
    memcpy(m, mask->addr_data, 16);

  for (i = 0; i < 16; i++)
    if ((i1[i] & m[i]) != (i2[i] & m[i]))
      return FALSE;

  return TRUE;
}

Boolean ssh_ipaddr_mask_equal(SshIpAddr ip, SshIpAddr masked_ip)
{
  register SshUInt32 *a1, *a2;
  register int ml;
#ifndef WORDS_BIGENDIAN
  register unsigned char *c1, *c2;
#endif

  /* Different type? */
  if (ip->type != masked_ip->type)
    return FALSE;

  a1 = (SshUInt32 *) ip->addr_data;
  a2 = (SshUInt32 *) masked_ip->addr_data;
  ml = masked_ip->mask_len;

  /* Chuck away ml in full 32-bit words */
  for (; ml > 31; ml -= 32)
    {
      if (*a1++ != *a2++)
        return FALSE;
    }

  if (ml == 0)
    return TRUE;

  /* Then we have only <32 bit part left */
#ifdef WORDS_BIGENDIAN
  if ((*a1 ^ *a2) & (0xffffffff << (32 - ml)))
    return FALSE;

  return TRUE;
#else
  c1 = (unsigned char *) a1;
  c2 = (unsigned char *) a2;

  for (; ml > 7; ml -= 8)
    {
      if (*c1++ != *c2++)
        return FALSE;
    }

  if (ml == 0)
    return TRUE;

  if ((*c1 ^ *c2) & (0xff  << (8 - ml)))
    return FALSE;

  return TRUE;
#endif
}

/* Render function to render IP addresses for %@ format string for
   ssh_e*printf */
int ssh_ipaddr_render(unsigned char *buf, int buf_size, int precision,
                      void *datum)
{
  const SshIpAddr ip = (SshIpAddr) datum;
  int len;

  if (ip == NULL)
    /* XXX temporary casts until library API is changed XXX */
    ssh_snprintf(ssh_sstr(buf), buf_size, "<null>");
  else if (ip->type == SSH_IP_TYPE_NONE)
    /* XXX temporary casts until library API is changed XXX */
    ssh_snprintf(ssh_sstr(buf), buf_size, "");
  else if (SSH_IP_ADDR_LEN(ip) * 8 == SSH_IP_MASK_LEN(ip))
    (void) ssh_ipaddr_print(ip, buf, buf_size);
  else
    (void) ssh_ipaddr_print_with_mask(ip, buf, buf_size);

  len = strlen(ssh_csstr(buf));

  if (len == buf_size)
    return len + 1;
  if (precision >= 0)
    if (len > precision)
      len = precision;

  return len;
}

int ssh_ipaddr4_uint32_render(unsigned char *buf, int buf_size, int precision,
                              void *datum)
{
  int len;
  unsigned char tmp[4];
  /* This small kludge is here to avoid compilation warnings on 64-bit
     platforms. */
  unsigned long tmp_num = (unsigned long)datum;

  SSH_PUT_32BIT(tmp, (SshUInt32) tmp_num);
  ssh_ipaddr_ipv4_print(tmp, buf, buf_size);

  len = strlen(ssh_csstr(buf));

  if (precision >= 0)
    if (len > precision)
      len = precision;

  return len;
}

int ssh_ipaddr6_byte16_render(unsigned char *buf, int buf_size, int precision,
                              void *datum)
{
  int len;

  ssh_ipaddr_ipv6_print((unsigned char*) datum, buf, buf_size);

  len = strlen(ssh_csstr(buf));

  if (len >= buf_size - 1) return buf_size + 1;

  if (precision >= 0)
    if (len > precision)
      len = precision;

  return len;
}



/* Renders an IP protocol name */
int ssh_ipproto_render(unsigned char *buf, int buf_size, int precision,
                       void *datum)
{
  SshInetIPProtocolID proto = (SshInetIPProtocolID) datum;
  int len, i;

  for (i = 0; ssh_ip_protocol_id_keywords[i].name; i++)
    if (proto == ssh_ip_protocol_id_keywords[i].code)
      {
        /* XXX temporary casts until library API is changed XXX */
        ssh_snprintf(ssh_sstr(buf), buf_size,
                     "%s", ssh_ip_protocol_id_keywords[i].name);
        break;
      }

  if (!ssh_ip_protocol_id_keywords[i].name)
    /* XXX temporary casts until library API is changed XXX */
    ssh_snprintf(ssh_sstr(buf), buf_size, "(unknown %u)", proto);

  len = strlen(ssh_csstr(buf));

  if (len >= buf_size - 1) return buf_size + 1;

  if (len >= buf_size - 1) return buf_size + 1;

  if (precision >= 0)
    if (len > precision)
      len = precision;

  return len;
}

/* Renders an IP address mask. */
int ssh_ipmask_render(unsigned char *buf, int buf_size, int precision,
                      void *datum)
{
  const SshIpAddr ip = (SshIpAddr) datum;
  int i, j;
  int bits = 0;
  int len;

  /* The non-IPv6 masks are printed just like IP addresses. */
  if (!SSH_IP_IS6(ip))
    return ssh_ipaddr_render(buf, buf_size, precision, datum);

  /* The IPv6 masks are rendered as number describing the prefix
     len. */

  for (i = 0; i < SSH_IP_ADDR_LEN(ip); i++)
    for (j = 7; j >= 0; j--)
      if (ip->addr_data[i] & (1 << j))
        bits++;

  /* XXX temporary casts until library API is changed XXX */
  ssh_snprintf(ssh_sstr(buf), buf_size, "%d", bits);
  len = strlen(ssh_csstr(buf));

  if (len >= buf_size - 1) return buf_size + 1;

  if (precision >= 0)
    if (len > precision)
      len = precision;

  return len;
}

/* Rendering function for Ethernet MAC addresses. */
int ssh_etheraddr_render(unsigned char *buf, int buf_size, int precision,
                         void *datum)
{
  int len;
  unsigned char *hw_addr = datum;

  ssh_snprintf(ssh_sstr(buf), buf_size, "%02x:%02x:%02x:%02x:%02x:%02x",
               hw_addr[0], hw_addr[1], hw_addr[2],
               hw_addr[3], hw_addr[4], hw_addr[5]);

  len = strlen(ssh_csstr(buf));

  if (len >= buf_size - 1) return buf_size + 1;

  if (precision >= 0 && len > precision)
    len = precision;

  return len;
}

/* Check if ipv6 address is just an ipv4 address mapped into ipv6 mask. */

Boolean ssh_inet_addr_is_ip6_mapped_ip4(SshIpAddr ip_addr)
{
  if (! SSH_IP_IS6(ip_addr))
    return FALSE;
  SSH_ASSERT(ip_addr->mask_len == 128);
  return ((ip_addr->addr_data[0] == 0x0) &&
          (ip_addr->addr_data[1] == 0x0) &&
          (ip_addr->addr_data[2] == 0x0) &&
          (ip_addr->addr_data[3] == 0x0) &&
          (ip_addr->addr_data[4] == 0x0) &&
          (ip_addr->addr_data[5] == 0x0) &&
          (ip_addr->addr_data[6] == 0x0) &&
          (ip_addr->addr_data[7] == 0x0) &&
          (ip_addr->addr_data[8] == 0x0) &&
          (ip_addr->addr_data[9] == 0x0) &&
          (ip_addr->addr_data[10] == 0xff) &&
          (ip_addr->addr_data[11] == 0xff));
}

/* Convert if ipv6 mapped ipv4 address to an ipv4 address, if possible. */

Boolean ssh_inet_convert_ip6_mapped_ip4_to_ip4(SshIpAddr ip_addr)
{
  if (! ssh_inet_addr_is_ip6_mapped_ip4(ip_addr))
    return FALSE;
  memcpy(ip_addr->addr_data, &(ip_addr->addr_data[12]), 4);
  memset(&(ip_addr->addr_data[4]), 0, 12);
  ip_addr->mask_len = 32;
  ip_addr->type = SSH_IP_TYPE_IPV4;
  return TRUE;
}
