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
# Basic test that shadowd will drive a migration over NFS.
#

. $(dirname $0)/lib.nfs.src.ksh
TST_ROOT=$NFS_MOUNTP
SRC_DIR=shadowd.src.data.$$
DATA_DIR=$TST_ROOT/$SRC_DIR

FILE_LIST=/tmp/smtsfl.$$
STAT_OUT=/tmp/smtsss.$$
DIFF_OUT=/tmp/smtsdo.$$
SAVE_NFSPATH=$TST_NFSPATH

function nfs_src_clean
{
	TST_NFSPATH=$SAVE_NFSPATH
	NFS_MOUNTP=/tmp/smtsmnt.$$
	mkdir -p $NFS_MOUNTP
	nfs_mount
	nfs_unmount
	rm -f $FILE_LIST
	rm -f $STAT_OUT
	rm -f $DIFF_OUT
}

nfs_mount
mkdir -p $DATA_DIR || fail "Unable to make source data directory"
tst_populate_source $DATA_DIR
nfs_unmount_nodestroy

TST_NFSPATH=$TST_NFSPATH/$SRC_DIR

mkdir $TST_SROOT || fail "Unable to make shadow mountpoint"

tst_create_dataset_nfs_src

# Start automated migration
tst_shadowd_enable

# Shadowstat should display statistics until migration completes
/usr/sbin/shadowstat >$STAT_OUT 2>&1
stat_result=$?

# End automated migration
tst_shadowd_disable

[ "$stat_result" != "0" ] && cat $STAT_OUT && nfs_src_clean && \
    fail "shadowstat exited with non-zero status"

grep $TST_RUNDIR /etc/mnttab >/dev/null && nfs_src_clean && \
    fail "shadow source still mounted"

SAVEDIR=`pwd`
cd $TST_SROOT
find . -type f | sort | xargs -L 1 cksum | \
    sed -e "s:/d.::" | diff $FILE_LIST - >$DIFF_OUT 2>&1
diff_result=$?
cd $SAVEDIR

[ "$diff_result" != "0" ] && cat $DIFF_OUT && nfs_src_clean && \
    fail "Unexpected difference between source and migrated"

nfs_src_clean

tst_destroy_dataset
