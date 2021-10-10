#!/usr/sbin/sh
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

# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.

#
# basic setup
#
GRUBDISKMAP=/system/volatile/solaris_grubdisk.map

/bin/rm -f "$GRUBDISKMAP"
/bin/touch "$GRUBDISKMAP"
/usr/sbin/biosdev 2> /dev/null | while read diskno diskpath
    do
	devname=`/bin/ls -l /dev/rdsk/*p0 | /bin/grep $diskpath | /bin/nawk '{ print $9 }'`
	ctdname=`echo $devname | /bin/sed "s#/dev/rdsk/##" | /bin/sed "s#p0##"`
	grubdisk=`echo $diskno | /bin/sed "s/0x8//"`
	echo "$grubdisk $ctdname $diskpath" >> "$GRUBDISKMAP"
    done

# cleanup
