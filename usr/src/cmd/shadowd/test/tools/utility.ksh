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
# This file contains a number of useful routines for ksh test scripts.
#

TST_ROOT=$ST_ROOT/$(basename $0).$$
TST_SROOT=$ST_ROOT/$(basename $0).$$.shadow
TST_DATASET=$ST_DATASET/$(basename $0).$$
TST_RDATASET=$ST_DATASET/$(basename $0).$$.root
TST_RUNDIR=
PATH=$(dirname $0):$ST_TOOLS:/usr/bin:$PATH

function fail
{
	echo "$(basename $0): $*" >& 2
	tst_shadow_enable
	tst_spacemap_enable
	stresume
	exit 1
}

function tst_populate_source
{
	SRCFS=$1

	SAVEDIR=`pwd`

	cd /kernel/drv
	find . -type f | head -20 | sort | xargs -L 1 cksum > $FILE_LIST

	LEFT=-20
	for d in d1 d2 d3 d4; do
		rm -rf $SRCFS/$d
		mkdir -p $SRCFS/$d
		cat $FILE_LIST | awk '{print $3}' | head -20 | \
		    tail $LEFT  | head -5 | cpio -pu $SRCFS/$d 2>/dev/null
		LEFT=`expr $LEFT + 5`
	done

	cd $SAVEDIR
}

function tst_shadowd_enable
{
	svcadm enable shadowd || \
	    fail "Unable to disable shadowd"
	sleep 5
}

function tst_shadowd_disable
{
	svcadm disable shadowd || \
	    fail "Unable to disable shadowd"
	sleep 5
}

function tst_shadow_disable
{
	echo "vfs_shadow_disable/W1" | mdb -kw > /dev/null || \
	    fail "failed to disable shadow processing"
}

function tst_shadow_enable
{
	echo "vfs_shadow_disable/W0" | mdb -kw > /dev/null || \
	    fail "failed to enable shadow processing"
}

function tst_spacemap_disable
{
	echo "vfs_shadow_spacemap_disable/W1" | mdb -kw > /dev/null || \
	    fail "failed to disable space map compression"
}

function tst_spacemap_enable
{
	echo "vfs_shadow_spacemap_disable/W0" | mdb -kw > /dev/null || \
	    fail "failed to disable space map compression"
}

function tst_set_timeout
{
	echo "vfs_shadow_timeout_freq/W0t$1" | mdb -kw > /dev/null || \
	    fail "failed to set timeout frequency"
}

function tst_create_root
{
	mkdir $TST_ROOT || fail "failed to make root directory"
}

function tst_create_root_dataset
{
	zfs create $* -o mountpoint=$TST_ROOT $TST_RDATASET || \
	    fail "failed to create root"
}

function tst_create_dataset
{
	zfs create $* -o mountpoint=$TST_SROOT \
	    -o shadow=file://$TST_ROOT $TST_DATASET || \
	    fail "failed to create dataset"
	TST_RUNDIR=/var/run/zfs/shadow/$(zfsguid $TST_DATASET)
}

function tst_create_dataset_nfs_src
{
	zfs create $* -o mountpoint=$TST_SROOT \
	    -o shadow=nfs://$TST_NFSHOST/$TST_NFSPATH $TST_DATASET || \
	    fail "failed to create dataset"
	TST_RUNDIR=/var/run/zfs/shadow/$(zfsguid $TST_DATASET)
}

function tst_create_legacy_dataset
{
	zfs create $* -o mountpoint=legacy $TST_DATASET || \
	    fail "failed to create dataset"
}

function tst_unmount_dataset
{
	zfs unmount $TST_DATASET || fail "failed to unmount dataset"
}

function tst_mount_dataset
{
	zfs mount $TST_DATASET || fail "failed to mount dataset"
}

function tst_clear_shadow
{
	zfs set shadow=none $TST_DATASET
}

function tst_destroy_dataset
{
	zfs destroy -r $TST_DATASET || fail "failed to destroy dataset"
}

function tst_destroy_root_dataset
{
	zfs destroy $TST_RDATASET || fail "failed to destroy root dataset"
}

function tst_create_ufs_root
{
	zfs create -s -V 1G $TST_RDATASET || fail "failed to create volume"
	newfs /dev/zvol/dsk/$TST_RDATASET > /dev/null 2>&1 || \
	    fail "failed to create UFS filesystem"
	[[ -d $TST_ROOT ]] || mkdir -p $TST_ROOT
	mount -F ufs /dev/zvol/dsk/$TST_RDATASET \
	    $TST_ROOT || fail "failed to mount UFS root filesystem"
}

function tst_create_ufs
{
	zfs create -s -V 1G $TST_DATASET || fail "failed to create volume"
	newfs /dev/zvol/dsk/$TST_DATASET > /dev/null 2>&1 || \
	    fail "failed to create UFS filesystem"
	[[ -d $TST_SROOT ]] || mkdir -p $TST_SROOT
	mount -F ufs -o shadow=$TST_ROOT /dev/zvol/dsk/$TST_DATASET \
	    $TST_SROOT || fail "failed to mount UFS filesystem"
}

function tst_destroy_ufs
{
	umount -f $TST_SROOT || fail "failed to unmount filesystem"
	zfs destroy $TST_DATASET || fail "failed to destroy volume"
	# not sure why this is necessary
	rm -f /dev/zvol/dsk/$TST_DATASET > /dev/null 2>&1
}

function tst_destroy_ufs_root
{
	umount -f $TST_ROOT || fail "failed to unmount root filesystem"
	zfs destroy $TST_RDATASET || fail "failed to destroy root volume"
	# not sure why this is necessary
	rm -f /dev/zvol/dsk/$TST_RDATASET > /dev/null 2>&1
}
