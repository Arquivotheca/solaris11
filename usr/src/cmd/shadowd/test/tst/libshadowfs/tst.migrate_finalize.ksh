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
# Tests finishing migration.  This is identical to cancelation except that we
# first check to see whether we're done with migration.
#

. $ST_TOOLS/utility.ksh

tst_create_root
tst_create_dataset

stmigrate $TST_SROOT 0 true && fail "successfully finalized migration"
stcomplete $TST_SROOT 2 true || fail "failed to complete migration"

zfs get -H -o value shadow $TST_DATASET
[[ -d $TST_SROOT ]] || fail "failed to migrate 'dir'"
[[ -f $TST_SROOT/dir/foo ]] && fail "successfully migrated 'dir/foo'"

runat $TST_SROOT ls SUNWshadow* 2>/dev/null && \
   "SUNWshadow* attributes present after cancellation"
runat $TST_SROOT/dir ls SUNWshadow >/dev/null || \
   "SUNWshadow attribute missing on 'dir' after cancellation"

tst_destroy_dataset

exit 0
