/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * clients/kdestroy/kdestroy.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 * Destroy the contents of your credential cache.
 */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "autoconf.h"
#include <krb5.h>
#include <com_err.h>
#include <string.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Solaris Kerberos */
#include <rpc/types.h>
#include <rpc/rpcsys.h>
#include <rpc/rpcsec_gss.h>
#include <libintl.h>
#include <locale.h>

#ifdef __STDC__
#define BELL_CHAR '\a'
#else
#define BELL_CHAR '\007'
#endif

extern int optind;
extern char *optarg;

#ifndef _WIN32
#define GET_PROGNAME(x) (strrchr((x), '/') ? strrchr((x), '/')+1 : (x))
#else
#define GET_PROGNAME(x) max(max(strrchr((x), '/'), strrchr((x), '\\')) + 1,(x))
#endif

char *progname;


static void usage()
{
#define KRB_AVAIL_STRING(x) ((x)?"available":"not available")

    fprintf(stderr, gettext("Usage: %s [-q] [-c cache_name]\n"), progname);
    fprintf(stderr, gettext("\t-q quiet mode\n"));
    fprintf(stderr, gettext("\t-c specify name of credentials cache\n"));
    exit(2);
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    krb5_context kcontext;
    krb5_error_code retval;
    int c;
    krb5_ccache cache = NULL;
    char *cache_name = NULL;
    int code = 0;
    int errflg = 0;
    int quiet = 0;

    /* Solaris Kerberos */
    krb5_principal me = NULL;
    char *client_name = NULL;
    struct krpc_revauth desarg;
    static  rpc_gss_OID_desc oid=
	{9, "\052\206\110\206\367\022\001\002\002"};
    static  rpc_gss_OID krb5_mech_type = &oid;

    progname = GET_PROGNAME(argv[0]);

    /* Solaris Kerberos - set locale and domain for internationalization */
    (void) setlocale(LC_ALL, ""); 
#if !defined(TEXT_DOMAIN) 
#define TEXT_DOMAIN "SYS_TEST"
#endif /* !TEXT_DOMAIN */
    (void) textdomain(TEXT_DOMAIN); 

    while ((c = getopt(argc, argv, "54qc:")) != -1) {
        switch (c) {
        case 'q':
            quiet = 1;
            break;
        case 'c':
            if (cache_name) {
                fprintf(stderr, gettext("Only one -c option allowed\n"));
                errflg++;
            } else {
                cache_name = optarg;
            }
            break;
        case '4':
            fprintf(stderr, gettext("Kerberos 4 is no longer supported\n"));
            exit(3);
            break;
        case '5':
            break;
        case '?':
        default:
            errflg++;
            break;
        }
    }

    if (optind != argc)
        errflg++;

    if (errflg) {
        usage();
    }

    retval = krb5_init_context(&kcontext);
    if (retval) {
        com_err(progname, retval, gettext("while initializing krb5"));
        exit(1);
    }

    /* 
     *  Solaris Kerberos
     *  Let us destroy the kernel cache first.  
     */ 
    desarg.version = 1; 
    desarg.uid_1 = geteuid(); 
    desarg.rpcsec_flavor_1 = RPCSEC_GSS; 
    desarg.flavor_data_1 = (void *) krb5_mech_type; 
    code = krpc_sys(KRPC_REVAUTH, (void *)&desarg); 
    if (code != 0) {
        fprintf(stderr, 
                gettext("%s: kernel creds cache error %d \n"), 
                progname, code); 
    }

    if (cache_name) {
        code = krb5_cc_resolve (kcontext, cache_name, &cache);
        if (code != 0) {
            com_err (progname, code, gettext("while resolving %s"), cache_name);
            exit(1);
        }
    } else {
        code = krb5_cc_default(kcontext, &cache);
        if (code) {
            com_err(progname, code, gettext("while getting default ccache"));
            exit(1);
        }
    }

    /* 
     * Solaris Kerberos
     * Get client name for ktkt_warnd(1M) msg.
     */
    code = krb5_cc_get_principal(kcontext, cache, &me); 
    if (code != 0) 
        fprintf(stderr, gettext 
                ("%s: Could not obtain principal name from cache\n"), progname); 
    else 
        if ((code = krb5_unparse_name(kcontext, me, &client_name))) 
            fprintf(stderr, gettext 
                    ("%s: Could not unparse principal name found in cache\n"), progname); 

    code = krb5_cc_destroy (kcontext, cache);
    if (code != 0) {
        com_err (progname, code, gettext("while destroying cache"));
        if (code != KRB5_FCC_NOFILE) {
            if (quiet)
                fprintf(stderr, gettext("Ticket cache NOT destroyed!\n"));
            else {
                fprintf(stderr, gettext("Ticket cache %cNOT%c destroyed!\n"),
                        BELL_CHAR, BELL_CHAR);
            }
            errflg = 1;
        }
    }

    /* Solaris Kerberos - Delete ktkt_warnd(1M) entry. */
    if (!errflg && client_name)
        kwarn_del_warning(client_name);
    else
        fprintf(stderr, gettext 
            ("%s: TGT expire warning NOT deleted\n"), progname); 

    /* Solaris Kerberos */
    if (client_name != NULL)
        free(client_name);
    krb5_free_principal(kcontext, me);

    return errflg;
}
