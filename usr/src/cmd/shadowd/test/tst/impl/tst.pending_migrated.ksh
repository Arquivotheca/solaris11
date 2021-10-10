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
# Tests that a file or directory in the pending list that has since been
# migrated (or recreated with the same FID) doesn't cause migration to spin in
# a loop.  The easiest way to simulate this is to simply turn off shadow
# migration and go and remove the attribute.
#

. $ST_TOOLS/utility.ksh

stsuspend

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to create dir"

tst_create_dataset

ls $TST_SROOT || fail "failed to list root contents"

stpending 0 $TST_SROOT $TST_SROOT/dir || fail "failed to validate pending list"

tst_shadow_disable

runat $TST_SROOT/dir rm -f "SUNWshadow*" || \
    fail "failed to remove shadow attributes"

tst_shadow_enable

stprocess $TST_SROOT || fail "failed to process pending list"

tst_destroy_dataset
