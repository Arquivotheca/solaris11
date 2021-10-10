/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 *	Openvision retains the copyright to derivative works of
 *	this source code.  Do *NOT* create a derivative of this
 *	source code before consulting with your legal department.
 *	Do *NOT* integrate *ANY* of this source code into another
 *	product before consulting with your legal department.
 *
 *	For further information, read the top-level Openvision
 *	copyright which is contained in the top-level MIT Kerberos
 *	copyright.
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include "autoconf.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <krb5.h>

/* Solaris Kerberos */
#include "kpasswd_solaris.h"
#include <com_err.h>
#include <libintl.h>
#include <locale.h>

#ifdef HAVE_PWD_H
#include <pwd.h>

static
void get_name_from_passwd_file(program_name, kcontext, me)
    char * program_name;
    krb5_context kcontext;
    krb5_principal * me;
{
    struct passwd *pw;
    krb5_error_code code;
    if ((pw = getpwuid(getuid()))) {
        if ((code = krb5_parse_name(kcontext, pw->pw_name, me))) {
            com_err (program_name, code, gettext("when parsing name %s"), pw->pw_name);
            exit(1);
        }
    } else {
        fprintf(stderr, gettext("Unable to identify user from password file\n"));
        exit(1);
    }
}
#else /* HAVE_PWD_H */
void get_name_from_passwd_file(kcontext, me)
    krb5_context kcontext;
    krb5_principal * me;
{
    fprintf(stderr, gettext("Unable to identify user\n"));
    exit(1);
}
#endif /* HAVE_PWD_H */

int main(int argc, char *argv[])
{
    krb5_error_code ret;
    krb5_context context;
    krb5_principal princ = NULL;
    char *pname;
    krb5_ccache ccache;

    /* Solaris Kerberos */
    (void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)  /* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
    (void) textdomain(TEXT_DOMAIN);

    if (argc > 2) {
        fprintf(stderr, gettext("usage: %s [principal]\n"), argv[0]);
        exit(1);
    }

    pname = argv[1];

    ret = krb5_init_context(&context);
    if (ret) {
        com_err(argv[0], ret, gettext("initializing kerberos library"));
        exit(1);
    }

    /* in order, use the first of:
       - a name specified on the command line
       - the principal name from an existing ccache
       - the name corresponding to the ruid of the process

       otherwise, it's an error.
    */

    if (pname) {
        if ((ret = krb5_parse_name(context, pname, &princ))) {
            com_err(argv[0], ret, gettext("parsing client name"));
            exit(1);
        }
    } else {
        ret = krb5_cc_default(context, &ccache);
        if (ret != 0) {
            com_err(argv[0], ret, gettext("opening default ccache"));
            exit(1);
        }

        ret = krb5_cc_get_principal(context, ccache, &princ);
        if (ret != 0 && ret != KRB5_CC_NOTFOUND && ret != KRB5_FCC_NOFILE) {
            com_err(argv[0], ret, gettext("getting principal from ccache"));
            exit(1);
        }

        ret = krb5_cc_close(context, ccache);
        if (ret != 0) {
            com_err(argv[0], ret, gettext("closing ccache"));
            exit(1);
        }

        if (princ == NULL)
            get_name_from_passwd_file(argv[0], context, &princ);
    }

    /* Solaris Kerberos */
    if ((ret = change_kpassword_solaris(context, argv[0], princ))) {
        exit(ret);
    }

    printf(gettext("Password changed.\n"));
    exit(0);
}
