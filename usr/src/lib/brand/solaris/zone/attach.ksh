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
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#

. /usr/lib/brand/solaris/common.ksh

# Allows developer to override some things like PATH and PYTHONPATH
. /usr/lib/brand/solaris/developerenv.ksh

m_attach_log=$(gettext "Log File: %s")
m_usage=$(gettext "Usage:\n\t attach [-uv] [-a archive | -d directory | -z zbe] [-c profile.xml | dir]\n\n\t-u\tUpdate the software in the attached zone boot environment to\n\t\tmatch the sofware in the global zone boot environment.\n\t-v\tVerbose.\n\t-c\tUpdate the zone configuration with the sysconfig profile\n\t\tspecified in the given file or directory.\n\t-a\tExtract the specified archive into the zone then attach the\n\t\tactive boot environment found in the archive.  The archive\n\t\tmay be a zfs, cpio, or tar archive.  It may be compressed with\n\t\tgzip or bzip2.\n\t-d\tCopy the specified directory into a new zone boot environment\n\t\tthen attach the zone boot environment.\n
\t-z\tAttach the specified zone boot environment.")

m_need_update=$(gettext  "                Evaluation: Packages in zone %s are out of sync with the global zone. To proceed, retry with the -u flag.")
m_cache=$(gettext        "                     Cache: Using %s.")
m_image_link=$(gettext   "  Updating non-global zone: Linking to image %s.")
m_image_audit=$(gettext  "  Updating non-global zone: Auditing packages.")
m_old_image=$(gettext    "  Updating non-global zone: Updating legacy packages.")
m_image_sync=$(gettext   "  Updating non-global zone: Syncing packages.")
m_image_update=$(gettext "  Updating non-global zone: Updating packages.")
m_sync_done=$(gettext    "  Updating non-global zone: Zone updated.")
m_complete=$(gettext     "                    Result: Attach Succeeded.")
m_failed=$(gettext       "                    Result: Attach Failed.")
m_active_zbe=$(gettext   "      Zone BE root dataset: %s")

f_sanity_variant=$(gettext "  Sanity Check: FAILED, couldn't determine %s from image.")
f_p2v=$(gettext "Could not update image variant to non-global.")
f_update=$(gettext "Could not update attaching zone")
e_dataset_not_in_be=$(gettext "Dataset %s mountpoint %s is not under zone root %s")
f_multiple_extractions=$(gettext "Zone already has one or more extracted zone boot environments.\nUse 'zoneadm -z <zone> attach -z <zbe>' to attach an existing zbe.\n")
f_bad_opt_combo=$(gettext "incompatible options -%s and %-s")
f_repeated_opt=$(gettext "repeated option -%s")
f_update_required=$(gettext "Attach failed. This zone must be attached with the -u or -U option.")

# Clean up on interrupt
trap_cleanup() {
	trap - INT

	log "$m_interrupt"

	# umount any mounted file systems
	umnt_fs

	trap_exit
}

# If the attach failed then clean up the ZFS datasets we created.
trap_exit() {
	#
	# Since trap_int calls trap_exit we need to cancel the exit
	# handler so that we don't do two passes.
	#
	trap - INT EXIT
	if [[ -n $EXIT_NOEXECUTE ]]; then
		# dryrun mode, nothing to do here; exit with whatever
		# EXIT_CODE is set to.
		;
	elif [[ $EXIT_CODE == $ZONE_SUBPROC_USAGE ]]; then
		# Usage message printed, nothing to do here.
		;
	elif [[ $EXIT_CODE == $ZONE_SUBPROC_OK ]]; then
		# unmount the zoneroot if labeled brand
		is_brand_labeled && ( umount $ZONEROOT || \
		    log "$f_zfs_unmount" "$ZONEPATH/root" )
		unpin_datasets "${zone.path.ds}" || error "$f_unpin"
	elif [[  $EXIT_CODE == $ZONE_SUBPROC_NOTCOMPLETE ]]; then
		unpin_datasets "${zone.path.ds}" || error "$f_unpin"
		log "$m_failed"
	else
		# Remove datasets that shouldn't exist
		if delete_unpinned_datasets "${zone.path.ds}"; then
			# If cleanup succeeded, don't force uninstall
			[[ $EXIT_CODE == $ZONE_SUBPROC_DETACHED ]] ||
			    EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
		fi
		unpin_datasets "${zone.path.ds}" || error "$f_unpin"
		log "$m_failed"
	fi

	vlog "Exiting with exit code $EXIT_CODE"
	finish_log zone

	exit $EXIT_CODE
}

EXIT_CODE=$ZONE_SUBPROC_USAGE
install_media="-"

# Will be used by start_log after option processing
set -A save_args "$0" "$@"

PKG=pkg
KEYDIR=/var/pkg/ssl

# If we weren't passed at least two arguments, exit now.
(( $# < 2 )) && exit $ZONE_SUBPROC_USAGE

zone=
init_zone zone "$1" "$2"
# Set ZONEPATH, etc.
eval $(bind_legacy_zone_globals zone)

shift; shift	# remove ZONENAME and ZONEPATH from arguments array

#
# Resetting GZ_IMAGE to something besides slash allows for simplified
# debugging of various global zone image configurations-- simply make
# an image somewhere with the appropriate interesting parameters.
#
GZ_IMAGE=${GZ_IMAGE:-/}
PKG_IMAGE=$GZ_IMAGE
export PKG_IMAGE

allow_update=none
noexecute=0

typeset -A opts		# Used in option compatibility checks.
unset inst_type
unset sc_config

# Get publisher information for global zone.  These structures are used
# to store information about the global zone publishers and
# incorporations.

typeset gz_incorporations=""

#
# If extracting an archive that contains multiple zbes, it is possible that
# we won't be able to automatically select which zbe to set as the active
# zbe.  In such a case, discover_active_be() will suggest the use of this
# command.  It may be augmented in getopts processing.
#
set -A ATTACH_Z_COMMAND zoneadm -z "${zone.name}" attach -z "<zbe>"

# Other brand attach options are invalid for this brand.
verbose=
while getopts "a:c:d:n:Uuvz:" opt; do
	opts[$opt]=1
	case $opt in
		a)	# If the path is automounted, [[ -f ... ]] does not
			# trigger a mount so we may get a false error.
			ls "$OPTARG" >/dev/null 2>&1
			[[ -f $OPTARG ]] || fatal "$f_arg_not_file" "$OPTARG"
		 	inst_type="archive"
			install_media="$OPTARG"
			;;
		c)	ls "$OPTARG" >/dev/null 2>&1
			[[ -f $OPTARG ]] || [[ -d $OPTARG ]] || fatal "$f_arg_not_file_or_dir" "$OPTARG"
			sc_config="$OPTARG"
			a_push ATTACH_Z_COMMAND -c "$OPTARG"
			;;
		d)	# If the path is automounted, [[ -d ... ]] does not
			# trigger a mount so we may get a false error.
			ls "$OPTARG" >/dev/null 2>&1
			[[ -d $OPTARG ]] || fatal "$f_arg_not_dir" "$OPTARG"
		 	inst_type="directory"
			install_media="$OPTARG"
			;;
		n)	noexecute=1
			EXIT_NOEXECUTE=1
			dryrun_mfst=$OPTARG
			;;
		u)	[[ $allow_update == all ]] && \
			    fatal "$f_bad_opt_combo" u U
			[[ $allow_update == min ]] && \
			    fatal "$f_repeated_opt" u
			allow_update=min
			a_push ATTACH_Z_COMMAND -u
			;;
		U)	
			[[ $allow_update == min ]] && \
			    fatal "$f_bad_opt_combo" u U
			[[ $allow_update == all ]] && \
			    fatal "$f_repeated_opt" U
			allow_update=all
			a_push ATTACH_Z_COMMAND -U
			;;
		v)	verbose=-v
			OPT_V=1		# used for vlog()
			;;
		z)	inst_type=zbe
			install_media="$OPTARG"
			;;
		?)	fail_usage "" ;;
	esac
done
shift $((OPTIND-1))

# Configuration profile file must have .xml suffix
if [[ -f $sc_config && $sc_config != *.xml ]]; then
	fail_usage "$f_scxml" "$sc_config"
fi

if [[ -n ${opts[a]} && -n ${opts[d]} ]]; then
	fail_usage "$f_incompat_options" a d
fi
if [[ -n ${opts[a]} && -n ${opts[z]} ]]; then
	fail_usage "$f_incompat_options" a z
fi
if [[ -n ${opts[d]} && -n ${opts[z]} ]]; then
	fail_usage "$f_incompat_options" d z
fi

get_current_gzbe

#
# Be sure that a previous attach -a didn't leave zbes behind.
#
if [[ $inst_type == archive || $inst_type == directory ]]; then
	/usr/sbin/zfs list -Hro name,$PROP_CANDIDATE "${zone.ROOT_ds}" \
	    2>/dev/null | while IFS='$\t' read name candidate ; do
		if [[ $candidate == "$CURRENT_GZBE" ]]; then
			fatal "$f_multiple_extractions"
		fi
	done
fi

[[ -z "$inst_type" ]] && inst_type="directory"

if [[ $noexecute == 1 ]]; then
	#
	# the zone doesn't have to exist when the -n option is used, so do
	# this work early.
	#

	# LIXXX There is no sw validation for IPS right now, so just pretend
	# everything will be ok.

	# Exit handler not yet active, so no need to worry about it.
	exit $ZONE_SUBPROC_OK
fi

trap trap_cleanup INT
trap trap_exit EXIT

EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
start_log zone attach "${save_args[@]}"
pin_datasets "${zone.path.ds}" || fatal "$f_pin"

enable_zones_services || exit $EXIT_CODE

attach_datasets -t "$inst_type" -m "$install_media" zone || exit $EXIT_CODE
migrate_export zone
migrate_rpool zone

log "$m_active_zbe" "${zone.active_ds}"

#
# Perform a sanity check to confirm that the image is not a global zone.
# If it is, fix it.
#
VARIANT=variant.opensolaris.zone
variant=$(LC_ALL=C $PKG -R $ZONEROOT variant -H $VARIANT)
[[ $? -ne 0 ]] && fatal "$f_sanity_variant" $VARIANT

echo $variant | IFS=" " read variantname variantval
[[ $? -ne 0 ]] && fatal "$f_sanity_variant" $VARIANT

# Check that we got the output we expect...
[[ $variantname == "$VARIANT" ]] || fatal "$f_sanity_variant" $VARIANT

# Check that the variant is non-global, else update it.
if [[ $variantval != "nonglobal" ]]; then
	/usr/lib/brand/solaris/p2v "${zone.name}" "${zone.root}" -X no-attach ||
	    fatal "$f_p2v"
fi

#
# We're done with the global zone: switch images to the non-global
# zone.
#
PKG_IMAGE="$ZONEROOT"

#
# If there is a cache, use it.
#
if [[ -f /var/pkg/pkg5.image && -d /var/pkg/publisher ]]; then
	# respect PKG_CACHEROOT if the caller has it set.
	[ -z "$PKG_CACHEROOT" ] && PKG_CACHEROOT=/var/pkg/publisher
	export PKG_CACHEROOT
	log "$m_cache" "$PKG_CACHEROOT"
fi

#
# pkg update-format doesn't allow a dry run or provide any other way to
# see if an update is needed.
#
log "$v_update_format"
if [[ $allow_update != none ]]; then
	$PKG update-format || pkg_err_check "$e_update_format"
fi

#
# Set the use-system-repo property.
#
LC_ALL=C $PKG set-property use-system-repo true
if [[ $? != 0 ]]; then
	log "\n$f_set_sysrepo_prop_fail"
	EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
	exit $EXIT_CODE
fi

#
# Update that catalogs once, subsequent packaging operations will use
# --no-refresh to avoid unnecessary catalog checks and updates.
#
LC_ALL=C $PKG refresh --full || pkg_err_check "$e_update_format"

#
# Reset the pkg image back to the global zone so that we link the
# zoneroot to that image.
#
PKG_IMAGE=$GZ_IMAGE

#
# Attach the zone to the global zone as a linked image.  This writes
# linked image metadata into the non-global zone which will constrain
# subsequent packaging operations.
#
set -A pkg_attach_args -- attach-linked --linked-md-only --no-refresh \
    --allow-relink -f $verbose
log "$m_image_link" $GZ_IMAGE
set -A cmd $PKG "${pkg_attach_args[@]}" -c zone:${zone.name} $ZONEROOT
vlog "Running '%s'" "${cmd[*]}"
LC_ALL=C "${cmd[@]}" || pkg_err_check "$f_update"

#
# Look for the 'entire' incorporation in the global zone.  We check for
# this because if the user has removed it then we'll want to remove it
# from the zone during attach.  The reason is that we're unlikely to be
# able to attach a highly constrained zone (ie, one that has entire) to
# a loosely constrainted global zone (ie, one that doesn't have entire
# installed).
#
gz_entire_fmri=$(get_entire_incorp)

#
# Reset the pkg image back to the nonglobal zone so that we can update
# it's package contents to be in sync with the global zone.
#
PKG_IMAGE=$ZONEROOT

#
# this is really gross.  before we bother to attach the image we need to
# verify that the image we're attaching doesn't have really old
# packages.  if it has old packages that don't have linked image
# metadata then a sync/update won't always sync the image.  old images
# are defined as pre-snv_168 images, but to make the check simpler we
# just look for pkg:///system/core-os (which was introduced in snv_170).
#
old_image=1
LC_ALL=C $PKG list pkg:///system/core-os >/dev/null 2>&1
[[ $? == 0 ]] && old_image=0

if [[ $old_image != 0 && $allow_update == none ]]; then
	log "\n$f_update_required"
	EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
	exit $EXIT_CODE
elif [[ $old_image != 0 && $allow_update != none ]]; then
	#
	# Because this is an old image we can't just sync/update it.  We
	# have to explicitly install newer packages that contain parent
	# dependencies.  Unfortunately we also need to specify any
	# package which has an install hold so we can relax those
	# install holds.
	#
	set -A pkg_install_args -- install -I $verbose --no-refresh --accept

	# check if the ngz has entire installed
	ngz_entire_fmri=$(get_entire_incorp)
	if [[ -z $gz_entire_fmri ]]; then
		a_push pkg_install_args --reject pkg:///entire
	elif [[ -n "$ngz_entire_fmri" ]]; then
		a_push pkg_install_args pkg:///$ngz_entire_fmri
	fi
	# relax install holds
	a_push pkg_install_args 'pkg:///consolidation/*/*-incorporation'
	a_push pkg_install_args pkg:///system/core-os
	set -A cmd $PKG "${pkg_install_args[@]}"
	log "$m_old_image"
	vlog "Running '%s'" "${cmd[*]}"
	LC_ALL=C "${cmd[@]}" || pkg_err_check "$f_update"
fi

# Assemble the arguments to sync the zone image
case $allow_update in
	none )
		log_msg=$m_image_audit
		set -A pkg_sync_args -- sync-linked --no-pkg-updates
		;;
	min )
		log_msg=$m_image_sync
		set -A pkg_sync_args -- sync-linked
		;;
	all )
		log_msg=$m_image_update
		set -A pkg_sync_args -- update -f
		;;
	* )
		fail_internal "Invalid allow_update value: $allow_update"
esac
a_push pkg_sync_args -I $verbose --no-refresh --accept
[[ $allow_update != none && -z $gz_entire_fmri ]] &&
	a_push pkg_sync_args --reject pkg:///entire

# Sync the zone image.
log "$log_msg"
set -A cmd $PKG "${pkg_sync_args[@]}"
vlog "Running '%s'" "${cmd[*]}"
LC_ALL=C "${cmd[@]}" || pkg_err_check "$f_update"

log "\n$m_sync_done"
log "$m_complete"

trap - EXIT
unpin_datasets "${zone.path.ds}" || error "$f_unpin"
finish_log zone
exit $ZONE_SUBPROC_OK
