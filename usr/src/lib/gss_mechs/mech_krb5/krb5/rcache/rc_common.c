/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file of the Kerberos V5 software is derived from public-domain code
 * contributed by Daniel J. Bernstein, <brnstnd@acf10.nyu.edu>.
 *
 */

#include "rc_common.h"

unsigned int
hash(krb5_donot_replay *rep, unsigned int hsize)
{
    unsigned int h = rep->cusec + rep->ctime;
    h += *rep->server;
    h += *rep->client;
    return h % hsize;
}

/*ARGSUSED*/
int
cmp(krb5_donot_replay *old, krb5_donot_replay *new1, krb5_deltat t)
{
    if ((old->cusec == new1->cusec) && /* most likely to distinguish */
        (old->ctime == new1->ctime) &&
        (strcmp(old->client, new1->client) == 0) &&
        (strcmp(old->server, new1->server) == 0)) { /* always true */
        /* If both records include message hashes, compare them as well. */
        if (old->msghash == NULL || new1->msghash == NULL ||
            strcmp(old->msghash, new1->msghash) == 0)
            return CMP_REPLAY;
    }
    return CMP_HOHUM;
}

int
alive(krb5_int32 mytime, krb5_donot_replay *new1, krb5_deltat t)
{
    if (mytime == 0)
        return CMP_HOHUM; /* who cares? */
    /* I hope we don't have to worry about overflow */
    if (new1->ctime + t < mytime)
        return CMP_EXPIRED;
    return CMP_HOHUM;
}

