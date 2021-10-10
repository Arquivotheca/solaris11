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
# image_install is used when installing a zone in a 'p2v' scenario.  In
# this case the zone install hook will branch off to this script which
# is responsible for setting up the physical system image in the zonepath
# and performing the various modifications necessary to enable a physical
# system image to run inside a zone.  This script sets up the image in the
# zonepath then calls the p2v script to modify the image to run in a zone.
#

. /usr/lib/brand/solaris/common.ksh

# Allows developer to override some things like PATH and PYTHONPATH
. /usr/lib/brand/solaris/developerenv.ksh
m_usage=$(gettext "\n        install {-a archive|-d path|-z zbe} {-p|-u} [-s|-v] [-c profile.xml | dir]")

install_log=$(gettext   "    Log File: %s")

p2ving=$(gettext        "Postprocessing: This may take a while...")
p2v_prog=$(gettext      "   Postprocess: ")
p2v_done=$(gettext      "        Result: Postprocessing complete.")
p2v_fail=$(gettext      "        Result: Postprocessing failed.")
m_postnote3=$(gettext "              Make any other adjustments, such as disabling SMF services\n              that are no longer needed.")

media_missing=\
$(gettext "%s: you must specify an installation source using '-a' or '-d'.")
cfgchoice_missing=\
$(gettext "you must specify -u (configure) or -p (preserve identity).")

# Clean up on interrupt
trap_cleanup()
{
	log "$m_interrupt"

	trap_exit
}

# If the install failed then clean up the ZFS datasets we created.
trap_exit()
{
	# trap_cleanup calls trap_exit.  Don't make multiple passes.
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
# only be called by pkgcreatezone to install an OpenSolaris system image into
# a zone.
#

#
# Exit code to return if install is interrupted or exit code is otherwise
# unspecified.
#
EXIT_CODE=$ZONE_SUBPROC_USAGE

# Used by start_log
set -A save_args "$0" "$@"

# If we weren't passed at least two arguments, exit now.
(( $# < 2 )) && exit $ZONE_SUBPROC_USAGE

init_zone zone "$1" "$2"
eval $(bind_legacy_zone_globals zone)
shift; shift	# remove zonename and zonepath from arguments array

unset inst_type
unset msg
unset silent_mode
unset verbose_mode
unset OPT_C

#
# It is worth noting here that we require the end user to pick one of
# -u (configure) or -p (preserve config).  This is because we can't
# really know in advance which option makes a better default.  Forcing
# the user to pick one or the other means that they will consider their
# choice and hopefully not be surprised or disappointed with the result.
#
unset unconfig_zone
unset preserve_zone

#
# If extracting an archive that contains multiple BEs, it is possible that
# we won't be able to automatically select which BE to set as the active
# zbe.  In such a case, discover_active_be() will suggest the use of this
# command.  It may be augmented in getopts processing.
#
set -A ATTACH_Z_COMMAND zoneadm -z "${zone.name}" attach -z "<zbe>"

while getopts "a:c:d:psuv" opt
do
	case "$opt" in
		a)
			if [[ -n "$inst_type" ]]; then
				fatal "$both_kinds" "zoneadm install"
			fi
			# [[ -f ... ]] does not trigger the automounter
			ls "$OPTARG" >/dev/null 2>&1
			[[ -f $OPTARG ]] || fatal "$f_arg_not_file" "$OPTARG"
		 	inst_type="archive"
			install_media="$OPTARG"
			;;
		c)	OPT_C="-c $OPTARG"
			a_push ATTACH_Z_COMMAND "-c" "$OPTARG"
			;;
		d)
			if [[ -n "$inst_type" ]]; then
				fatal "$both_kinds" "zoneadm install"
			fi
			# [[ -d ... ]] does not trigger the automounter
			ls "$OPTARG" >/dev/null 2>&1
			[[ -d $OPTARG ]] || fatal "$f_arg_not_dir" "$OPTARG"
		 	inst_type="directory"
			install_media="$OPTARG"
			;;
		p)	preserve_zone="-p";;
		s)	silent_mode=1;;
		u)	unconfig_zone="-u"
			#
			# attach doesn't have an "unconfigure" option, so if
			# attach -z will be needed, provide an appropriate
			# option for a sysconfig profile that will do the
			# unconfigure.
			#
			a_push ATTACH_Z_COMMAND "-c" \
			    /usr/share/auto_install/sc_profiles/enable_sci.xml
			;;
		v)	verbose_mode="-v"
			a_push ATTACH_Z_COMMAND "-v"
			;;
		*)	exit $ZONE_SUBPROC_USAGE;;
	esac
done
shift OPTIND-1

# The install can't be both verbose AND silent...
[[ -n $silent_mode && -n $verbose_mode ]] && \
    fail_usage "$f_incompat_options" "-s" "-v"

[[ -z $install_media ]] && fail_usage "$media_missing" "zoneadm install"

# The install can't both preserve and unconfigure
[[ -n $unconfig_zone && -n $preserve_zone ]] && \
    fail_usage "$f_incompat_options" "-u" "-p"

# Must pick one or the other.
[[ -z $unconfig_zone && -z $preserve_zone ]] && fail_usage "$cfgchoice_missing"

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

start_log zone attach "${save_args[@]}"
vlog "Starting pre-installation tasks."
pin_datasets "${zone.path.ds}" || fatal "$f_pin"

# ZONEROOT was created by our caller (pkgcreatezone)

vlog "Installation started for zone \"$ZONENAME\""
install_image zone "$inst_type" "$install_media"

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
vlog "running: p2v $verbose_mode $unconfig_zone $OPT_C $ZONENAME $ZONEPATH"
/usr/lib/brand/solaris/p2v $verbose_mode $unconfig_zone $OPT_C \
    $ZONENAME $ZONEPATH
p2v_exit=$?
if (( $p2v_exit != 0 )); then
	EXIT_CODE=$p2v_exit
	log "$p2v_fail"
	fatal "\n$install_fail"
fi
vlog "$p2v_done"

mount_active_be -c zone || fatal "$f_mount_active_be"
unpin_datasets "${zone.path.ds}" || error "$f_unpin"
finish_log zone logfile
trap - EXIT

# Do not leave the BE mounted for labeled zones.
[[ ${zone.brand} == labeled ]] && unmount_be zone

log "\n$m_complete" ${SECONDS}
printf "$install_log\n" "$logfile"
printf "$m_postnote\n"
printf "$m_postnote2\n"
printf "$m_postnote3\n"

exit $ZONE_SUBPROC_OK
