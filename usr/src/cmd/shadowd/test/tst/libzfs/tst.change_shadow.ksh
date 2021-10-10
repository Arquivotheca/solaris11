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
# Tests that the shadow property can be changed while the filesystem is
# mounted.
#

. $ST_TOOLS/utility.ksh

ROOT_ONE=$TST_ROOT/one
ROOT_TWO=$TST_ROOT/two

mkdir -p $ROOT_ONE || fail "failed to make root/one"
mkdir -p $ROOT_TWO || fail "failed to make root/one"

echo "this is file a" > $ROOT_ONE/a || fail "failed to create $ROOT_ONE/a"
echo "this is file b" > $ROOT_ONE/b || fail "failed to create $ROOT_ONE/b"
echo "THIS IS FILE A" > $ROOT_TWO/a || fail "Failed to create $ROOT_TWO/a"
echo "THIS IS FILE B" > $ROOT_TWO/b || fail "Failed to create $ROOT_TWO/b"

TST_ROOT=$ROOT_ONE

tst_create_dataset

cat $TST_SROOT/a || fail "failed to cat a"

zfs set shadow=file://$ROOT_TWO $TST_DATASET || \
    fail "failed to change shadow property"

cat $TST_SROOT/b || fail "failed to cat b"

tst_destroy_dataset

exit 0
