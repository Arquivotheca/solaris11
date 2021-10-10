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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

. /usr/lib/brand/solaris10/common.ksh

m_usage=$(gettext "solaris10 brand usage: clone {sourcezone} [-c sysidcfg].")
f_sysunconfig=$(gettext "Error: sys-unconfig failed.")

# Clean up on failure
trap_exit()
{
	if (( ZONE_IS_MOUNTED != 0 )); then
		error "$v_unmount"
		zoneadm -z $ZONENAME unmount
	fi

	if [[ $EXIT_CODE == $ZONE_SUBPROC_OK ]]; then
		unpin_datasets "${dst.path.ds}" || error "$f_unpin"
	elif [[  $EXIT_CODE == $ZONE_SUBPROC_NOTCOMPLETE ]]; then
		unpin_datasets "${dst.path.ds}" || error "$f_unpin"
		log "$m_failed"
	else
		# Remove datasets that shouldn't exist
		delete_unpinned_datasets "${dst.path.ds}" &&
		    EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
		unpin_datasets "${dst.path.ds}" || error "$f_unpin"
		log "$m_failed"
	fi

	finish_log dst

	exit $EXIT_CODE
}

set -A save_args "$0" "$@"
EXIT_CODE=$ZONE_SUBPROC_USAGE

# Source and destination zones
unset sc_sysidcfg
typeset src dst
# Other brand clone options are invalid for this brand.
while getopts "R:z:c:" opt; do
	case $opt in
		R)	opt_R="$OPTARG" ;;
		z)	opt_z="$OPTARG" ;;
		c)	sc_sysidcfg="$OPTARG" ;;
		*)	fail_usage "";;
	esac
done
shift $((OPTIND-1))

if (( $# < 1 )); then
	fail_usage ""
fi

init_zone dst "$opt_z" "$opt_R"
init_zone src "$1"
shift

# Clear the child dataset list - solaris10 should not create them.
set -A src.new_be_datasets
set -A dst.new_be_datasets

start_log dst clone "${save_args[@]}"
pin_datasets "${dst.path.ds}" || fatal "$f_pin"

EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
ZONE_IS_MOUNTED=1
trap trap_exit EXIT

get_active_be src || fatal "$e_no_active_be"

# If the zonepath is not yet its own file system, create it.
eval $(bind_legacy_zone_globals dst)

# Make dataset snapshots
snapshot_zone_rpool src "${dst}_snap%02d" snapname \
    || fail_incomplete "$f_zfs_snapshot"

# Make dataset clones.  This sets EXIT_CODE if datasets are created.
clone_zone_rpool src dst "$snapname" || fail_fatal "$f_zone_clone"

# Don't re-sysunconfig if we've already done so
if [[ ! -f "${dst.root}"/etc/.UNCONFIGURED ]]; then
	/usr/sbin/zoneadm -z "${dst.name}" boot -f -- -m milestone=none
	if (( $? != 0 )); then
		error "$e_badboot"
		fail_incomplete "$f_sysunconfig"
	fi

	sysunconfig_zone
	if (( $? != 0 )); then
		/usr/sbin/zoneadm -z "${dst.name}" halt
		fail_incomplete "$f_sysunconfig"
	fi

	/usr/sbin/zoneadm -z "${dst.name}" halt
fi

# Copy in sysidcfg file
if [[ -n $sc_sysidcfg ]]; then
	safe_copy $sc_sysidcfg "${dst.root}"/etc/sysidcfg
fi

# Add a service tag for this zone.
add_svc_tag "${dst.name}" "clone ${src.name}"

finish_log dst
trap - EXIT
exit $ZONE_SUBPROC_OK
