/*
 * Copyright 2001-2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#pragma ident	"%Z%%M%	%I%	%E% SMI"


/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation. Portions created by Netscape are
 * Copyright (C) 1998-1999 Netscape Communications Corporation. All
 * Rights Reserved.
 */

/*
 * Copyright (c) 1990 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */
/* lbet-int.h - internal header file for liblber */

#ifndef _LBERINT_H
#define _LBERINT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#ifdef LDAP_SASLIO_HOOKS
#include <sasl/sasl.h>
#endif

#ifdef macintosh
# include "ldap-macos.h"
#else /* macintosh */
#if !defined(BSDI)
# include <malloc.h>
#endif
# include <errno.h>
# include <sys/types.h>
#if defined(SUNOS4) || defined(SCOOS)
# include <sys/time.h>
#endif
#if defined( _WINDOWS )
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <time.h>
/* No stderr in a 16-bit Windows DLL */
#  if defined(_WINDLL) && !defined(_WIN32)
#    define USE_DBG_WIN
#  endif
# else
#if !defined(XP_OS2)
/* #  include <sys/varargs.h> */
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#endif
# endif /* defined( _WINDOWS ) */
#endif /* macintosh */

#include <memory.h>
#include <string.h>
#include "portable.h"

#ifdef _WINDOWS
#include <winsock.h>
#include <io.h>
#endif /* _WINDOWS */

#ifdef XP_OS2
#include <os2sock.h>
#include <io.h>
#endif /* XP_OS2 */

/* No stderr in a 16-bit Windows DLL */
#if defined(_WINDLL) && !defined(_WIN32)
#define stderr NULL
#endif

#include "lber.h"

#ifdef _SOLARIS_SDK
#include <libintl.h>
#include "solaris-int.h"
#endif

#ifdef macintosh
#define NSLDAPI_LBER_SOCKET_IS_PTR
#endif

#define OLD_LBER_SEQUENCE	0x10	/* w/o constructed bit - broken */
#define OLD_LBER_SET		0x11	/* w/o constructed bit - broken */

#ifndef _IFP
#define _IFP
typedef int (LDAP_C LDAP_CALLBACK *IFP)();
#endif

typedef struct seqorset {
	ber_len_t	sos_clen;
	ber_tag_t	sos_tag;
	char		*sos_first;
	char		*sos_ptr;
	struct seqorset	*sos_next;
} Seqorset;
#define NULLSEQORSET	((Seqorset *) 0)

#define SOS_STACK_SIZE 8 /* depth of the pre-allocated sos structure stack */

struct berelement {
	char		*ber_buf;
	char		*ber_ptr;
	char		*ber_end;
	struct seqorset	*ber_sos;
	ber_tag_t	ber_tag;
	ber_len_t	ber_len;
	int		ber_usertag;
	char		ber_options;
	char		*ber_rwptr;
	BERTranslateProc ber_encode_translate_proc;
	BERTranslateProc ber_decode_translate_proc;
	int		ber_flags;
#define LBER_FLAG_NO_FREE_BUFFER	1	/* don't free ber_buf */
	int		ber_sos_stack_posn;
	Seqorset	ber_sos_stack[SOS_STACK_SIZE];
};

#ifndef _SOLARIS_SDK
#define NULLBER ((BerElement *)NULL)
#endif

#ifdef LDAP_DEBUG
void ber_dump( BerElement *ber, int inout );
#endif



/*
 * structure for read/write I/O callback functions.
 */
struct nslberi_io_fns {
    LDAP_IOF_READ_CALLBACK	*lbiof_read;
    LDAP_IOF_WRITE_CALLBACK	*lbiof_write;
};


struct sockbuf {
	LBER_SOCKET	sb_sd;
	BerElement	sb_ber;
	int		sb_naddr;	/* > 0 implies using CLDAP (UDP) */
	void		*sb_useaddr;	/* pointer to sockaddr to use next */
	void		*sb_fromaddr;	/* pointer to message source sockaddr */
	void		**sb_addrs;	/* actually an array of pointers to
					   sockaddrs */

	int		sb_options;	/* to support copying ber elements */
	LBER_SOCKET	sb_copyfd;	/* for LBER_SOCKBUF_OPT_TO_FILE* opts */
	ber_uint_t	sb_max_incoming;

	struct nslberi_io_fns
			sb_io_fns;	/* classic I/O callback functions */

	struct lber_x_ext_io_fns
			sb_ext_io_fns;	/* extended I/O callback functions */
#ifdef LDAP_SASLIO_HOOKS
        sasl_conn_t     *sb_sasl_ctx;   /* pointer to sasl context */
        char            *sb_sasl_ibuf;  /* sasl decrypted input buffer */
        char            *sb_sasl_iptr;  /* current location in buffer */
        int             sb_sasl_bfsz;   /* Alloc'd size of input buffer */
        int             sb_sasl_ilen;   /* remaining length to process */
        struct lber_x_ext_io_fns
                        sb_sasl_fns;    /* sasl redirect copy ext I/O funcs */
        void            *sb_sasl_prld;  /* reverse ld pointer for callbacks */
#endif
};
#define NULLSOCKBUF	((Sockbuf *)NULL)


#ifndef NSLBERI_LBER_INT_FRIEND
/*
 * Everything from this point on is excluded if NSLBERI_LBER_INT_FRIEND is
 * defined.  The code under ../libraries/libldap defines this.
 */

#define READBUFSIZ	8192

/*
 * macros used to check validity of data structures and parameters
 */
#define NSLBERI_VALID_BERELEMENT_POINTER( ber ) \
	( (ber) != NULLBER )

#define NSLBERI_VALID_SOCKBUF_POINTER( sb ) \
	( (sb) != NULLSOCKBUF )


#if defined(_WIN32) && defined(_ALPHA)
#define LBER_HTONL( _l ) \
	    ((((_l)&0xff)<<24) + (((_l)&0xff00)<<8) + \
	     (((_l)&0xff0000)>>8) + (((_l)&0xff000000)>>24))
#define LBER_NTOHL(_l) LBER_HTONL(_l)

#elif !defined(__alpha) || defined(VMS)

#define LBER_HTONL( l )	htonl( l )
#define LBER_NTOHL( l )	ntohl( l )

#else /* __alpha */
/*
 * htonl and ntohl on the DEC Alpha under OSF 1 seem to only swap the
 * lower-order 32-bits of a (64-bit) long, so we define correct versions
 * here.
 */
#define LBER_HTONL( l )	(((long)htonl( (l) & 0x00000000FFFFFFFF )) << 32 \
    			| htonl( ( (l) & 0xFFFFFFFF00000000 ) >> 32 ))

#define LBER_NTOHL( l )	(((long)ntohl( (l) & 0x00000000FFFFFFFF )) << 32 \
    			| ntohl( ( (l) & 0xFFFFFFFF00000000 ) >> 32 ))
#endif /* __alpha */


/* function prototypes */
#ifdef LDAP_DEBUG
void lber_bprint( char *data, int len );
#endif
void ber_err_print( char *data );
void *nslberi_malloc( size_t size );
void *nslberi_calloc( size_t nelem, size_t elsize );
void *nslberi_realloc( void *ptr, size_t size );
void nslberi_free( void *ptr );
int nslberi_ber_realloc( BerElement *ber, ber_len_t len );



/* blame: dboreham 
 * slapd spends much of its time doing memcpy's for the ber code.
 * Most of these are single-byte, so we special-case those and speed
 * things up considerably. 
 */

#ifdef sunos4
#define THEMEMCPY( d, s, n )	bcopy( s, d, n )
#else /* sunos4 */
#define THEMEMCPY( d, s, n )	memmove( d, s, n )
#endif /* sunos4 */

#ifdef SAFEMEMCPY
#undef SAFEMEMCPY
#define SAFEMEMCPY(d,s,n) if (1 == n) *((char*)d) = *((char*)s); else THEMEMCPY(d,s,n);
#endif

/*
 * Memory allocation done in liblber should all go through one of the
 * following macros. This is so we can plug-in alternative memory
 * allocators, etc. as the need arises.
 */
#define NSLBERI_MALLOC( size )		nslberi_malloc( size )
#define NSLBERI_CALLOC( nelem, elsize )	nslberi_calloc( nelem, elsize )
#define NSLBERI_REALLOC( ptr, size )	nslberi_realloc( ptr, size )
#define NSLBERI_FREE( ptr )		nslberi_free( ptr )

/* allow the library to access the debug variable */

extern int lber_debug;

#endif /* !NSLBERI_LBER_INT_FRIEND */


#ifdef __cplusplus
}
#endif
#endif /* _LBERINT_H */
