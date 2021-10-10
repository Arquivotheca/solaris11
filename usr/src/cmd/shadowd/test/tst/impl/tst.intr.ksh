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
# Tests that we can interrupt a long-running migration.  This uses a special
# debugging flag that places the mount into a state where it is waiting for a
# signal during migration.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo "this is file a" > $TST_ROOT/a || fail "failed to create file a"

tst_create_dataset

ls $TST_SROOT

stspin $TST_SROOT on

cat $TST_SROOT/a &
CHILD=$!

sleep 0.1

kill -HUP $CHILD
wait $CHILD

[[ $? = 0 ]] && fail "child suceeded"

tst_destroy_dataset

exit 0
