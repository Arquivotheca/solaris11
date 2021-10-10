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
# This test reproduces an issue where a directory could be processed before its
# parent, resulting in an inappropriate EEXIST error.  The sequence of actions
# is:
#
#	1. Create a directory tree of the form:
#
#		dir/subdir/file
#
#	2. Rotate the pending FID list, so that new entries are going to a
#	   different FID list.
#
#	3. Migrate 'dir', but abort after creating 'subdir' and before
#	   finishing on 'dir'.
#
#	4. At this point, 'dir' will be on one pending FID list, and
#	   'dir/subdir' on the other.
#
#	5. Modify the target so that these entries must be read through the FID
#	   list.
#
#	6. Migrate 'dir/subdir' first.
#
#	7. When we try to later migrate 'dir', we'll try to recreate
#	  'dir/subdir', get EEXIST, and try to remove it, but can't
#	  because it's non-empty.
#
# If we are suffering from the above bug, it will mean attempts to read 'dir'
# will fail with EEXIST.  In order to simulate this, we can't quite abort the
# processing where we need to, so we migrate 'dir' and then manually mark it
# with a SUNWshadow attribute.  Knowing that pending rotation is disabled,
# we're guaranteed that 'dir' will still be in the pending list.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir -p $TST_ROOT/dir/subdir || fail "failed to create dir/subdir"
echo file > $TST_ROOT/dir/subdir/file || fail "failed to create file"

stsuspend

tst_create_dataset

#
# Pending list state:
#
#	0	'dir'		(active)
#	1	<empty>
#
strotate $TST_SROOT || fail "failed to rotate pending list"

#
# Pending list state:
#
#	0	'dir'
#	1	<empty>		(active)
#
ls $TST_SROOT/dir > /dev/null || fail "failed to read target dir"

#
# Pending list state:
#
#	0	'dir'
#	1	'dir/subdir'	(active)
#

# restore 'dir' SUNWshadow attribute
tst_shadow_disable

runat $TST_SROOT/dir cat SUNWshadow > /dev/null 2>&1 && \
    fail "dir not migrated"
runat $TST_SROOT/dir/subdir cat SUNWshadow > /dev/null 2>&1 || \
    fail "dir/subdir migrated"
cat $TST_SROOT/dir/subdir/file > /dev/null 2>&1 && \
    fail "dir/subdir/file migrated"
runat $TST_SROOT/dir "printf %s dir > SUNWshadow" || \
    fail "failed to create attr"

#
# Force 'dir' out the cache.  As part of this, we'll rotate 'dir' out of the
# pending lists.  So save the pending list for later to simulate total failure.
# We also need to swap the pending lists so we process dir/subdir first.
#
PENDING=$TST_SROOT/.SUNWshadow/pending
cp $PENDING/0 $PENDING/0.bak || fail "failed to backup pending list 0"
cp $PENDING/1 $PENDING/1.bak || fail "failed to backup pending list 1"
tst_unmount_dataset
tst_mount_dataset
mv $PENDING/0.bak $PENDING/1 || fail "failed to restore pending list 0"
mv $PENDING/1.bak $PENDING/0 || fail "failed to restore pending list 1"

tst_shadow_enable

stprocess $TST_SROOT || fail "failed to process pending FID list"

cat $TST_SROOT/dir/subdir/file || fail "failed to cat file"

stresume

tst_destroy_dataset
