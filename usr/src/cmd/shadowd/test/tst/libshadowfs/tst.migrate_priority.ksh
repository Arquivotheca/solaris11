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
# Tests the priority sorting of entries.  The first priority is by depth, and
# the second is by atime.
#

. $ST_TOOLS/utility.ksh

#
# Simple root with only files.  The priority list will be in inverted order.
#
tst_create_root

function create_tree
{
	echo d > $TST_ROOT/d || fail "failed to touch 'c'"
	sleep 0.1
	mkdir $TST_ROOT/dir1 || fail "failed to mkdir 'dir1'"
	echo c > $TST_ROOT/dir1/c || fail "failed to touch 'c'"
	sleep 0.1
	mkdir $TST_ROOT/dir1/dir2 || fail "failed to mkdir 'dir2'"
	echo b > $TST_ROOT/dir1/dir2/b || fail "failed to touch 'b'"
	sleep 0.1
	echo a > $TST_ROOT/a || fail "failed to touch 'a'"
}

create_tree

tst_create_dataset

stmigrate $TST_SROOT 1 || fail "failed to migrate 1 entry"
echo

stmigrate $TST_SROOT 3 || fail "failed to migrate 2 entries"
echo

tst_destroy_dataset

rm -rf $TST_ROOT/*
create_tree
tst_create_dataset

stmigrate $TST_SROOT 4 || fail "failed to migrate 3 entries"

tst_destroy_dataset

exit 0
