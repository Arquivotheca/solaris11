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
# Tests behavior when we encounter an error during the normal processing phase.
# We should issue a SHADOW_IOC_GETPATH ioctl() after the fact and record the
# error in our table.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo foo > $TST_ROOT/a || fail "failed to create a"

tst_create_dataset

ls $TST_SROOT > /dev/null || fail "failed to list root"
mv $TST_ROOT/a $TST_ROOT/b || fail "failed to rename a"

sterrors $TST_SROOT 2 || fail "failed to migrate directory"

tst_destroy_dataset
