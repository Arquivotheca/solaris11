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

. /usr/lib/brand/solaris/common.ksh

typeset zone
init_zone zone "$1" "$2" || {
	error "Usage: %s zone zonepath" "$0"
	exit $ZONE_SUBPROC_USAGE
}

# Mount the active boot environment on the zoneroot.
mount_active_be -c zone || exit $ZONE_SUBPROC_NOTCOMPLETE

enable_zones_services || exit $ZONE_SUBPROC_NOTCOMPLETE

exit $ZONE_SUBPROC_OK
