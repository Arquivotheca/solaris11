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
# Renames files before they've been migrated, and verifies they are migrated
# properly.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/a
echo "this is file foo" > $TST_ROOT/a/foo
echo "this is file bar" > $TST_ROOT/a/bar

cat $TST_ROOT/a/foo || fail "failed to cat a/foo"
cat $TST_ROOT/a/bar || fail "failed to cat a/bar"
echo

tst_create_dataset

mv $TST_SROOT/a $TST_SROOT/b || fail "failed to mv a"
mv $TST_SROOT/b/bar $TST_SROOT/baz || fail "failed to mv a/bar"

cat $TST_SROOT/b/foo || fail "failed to cat b/foo"
cat $TST_SROOT/baz || fail "failed to cat baz"
echo

tst_clear_shadow

cat $TST_SROOT/b/foo || fail "failed to cat b/foo"
cat $TST_SROOT/baz || fail "failed to cat baz"

tst_destroy_dataset
