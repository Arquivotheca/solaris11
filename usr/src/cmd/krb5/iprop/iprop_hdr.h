/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _IPROP_HDR_H
#define _IPROP_HDR_H

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * This file has some defines common to the iprop client and
 * server routines.
 */

/*
 * Maximum size for each ulog entry is 2KB and maximum
 * possible attribute-value pairs for each ulog entry is 20
 */
#define MAXENTRY_SIZE   2048
#define MAXATTRS_SIZE   20

#define KIPROP_SVC_NAME "kiprop"
#define MAX_BACKOFF     300     /* Backoff for a maximum for 5 mts */

enum iprop_role {
    IPROP_NULL = 0,
    IPROP_MASTER = 1,
    IPROP_SLAVE = 2
};
typedef enum iprop_role iprop_role;

/*
 * Full resync dump versioning
 */
#define IPROPX_VERSION_0    0
#define IPROPX_VERSION_1    1
#define IPROPX_VERSION      IPROPX_VERSION_1

#ifdef  __cplusplus
}
#endif

#endif /* !_IPROP_HDR_H */
