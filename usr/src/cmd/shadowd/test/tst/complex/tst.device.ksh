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
# We have a test for special devices, but this tests validates migration of
# real device files, as there have been bugs in the past relating to syncing
# device files.  To trigger this behavior, we have to migrate a device that
# supports the DKIOCFLUSHWRITECACHE ioctl(), so we go through and copy all of
# /dev/dsk and /dev/rdsk.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/devices
cd / && find devices -depth -print | cpio -pdmu $TST_ROOT/devices

tst_create_dataset

find $TST_SROOT

tst_destroy_dataset
