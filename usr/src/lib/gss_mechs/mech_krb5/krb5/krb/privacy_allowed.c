/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "k5-int.h"
#include "auth_con.h"

krb5_boolean
krb5_privacy_allowed(void)
{
#ifdef	KRB5_NO_PRIVACY
	return (FALSE);
#else
	return (TRUE);
#endif
}
