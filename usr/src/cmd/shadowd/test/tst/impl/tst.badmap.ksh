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

#
# This verifies that we gracefully handle garbage shadow maps, including
# duplicate entries and random data.
#

. $ST_TOOLS/utility.ksh

#
# Create the template for the file header.
#
perl -e 'print "\x2b\x59\x01\x00"' > /tmp/tst.badmap.$$

function fill_map
{
	typeset file=$1 input=$2

	runat $file dd if=$input of=SUNWshadow.map bs=8k count=1 || \
	    fail "failed to update map contents"

	runat $file dd if=/tmp/tst.badmap.$$ of=SUNWshadow.map conv=notrunc || \
	    fail "failed to overwrite header"
}

tst_create_root

echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"
echo "this is file b" > $TST_ROOT/b || fail "failed to create file a"

tst_create_dataset

ls $TST_SROOT > /dev/null || fail "failed to list directory contents"

tst_shadow_disable

fill_map $TST_SROOT/a /dev/zero
fill_map $TST_SROOT/b /dev/urandom

tst_shadow_enable

#
# We know that all zeros will fail, but we're just crossing our fingers that we
# generate enough random data to trigger an invalid map.
#
cat $TST_SROOT/a && fail "successfully read file a"
cat $TST_SROOT/b && fail "successfully read file b"

tst_destroy_dataset

exit 0
