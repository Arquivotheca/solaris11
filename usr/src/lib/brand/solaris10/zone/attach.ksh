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

. /usr/lib/brand/solaris10/common.ksh

m_attach_log=$(gettext "Log File: %s")
m_usage=$(gettext  "solaris10 brand usage:\n\tattach [-a archive | -d directory | -r recv_type] [-c sysidcfg]\n\tThe -a archive option specifies an archive name which can be a flar,\n\ttar, pax or cpio archive.\n\tThe -d directory option specifies an existing directory.\n\tThe -r recv_type option specifies the type of archive to be read from\n\tstdin.\n\tThe -c option gives a sysidcfg file and causes an unconfiguration of the zone.")
m_complete=$(gettext "Attach complete.")

install_fail=$(gettext  "*** Attach FAILED ***")
f_sysunconfig=$(gettext "Error: sys-unconfig failed.")

f_zfs=$(gettext "Error creating a ZFS file system (%s) for the zone.")
f_sanity_notzone=$(gettext "Error: this is a system image and not a zone image.")

# Clean up on interrupt
trap_cleanup()
{
	log "$m_interrupt"

	# umount any mounted file systems
	umnt_fs

	trap_exit
}

# If the attach failed then clean up the ZFS datasets we created.
trap_exit() {
	# trap_cleanup calls trap_exit.  Do not run trap_exit twice.
	trap - INT EXIT
	if [[ -n $EXIT_NOEXECUTE ]]; then
		# dryrun mode, nothing to do here; exit with whatever
		# EXIT_CODE is set to.
		;
	elif [[ $EXIT_CODE == $ZONE_SUBPROC_USAGE ]]; then
		# Usage message printed, nothing to do here.
		;
	elif [[ $EXIT_CODE == $ZONE_SUBPROC_OK ]]; then
		unpin_datasets "${zone.path.ds}" || error "$f_unpin"
	elif [[  $EXIT_CODE == $ZONE_SUBPROC_NOTCOMPLETE ]]; then
		unpin_datasets "${zone.path.ds}" || error "$f_unpin"
		log "$m_failed"
	else
		# Remove datasets that shouldn't exist
		delete_unpinned_datasets "${zone.path.ds}" &&
		    EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
		unpin_datasets "${zone.path.ds}" || error "$f_unpin"
		log "$m_failed"
	fi

	vlog "Exiting with exit code $EXIT_CODE"
	finish_log zone

	exit $EXIT_CODE
}

EXIT_CODE=$ZONE_SUBPROC_USAGE
install_media="-"
rm_ds=0

# If we weren't passed at least two arguments, exit now.
(( $# < 2 )) && exit $ZONE_SUBPROC_USAGE

# Used by start_log()
set -A save_args "$0" "$@"

zone=
init_zone zone "$1" "$2"
# Set ZONEPATH, etc.
eval $(bind_legacy_zone_globals zone)

# Clear the child dataset list - solaris10 should not create them.
set -A zone.new_be_datasets

shift; shift	# remove ZONENAME and ZONEPATH from arguments array

noexecute=0

unset inst_type
unset sc_sysidcfg

# Other brand attach options are invalid for this brand.
while getopts "a:c:d:nr:" opt; do
	case $opt in
		a)
			if [[ -n "$inst_type" ]]; then
				fatal "$incompat_options" "$m_usage"
			fi
		 	inst_type="archive"
			install_media="$OPTARG"
			;;
		c)	sc_sysidcfg="$OPTARG" ;;
		d)
			if [[ -n "$inst_type" ]]; then
				fatal "$incompat_options" "$m_usage"
			fi
		 	inst_type="directory"
			install_media="$OPTARG"
			# '-d -' means use the existing zonepath.
			if [[ "$install_media" == "$ZONEPATH" ]]; then
				install_media="-"
			fi
			;;
		n)	noexecute=1 ;;
		r)
			if [[ -n "$inst_type" ]]; then
				fatal "$incompat_options" "$m_usage"
			fi
		 	inst_type="stdin"
			install_media="$OPTARG"
			;;
		?)	fail_usage "" ;;
		*)	fail_usage "";;
	esac
done
shift $((OPTIND-1))

if [[ $noexecute == 1 && -n "$inst_type" ]]; then
	fatal "$m_usage"
fi

[[ -z "$inst_type" ]] && inst_type="directory"

if [ $noexecute -eq 1 ]; then
	#
	# The zone doesn't have to exist when the -n option is used, so do
	# this work early.
	#

	# XXX do the sw validation for solaris10 minimal patch level to ensure
	# everything will be ok.

	# Exit handler not yet active, so no need to worry about it.
	exit $ZONE_SUBPROC_OK
fi

trap trap_cleanup INT
trap trap_exit EXIT

EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
start_log zone attach "${save_args[@]}"
pin_datasets "${zone.path.ds}" || fatal "$f_pin"

log "$m_attach_log" "$LOGFILE"

attach_datasets -t "$inst_type" -m "$install_media" zone || exit $EXIT_CODE
migrate_export zone
migrate_rpool zone

mk_zone_dirs

#
# Perform a final check that this is really a zone image and not an archive of
# a system image which would need p2v.  Check for a well-known S10 SMF service
# that shouldn't exist in a zone.
#
if [[ -e ${zone.root}/var/svc/manifest/system/sysevent.xml ]]; then
	log "$f_sanity_notzone"
	exit $ZONE_SUBPROC_NOTCOMPLETE
fi

EXIT_CODE=$ZONE_SUBPROC_OK

log "$m_complete"

trap - EXIT
unpin_datasets "${zone.path.ds}" || error "$f_unpin"
finish_log zone
log "$m_attach_log" "$LOGFILE"

exit $ZONE_SUBPROC_OK
