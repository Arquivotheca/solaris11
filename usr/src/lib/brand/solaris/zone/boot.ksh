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

ZONENAME=$1
ZONEPATH=$2

f_old_image_fail=$(gettext "Cannot boot zone.  The system was unable to verify that zone doesn't contain old and incompatible packages. Please perform a zoneadm(1m) detach followed by attach -u or attach -U to attempt to update the packages within the zone.")

f_audit_fail=$(gettext "Cannot boot zone.  The system was unable to verify that the packages installed in the zone are in sync with the global zone. Please perform a zoneadm(1m) detach followed by attach -u or attach -U to attempt to update the packages within the zone.")

PKG=pkg

init_zone zone "$ZONENAME" "$ZONEPATH"
eval $(bind_legacy_zone_globals zone)

enable_zones_services
if [[ $? -ne 0 ]]; then
	exit $ZONE_SUBPROC_NOTCOMPLETE
fi

#
# this is really gross.  before we bother to audit the image we need to
# verify that the image isn't really old (pre snv_168).  to make the
# check simpler we just look for pkg:///system/core-os (which was
# introduced in snv_170).
#
LC_ALL=C $PKG -R $ZONEROOT list pkg:///system/core-os >/dev/null
[[ $? != 0 ]] && fail_fatal "$f_old_image_fail"

# audit the image to make sure it's in sync with the global zone
set -A pkg_audit_args -- audit-linked -q -l zone:$ZONENAME
set -A cmd $PKG "${pkg_audit_args[@]}"
vlog "Running '%s'" "${cmd[*]}"
LC_ALL=C "${cmd[@]}" || pkg_err_check "$f_audit_fail"

#
# Run cluster hook
#
call_cluster_hook boot "$@"  || exit $?

/usr/lib/zones/zoneproxy-adm $ZONENAME
if [[ $? -ne 0 ]]; then
	exit $ZONE_SUBPROC_NOTCOMPLETE
fi

# Unmount all ZFS datsets except the root, because
# svc:/system/filesystem/minimal:default needs to mount them to ensure
# /etc/mnttab in the zone is correct.  Note that non-zfs file systems, such as
# /dev, will not be unmounted.
unmount_be -C zone || fatal "$f_unmount_be"

exit $ZONE_SUBPROC_OK
