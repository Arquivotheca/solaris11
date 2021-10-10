/*
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef	ERROR_MESSAGE_H
#define	ERROR_MESSAGE_H

#define	ERRCODE_RANGE 8

extern const char *
imp_error_table(long);

extern const char *
pty_error_table(long);

extern const char *
kpws_error_table(long);

extern const char *
kdc5_error_table(long);

extern const char *
kadm_error_table(long);

extern const char *
asn1_error_table(long);

extern const char *
kdb5_error_table(long);

extern const char *
krb5_error_table(long);

extern const char *
adb_error_table(long);

extern const char *
kv5m_error_table(long);

extern const char *
prof_error_table(long);

extern const char *
ggss_error_table(long);

extern const char *
ss_error_table(long);

extern const char *
k5g_error_table(long);

extern const char *
ovku_error_table(long);

extern const char *
ovk_error_table(long);

#endif	/* ERROR_MESSAGE_H */
