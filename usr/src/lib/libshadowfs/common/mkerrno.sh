#!/bin/sh
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

echo "/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <strings.h>
#include <libshadowfs.h>

static const struct {
\tchar *se_name;\t\t/* error name */
\tchar *se_msg;\t\t/* error message */
} _shadow_errstr[] = {"

pattern='^	\(ESHADOW_[A-Z0-9_]*\),*'
replace='	{ "\1",'
open='	\/\* '
openrepl='"'
close=' \*\/$'
closerepl='" },'

( sed -n "s/$pattern/$replace/p" | sed -n "s/$open/$openrepl/p" |
    sed -n "s/$close/$closerepl/p" ) || exit 1

echo "\
};\n\
\n\
static int _shadow_nerrno = sizeof (_shadow_errstr) / sizeof (_shadow_errstr[0]);\n\
\n\
const char *
shadow_strerror(shadow_errno_t err)
{
	return (err < 0 || err >= _shadow_nerrno ? \"unknown error\" :
	     _shadow_errstr[err].se_msg);
}

const char *
shadow_errname(shadow_errno_t err)
{
	return (err < 0 || err >= _shadow_nerrno ? NULL :
	     _shadow_errstr[err].se_name);
}

shadow_errno_t
shadow_errcode(const char *name)
{
	shadow_errno_t err;

	for (err = 0; err < _shadow_nerrno; err++) {
		if (strcmp(name, _shadow_errstr[err].se_name) == 0)
			return (err);
	}

	return (ESHADOW_UNKNOWN);
}"

exit 0
