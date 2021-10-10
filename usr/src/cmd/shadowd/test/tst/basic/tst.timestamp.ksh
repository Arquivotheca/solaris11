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
# Tests that atime and mtime are preserved.  We cannot set the ctime, so we
# explicitly ignore that.
#

. $ST_TOOLS/utility.ksh

function print_timestamps
{
	typeset file=$1 output=$2

	ls -E -% all $file | tail -3 | grep -v ctime > $output || \
	    fail "failed to attributes of $file"
}

tst_create_root atime=off

touch $TST_ROOT/a
sleep 1
touch $TST_ROOT/a

print_timestamps $TST_ROOT/a $TST_ROOT/timestamps

tst_create_dataset

print_timestamps $TST_SROOT/a $TST_ROOT/timestamps.shadow

tst_clear_shadow

diff $TST_ROOT/timestamps $TST_ROOT/timestamps.shadow || \
    fail "timestamps differ"

tst_destroy_dataset
