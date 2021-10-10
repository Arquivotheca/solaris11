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
# Tests that if we can't initially read a hard link, the entry in the hard link
# directory is still created, linked together, and a subsequent successful read
# of the file returns the correct data.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to create directory dir"
echo "this is file a" > $TST_ROOT/dir/a || fail "failed to create file a"
chmod 600 $TST_ROOT/dir/a || fail "failed to chmod file a"
ln $TST_ROOT/dir/a $TST_ROOT/dir/a.link || fail "failed to create file a.link"

tst_create_dataset

ls $TST_SROOT > /dev/null || fail "failed to list root directory"

su nobody -c $PWD/stcredset || fail "failed to change credentials"

cat $TST_SROOT/dir/a && fail "successfully read shadow a"

stcredclear || fail "failed to restore credentials"

FID=$(stfid $TST_ROOT/dir/a)

[[ -f $TST_SROOT/.SUNWshadow/link/0.$FID.ref ]] || \
    fail "failed to find ref for file dir/a ($FID)"
[[ -f $TST_SROOT/.SUNWshadow/link/0.$FID.path ]] || \
    fail "failed to find path for file dir/a ($FID)"

cat $TST_SROOT/dir/a.link

tst_destroy_dataset

exit 0
