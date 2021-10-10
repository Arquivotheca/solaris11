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
# Remove a file from the source and make sure we can't migrate it.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo foo > $TST_ROOT/foo || fail "failed to create foo"

tst_create_dataset

ls $TST_SROOT
rm $TST_ROOT/foo
ls $TST_SROOT
cat $TST_SROOT/foo > /dev/null 2>&1 && fail "successfully read foo"

tst_destroy_dataset
