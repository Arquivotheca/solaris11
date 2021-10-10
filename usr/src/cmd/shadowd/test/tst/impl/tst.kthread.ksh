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
# Migrates a file and directory form a kernel thread.  We previously had
# implicit dependencies on the presence of a lwp structure, so this makes sure
# we don't make that mistake again.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to create dir"
echo "this is foo" > $TST_ROOT/dir/foo || fail "failed to create foo"

tst_create_dataset

stkthread $TST_SROOT/dir || fail "failed to migrate dir"

tst_shadow_disable

ls $TST_SROOT/dir

tst_shadow_enable

stkthread $TST_SROOT/dir/foo || fail "failed to migrate foo"

tst_clear_shadow

cat $TST_SROOT/dir/foo

tst_destroy_dataset

exit 0
