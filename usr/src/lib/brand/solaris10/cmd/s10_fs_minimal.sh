#!/sbin/sh
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

# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T.
# All rights reserved.
#
# THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
# The copyright notice above does not evidence any
# actual or intended publication of such source code.
#

#
# The following is used to detect if the fs minimal script has been replaced
# THIS_IS_SOLARIS10_BRAND_FS_MINIMAL
#

# Do the real s10 fs-minimal
/lib/svc/method/fs-minimal.pre_p2v || exit $?

. /lib/svc/share/fs_include.sh

#
# The following three updates to /var are done after fs-minimal, as
# fs-minimal mounts /var.  Performing these updates before fs-minimal would
# cause them to be hidden if /var is mounted.  They could also cause /var
# to fail to mount.
#

#
# /var 1, mount /var/run to /etc/svc/volatile to allow native services to find
# doors in /var/run.
#
mounted /var/run - lofs < /etc/mnttab
if [ $? -ne 0 ] ; then
	mountfs -O /var/run lofs - /etc/svc/volatile || exit $SMF_EXIT_ERR_FATAL
fi

#
# /var 2, install network managment service.  Manifest import depends on
# fs-minimal, so it will run after this.
#
/usr/bin/cp -f /.SUNWnative/lib/svc/manifest/network/network-ipmgmt.xml \
    /var/svc/manifest/network/network-ipmgmt.xml

# /var 3, create /var/dhcp directory for the native dhcp agent.
[ -h /var/dhcp ] && rm /var/dhcp
if [ ! -d /var/dhcp ] ; then
	mkdir /var/dhcp
	if [ $? -ne 0 ] ; then
		cecho "WARNING: - Unable to create /var/dhcp directory"
	fi
fi

exit $SMF_EXIT_OK
