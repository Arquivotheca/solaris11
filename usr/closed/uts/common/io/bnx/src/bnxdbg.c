/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2007-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/

#include "bnx.h"

#define BNX_BUF_SIZE 256


void debug_break(void * ctx)
{
    um_device_t * um = (um_device_t *)ctx;
    cmn_err(CE_PANIC, "-> %s panic <-", (um) ? um->dev_name : "(unknown)");
}


void debug_msg(void * ctx,
               unsigned long level,
               char * file,
               unsigned long line,
               char * msg,
               ...)
{
    um_device_t * um = (um_device_t *)ctx;
    char buf[BNX_BUF_SIZE];
    va_list argp;

    *buf = 0;

    if (um)
    {
        snprintf(buf, BNX_BUF_SIZE, "%s %s:%lu ", um->dev_name, file, line);
    }
    else
    {
        snprintf(buf, BNX_BUF_SIZE, "%s:%lu ", file, line);
    }

    strncat(buf, msg, BNX_BUF_SIZE);

    va_start(argp, msg);
    vcmn_err(CE_WARN, buf, argp);
    va_end(argp);
}


void debug_msgx(void * ctx,
                unsigned long level,
                char * msg,
                ...)
{
    um_device_t * um = (um_device_t *)ctx;
    char buf[BNX_BUF_SIZE];
    va_list argp;

    *buf = 0;

    if (um)
    {
        snprintf(buf, BNX_BUF_SIZE, "%s ", um->dev_name);
    }

    strncat(buf, msg, BNX_BUF_SIZE);

    va_start(argp, msg);
    vcmn_err(CE_WARN, buf, argp);
    va_end(argp);
}

