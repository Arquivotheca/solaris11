#!/bin/ksh -p
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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

. $ST_TOOLS/utility.ksh

function bad_option
{
	typeset shadow=$1

	mount -F tmpfs -o shadow=$shadow $TST_ROOT > /dev/null 2>&1 && \
	    fail "successfully mounted with shadow $shadow"
}

tst_create_root

# syntatically invalid
bad_option ""
bad_option foo
bad_option /foo=bar
bad_option /foo,bar

# non-existent directory
bad_option $TST_ROOT/notadir

# shadowed filesystem
tst_create_dataset
bad_option $TST_SROOT
tst_destroy_dataset

exit 0
