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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# This is a collection of functions to handle testing of NFS functionality.
# When a script sources this file, the things that happen are:
#
#	- Test is skipped if '-n' wasn't passed to shadowtest
#	- Mount point created where nfs source directory should mount
#	- $TST_NFSHOST and $TST_NFSPATH set
#

. $ST_TOOLS/utility.ksh

[[ -z $ST_NFS_PATH ]] && exit $ST_SKIP

NFS_MOUNTP=$ST_ROOT/shadow.src.$$
mkdir $NFS_MOUNTP || fail "failed to create $NFS_MOUNTP"
chmod 777 $NFS_MOUNTP

TST_NFSHOST=$ST_NFS_HOST
TST_NFSPATH=$ST_NFS_PATH

function nfs_mount
{
	mount -F nfs $TST_NFSHOST:$TST_NFSPATH $NFS_MOUNTP || \
	    fail "fail to mount $TST_NFSHOST:$TST_NFSPATH"
}

function nfs_unmount_nodestroy
{
	umount -f $NFS_MOUNTP || fail "failed to unmount $NFS_MOUNTP"
}

function nfs_unmount
{
	rm -rf $NFS_MOUNTP/* || fail "failed to remove contents of NFS dir"
	umount -f $NFS_MOUNTP || fail "failed to unmount $NFS_MOUNTP"
	rmdir $NFS_MOUNTP || fail "failed to remove $NFS_MOUNTP"
}
