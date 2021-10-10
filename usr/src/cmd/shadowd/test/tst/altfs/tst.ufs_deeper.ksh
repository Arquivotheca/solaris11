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
# Tests basic migration of a UFS filesystem.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/d || fail "failed to create dir d"
mkdir $TST_ROOT/d/sd || fail "failed to create dir d/sd"
echo foo > $TST_ROOT/d/sd/f || fail "failed to create file f"
echo bar > $TST_ROOT/d/sd/g || fail "failed to create file g"

tst_create_ufs

cd $TST_SROOT
ls
cd d
ls
cd sd
ls
cat f g

tst_destroy_ufs

exit 0
