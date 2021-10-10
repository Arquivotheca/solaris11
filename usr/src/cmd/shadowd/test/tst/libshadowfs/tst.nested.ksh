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
# Makes sure that if we have a nested local filesystem, we don't bother
# traversing into subdirectories that aren't part of the shadow filesystem.
#

. $ST_TOOLS/utility.ksh

TST_NESTED=$ST_DATASET/$(basename $0).$$.nested

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to mkdir dir"
echo foo > $TST_ROOT/foo || fail "failed to create foo"

tst_create_dataset

zfs create -o mountpoint=$TST_SROOT/dir $TST_NESTED || \
    fail "failed to create nested filesystem"

touch $TST_SROOT/dir/{a,b,c,d} || fail "failed to create nested files"

stcomplete $TST_SROOT 3 || fail "failed to complete migration"

zfs destroy $TST_NESTED
tst_destroy_dataset
