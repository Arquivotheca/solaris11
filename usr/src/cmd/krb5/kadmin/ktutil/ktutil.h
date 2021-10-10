/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
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


/*
 * kadmin/ktutil/ktutil.h
 *
 * Copyright 1995 by the Massachusetts Institute of Technology.
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
 */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

typedef struct _krb5_kt_list {
    struct _krb5_kt_list *next;
    krb5_keytab_entry *entry;
} *krb5_kt_list;

krb5_error_code ktutil_free_kt_list (krb5_context, krb5_kt_list);

krb5_error_code ktutil_delete (krb5_context, krb5_kt_list *, int);

krb5_error_code ktutil_add (krb5_context,
                            krb5_kt_list *,
                            char *,
                            krb5_kvno,
                            char *,
                            int);

krb5_error_code ktutil_read_keytab (krb5_context,
                                    char *,
                                    krb5_kt_list *);

krb5_error_code ktutil_write_keytab (krb5_context,
                                     krb5_kt_list,
                                     char *);

krb5_error_code ktutil_read_srvtab (krb5_context,
                                    char *,
                                    krb5_kt_list *);

/* Solaris Kerberos - errors are now returned. */
int ktutil_add_entry (int, char *[]);

int ktutil_clear_list (int, char *[]);

int ktutil_read_v5 (int, char *[]);

int ktutil_read_v4 (int, char *[]);

int ktutil_write_v5 (int, char *[]);

int ktutil_write_v4 (int, char *[]);

int ktutil_delete_entry (int, char *[]);

int ktutil_list (int, char *[]);
