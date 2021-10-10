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

m_usage=$(gettext "solaris10 brand usage:\n\tinstall -u | -p [-v | -s] -a archive | -d directory [-c sysidcfg].\n\tThe -a archive option specifies an archive name which can be a flar,\n\ttar, pax or cpio archive.\n\tThe -d directory option specifies an existing directory.\n\tThe -u option unconfigures the zone, -p preserves the configuration.\n\tThe -c option gives a sysidcfg file and causes an unconfiguration of the zone.")

no_install=$(gettext "Could not create install directory '%s'")

install_fail=$(gettext  "        Result: *** Installation FAILED ***")
install_log=$(gettext   "      Log File: %s")

install_good=$(gettext  "        Result: Installation completed successfully.")

sanity_ok=$(gettext     "  Sanity Check: Passed.  Looks like a Solaris 10 system.")
sanity_fail=$(gettext   "  Sanity Check: FAILED (see log for details).")


p2ving=$(gettext        "Postprocessing: This may take a while...")
p2v_prog=$(gettext      "   Postprocess: ")
p2v_done=$(gettext      "        Result: Postprocessing complete.")
p2v_fail=$(gettext      "        Result: Postprocessing failed.")

media_missing=\
$(gettext "you must specify an installation source using '-a', '-d' or '-r'.\n%s")

cfgchoice_missing=\
$(gettext "you must specify -u (sys-unconfig) or -p (preserve identity).\n%s")

mount_failed=$(gettext "ERROR: zonecfg(1M) 'fs' mount failed")

not_flar=$(gettext "Input is not a flash archive")
bad_flar=$(gettext "Flash archive is a corrupt")
unknown_archiver=$(gettext "Archiver %s is not supported")

# Clean up on interrupt
trap_cleanup() {
	log "$m_interrupt"

	trap_exit
}

# If the install failed then clean up the ZFS datasets we created.
trap_exit() {
	# trap_cleanup calls trap_exit.  Do not run trap_exit twice.
	trap - INT EXIT
	# umount any mounted file systems
	[[ -n "$fstmpfile" ]] && umnt_fs

	if (( $EXIT_CODE != $ZONE_SUBPROC_OK )); then
		delete_unpinned_datasets "${zone.path.ds}"
		#
		# If cleanup completed, don't force the use of zoneadm
		# uninstall, as it will have no work to do.  Note that the
		# variable ZONE_SUBPROC_NOTCOMPLETE is poorly named - it
		# does not force the zone into the incomplete state.
		#
		if (( $? == 0 && $EXIT_CODE == $ZONE_SUBPROC_FATAL )); then
			EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
		fi
		unpin_datasets "${zone.path.ds}" || error "$f_unpin"
	fi

	vlog "Exiting with exit code $EXIT_CODE"
	finish_log zone

	exit $EXIT_CODE
}

#
# The main body of the script starts here.
#
# This script should never be called directly by a user but rather should
# only be called by zoneadm to install a s10 system image into a zone.
#

# If we weren't passed at least two arguments, exit now.
(( $# < 2 )) && exit $ZONE_SUBPROC_USAGE

typeset zone
init_zone zone "$1" "$2"
eval $(bind_legacy_zone_globals zone)

shift; shift	# remove ZONENAME and ZONEPATH from arguments array

# Clear the child dataset list - solaris10 should not create them.
set -A zone.new_be_datasets

#
# Exit code to return if install is interrupted or exit code is otherwise
# unspecified.
#
EXIT_CODE=$ZONE_SUBPROC_USAGE

unset inst_type
unset msg
unset silent_mode
unset OPT_V
unset OPT_C

#
# It is worth noting here that we require the end user to pick one of
# -u (sys-unconfig) or -p (preserve config).  This is because we can't
# really know in advance which option makes a better default.  Forcing
# the user to pick one or the other means that they will consider their
# choice and hopefully not be surprised or disappointed with the result.
#
unset unconfig_zone
unset preserve_zone
unset SANITY_SKIP

while getopts "a:c:d:Fpr:suv" opt
do
	case "$opt" in
		a)
			if [[ -n "$inst_type" ]]; then
				fatal "$incompat_options" "$m_usage"
			fi
		 	inst_type="archive"
			install_media="$OPTARG"
			;;
		c)	OPT_C="-c $OPTARG" ;;
		d)
			if [[ -n "$inst_type" ]]; then
				fatal "$incompat_options" "$m_usage"
			fi
		 	inst_type="directory"
			install_media="$OPTARG"
			;;
		F)	SANITY_SKIP=1;;
		p)	preserve_zone="-p";;
		r)
			if [[ -n "$inst_type" ]]; then
				fatal "$incompat_options" "$m_usage"
			fi
		 	inst_type="stdin"
			install_media="$OPTARG"
			;;
		s)	silent_mode=1;;
		u)	unconfig_zone="-u";;
		v)	OPT_V="-v";;
		*)	printf "$m_usage\n"
			exit $ZONE_SUBPROC_USAGE;;
	esac
done
shift OPTIND-1

# The install can't be both verbose AND silent...
if [[ -n $silent_mode && -n $OPT_V ]]; then
	fatal "$incompat_options" "$m_usage"
fi

if [[ -z $install_media ]]; then
	fatal "$media_missing" "$m_usage"
fi

# The install can't both preserve and unconfigure
if [[ -n $unconfig_zone && -n $preserve_zone ]]; then
	fatal "$incompat_options" "$m_usage"
fi

# Must pick one or the other.
if [[ -z $unconfig_zone && -z $preserve_zone ]]; then
	fatal "$cfgchoice_missing" "$m_usage"
fi

#
# From here on out, an unspecified exit or interrupt should exit with
# ZONE_SUBPROC_FATAL, meaning a user will need to do an uninstall before
# attempting another install, as we've modified the directories we were going
# to install to in some way.  Note, however, that this is influenced by
# pin_datasets() and trap_exit().
#
trap trap_cleanup INT
trap trap_exit EXIT
EXIT_CODE=$ZONE_SUBPROC_FATAL

start_log zone install "${save_args[@]}"
vlog "Starting pre-installation tasks."
pin_datasets "${zone.path.ds}" || fatal "$f_pin"

vlog "Installation started for zone \"${zone.name}\""
install_image zone "$inst_type" "$install_media"

[[ "$SANITY_SKIP" == "1" ]] && touch "${zone.root}/.sanity_skip"

#
# Run p2v.
#
# Getting the output to the right places is a little tricky because what
# we want is for p2v to output in the same way the installer does: verbose
# messages to the log file always, and verbose messages printed to the
# user if the user passes -v.  This rules out simple redirection.  And
# we can't use tee or other tricks because they cause us to lose the
# return value from the p2v script due to the way shell pipelines work.
#
# The simplest way to do this seems to be to hand off the management of
# the log file to the p2v script.  So we run p2v with -l to tell it where
# to find the log file and then reopen the log (O_APPEND) when p2v is done.
#
log "$p2ving"
vlog "running: p2v $OPT_V $unconfig_zone $OPT_C ${zone.name} ${zone.path}"

/usr/lib/brand/solaris10/p2v -m "$p2v_prog" \
     $OPT_V $unconfig_zone $OPT_C "${zone.name}" "${zone.path}"
p2v_result=$?
if (( $p2v_result == 0 )); then
	vlog "$p2v_done"
else
	log "$p2v_fail"
	log "\n$install_fail"
	log "$install_log" "$LOGFILE"
	exit $ZONE_SUBPROC_FATAL
fi

# Add a service tag for this zone.
add_svc_tag "${zone.name}" "install $inst_type $(basename $install_media)"

log "\n$install_good" "${zone.name}"

mount_active_be -c zone || fatal "$f_mount_active_be"
unpin_datasets "${zone.path.ds}" || error "$f_unpin"
finish_log zone logfile
trap - EXIT

# This needs to be set since the exit trap handler is going run.
EXIT_CODE=$ZONE_SUBPROC_OK

exit $ZONE_SUBPROC_OK
