/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Pager: Routines to create a "more" running out of a particular file
 * descriptor.
 *
 * Copyright 1987, 1988 by MIT Student Information Processing Board
 *
 * For copyright information, see copyright.h.
 */

/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ss_internal.h"
#include "copyright.h"
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/file.h>
#include <signal.h>
/* Solaris Kerberos */
#include <unistd.h>

static char MORE[] = "more";
extern char *_ss_pager_name;
extern char *getenv();
/*
 * Solaris Kerberos: indicates if it is OK to invoke pager, 0 means no, 1 means
 * yes.
 */
static int USE_PAGER = 0;

/*
 * this needs a *lot* of work....
 *
 * run in same process
 * handle SIGINT sensibly
 * allow finer control -- put-page-break-here
 */
void ss_page_stdin();

/* Solaris Kerberos: Begin: allow a caller to indicate if paging is ok */
void ss_set_use_pager(int input)
{
    USE_PAGER = input;
}

int ss_use_pager()
{
    return (USE_PAGER);
}
/* Solaris Kerberos: End: allow a caller to indicate if paging is ok */

#ifndef NO_FORK
int ss_pager_create()
{
    int filedes[2];

    if (pipe(filedes) != 0)
        return(-1);

    switch((int) fork()) {
    case -1:
        return(-1);
    case 0:
        /*
         * Child; dup read half to 0, close all but 0, 1, and 2
         */
        if (dup2(filedes[0], 0) == -1)
            exit(1);
        ss_page_stdin();
    default:
        /*
         * Parent:  close "read" side of pipe, return
         * "write" side.
         */
        (void) close(filedes[0]);
        set_cloexec_fd(filedes[1]);
        return(filedes[1]);
    }
}
#else /* don't fork */
int ss_pager_create()
{
    int fd;
    fd = open("/dev/tty", O_WRONLY, 0);
    if (fd >= 0)
        set_cloexec_fd(fd);
    return fd;
}
#endif

void ss_page_stdin()
{
    int i;
#ifdef POSIX_SIGNALS
    struct sigaction sa;
    sigset_t mask;
#endif
    for (i = 3; i < 32; i++)
        (void) close(i);
#ifdef POSIX_SIGNALS
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, (struct sigaction *)0);
#else
    (void) signal(SIGINT, SIG_DFL);
#endif
    {
#ifdef POSIX_SIGNALS
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigprocmask(SIG_UNBLOCK, &mask, (sigset_t *)0);
#else
        int mask = sigblock(0);
        mask &= ~sigmask(SIGINT);
        sigsetmask(mask);
#endif
    }
    if (_ss_pager_name == (char *)NULL) {
        if ((_ss_pager_name = getenv("PAGER")) == (char *)NULL)
            _ss_pager_name = MORE;
    }
    /*
     * Solaris Kerberos
     * We need to ensure that when the PAGER is invoked, that we only
     * execute with the real uid, not the effective - elevated uid.
     */
    if (setuid(getuid()) == 0)
	(void) execlp(_ss_pager_name, _ss_pager_name, (char *) NULL);
    {
        /* minimal recovery if pager program isn't found */
        char buf[80];
        register int n;
        while ((n = read(0, buf, 80)) > 0)
            write(1, buf, (unsigned) n);
    }
    exit(errno);
}
