/*
 *
 * Author: Tero Kivinen <kivinen@iki.fi>
 *
*  Copyright:
*          Copyright (c) 2002, 2003 SFNT Finland Oy.
 */
/*
 *        Program: sshisakmp
 *        $Source: /project/cvs/src/ipsec/lib/sshisakmp/isakmp_cookie.c,v $
 *        $Author: irwin $
 *        $Author: irwin $
 *
 *        Creation          : 14:48 Jul 30 1997 kivinen
 *        Last Modification : 20:47 Mar  5 2002 kivinen
 *        Last check in     : $Date: 2003/11/28 20:00:47 $
 *        Revision number   : $Revision: 1.1.6.1 $
 *        State             : $State: Exp $
 *        Version           : 1.96
 *        
 *
 *        Description       : Isakmp anti-cloggin token (cookie) module
 *
 *        $Log: isakmp_cookie.c,v $ *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        $EndLog$
 */

#include "sshincludes.h"
#include "isakmp.h"
#include "isakmp_internal.h"
#include "sshdebug.h"
#include "sshtimeouts.h"

#define SSH_IKE_TIME_LEN                3

#define SSH_DEBUG_MODULE "SshIkeCookie"

/*                                                              shade{0.9}
 * Create isakmp cookie. Generate completely random
 * cookie, as checking the cookie from the hash table is
 * about as fast or faster than hashing stuff together.
 * This also makes cookies movable against multiple machines
 * (high availability or checkpointing systems).
 * The return_buffer must be SSH_IKE_COOKIE_LENGTH
 * bytes long.                                                  shade{1.0}
 */
void ike_cookie_create(SshIkeContext isakmp_context,
                       unsigned char *cookie)
{
  int i;

  for (i = 0; i < SSH_IKE_COOKIE_LENGTH; i++)
    cookie[i] = ssh_random_get_byte();
}
