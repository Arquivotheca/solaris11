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
# This is a collection of functions to handle testing of NFS functionality.
# When a script sources this file, the things that happen are:
#
#	- Test is skipped if '-n' wasn't passed to shadowtest
#	- Filesystem is mounted and a subdirectory created
#	- $ST_NFS_PATH updated to reflect new subdirectory
#	- $NFS_ROOT set to locally mounted subdirectory
#
# If $NFS_VERSION is set, then that version is used when doing the mount.  It
# defaults to '4'.  We can't control what version is exported from the server,
# but we can approximate most behaviors by doing a lofs shadow of $NFS_ROOT.
#
# The test should create files in $NFS_ROOT, and then either mount it
# explicitly (only possible with NFSv4) or do a lofs mount of the local share.
#

. $ST_TOOLS/utility.ksh

[[ -z $ST_NFS_PATH ]] && exit $ST_SKIP
[[ -z $NFS_VERSION ]] && NFS_VERSION=4

NFS_MOUNTP=$ST_ROOT/$(basename $0).nfs.$$

mkdir $NFS_MOUNTP || fail "failed to create $NFS_MOUNTP"

mount -F nfs -o vers=$NFS_VERSION $ST_NFS_HOST:$ST_NFS_PATH \
    $NFS_MOUNTP || fail "fail to mount $ST_NFS_HOST:$ST_NFS_PATH"

NFS_SUBDIR=$(basename $0).$$
NFS_ROOT=$NFS_MOUNTP/$NFS_SUBDIR
ST_NFS_PATH=$ST_NFS_PATH/$NFS_SUBDIR

mkdir $NFS_ROOT || fail "failed to create $NFS_ROOT"
chmod 777 $NFS_ROOT || Fail "failed to chmod $NFS_ROOT"

function nfs_unmount
{
	rm -rf $NFS_ROOT || fail "failed to remove contents of NFS dir"
	umount -f $NFS_MOUNTP || fail "failed to unmount $NFS_MOUNTP"
	rmdir $NFS_MOUNTP || fail "failed to remove $NFS_MOUNTP"
}
