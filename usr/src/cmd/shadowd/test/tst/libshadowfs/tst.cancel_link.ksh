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
# Tests that any hard linked files end up with the correct link count, and any
# internal translation tables in .SUNWshadow are torn down as part of
# cancelation/finalization.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo "I am a hard link" > $TST_ROOT/a || fail "failed to create a"
mkdir $TST_ROOT/dir || fail "failed to mkdir dir"
ln $TST_ROOT/a $TST_ROOT/dir/b || fail "Failed to link a to b"

cat $TST_ROOT/a
ls -l $TST_ROOT/a | awk '{print $2}'
cat $TST_ROOT/dir/b
ls -l $TST_ROOT/dir/b | awk '{print $2}'
echo 

tst_create_dataset

cat $TST_SROOT/a
ls -l $TST_SROOT/a | awk '{print $2}'
cat $TST_SROOT/dir/b
ls -l $TST_SROOT/dir/b | awk '{print $2}'
echo 

stcancel $TST_SROOT || fail "failed to cancel shadow migration"

ls -l $TST_SROOT/a | awk '{print $2}'
ls -l $TST_SROOT/dir/b | awk '{print $2}'

[[ -d $TST_SROOT/.SUNWshadow ]] && fail ".SUNWshadow still exists"

tst_destroy_dataset
