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
# Create a ZFS dataset with a shadow mount option, verify that we have a lofs
# mount in the appropriate place, and that we can unmount and mount the
# dataset.
#

. $ST_TOOLS/utility.ksh

tst_create_root
tst_create_dataset

[[ -d $TST_RUNDIR ]] || fail "directory $TST_RUNDIR doesn't exist"

tst_unmount_dataset

[[ -d $TST_RUNDIR ]] && fail "directory $TST_RUNDIR still exists"

tst_mount_dataset

[[ -d $TST_RUNDIR ]] || fail "directory $TST_RUNDIR doesn't exist after mount"

tst_destroy_dataset

exit 0
