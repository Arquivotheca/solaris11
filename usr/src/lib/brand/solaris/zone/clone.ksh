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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

. /usr/lib/brand/solaris/common.ksh

m_usage=$(gettext "clone [-c profile.xml | dir] {sourcezone}\n\tThe -c option gives a profile or a directory of profiles to be applied to the system after clone.")

# Clean up on failure
trap_exit()
{
	if (( ZONE_IS_MOUNTED != 0 )); then
		error "$v_unmount"
		zoneadm -z $ZONENAME unmount
	fi

	if [[ $EXIT_CODE == $ZONE_SUBPROC_OK ]]; then
		# unmount the zoneroot if labeled brand
		is_brand_labeled
		(( $? == 1 )) && ( umount "${dst.root}" || \
		    log "$f_zfs_unmount" "${dst.root}" )
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
unset sc_config
typeset src dst
# Other brand clone options are invalid for this brand.
while getopts "R:z:c:" opt; do
	case $opt in
		R)	opt_R="$OPTARG" ;;
		z)	opt_z="$OPTARG" ;;
		c)	ls "$OPTARG" >/dev/null 2>&1
			[[ -f $OPTARG ]] || [[ -d $OPTARG ]] || fatal "$f_arg_not_file_or_dir" "$OPTARG"
			sc_config="$OPTARG"
			;;
		*)	fail_usage "";;
	esac
done
shift $((OPTIND-1))

if (($# < 1)); then
	fail_usage ""
fi

# Configuration profile file must have .xml suffix
if [[ -f $sc_config && $sc_config != *.xml ]]; then
	fail_usage "$f_scxml" "$sc_config"
fi

init_zone dst "$opt_z" "$opt_R"
init_zone src "$1"
shift

start_log dst clone "${save_args[@]}"
pin_datasets "${dst.path.ds}" || fatal "$f_pin"

EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
trap trap_exit EXIT

get_current_gzbe
get_active_be src || fatal "$e_no_active_be"

# From here on out the global variables referenced are for the destination zone
eval $(bind_legacy_zone_globals dst)

# Make dataset snapshots
snapshot_zone_rpool src "${dst}_snap%02d" snapname \
    || fail_incomplete "$f_zfs_snapshot"

# Make dataset clones.  This sets EXIT_CODE if datasets created.
clone_zone_rpool src dst "$snapname" || fail_fatal "$f_zone_clone"

ZONE_IS_MOUNTED=1

#
# Completion of reconfigure_zone will leave the zone root mounted for
# solaris brand zones.  The root won't be mounted for labeled brand zones.
#
is_brand_labeled || reconfigure_zone $sc_config

unpin_datasets "${dst.path.ds}" || error "$f_unpin"
finish_log dst
trap - EXIT
exit $ZONE_SUBPROC_OK
