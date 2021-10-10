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

. /usr/lib/brand/solaris/common.ksh

# States
# ZONE_STATE_CONFIGURED           0 (never see)
# ZONE_STATE_INCOMPLETE           1 (never see)
# ZONE_STATE_INSTALLED            2
# ZONE_STATE_READY                3
# ZONE_STATE_RUNNING              4
# ZONE_STATE_SHUTTING_DOWN        5
# ZONE_STATE_DOWN                 6
# ZONE_STATE_MOUNTED              7

# cmd
#
# ready			0
# boot			1
# halt			4

ZONENAME=$1
ZONEPATH=$2
state=$3
cmd=$4
ALTROOT=$5

typeset zone
init_zone zone "$ZONENAME" "$ZONEPATH"
eval $(bind_legacy_zone_globals zone)

# If we're not readying the zone, then just return.
case $cmd in
	0)
		mount_active_be zone || exit $ZONE_SUBPROC_NOTCOMPLETE
		;;
	1)
		# Mount the children of the BE that is mounted (and mount the
		# root of the BE too, if needed).  This is needed so that the
		# boot script can verify the sanity of the BE, including parts
		# that may be in a separate /var.  The boot hook unmounts these
		# non-root ZFS file systems before zoneadmd starts the in-zone
		# boot process.
		mount_active_be -C zone || exit $ZONE_SUBPROC_NOTCOMPLETE
		;;
esac

exit $ZONE_SUBPROC_OK
