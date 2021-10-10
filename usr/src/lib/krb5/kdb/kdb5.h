/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _KRB5_KDB5_H_
#define _KRB5_KDB5_H_


#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <errno.h>
#include <utime.h>
#include <utime.h>
#include <k5-int.h>
#include "kdb.h"

#define KRB5_DB_GET_DB_CONTEXT(kcontext) (((kdb5_dal_handle*) (kcontext)->db_context)->db_context)
#define KRB5_DB_GET_PROFILE(kcontext)  ((kcontext)->profile)
#define KRB5_DB_GET_REALM(kcontext)    ((kcontext)->default_realm)

typedef struct _db_library {
    char name[KDB_MAX_DB_NAME];
    int reference_cnt;
    struct plugin_dir_handle dl_dir_handle;
    kdb_vftabl vftabl;
    struct _db_library *next, *prev;
} *db_library;

struct _kdb5_dal_handle
{
    /* Helps us to change db_library without affecting modules to some
       extent.  */
    void *db_context;
    db_library lib_handle;
};

#endif  /* end of _KRB5_KDB5_H_ */
