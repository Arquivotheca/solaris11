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
# This tests that if we fail to migrate one link to a file and later find
# another hard link to the same file, we get the correct contents.  To do this,
# we create a file that has an extended attribute that cannot be read by
# non-root users, and first attempt the migration as nobody.  We then restore
# our credentials and attempt to read the contents via another link.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir1 || fail "failed to mkdir dir1"
mkdir $TST_ROOT/dir2 || fail "failed to mkdir dir2"
echo "this is a file" > $TST_ROOT/dir1/a || fail "failed to create dir1/a"
runat $TST_ROOT/dir1/a mkfile 1k attr || fail "failed to create xattr"
runat $TST_ROOT/dir1/a chmod 600 attr || fail "failed to chmod xattr"
ln $TST_ROOT/dir1/a $TST_ROOT/dir2/a || fail "failed to link dir2/a"

tst_create_dataset

ls $TST_SROOT > /dev/null || fail "failed to list root directory"

su nobody -c $PWD/stcredset || fail "failed to set credentials"

cat $TST_SROOT/dir1/a && fail "successfully read dir1/a"

stcredclear || fail "failed to restore credentials"

cat $TST_SROOT/dir2/a || fail "failed to read dir2/a"

tst_destroy_dataset
