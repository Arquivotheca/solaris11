/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* sshconf.h.  Generated automatically by configure.  */
/* sshconf.h.in.  Generated automatically from configure.in by autoheader.  */
#ifndef SSHCONF_H
#define SSHCONF_H

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define if you have a working `mmap' system call.  */
/* #undef HAVE_MMAP */

/* Define if you have <sys/wait.h> that is POSIX.1 compatible.  */
#define HAVE_SYS_WAIT_H 1

/* Define as __inline if that's what the C compiler calls it.  */
/* #undef inline */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef mode_t */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef off_t */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Define to `unsigned' if <sys/types.h> doesn't define.  */
/* #undef size_t */

/* Define if the `S_IS*' macros in <sys/stat.h> do not work properly.  */
/* #undef STAT_MACROS_BROKEN */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
#define TIME_WITH_SYS_TIME 1

/* Define if your processor stores words with the most significant
   byte first (like Motorola and SPARC, unlike Intel and VAX).  */
/* #define WORDS_BIGENDIAN 1 */

/* EFENCE memory debugger */
/* #undef EFENCE */

/* Use system's memcmp - may be faster, but broken. */
/* #undef WITH_SYSTEM_MEMCMP */

/* Global variable emulation, see lib/sshutil/sshcore/sshglobals.h. */
/* #undef SSH_GLOBALS_EMULATION */

/* Light debugging */
/* #undef DEBUG_LIGHT */

/* Heavy debugging */
/* #undef DEBUG_HEAVY */

/* Inet addr is broken on this system */
/* #undef BROKEN_INET_ADDR */

/* Sizes of usermode basic types */
#define USERMODE_SIZEOF_INT 4
#define USERMODE_SIZEOF_LONG 4
#define USERMODE_SIZEOF_LONG_LONG 8
#define USERMODE_SIZEOF_SHORT 2
#define USERMODE_SIZEOF_VOID_P 4

/* "Have" for the usermode types */
#define HAVE_USERMODE_INT 1
#define HAVE_USERMODE_LONG 1
#define HAVE_USERMODE_LONG_LONG 1
#define HAVE_USERMODE_SHORT 1
#define HAVE_USERMODE_VOID_P 1

/* Is this source tree compiled with purify? */
/* #undef WITH_PURIFY */

/* How large data and insn caches do we have, in kB. */
#define SSH_DATA_CACHE_SIZE 256
#define SSH_INSN_CACHE_SIZE 256

/* Define this for Hi/Fn 6500 support */
/* #undef WITH_HIFN6500 */

/* ENABLE_HIFN_HSP */
/* #undef ENABLE_HIFN_HSP */

/* Define this if you are using HP-UX.  HP-UX uses non-standard shared
   memory communication for X, which seems to be enabled by the display name
   matching that of the local host.  This circumvents it by using the IP
   address instead of the host name in DISPLAY. */
/* #undef HPUX_NONSTANDARD_X11_KLUDGE */

/* SSH Distribution name ("quicksec-complete") */
#define SSH_DIST_NAME "quicksec-access"

/* SSH version number (or yyyy-mm-dd-hh-mm-ss if no version defined)
   as string (i.e "2.0") */ 
#define SSH_DIST_VERSION "2.1"

/* SSH distribution name and version (i.e "quicksec-complete 2.0") */
#define SSH_DIST_NAME_VERSION "quicksec-access 2.1"

/* SSH base distribution name (truncate to first -) ("quicksec") */
#define SSH_DIST_BASENAME "quicksec"

/* SSH base distribution name and version (i.e "quicksec 2.0") */
#define SSH_DIST_BASENAME_VERSION "quicksec 2.1"

/* Usermode route information handling in engine */
#define SSH_ENGINE_USERMODE_ROUTES 1

/* Compile a minimal engine */
/* #undef SSH_IPSEC_SMALL */

/* Kludge for platforms where no arp packets can be received. The
   first one is for Express and second for the QuickSec. */
#define SSH_ETHER_WITHOUT_ARP_KLUDGE 1
#define SSH_ENGINE_MEDIA_ETHER_NO_ARP_RESPONSES 1

#ifdef SSHDIST_PLATFORM_NETBSD
/* The current NetBSD version. */
/* #undef SSH_NetBSD_132 */
/* #undef SSH_NetBSD_133 */
/* #undef SSH_NetBSD_140 */
/* #undef SSH_NetBSD_150 */
/* #undef SSH_NetBSD_160 */

/* The NetBSD version number. */
/* #undef SSH_NetBSD */
#endif /* SSHDIST_PLATFORM_NETBSD */

#ifdef SSHDIST_PLATFORM_LINUX
/* Here will probably come kernel-version dependend SSH_Linux_XX defines.
   For generic in-kernel linux tests, use symbol __linux__, it is defined
   by the preprocessor. */
/* #undef SSH_USE_LINUX_SKB_SECURITY */
#endif /* SSHDIST_PLATFORM_LINUX*/

/* Define this to the canonical name of your host type (e.g.,
   "sparc-sun-sunos4.0.3"). */
#define HOSTTYPE "sparc-sun-solaris2.10"

/* Need defines for readonly versions of pullup and iteration packet
   routines */
/* #undef NEED_PACKET_READONLY_DEFINES */

/* Interceptor has its own version of
   ssh_interceptor_packet_alloc_and_copy_ext_data */
/* #undef INTERCEPTOR_HAS_PACKET_ALLOC_AND_COPY_EXT_DATA */

/* Interceptor has its own version of ssh_interceptor_packet_copy */
/* #undef INTERCEPTOR_HAS_PACKET_COPY */

/* Interceptor has its own version of ssh_interceptor_packet_copyin */
#define INTERCEPTOR_HAS_PACKET_COPYIN 1

/* Interceptor has its own version of ssh_interceptor_mark() function */
/* #undef INTERCEPTOR_HAS_MARK_FUNC */

/* Interceptor has its own version of ssh_interceptor_packet_copyout */
#define INTERCEPTOR_HAS_PACKET_COPYOUT 1

/* Interceptor has its own versions of
   ssh_interceptor_export_internal_data and
   ssh_interceptor_import_internal_data */
/* #undef INTERCEPTOR_HAS_PACKET_INTERNAL_DATA_ROUTINES */

/* Interceptor has its own version of ssh_interceptor_has_packet_detach() */
/* #undef INTERCEPTOR_HAS_PACKET_DETACH */

/* Interceptor has "platform_interceptor.h" include file
   to be included by interceptor.h. */
/* #undef INTERCEPTOR_HAS_PLATFORM_INCLUDE */

/* Interceptor sees and sets the SSH_PACKET_FORWARDED flag */
/* #undef INTERCEPTOR_SETS_IP_FORWARDING */

/* Interceptor handles loopback packets and never passes them to the
   packet callback */
#define INTERCEPTOR_HANDLES_LOOPBACK_PACKETS 1

/* Does the interceptor have virtual adapters */
/* #undef INTERCEPTOR_HAS_VIRTUAL_ADAPTERS */

/* Does the interceptor support memory mapped files (on the IPM
   device) interface? */
#define INTERCEPTOR_SUPPORTS_MAPPED_MEMORY 1

/* Sizes of kernel basic types */
/* #undef KERNEL_SIZEOF_INT */
/* #undef KERNEL_SIZEOF_LONG */
/* #undef KERNEL_SIZEOF_LONG_LONG */
/* #undef KERNEL_SIZEOF_SHORT */
/* #undef KERNEL_SIZEOF_VOID_P */

/* "Have" for kernel basic types */
/* #undef HAVE_KERNEL_INT */
/* #undef HAVE_KERNEL_LONG */
/* #undef HAVE_KERNEL_LONG_LONG */
/* #undef HAVE_KERNEL_SHORT */
/* #undef HAVE_KERNEL_VOID_P */

/* This is defined if /var/run exists. */
#define HAVE_VAR_RUN 1

/* Define this to enable setting TCP_NODELAY for tcp sockets. */
#define ENABLE_TCP_NODELAY 1

/* Define this if connect(2) system call fails with nonblocking sockets. */
/* #undef NO_NONBLOCKING_CONNECT */

/* Define this if S_IFSOCK is defined */
#define HAVE_S_IFSOCK 1

/* Support for Secure RPC */
#define SECURE_RPC 1

/* Support for Secure NFS */
#define SECURE_NFS 1

/* Does struct tm have tm_gmtoff member? */
/* #undef HAVE_TM_GMTOFF_IN_STRUCT_TM */

/* Does struct tm have __tm_gmtoff__ member? (older Linux distributions) */
/* #undef HAVE_OLD_TM_GMTOFF_IN_STRUCT_TM */

/* Does struct tm have tm_isdst member? */
#define HAVE_TM_ISDST_IN_STRUCT_TM 1

/* Does system keep gmt offset in external variable "timezone"? */
#define HAVE_EXTERNAL_TIMEZONE 1

/* Should sshtime routines avoid using system provided gmtime(3)
   and localtime(3) functions? */
/* #undef USE_SSH_INTERNAL_LOCALTIME */

/* Do we have POSIX style accept(2), send(2), sendto(2), recv(2),
   recvfrom(2), getsockopt(2), setsockopt(2), getpeername(2),
   getsockneme(2) and other socket call prototypes
   with argument pointing address representation length
   size as size_t instead of the old ones (still present in most of
   the systems) with int type argument? */
/* #undef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */

/* Do we have threads? */
/* #undef HAVE_THREADS */

/* Do we have posix threads */
/* #undef HAVE_PTHREADS */

/* Do we have IPv6 socket structures */
#define HAVE_SOCKADDR_IN6_STRUCT 1

/* Does IPv6 have the RFC2533 defined "sin6_scope_id" field? */
#define HAVE_SOCKADDR_IN6_SCOPE_ID 1

/* Whether termios.h needs modem.h to also be included in
   sshserialstream. */
/* #undef TERMIOS_H_NEEDS_MODEM_H */

/* Define this to enable IPv6 support. */
#define WITH_IPV6 1

/* Whether we can use __attribute__ ((weak)) with GCC */
/* #undef HAVE_GCC_ATTRIBUTE_WEAK */

/* Prefer select() over poll() ? */
/* #undef ENABLE_SELECT */

/* Stack trace support for memory leaks? */
/* #undef MEMORY_LEAK_STACK_TRACE */
/* Define this to use assembler routines in sshmath library. */
/* #undef SSHMATH_ASSEMBLER_SUBROUTINES */

/* Define this to use assembler macros in sshmath library. */
/* #undef SSHMATH_ASSEMBLER_MACROS */

/* Define this to use i386 assembler routines in sshmath library. */
/* #undef SSHMATH_I386 */

/* Define this to use Digital CC V5.3 assembler inline macros in sshmath
library. */
/* #undef SSHMATH_ALPHA_DEC_CC_ASM */

/* Define this to obtain a minimal implementation of the mathematics library. 
   No library initialization is performed and modular exponentation assumes 
   an odd modulus. Routines which only are used for elliptic curves are 
   omitted. 
*/
/* #undef SSHMATH_MINIMAL */

/* Up to what bit size do we use static memory for MP integers? */
#define SSH_MP_INTEGER_BIT_SIZE_STATIC 0
/* NFAST driver */
/* #undef HAVE_NFAST */

/* SAFENET CGX driver */
/* #undef HAVE_SAFENET_CGX */

/* Enable the I386 assembler optimizations. */
/* #undef QUICKSEC_ASM_I386 */
/* Defined if we are using transform (combined) level hardware acceleration */
/* #undef SSH_IPSEC_HWACCEL_USE_COMBINED_TRANSFORM */

/* Defined if the transform (combined) level hardware acceleration performs 
   antireplay detection. */
/* #undef SSH_IPSEC_HWACCEL_DOES_ANTIREPLAY */
/* #undef HAVE_GETPASS *//* Enable the IDEA cipher. */
/* #undef WITH_IDEA */

/* Enable the RSA code. */
#define WITH_RSA 1

/* Assember code for Blowfish included. */
/* #undef ASM_BLOWFISH */

/* Assembler code for DES included. */
/* #undef ASM_DES */

/* Assembler code for ARCFOUR included. */
/* #undef ASM_ARCFOUR */

/* Assembler code for MD5 included. */
/* #undef ASM_MD5 */

/* Defined if compiled symbols are _not_ prepended with underscore `_' */
#define HAVE_NO_SYMBOL_UNDERSCORE 1
/* Define this to use the ANSI X9.17 Random Number Generator */
/* #undef WITH_ANSI_RNG */

/* Define if you have the chmod function.  */
#define HAVE_CHMOD 1

/* Define if you have the chown function.  */
#define HAVE_CHOWN 1

/* Define if you have the clock function.  */
#define HAVE_CLOCK 1

/* Define if you have the crypt function.  */
#define HAVE_CRYPT 1

/* Define if you have the ctime function.  */
#define HAVE_CTIME 1

/* Define if you have the dl function.  */
/* #undef HAVE_DL */

/* Define if you have the dladdr function.  */
#define HAVE_DLADDR 1

/* Define if you have the dlclose function.  */
#define HAVE_DLCLOSE 1

/* Define if you have the dlopen function.  */
#define HAVE_DLOPEN 1

/* Define if you have the dlsym function.  */
#define HAVE_DLSYM 1

/* Define if you have the endgrent function.  */
#define HAVE_ENDGRENT 1

/* Define if you have the endpwent function.  */
#define HAVE_ENDPWENT 1

/* Define if you have the endservent function.  */
#define HAVE_ENDSERVENT 1

/* Define if you have the fchmod function.  */
#define HAVE_FCHMOD 1

/* Define if you have the fchown function.  */
#define HAVE_FCHOWN 1

/* Define if you have the fstat function.  */
#define HAVE_FSTAT 1

/* Define if you have the fsync function.  */
#define HAVE_FSYNC 1

/* Define if you have the ftruncate function.  */
#define HAVE_FTRUNCATE 1

/* Define if you have the futimes function.  */
/* #undef HAVE_FUTIMES */

/* Define if you have the getenv function.  */
#define HAVE_GETENV 1

/* Define if you have the geteuid function.  */
#define HAVE_GETEUID 1

/* Define if you have the getgid function.  */
#define HAVE_GETGID 1

/* Define if you have the getgrgid function.  */
#define HAVE_GETGRGID 1

/* Define if you have the gethostbyname2 function.  */
/* #undef HAVE_GETHOSTBYNAME2 */

/* Define if you have the gethostname function.  */
#define HAVE_GETHOSTNAME 1

/* Define if you have the getipnodebyaddr function.  */
#define HAVE_GETIPNODEBYADDR 1

/* Define if you have the getipnodebyname function.  */
#define HAVE_GETIPNODEBYNAME 1

/* Define if you have the getopt function.  */
#define HAVE_GETOPT 1

/* Define if you have the getpagesize function.  */
#define HAVE_GETPAGESIZE 1

/* Define if you have the getpass function.  */
#define HAVE_GETPASS 1

/* Define if you have the getpgrp function.  */
#define HAVE_GETPGRP 1

/* Define if you have the getpid function.  */
#define HAVE_GETPID 1

/* Define if you have the getppid function.  */
#define HAVE_GETPPID 1

/* Define if you have the getpwuid function.  */
#define HAVE_GETPWUID 1

/* Define if you have the getrusage function.  */
#define HAVE_GETRUSAGE 1

/* Define if you have the getservbyname function.  */
#define HAVE_GETSERVBYNAME 1

/* Define if you have the getservbyport function.  */
#define HAVE_GETSERVBYPORT 1

/* Define if you have the gettimeofday function.  */
#define HAVE_GETTIMEOFDAY 1

/* Define if you have the getuid function.  */
#define HAVE_GETUID 1

/* Define if you have the inet_aton function.  */
/* #undef HAVE_INET_ATON */

/* Define if you have the key_secretkey_is_set function.  */
/* #undef HAVE_KEY_SECRETKEY_IS_SET */

/* Define if you have the loadquery function.  */
/* #undef HAVE_LOADQUERY */

/* Define if you have the localtime function.  */
#define HAVE_LOCALTIME 1

/* Define if you have the lockf function.  */
#define HAVE_LOCKF 1

/* Define if you have the lstat function.  */
#define HAVE_LSTAT 1

/* Define if you have the lutimes function.  */
/* #undef HAVE_LUTIMES */

/* Define if you have the memcpy function.  */
#define HAVE_MEMCPY 1

/* Define if you have the memmove function.  */
#define HAVE_MEMMOVE 1

/* Define if you have the memset function.  */
#define HAVE_MEMSET 1

/* Define if you have the mprotect function.  */
#define HAVE_MPROTECT 1

/* Define if you have the nanosleep function.  */
/* #undef HAVE_NANOSLEEP */

/* Define if you have the poll function.  */
#define HAVE_POLL 1

/* Define if you have the popen function.  */
#define HAVE_POPEN 1

/* Define if you have the putenv function.  */
#define HAVE_PUTENV 1

/* Define if you have the random function.  */
#define HAVE_RANDOM 1

/* Define if you have the remove function.  */
#define HAVE_REMOVE 1

/* Define if you have the select function.  */
#define HAVE_SELECT 1

/* Define if you have the setsid function.  */
#define HAVE_SETSID 1

/* Define if you have the shl_get function.  */
/* #undef HAVE_SHL_GET */

/* Define if you have the signal function.  */
#define HAVE_SIGNAL 1

/* Define if you have the sleep function.  */
#define HAVE_SLEEP 1

/* Define if you have the strcasecmp function.  */
#define HAVE_STRCASECMP 1

/* Define if you have the strchr function.  */
#define HAVE_STRCHR 1

/* Define if you have the strerror function.  */
#define HAVE_STRERROR 1

/* Define if you have the strncasecmp function.  */
#define HAVE_STRNCASECMP 1

/* Define if you have the times function.  */
#define HAVE_TIMES 1

/* Define if you have the truncate function.  */
#define HAVE_TRUNCATE 1

/* Define if you have the uname function.  */
#define HAVE_UNAME 1

/* Define if you have the usleep function.  */
#define HAVE_USLEEP 1

/* Define if you have the utime function.  */
#define HAVE_UTIME 1

/* Define if you have the utimes function.  */
#define HAVE_UTIMES 1

/* Define if you have the <dirent.h> header file.  */
#define HAVE_DIRENT_H 1

/* Define if you have the <dl.h> header file.  */
/* #undef HAVE_DL_H */

/* Define if you have the <dlfcn.h> header file.  */
#define HAVE_DLFCN_H 1

/* Define if you have the <endian.h> header file.  */
/* #undef HAVE_ENDIAN_H */

/* Define if you have the <grp.h> header file.  */
#define HAVE_GRP_H 1

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H 1

/* Define if you have the <machine/endian.h> header file.  */
/* #undef HAVE_MACHINE_ENDIAN_H */

/* Define if you have the <machine/spl.h> header file.  */
/* #undef HAVE_MACHINE_SPL_H */

/* Define if you have the <ndir.h> header file.  */
/* #undef HAVE_NDIR_H */

/* Define if you have the <netinet/in.h> header file.  */
#define HAVE_NETINET_IN_H 1

/* Define if you have the <netinet/in_systm.h> header file.  */
#define HAVE_NETINET_IN_SYSTM_H 1

/* Define if you have the <paths.h> header file.  */
#define HAVE_PATHS_H 1

/* Define if you have the <pwd.h> header file.  */
#define HAVE_PWD_H 1

/* Define if you have the <rusage.h> header file.  */
/* #undef HAVE_RUSAGE_H */

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <sys/callout.h> header file.  */
/* #undef HAVE_SYS_CALLOUT_H */

/* Define if you have the <sys/dir.h> header file.  */
/* #undef HAVE_SYS_DIR_H */

/* Define if you have the <sys/ioctl.h> header file.  */
#define HAVE_SYS_IOCTL_H 1

/* Define if you have the <sys/ldr.h> header file.  */
/* #undef HAVE_SYS_LDR_H */

/* Define if you have the <sys/modem.h> header file.  */
/* #undef HAVE_SYS_MODEM_H */

/* Define if you have the <sys/ndir.h> header file.  */
/* #undef HAVE_SYS_NDIR_H */

/* Define if you have the <sys/poll.h> header file.  */
#define HAVE_SYS_POLL_H 1

/* Define if you have the <sys/select.h> header file.  */
#define HAVE_SYS_SELECT_H 1

/* Define if you have the <sys/stat.h> header file.  */
#define HAVE_SYS_STAT_H 1

/* Define if you have the <sys/time.h> header file.  */
#define HAVE_SYS_TIME_H 1

/* Define if you have the <sys/types.h> header file.  */
#define HAVE_SYS_TYPES_H 1

/* Define if you have the <sys/un.h> header file.  */
#define HAVE_SYS_UN_H 1

/* Define if you have the <sys/utsname.h> header file.  */
#define HAVE_SYS_UTSNAME_H 1

/* Define if you have the <termcap.h> header file.  */
/* #undef HAVE_TERMCAP_H */

/* Define if you have the <termios.h> header file.  */
#define HAVE_TERMIOS_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the crypt library (-lcrypt).  */
/* #undef HAVE_LIBCRYPT */

/* Define if you have the dl library (-ldl).  */
#define HAVE_LIBDL 1

/* Define if you have the exc library (-lexc).  */
/* #undef HAVE_LIBEXC */

/* Define if you have the inet library (-linet).  */
/* #undef HAVE_LIBINET */

/* Define if you have the nsl library (-lnsl).  */
#define HAVE_LIBNSL 1

/* Define if you have the socket library (-lsocket).  */
#define HAVE_LIBSOCKET 1

/* Name of package */
#define PACKAGE "sshtree"

/* Version number of package */
#define VERSION "1.0"

#endif /* SSHCONF_H */
