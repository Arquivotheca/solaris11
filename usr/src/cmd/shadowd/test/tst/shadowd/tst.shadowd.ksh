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

. $ST_TOOLS/utility.ksh

tst_create_root
tst_create_root_dataset

FILE_LIST=/tmp/smtsffl.$$
STAT_OUT=/tmp/smtsfss.$$
DIFF_OUT=/tmp/smtsfdo.$$

function src_clean
{
	rm -f $FILE_LIST
	rm -f $STAT_OUT
	rm -f $DIFF_OUT
}

tst_populate_source $TST_ROOT

tst_create_dataset

# Start automated migration
tst_shadowd_enable

# Shadowstat should display statistics until migration completes
/usr/sbin/shadowstat >$STAT_OUT 2>&1
stat_result=$?

# End automated migration
tst_shadowd_disable

[ "$stat_result" != "0" ] && cat $STAT_OUT && src_clean && \
    fail "shadowstat exited with non-zero status"

grep $TST_RUNDIR /etc/mnttab >/dev/null && src_clean && \
    fail "shadow source still mounted"

SAVEDIR=`pwd`
cd $TST_SROOT
find . -type f | sort | xargs -L 1 cksum | \
    sed -e "s:/d.::" | diff $FILE_LIST - >$DIFF_OUT 2>&1
diff_result=$?
cd $SAVEDIR

[ "$diff_result" != "0" ] && cat $DIFF_OUT && src_clean && \
    fail "Unexpected difference between source and migrated"

src_clean

tst_destroy_dataset

tst_destroy_root_dataset
