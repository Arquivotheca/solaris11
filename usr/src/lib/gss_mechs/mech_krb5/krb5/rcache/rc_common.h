/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_KRB5_RC_COM_H
#define	_KRB5_RC_COM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "k5-int.h"

/*
 * This file of the Kerberos V5 software is derived from public-domain code
 * contributed by Daniel J. Bernstein, <brnstnd@acf10.nyu.edu>.
 *
 */

#ifndef HASHSIZE
#define HASHSIZE 997 /* a convenient prime */
#endif

#ifndef EXCESSREPS
#define EXCESSREPS 30
#endif

#define CMP_MALLOC -3
#define CMP_EXPIRED -2
#define CMP_REPLAY -1
#define CMP_HOHUM 0

struct authlist
{
    krb5_donot_replay rep;
    struct authlist *na;
    struct authlist *nh;
};

unsigned int
hash(krb5_donot_replay *, unsigned int);

int
cmp(krb5_donot_replay *, krb5_donot_replay *, krb5_deltat);

int
alive(krb5_int32, krb5_donot_replay *, krb5_deltat);

#ifdef __cplusplus
}
#endif

#endif /* !_KRB5_RC_COM_H */
