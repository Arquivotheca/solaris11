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
# Tests canceling migration on ZFS filesystems.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to mkdir 'dir'"
echo foo > $TST_ROOT/dir/foo || fail "failed to create 'dir/foo'"

tst_create_ufs

ls $TST_SROOT >/dev/null || fail "failed to list contents of $TST_SROOT"

stcancel $TST_SROOT || fail "failed to cancel shadow migration"

[[ -d $TST_SROOT ]] || fail "failed to migrate 'dir'"
[[ -f $TST_SROOT/dir/foo ]] && fail "successfully migrated 'dir/foo'"

runat $TST_SROOT ls SUNWshadow* 2>/dev/null && \
   "SUNWshadow* attributes present after cancellation"
runat $TST_SROOT/dir ls SUNWshadow >/dev/null || \
   "SUNWshadow attribute missing on 'dir' after cancellation"

tst_destroy_ufs

exit 0
