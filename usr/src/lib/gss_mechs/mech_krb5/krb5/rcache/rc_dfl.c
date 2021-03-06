/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * lib/krb5/rcache/rc_dfl.c
 *
 * This file of the Kerberos V5 software is derived from public-domain code
 * contributed by Daniel J. Bernstein, <brnstnd@acf10.nyu.edu>.
 *
 */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * An implementation for the default replay cache type.
 */
#include "rc_base.h"
#include "rc_dfl.h"
#include "rc_io.h"
#include "k5-int.h"
#include "rc-int.h"

/* Solaris Kerberos */
#include <kstat.h>
#include <atomic.h>
#include <assert.h>
#include <syslog.h>

/*
 * If NOIOSTUFF is defined at compile time, dfl rcaches will be per-process.
 */

/*
  Local stuff:

  static int hash(krb5_donot_replay *rep, int hsize)
  returns hash value of *rep, between 0 and hsize - 1
  HASHSIZE
  size of hash table (constant), can be preset
  static int cmp(krb5_donot_replay *old, krb5_donot_replay *new, krb5_deltat t)
  compare old and new; return CMP_REPLAY or CMP_HOHUM
  static int alive(krb5_context, krb5_donot_replay *new, krb5_deltat t)
  see if new is still alive; return CMP_EXPIRED or CMP_HOHUM
  CMP_MALLOC, CMP_EXPIRED, CMP_REPLAY, CMP_HOHUM
  return codes from cmp(), alive(), and store()
  struct dfl_data
  data stored in this cache type, namely "dfl"
  struct authlist
  multilinked list of reps
  static int rc_store(context, krb5_rcache id, krb5_donot_replay *rep)
  store rep in cache id; return CMP_REPLAY if replay, else CMP_MALLOC/CMP_HOHUM

*/

/*
 * Solaris Kerberos
 * The following code is shared between the file and memory replay cache
 * implementations and lives in rc_common.[ch].
 */
#include "rc_common.h"
#if 0

#ifndef HASHSIZE
#define HASHSIZE 997 /* a convenient prime */
#endif

#ifndef EXCESSREPS
#define EXCESSREPS 30
#endif

/*
 * The rcache will be automatically expunged when the number of
 * expired krb5_donot_replays encountered incidentally in searching
 * exceeds the number of live krb5_donot_replays by EXCESSREPS. With
 * the defaults here, a typical cache might build up some 10K of
 * expired krb5_donot_replays before an automatic expunge, with the
 * waste basically independent of the number of stores per minute.
 *
 * The rcache will also automatically be expunged when it encounters
 * more than EXCESSREPS expired entries when recovering a cache in
 * dfl_recover.
 */

static unsigned int
hash(krb5_donot_replay *rep, unsigned int hsize)
{
    unsigned int h = rep->cusec + rep->ctime;
    h += *rep->server;
    h += *rep->client;
    return h % hsize;
}

#define CMP_MALLOC -3
#define CMP_EXPIRED -2
#define CMP_REPLAY -1
#define CMP_HOHUM 0

/*ARGSUSED*/
static int
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

static int
alive(krb5_int32 mytime, krb5_donot_replay *new1, krb5_deltat t)
{
    if (mytime == 0)
        return CMP_HOHUM; /* who cares? */
    /* I hope we don't have to worry about overflow */
    if (new1->ctime + t < mytime)
        return CMP_EXPIRED;
    return CMP_HOHUM;
}

#endif

struct dfl_data
{
    char *name;
    krb5_deltat lifespan;
    unsigned int hsize;
    int numhits;
    int nummisses;
    struct authlist **h;
    struct authlist *a;
#ifndef NOIOSTUFF
    krb5_rc_iostuff d;
#endif
    char recovering;
};

/* Solaris Kerberos - Moved to rc_common.h */
#if 0
struct authlist
{
    krb5_donot_replay rep;
    struct authlist *na;
    struct authlist *nh;
};
#endif

/* of course, list is backwards from file */
/* hash could be forwards since we have to search on match, but naaaah */

static int
rc_store(krb5_context context, krb5_rcache id, krb5_donot_replay *rep,
         krb5_int32 now, krb5_boolean fromfile)
{
    struct dfl_data *t = (struct dfl_data *)id->data;
    unsigned int rephash;
    struct authlist *ta;

    /*
     * Solaris Kerberos:
     * calling alive() on rep since it doesn't make sense to store an
     * expired replay.
     */
    if (alive(now, rep, t->lifespan) == CMP_EXPIRED){
        return CMP_EXPIRED;
    }

    rephash = hash(rep, t->hsize);

    for (ta = t->h[rephash]; ta; ta = ta->nh) {
        switch(cmp(&ta->rep, rep, t->lifespan))
        {
        case CMP_REPLAY:
            if (fromfile) {
                /*
                 * This is an expected collision between a hash
                 * extension record and a normal-format record.  Make
                 * sure the message hash is included in the stored
                 * record and carry on.
                 */
                if (!ta->rep.msghash && rep->msghash) {
                    if (!(ta->rep.msghash = strdup(rep->msghash)))
                        return CMP_MALLOC;
                }
                return CMP_HOHUM;
            } else
                return CMP_REPLAY;
        case CMP_HOHUM:
            if (alive(now, &ta->rep, t->lifespan) == CMP_EXPIRED)
                t->nummisses++;
            else
                t->numhits++;
            break;
        default:
            ; /* wtf? */
        }
    }

    if (!(ta = (struct authlist *) malloc(sizeof(struct authlist))))
        return CMP_MALLOC;
    ta->rep = *rep;
    ta->rep.client = ta->rep.server = ta->rep.msghash = NULL;
    if (!(ta->rep.client = strdup(rep->client)))
        goto error;
    if (!(ta->rep.server = strdup(rep->server)))
        goto error;
    if (rep->msghash && !(ta->rep.msghash = strdup(rep->msghash)))
        goto error;
    ta->na = t->a; t->a = ta;
    ta->nh = t->h[rephash]; t->h[rephash] = ta;
    return CMP_HOHUM;
error:
    if (ta->rep.client)
        free(ta->rep.client);
    if (ta->rep.server)
        free(ta->rep.server);
    if (ta->rep.msghash)
        free(ta->rep.msghash);
    free(ta);
    return CMP_MALLOC;
}

char * KRB5_CALLCONV
krb5_rc_dfl_get_name(krb5_context context, krb5_rcache id)
{
    return ((struct dfl_data *) (id->data))->name;
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_get_span(krb5_context context, krb5_rcache id,
                     krb5_deltat *lifespan)
{
    krb5_error_code err;
    struct dfl_data *t;

    err = k5_mutex_lock(&id->lock);
    if (err)
        return err;
    t = (struct dfl_data *) id->data;
    *lifespan = t->lifespan;
    k5_mutex_unlock(&id->lock);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_init_locked(krb5_context context, krb5_rcache id, krb5_deltat lifespan)
{
    struct dfl_data *t = (struct dfl_data *)id->data;
    krb5_error_code retval;

    t->lifespan = lifespan ? lifespan : context->clockskew;
    /* default to clockskew from the context */
#ifndef NOIOSTUFF
    if ((retval = krb5_rc_io_creat(context, &t->d, &t->name))) {
        return retval;
    }
    if ((krb5_rc_io_write(context, &t->d,
                          (krb5_pointer) &t->lifespan, sizeof(t->lifespan))
         || krb5_rc_io_sync(context, &t->d))) {
        return KRB5_RC_IO;
    }
#endif
    return 0;
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_init(krb5_context context, krb5_rcache id, krb5_deltat lifespan)
{
    krb5_error_code retval;

    retval = k5_mutex_lock(&id->lock);
    if (retval)
        return retval;
    retval = krb5_rc_dfl_init_locked(context, id, lifespan);
    k5_mutex_unlock(&id->lock);
    return retval;
}

/* Called with the mutex already locked.  */
krb5_error_code
krb5_rc_dfl_close_no_free(krb5_context context, krb5_rcache id)
{
    struct dfl_data *t = (struct dfl_data *)id->data;
    struct authlist *q;

    free(t->h);
    if (t->name)
        free(t->name);
    while ((q = t->a))
    {
        t->a = q->na;
        free(q->rep.client);
        free(q->rep.server);
        if (q->rep.msghash)
            free(q->rep.msghash);
        free(q);
    }
#ifndef NOIOSTUFF
    (void) krb5_rc_io_close(context, &t->d);
#endif
    free(t);
    /* Solaris Kerberos */
    id->data = NULL;
    return 0;
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_close(krb5_context context, krb5_rcache id)
{
    krb5_error_code retval;
    retval = k5_mutex_lock(&id->lock);
    if (retval)
        return retval;
    krb5_rc_dfl_close_no_free(context, id);
    k5_mutex_unlock(&id->lock);
    k5_mutex_destroy(&id->lock);
    free(id);
    return 0;
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_destroy(krb5_context context, krb5_rcache id)
{
#ifndef NOIOSTUFF
    if (krb5_rc_io_destroy(context, &((struct dfl_data *) (id->data))->d))
        return KRB5_RC_IO;
#endif
    return krb5_rc_dfl_close(context, id);
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_resolve(krb5_context context, krb5_rcache id, char *name)
{
    struct dfl_data *t = 0;
    krb5_error_code retval;

    /* allocate id? no */
    if (!(t = (struct dfl_data *) calloc(1, sizeof(struct dfl_data))))
        return KRB5_RC_MALLOC;
    id->data = (krb5_pointer) t;
    if (name) {
        t->name = strdup(name);
        if (!t->name) {
            retval = KRB5_RC_MALLOC;
            goto cleanup;
        }
    } else
        t->name = 0;
    t->numhits = t->nummisses = 0;
    t->hsize = HASHSIZE; /* no need to store---it's memory-only */
    t->h = (struct authlist **) malloc(t->hsize*sizeof(struct authlist *));
    if (!t->h) {
        retval = KRB5_RC_MALLOC;
        goto cleanup;
    }
    memset(t->h, 0, t->hsize*sizeof(struct authlist *));
    t->a = (struct authlist *) 0;
#ifndef NOIOSTUFF
    t->d.fd = -1;
#endif
    t->recovering = 0;
    return 0;

cleanup:
    if (t) {
        if (t->name)
            free(t->name);
        if (t->h)
            free(t->h);
        free(t);
        /* Solaris Kerberos */
        id->data = NULL;
    }
    return retval;
}

void
krb5_rc_free_entry(krb5_context context, krb5_donot_replay **rep)
{
    krb5_donot_replay *rp = *rep;

    *rep = NULL;
    if (rp)
    {
        if (rp->client)
            free(rp->client);
        if (rp->server)
            free(rp->server);
        if (rp->msghash)
            free(rp->msghash);
        rp->client = NULL;
        rp->server = NULL;
        rp->msghash = NULL;
        free(rp);
    }
}

/*
 * Parse a string in the format <len>:<data>, with the length
 * represented in ASCII decimal.  On parse failure, return 0 but set
 * *result to NULL.
 */
static krb5_error_code
parse_counted_string(char **strptr, char **result)
{
    char *str = *strptr, *end;
    unsigned long len;

    *result = NULL;

    /* Parse the length, expecting a ':' afterwards. */
    errno = 0;
    len = strtoul(str, &end, 10);
    if (errno != 0 || *end != ':' || len > strlen(end + 1))
        return 0;

    /* Allocate space for *result and copy the data. */
    *result = malloc(len + 1);
    if (!*result)
        return KRB5_RC_MALLOC;
    memcpy(*result, end + 1, len);
    (*result)[len] = '\0';
    *strptr = end + 1 + len;
    return 0;
}

/*
 * Hash extension records have the format:
 *  client = <empty string>
 *  server = HASH:<msghash> <clientlen>:<client> <serverlen>:<server>
 * Spaces in the client and server string are represented with
 * with backslashes.  Client and server lengths are represented in
 * ASCII decimal (which is different from the 32-bit binary we use
 * elsewhere in the replay cache).
 *
 * On parse failure, we leave the record unmodified.
 */
static krb5_error_code
check_hash_extension(krb5_donot_replay *rep)
{
    char *msghash = NULL, *client = NULL, *server = NULL, *str, *end;
    krb5_error_code retval = 0;

    /* Check if this appears to match the hash extension format. */
    if (*rep->client)
        return 0;
    if (strncmp(rep->server, "HASH:", 5) != 0)
        return 0;

    /* Parse out the message hash. */
    str = rep->server + 5;
    end = strchr(str, ' ');
    if (!end)
        return 0;
    msghash = malloc(end - str + 1);
    if (!msghash)
        return KRB5_RC_MALLOC;
    memcpy(msghash, str, end - str);
    msghash[end - str] = '\0';
    str = end + 1;

    /* Parse out the client and server. */
    retval = parse_counted_string(&str, &client);
    if (retval != 0 || client == NULL)
        goto error;
    if (*str != ' ')
        goto error;
    str++;
    retval = parse_counted_string(&str, &server);
    if (retval != 0 || server == NULL)
        goto error;
    if (*str)
        goto error;

    free(rep->client);
    free(rep->server);
    rep->client = client;
    rep->server = server;
    rep->msghash = msghash;
    return 0;

error:
    if (msghash)
        free(msghash);
    if (client)
        free(client);
    if (server)
        free(server);
    return retval;
}

static krb5_error_code
krb5_rc_io_fetch(krb5_context context, struct dfl_data *t,
                 krb5_donot_replay *rep, int maxlen)
{
    int len2;
    unsigned int len;
    krb5_error_code retval;

    rep->client = rep->server = rep->msghash = NULL;

    retval = krb5_rc_io_read(context, &t->d, (krb5_pointer) &len2,
                             sizeof(len2));
    if (retval)
        return retval;

    if ((len2 <= 0) || (len2 >= maxlen))
        return KRB5_RC_IO_EOF;

    len = len2;
    rep->client = malloc (len);
    if (!rep->client)
        return KRB5_RC_MALLOC;

    retval = krb5_rc_io_read(context, &t->d, (krb5_pointer) rep->client, len);
    if (retval)
        goto errout;

    retval = krb5_rc_io_read(context, &t->d, (krb5_pointer) &len2,
                             sizeof(len2));
    if (retval)
        goto errout;

    if ((len2 <= 0) || (len2 >= maxlen)) {
        retval = KRB5_RC_IO_EOF;
        goto errout;
    }
    len = len2;

    rep->server = malloc (len);
    if (!rep->server) {
        retval = KRB5_RC_MALLOC;
        goto errout;
    }

    retval = krb5_rc_io_read(context, &t->d, (krb5_pointer) rep->server, len);
    if (retval)
        goto errout;

    retval = krb5_rc_io_read(context, &t->d, (krb5_pointer) &rep->cusec,
                             sizeof(rep->cusec));
    if (retval)
        goto errout;

    retval = krb5_rc_io_read(context, &t->d, (krb5_pointer) &rep->ctime,
                             sizeof(rep->ctime));
    if (retval)
        goto errout;

    retval = check_hash_extension(rep);
    if (retval)
        goto errout;

    return 0;

errout:
    if (rep->client)
        free(rep->client);
    if (rep->server)
        free(rep->server);
    if (rep->msghash)
        free(rep->msghash);
    rep->client = rep->server = 0;
    return retval;
}


static krb5_error_code
krb5_rc_dfl_expunge_locked(krb5_context context, krb5_rcache id);

static krb5_error_code
krb5_rc_dfl_recover_locked(krb5_context context, krb5_rcache id)
{
#ifdef NOIOSTUFF
    return KRB5_RC_NOIO;
#else

    struct dfl_data *t = (struct dfl_data *)id->data;
    krb5_donot_replay *rep = 0;
    krb5_error_code retval;
    long max_size;
    int expired_entries = 0;
    krb5_int32 now;

    if ((retval = krb5_rc_io_open(context, &t->d, t->name))) {
        return retval;
    }

    t->recovering = 1;

    max_size = krb5_rc_io_size(context, &t->d);

    rep = NULL;
    if (krb5_rc_io_read(context, &t->d, (krb5_pointer) &t->lifespan,
                        sizeof(t->lifespan))) {
        retval = KRB5_RC_IO;
        goto io_fail;
    }

    if (!(rep = (krb5_donot_replay *) malloc(sizeof(krb5_donot_replay)))) {
        retval = KRB5_RC_MALLOC;
        goto io_fail;
    }
    rep->client = rep->server = rep->msghash = NULL;

    if (krb5_timeofday(context, &now))
        now = 0;

    /* now read in each auth_replay and insert into table */
    for (;;) {
        if (krb5_rc_io_mark(context, &t->d)) {
            retval = KRB5_RC_IO;
            goto io_fail;
        }

        retval = krb5_rc_io_fetch(context, t, rep, (int) max_size);

        if (retval == KRB5_RC_IO_EOF)
            break;
        else if (retval != 0)
            goto io_fail;

        if (alive(now, rep, t->lifespan) != CMP_EXPIRED) {
            if (rc_store(context, id, rep, now, TRUE) == CMP_MALLOC) {
                retval = KRB5_RC_MALLOC; goto io_fail;
            }
        } else {
            expired_entries++;
        }

        /*
         *  free fields allocated by rc_io_fetch
         */
        free(rep->server);
        free(rep->client);
        if (rep->msghash)
            free(rep->msghash);
        rep->client = rep->server = rep->msghash = NULL;
    }
    retval = 0;
    krb5_rc_io_unmark(context, &t->d);
    /*
     *  An automatic expunge here could remove the need for
     *  mark/unmark but that would be inefficient.
     */
io_fail:
    krb5_rc_free_entry(context, &rep);
    if (retval)
        krb5_rc_io_close(context, &t->d);
    else if (expired_entries > EXCESSREPS)
        retval = krb5_rc_dfl_expunge_locked(context, id);
    t->recovering = 0;
    return retval;

#endif
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_recover(krb5_context context, krb5_rcache id)
{
    krb5_error_code ret;
    ret = k5_mutex_lock(&id->lock);
    if (ret)
        return ret;
    ret = krb5_rc_dfl_recover_locked(context, id);
    k5_mutex_unlock(&id->lock);
    return ret;
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_recover_or_init(krb5_context context, krb5_rcache id,
                            krb5_deltat lifespan)
{
    krb5_error_code retval;

    retval = k5_mutex_lock(&id->lock);
    if (retval)
        return retval;
    retval = krb5_rc_dfl_recover_locked(context, id);
    if (retval)
        retval = krb5_rc_dfl_init_locked(context, id, lifespan);
    k5_mutex_unlock(&id->lock);
    return retval;
}

static krb5_error_code
krb5_rc_io_store(krb5_context context, struct dfl_data *t,
                 krb5_donot_replay *rep)
{
    size_t clientlen, serverlen;
    ssize_t buflen;
    unsigned int len;
    krb5_error_code ret;
    struct k5buf buf, extbuf;
    char *bufptr, *extstr;

    clientlen = strlen(rep->client);
    serverlen = strlen(rep->server);

    if (rep->msghash) {
        /*
         * Write a hash extension record, to be followed by a record
         * in regular format (without the message hash) for the
         * benefit of old implementations.
         */

        /* Format the extension value so we know its length. */
        krb5int_buf_init_dynamic(&extbuf);
        krb5int_buf_add_fmt(&extbuf, "HASH:%s %lu:%s %lu:%s", rep->msghash,
                            (unsigned long) clientlen, rep->client,
                            (unsigned long) serverlen, rep->server);
        extstr = krb5int_buf_data(&extbuf);
        if (!extstr)
            return KRB5_RC_MALLOC;

        /*
         * Put the extension value into the server field of a
         * regular-format record, with an empty client field.
         */
        krb5int_buf_init_dynamic(&buf);
        len = 1;
        krb5int_buf_add_len(&buf, (char *) &len, sizeof(len));
        krb5int_buf_add_len(&buf, "", 1);
        len = strlen(extstr) + 1;
        krb5int_buf_add_len(&buf, (char *) &len, sizeof(len));
        krb5int_buf_add_len(&buf, extstr, len);
        krb5int_buf_add_len(&buf, (char *) &rep->cusec, sizeof(rep->cusec));
        krb5int_buf_add_len(&buf, (char *) &rep->ctime, sizeof(rep->ctime));
        free(extstr);
    } else  /* No extension record needed. */
        krb5int_buf_init_dynamic(&buf);

    len = clientlen + 1;
    krb5int_buf_add_len(&buf, (char *) &len, sizeof(len));
    krb5int_buf_add_len(&buf, rep->client, len);
    len = serverlen + 1;
    krb5int_buf_add_len(&buf, (char *) &len, sizeof(len));
    krb5int_buf_add_len(&buf, rep->server, len);
    krb5int_buf_add_len(&buf, (char *) &rep->cusec, sizeof(rep->cusec));
    krb5int_buf_add_len(&buf, (char *) &rep->ctime, sizeof(rep->ctime));

    bufptr = krb5int_buf_data(&buf);
    buflen = krb5int_buf_len(&buf);
    if (bufptr == NULL || buflen < 0)
        return KRB5_RC_MALLOC;

    ret = krb5_rc_io_write(context, &t->d, bufptr, buflen);
    krb5int_free_buf(&buf);
    return ret;
}

static krb5_error_code krb5_rc_dfl_expunge_locked(krb5_context, krb5_rcache);


/*
 * Solaris Kerberos
 *
 * Get time of boot.  This is needed for fsync()-less operation.  See below.
 *
 * Cstyle note: MIT style used here.
 */
static
krb5_timestamp
get_boot_time(krb5_timestamp now)
{
    krb5_timestamp bt;
    kstat_ctl_t *kc;
    kstat_t *k;
    kstat_named_t *kn;
    kid_t rc;

    /*
     * We use the boot_time kstat from the "unix" module.
     *
     * It's hard to determine the interface stability of kstats.  To be safe
     * we treat boot_time with extra care: if it disappears or is renamed,
     * or if its type changes, or if its value appears to be in the future,
     * then we fail to get boot time and the rcache falls back on slow
     * behavior (fsync()ing at every write).  If this kstat should produce a
     * time less than the actual boot time then this increases the chance of
     * post-crash replays of Authenticators whose rcache entries were not
     * fsync()ed and were lost.
     *
     * We consider it extremely unlikely that this kstat will ever change at
     * all however, much less to change in such a way that it will return
     * the wrong boot time as an unsigned 32-bit integer.  If we fail to
     * find the kstat we expect we log loudly even though the rcache remains
     * functional.
     */
    if ((kc = kstat_open()) == NULL ||
	(k = kstat_lookup(kc, "unix", 0, "system_misc")) == NULL ||
	(rc = kstat_read(kc, k, NULL)) == -1 ||
	(kn = kstat_data_lookup(k, "boot_time")) == NULL ||
	/* check that the kstat's type hasn't changed */
	kn->data_type != KSTAT_DATA_UINT32 ||
	/* boot_time value sanity check */
	kn->value.i32 > now ||
	/* krb5_timestamp is int32_t, this kstat is uint32_t; 2038 problem! */
	kn->value.i32 < 0) {

	/* Return boot time to 1 to indicate failure to get actual boot time */
	bt = 1;
	syslog(LOG_ALERT, "Alert: Unable to determine boot_time (boot_time "
	    "kstat removed or changed?); rcache will be functional, but slow");
    } else {
	bt = kn->value.i32;
    }

    if (kc != NULL)
	    (void) kstat_close(kc);

    return (bt);
}

/*
 * Solaris Kerberos
 *
 * We optimize the rcache by foregoing fsync() in the most common cases.
 * Foregoing fsync() requires an early boot procedure to ensure that we
 * never accept an authenticator that could be a replay of one whose
 * rcache entry we've lost.
 *
 * We do this by picking an arbitrary, small time delta such that
 * storing any krb5_donot_replays whose ctime is further into the future
 * than now + that small delta causes an fsync() of the rcache.  Early
 * after booting we must reject all krb5_donot_replays whose ctime falls
 * before time of boot + that delta.
 *
 * This works well as long as client clocks are reasonably synchronized
 * or as long as they use kdc_timesync.  Clients with clocks faster than
 * this delta will find their AP exchanges are slower than clients with
 * good or slow clocks.  Clients with very slow clocks will find that
 * their AP-REQs are rejected by servers that have just booted.  In all
 * other cases clients will notice only that AP exchanges are much
 * faster as a result of the missing fsync()s.
 *
 * KRB5_RC_FSYNCLESS_FAST_SKEW is that time delta, in seconds.  Five
 * seconds seems like a reasonable delta.  If it takes more than five
 * seconds from the time the kernel initializes itself to the time when
 * a kerberized system starts, and clients have good clocks or use
 * kdc_timesync, then no authenticators will be rejected.
 */
#define KRB5_RC_FSYNCLESS_FAST_SKEW 5

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_store(krb5_context context, krb5_rcache id, krb5_donot_replay *rep)
{
    krb5_error_code ret;
    struct dfl_data *t;
    static krb5_timestamp boot_time = 0;
    krb5_int32 now;

    ret = krb5_timeofday(context, &now);
    if (ret)
        return ret;

    /*
     * Solaris Kerberos
     *
     * if boot_time <= 1 -> we always fsync() (see below)
     * if boot_time == 1 -> don't bother trying to get it again (as it could be
     * a slow operation)
     */
    if (boot_time == 0) {
        krb5_timestamp btime = get_boot_time(now);

        assert(sizeof (boot_time) == sizeof (krb5_int32) && sizeof (krb5_int32) == sizeof (uint32_t));
        (void) atomic_cas_32((uint32_t *)&boot_time, 0, btime);
    }

    /*
     * Solaris Kerberos
     *
     * fsync()-less-ness requires safety.  If we just booted then we want to
     * reject all Authenticators whose timestamps are old enough that we might
     * not have fsync()ed rcache entries for them prior to booting.  See
     * comment above where KRB5_RC_FSYNCLESS_FAST_SKEW is defined.  See
     * also below, where krb5_rc_io_sync() is called.
     *
     * If we could tell here the time of the last system crash then we
     * could do better because we could know that the rcache has been
     * synced to disk.  But there's no reliable way to detect past
     * crashes in this code; getting the time of boot is hard enough.
     */
    if (boot_time > 1 &&
        rep->ctime < (boot_time + KRB5_RC_FSYNCLESS_FAST_SKEW)) {
        /*
         * A better error code would be nice; clients might then know
         * that nothing's necessarily wrong with their (or our) clocks
         * and that they should just wait a while (or even set their
         * clock offset slow so that their timestamps then appear into
         * the future, where we'd accept them.
         *
         * KRB5KRB_AP_ERR_SKEW will just have to do.
         */
        return KRB5KRB_AP_ERR_SKEW;
    }

    ret = k5_mutex_lock(&id->lock);
    if (ret)
        return ret;

    switch(rc_store(context, id, rep, now, FALSE)) {
    case CMP_MALLOC:
        k5_mutex_unlock(&id->lock);
        return KRB5_RC_MALLOC;
    case CMP_REPLAY:
        k5_mutex_unlock(&id->lock);
        return KRB5KRB_AP_ERR_REPEAT;
    /* Solaris Kerberos */
    case CMP_EXPIRED:
        k5_mutex_unlock(&id->lock);
        return KRB5KRB_AP_ERR_SKEW;
    case 0: break;
    default: /* wtf? */ ;
    }
    t = (struct dfl_data *)id->data;
#ifndef NOIOSTUFF
    ret = krb5_rc_io_store(context, t, rep);
    if (ret) {
        k5_mutex_unlock(&id->lock);
        return ret;
    }
#endif
    /* Shall we automatically expunge? */
    if (t->nummisses > t->numhits + EXCESSREPS)
    {
        /* Solaris Kerberos - Expunge calls krb5_rc_io_sync() */
        ret = krb5_rc_dfl_expunge_locked(context, id);
        k5_mutex_unlock(&id->lock);
        return ret;
    }
#ifndef NOIOSTUFF
    /* Solaris Kerberos */
    else if (boot_time <= 1 || rep->ctime > (now + KRB5_RC_FSYNCLESS_FAST_SKEW))
    {
        /*
         * fsync() only when necessary:
         *
         *  - on expunge (see above)
         *  - if we don't know when we booted
         *  - if rep->ctime is too far into the future
         */
        if (krb5_rc_io_sync(context, &t->d)) {
            k5_mutex_unlock(&id->lock);
            return KRB5_RC_IO;
        }
    }
#endif
    k5_mutex_unlock(&id->lock);
    return 0;
}

static krb5_error_code
krb5_rc_dfl_expunge_locked(krb5_context context, krb5_rcache id)
{
    struct dfl_data *t = (struct dfl_data *)id->data;
#ifdef NOIOSTUFF
    unsigned int i;
    struct authlist **q;
    struct authlist **qt;
    struct authlist *r;
    struct authlist *rt;
    krb5_int32 now;

    /* Solaris Kerberos */
    if (krb5_timeofday(context, &now))
        now = 0;

    for (q = &t->a; *q; q = qt) {
        qt = &(*q)->na;
        if (alive(now, &(*q)->rep, t->lifespan) == CMP_EXPIRED) {
            free((*q)->rep.client);
            free((*q)->rep.server);
            if ((*q)->rep.msghash)
                free((*q)->rep.msghash);
            free(*q);
            *q = *qt; /* why doesn't this feel right? */
        }
    }
    for (i = 0; i < t->hsize; i++)
        t->h[i] = (struct authlist *) 0;
    for (r = t->a; r; r = r->na) {
        i = hash(&r->rep, t->hsize);
        rt = t->h[i];
        t->h[i] = r;
        r->nh = rt;
    }
    return 0;
#else
    struct authlist *q;
    char *name;
    krb5_error_code retval = 0;
    krb5_rcache tmp;
    krb5_deltat lifespan = t->lifespan;  /* save original lifespan */

    if (! t->recovering) {
        name = t->name;
        t->name = 0;            /* Clear name so it isn't freed */
        (void) krb5_rc_dfl_close_no_free(context, id);
        retval = krb5_rc_dfl_resolve(context, id, name);
        free(name);
        if (retval)
            return retval;
        retval = krb5_rc_dfl_recover_locked(context, id);
        if (retval)
            return retval;
        t = (struct dfl_data *)id->data; /* point to recovered cache */
    }

    /* Solaris Kerberos */
    retval = krb5_rc_resolve_type(context, &tmp, "FILE");
    if (retval)
        return retval;
    retval = krb5_rc_resolve(context, tmp, 0);
    if (retval)
        goto cleanup;
    retval = krb5_rc_initialize(context, tmp, lifespan);
    if (retval)
        goto cleanup;
    for (q = t->a; q; q = q->na) {
        if (krb5_rc_io_store(context, (struct dfl_data *)tmp->data, &q->rep)) {
            retval = KRB5_RC_IO;
            goto cleanup;
        }
    }
    /* NOTE: We set retval in case we have an error */
    retval = KRB5_RC_IO;
    if (krb5_rc_io_sync(context, &((struct dfl_data *)tmp->data)->d))
        goto cleanup;
    if (krb5_rc_io_sync(context, &t->d))
        goto cleanup;
    if (krb5_rc_io_move(context, &t->d, &((struct dfl_data *)tmp->data)->d))
        goto cleanup;
    retval = 0;
cleanup:
    (void) krb5_rc_dfl_close(context, tmp);
    return retval;
#endif
}

krb5_error_code KRB5_CALLCONV
krb5_rc_dfl_expunge(krb5_context context, krb5_rcache id)
{
    krb5_error_code ret;
    ret = k5_mutex_lock(&id->lock);
    if (ret)
        return ret;
    ret = krb5_rc_dfl_expunge_locked(context, id);
    k5_mutex_unlock(&id->lock);
    return ret;
}
