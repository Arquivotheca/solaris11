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

#
# common shell script functions
#
. /usr/lib/brand/solaris/common.ksh
. /usr/lib/brand/shared/uninstall.ksh

trap_exit() {
	finish_log zone
	exit $EXIT_CODE
}
EXIT_CODE=$ZONE_SUBPROC_USAGE

#
# options processing
#

# If we weren't passed at least two arguments, exit now.
(( $# < 2 )) && fail_fatal "$f_abort"

# used by start_log()
set -A save_args "$0" "$@"

typeset zone
init_zone zone "$1" "$2"
eval $(bind_legacy_zone_globals zone)
shift 2

trap trap_exit EXIT

options="FhHnv"
options_repeat=""
options_seen=""

opt_F=""
opt_n=""
opt_v=""

# check for bad or duplicate options
OPTIND=1
while getopts $options OPT ; do
case $OPT in
	\? ) usage_err ;; # invalid argument
	: ) usage_err ;; # argument expected
	* )
		opt=`echo $OPT | sed 's/-\+//'`
		if [ -n "$options_repeat" ]; then
			echo $options_repeat | grep $opt >/dev/null
			[ $? = 0 ] && break
		fi
		( echo $options_seen | grep $opt >/dev/null ) &&
			usage_err
		options_seen="${options_seen}${opt}"
		;;
esac
done

# check for a help request
OPTIND=1
while getopts :$options OPT ; do
case $OPT in
	h|H ) usage
esac
done

# process options
OPTIND=1
while getopts :$options OPT ; do
case $OPT in
	F) opt_F="-F" ;;
	n) opt_n="-n" ;;
	v) opt_v="-v" ;;
esac
done
shift `expr $OPTIND - 1`

[ $# -gt 0 ]  && usage_err

#
# main
#
EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
start_log zone uninstall "${save_args[@]}"

nop=""
if [[ -n "$opt_n" ]]; then
	nop="echo"
	#
	# in '-n' mode we should never return success (since we haven't
	# actually done anything). so override ZONE_SUBPROC_OK here.
	#
	ZONE_SUBPROC_OK=$ZONE_SUBPROC_FATAL
fi

get_current_gzbe

# find all the zone BEs associated with this global zone BE.
typeset -a belist
if [[ -n "$CURRENT_GZBE" ]]; then
	zfs list -H -t filesystem -r -d 1 -o \
	    $PROP_PARENT,$PROP_ACTIVE,$PROP_CANDIDATE,name "${zone.ROOT_ds}" \
	    2>/dev/null | while IFS=$'\t' read parent active candidate fs; do

		# Skip the ROOT dataset
		[[ "$fs" == "${zone.ROOT_ds}" ]] && continue

		zbe=$(basename "$fs")
		#
		# match by PROP_PARENT uuid.  If the uuid is not set ("-"), the
		# BE is invalid (interrupted install?) and should be deleted.
		#
		if [[ $parent == "-" || $parent == "${CURRENT_GZBE}" ]]; then
			a_push belist "$zbe"
			continue
		fi

		#
		# If 'install -a' or 'attach -a' extracted multiple ZBEs and
		# could not figure out which one to attach, the multiple ZBEs
		# may have been left behind.  If 'attach -z' will not be used
		# to attach one of those ZBEs, then 'mark incomplete' can
		# be used to allow uninstall to clean up these extracted
		# (candidate) ZBEs.
		#
		if [[ $candidate == "$CURRENT_GZBE" ]]; then
			a_push belist "$zbe"
			continue
		fi
	done
fi

destroy_zone_datasets zone -b belist

finish_log zone
# Set exit code for trap handler
EXIT_CODE=$ZONE_SUBPROC_OK
exit $ZONE_SUBPROC_OK
