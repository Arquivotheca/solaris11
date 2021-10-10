#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation. Portions created by Netscape are
 * Copyright (C) 1998-1999 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s):
 */

/*
 * Utilities for manageing the relationship between NSPR errors and
 * OS (errno-style) errors.
 *
 * The overall strategy used is to map NSPR errors into OS errors.
 */

#include "ldappr-int.h"


void
prldap_set_system_errno( int oserrno )
{
    PR_SetError( PR_UNKNOWN_ERROR, oserrno );
}


int
prldap_get_system_errno( void )
{
    return( PR_GetOSError());
}

/*
 * Retrieve the NSPR error number, convert to a system error code, and return
 * the result.
 */
struct prldap_errormap_entry {
    PRInt32	erm_nspr;	/* NSPR error code */
    int		erm_system;	/* corresponding system error code */
};

/* XXX: not sure if this extra mapping for Windows is good or correct */
#ifdef _WINDOWS
#ifndef ENOTSUP
#define ENOTSUP		-1
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT	WSAETIMEDOUT
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL	WSAEADDRNOTAVAIL
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT	WSAEAFNOSUPPORT
#endif
#ifndef EISCONN
#define EISCONN		WSAEISCONN
#endif
#ifndef EADDRINUSE
#define EADDRINUSE	WSAEADDRINUSE
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED	WSAECONNREFUSED
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH	WSAEHOSTUNREACH
#endif
#ifndef ENOTCONN
#define ENOTCONN	WSAENOTCONN
#endif
#ifndef ENOTSOCK
#define ENOTSOCK	WSAENOTSOCK
#endif
#ifndef EPROTOTYPE
#define EPROTOTYPE	WSAEPROTOTYPE
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP	WSAEOPNOTSUPP
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT	WSAEPROTONOSUPPORT
#endif
#ifndef EOVERFLOW
#define EOVERFLOW	-1
#endif
#ifndef ECONNRESET
#define ECONNRESET	WSAECONNRESET
#endif
#ifndef ELOOP
#define ELOOP		WSAELOOP
#endif
#ifndef ENOTBLK
#define ENOTBLK		-1
#endif
#ifndef ETXTBSY
#define ETXTBSY		-1
#endif
#ifndef ENETDOWN
#define ENETDOWN	WSAENETDOWN
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN	WSAESHUTDOWN
#endif
#ifndef ECONNABORTED
#define ECONNABORTED	WSAECONNABORTED
#endif
#endif /* _WINDOWS */

/* XXX: need to verify that the -1 entries are correct (no mapping) */
static struct prldap_errormap_entry prldap_errormap[] = {
    {  PR_OUT_OF_MEMORY_ERROR, ENOMEM },
    {  PR_BAD_DESCRIPTOR_ERROR, EBADF },
    {  PR_WOULD_BLOCK_ERROR, EAGAIN },
    {  PR_ACCESS_FAULT_ERROR, EFAULT },
    {  PR_INVALID_METHOD_ERROR, EINVAL },	/* XXX: correct mapping ? */
    {  PR_ILLEGAL_ACCESS_ERROR, EACCES },	/* XXX: correct mapping ? */
    {  PR_UNKNOWN_ERROR, -1 },
    {  PR_PENDING_INTERRUPT_ERROR, -1 },
    {  PR_NOT_IMPLEMENTED_ERROR, ENOTSUP },
    {  PR_IO_ERROR, EIO },
    {  PR_IO_TIMEOUT_ERROR, ETIMEDOUT },	/* XXX: correct mapping ? */
    {  PR_IO_PENDING_ERROR, -1 },
    {  PR_DIRECTORY_OPEN_ERROR, ENOTDIR },
    {  PR_INVALID_ARGUMENT_ERROR, EINVAL },
    {  PR_ADDRESS_NOT_AVAILABLE_ERROR, EADDRNOTAVAIL },
    {  PR_ADDRESS_NOT_SUPPORTED_ERROR, EAFNOSUPPORT },
    {  PR_IS_CONNECTED_ERROR, EISCONN },
    {  PR_BAD_ADDRESS_ERROR, EFAULT },		/* XXX: correct mapping ? */
    {  PR_ADDRESS_IN_USE_ERROR, EADDRINUSE },
    {  PR_CONNECT_REFUSED_ERROR, ECONNREFUSED },
    {  PR_NETWORK_UNREACHABLE_ERROR, EHOSTUNREACH },
    {  PR_CONNECT_TIMEOUT_ERROR, ETIMEDOUT },
    {  PR_NOT_CONNECTED_ERROR, ENOTCONN },
    {  PR_LOAD_LIBRARY_ERROR, -1 },
    {  PR_UNLOAD_LIBRARY_ERROR, -1 },
    {  PR_FIND_SYMBOL_ERROR, -1 },
    {  PR_INSUFFICIENT_RESOURCES_ERROR, -1 },
    {  PR_DIRECTORY_LOOKUP_ERROR, -1 },
    {  PR_TPD_RANGE_ERROR, -1 },
    {  PR_PROC_DESC_TABLE_FULL_ERROR, -1 },
    {  PR_SYS_DESC_TABLE_FULL_ERROR, -1 },
    {  PR_NOT_SOCKET_ERROR, ENOTSOCK },
    {  PR_NOT_TCP_SOCKET_ERROR, EPROTOTYPE },
    {  PR_SOCKET_ADDRESS_IS_BOUND_ERROR, -1 },
    {  PR_NO_ACCESS_RIGHTS_ERROR, EACCES },	/* XXX: correct mapping ? */
    {  PR_OPERATION_NOT_SUPPORTED_ERROR, EOPNOTSUPP },
    {  PR_PROTOCOL_NOT_SUPPORTED_ERROR, EPROTONOSUPPORT },
    {  PR_REMOTE_FILE_ERROR, -1 },
#if defined(OSF1)
    {  PR_BUFFER_OVERFLOW_ERROR, -1 },
#else
    {  PR_BUFFER_OVERFLOW_ERROR, EOVERFLOW },
#endif /* OSF1 */
    {  PR_CONNECT_RESET_ERROR, ECONNRESET },
    {  PR_RANGE_ERROR, ERANGE },
    {  PR_DEADLOCK_ERROR, EDEADLK },
#if defined(HPUX11) || defined(AIX4_3) || defined(OSF1)
    {  PR_FILE_IS_LOCKED_ERROR, -1 },	/* XXX: correct mapping ? */
#else
    {  PR_FILE_IS_LOCKED_ERROR, EDEADLOCK },	/* XXX: correct mapping ? */
#endif /* HPUX11 */
    {  PR_FILE_TOO_BIG_ERROR, EFBIG },
    {  PR_NO_DEVICE_SPACE_ERROR, ENOSPC },
    {  PR_PIPE_ERROR, EPIPE },
    {  PR_NO_SEEK_DEVICE_ERROR, ESPIPE },
    {  PR_IS_DIRECTORY_ERROR, EISDIR },
    {  PR_LOOP_ERROR, ELOOP },
    {  PR_NAME_TOO_LONG_ERROR, ENAMETOOLONG },
    {  PR_FILE_NOT_FOUND_ERROR, ENOENT },
    {  PR_NOT_DIRECTORY_ERROR, ENOTDIR },
    {  PR_READ_ONLY_FILESYSTEM_ERROR, EROFS },
    {  PR_DIRECTORY_NOT_EMPTY_ERROR, ENOTEMPTY },
    {  PR_FILESYSTEM_MOUNTED_ERROR, EBUSY },
    {  PR_NOT_SAME_DEVICE_ERROR, EXDEV },
    {  PR_DIRECTORY_CORRUPTED_ERROR, -1 },
    {  PR_FILE_EXISTS_ERROR, EEXIST },
    {  PR_MAX_DIRECTORY_ENTRIES_ERROR, -1 },
    {  PR_INVALID_DEVICE_STATE_ERROR, ENOTBLK }, /* XXX: correct mapping ? */
    {  PR_DEVICE_IS_LOCKED_ERROR, -2 },
    {  PR_NO_MORE_FILES_ERROR, ENFILE },
    {  PR_END_OF_FILE_ERROR, -1 },
    {  PR_FILE_SEEK_ERROR, ESPIPE },		/* XXX: correct mapping ? */
    {  PR_FILE_IS_BUSY_ERROR, ETXTBSY },
    {  PR_OPERATION_ABORTED_ERROR, -1 },
    {  PR_IN_PROGRESS_ERROR, -1 },
    {  PR_ALREADY_INITIATED_ERROR, -1 },
    {  PR_GROUP_EMPTY_ERROR, -1 },
    {  PR_INVALID_STATE_ERROR, -1 },
    {  PR_NETWORK_DOWN_ERROR, ENETDOWN },
    {  PR_SOCKET_SHUTDOWN_ERROR, ESHUTDOWN },
    {  PR_CONNECT_ABORTED_ERROR, ECONNABORTED },
    {  PR_HOST_UNREACHABLE_ERROR, EHOSTUNREACH },
    {  PR_MAX_ERROR, -1 },
};


int
prldap_prerr2errno( void )
{
    int		oserr, i;
    PRInt32	nsprerr;

    nsprerr = PR_GetError();

    oserr = -1;		/* unknown */
    for ( i = 0; prldap_errormap[i].erm_nspr != PR_MAX_ERROR; ++i ) {
	if ( prldap_errormap[i].erm_nspr == nsprerr ) {
	    oserr = prldap_errormap[i].erm_system;
	    break;
	}
    }

    return( oserr );
}
