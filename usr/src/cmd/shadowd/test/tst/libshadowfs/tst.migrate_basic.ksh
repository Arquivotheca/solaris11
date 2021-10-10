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
# Tests basic background migration through libshadowfs.
#

. $ST_TOOLS/utility.ksh

#
# Simple root with only files.  The priority list will be in inverted order.
#
tst_create_root

touch $TST_ROOT/c || fail "failed to touch 'c'"
sleep 0.1
touch $TST_ROOT/b || fail "failed to touch 'b'"
sleep 0.1
touch $TST_ROOT/a || fail "failed to touch 'a'"

tst_create_dataset

for i in 0 1 2 3 4; do
	stmigrate $TST_SROOT $i || fail "failed to migrate $i entries"
	echo
done

tst_destroy_dataset

exit 0
