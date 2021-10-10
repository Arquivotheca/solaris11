/*
 *
 * sshnameserver.h
 *
 * Author: Markku Rossi <mtr@ssh.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 *                  All rights reserved.
 *
 * Functions for name server lookups.
 *
 */

#ifndef SSHNAMESERVER_H
#define SSHNAMESERVER_H

#include "sshtcp.h"

/* Gets the name of the host we are running on.  To get the
   corresponding IP address(es), a name server lookup must be done
   using the functions below. */
void ssh_tcp_get_host_name(unsigned char *buf, size_t buflen);

/* Callback function for name server lookups.  The function should
   copy the result; the argument string is only valid until this call
   returns.  The result is only valid if error is SSH_TCP_OK. */
typedef void (*SshLookupCallback)(SshTcpError error,
                                  const unsigned char *result,
                                  void *context);

/* Looks up all ip-addresses of the host, returning them as a
   comma-separated list. The host name may already be an ip address,
   in which case it is returned directly. This is an simplification of
   function ssh_tcp_get_host_addrs_by_name for situations when the
   operation may block.

   The function returns NULL if the name can not be resolved. When the
   return value is non null, it is a pointer to a string allocated by
   this function, and must be freed by the caller when no longer
   needed. */
unsigned char *ssh_tcp_get_host_addrs_by_name_sync(const unsigned char *name);

/* Looks up all ip-addresses of the host, returning them as a
   comma-separated list when calling the callback.  The host name may
   already be an ip address, in which case it is returned directly. */
SshOperationHandle ssh_tcp_get_host_addrs_by_name(const unsigned char *name,
                                                  SshLookupCallback callback,
                                                  void *context);

/* Looks up the name of the host by its ip-address.  Verifies that the
   address returned by the name servers also has the original ip
   address. This is an simplification of function
   ssh_tcp_get_host_by_addr for situations when the operation may
   block.

   Function returns NULL, if the reverse lookup fails for some reason,
   or pointer to dynamically allocated memory containing the host
   name.  The memory should be deallocated by the caller when no
   longer needed.  */
unsigned char *ssh_tcp_get_host_by_addr_sync(const unsigned char *addr);

/* Looks up the name of the host by its ip-address.  Verifies that the
   address returned by the name servers also has the original ip
   address.  Calls the callback with either error or success.  The
   callback should copy the returned name. */
SshOperationHandle ssh_tcp_get_host_by_addr(const unsigned char *addr,
                                            SshLookupCallback callback,
                                            void *context);

#endif /* not SSHNAMESERVER_H */
