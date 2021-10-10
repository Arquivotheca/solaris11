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
#
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#

. /lib/svc/share/smf_include.sh
. /lib/svc/share/fs_include.sh

UPDATEFILE=$SMF_SYSVOL_FS/boot_archive_safefile_update

smf_assert_globalzone

# on x86 get rid of transient reboot entry in the GRUB menu
#
if [ `uname -p` = "i386" ]; then
	if [ -f /stubboot/boot/grub/menu.lst ]; then
		/usr/sbin/bootadm -m update_temp -R /stubboot
	else
		/usr/sbin/bootadm -m update_temp
	fi
fi

if [ -f $UPDATEFILE ] || [ -f /reconfigure ]; then
	/usr/sbin/rtc -c > /dev/null 2>&1
	/usr/sbin/bootadm update-archive
	rm -f $UPDATEFILE
fi

exit $SMF_EXIT_OK
