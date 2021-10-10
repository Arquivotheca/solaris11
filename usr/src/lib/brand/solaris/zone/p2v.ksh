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

# NOTE: this script runs in the global zone and touches the non-global
# zone, so care should be taken to validate any modifications so that they
# are safe.

#
# Only change PATH if you give full consideration to GNU or other variants
# of common commands having different arguments and output.
#
export PATH=/usr/bin:/usr/sbin
unset LD_LIBRARY_PATH

. /usr/lib/brand/solaris/common.ksh

# Allows developer to override some things like PATH and PYTHONPATH
. /usr/lib/brand/solaris/developerenv.ksh

set -A save_args "$0" "$@"

PKG=pkg
EXIT_CODE=$ZONE_SUBPROC_USAGE

# Clean up on failure
trap_exit()
{
	if (( $ZONE_IS_MOUNTED != 0 )); then
		error "$v_unmount"
		zoneadm -z $ZONENAME unmount
	fi
	vlog "Exiting with exit code $EXIT_CODE"
	finish_log zone

	exit $EXIT_CODE
}

#
# Comment out most of the old mounts since they are either unneeded or
# likely incorrect within a zone.  Specific mounts can be manually
# reenabled if the corresponding device is added to the zone.
#
fix_vfstab()
{
	if [[ -h $ZONEROOT/etc/vfstab || ! -f $ZONEROOT/etc/vfstab ]]; then
		error "$e_badfile" "/etc/vfstab"
		return
	fi

	tmpfile=$(mktemp -t)
	if [[ -z "$tmpfile" ]]; then
		error "$e_tmpfile"
		return
	fi

	nawk '{
		if (substr($1, 0, 1) == "#") {
			print $0
		} else if ($1 == "fd" || $1 == "/proc" || $1 == "swap" ||
		    $1 == "ctfs" || $1 == "objfs" || $1 == "sharefs" ||
		    $4 == "nfs" || $4 == "lofs") {
			print $0
		} else {
			print "#", $0
			modified=1
		}
	}
	END {
		if (modified == 1) {
			printf("# Modified by p2v ")
			system("/usr/bin/date")
			exit 0
		}
		exit 1
	}' $ZONEROOT/etc/vfstab >>$tmpfile

	if (( $? == 0 )); then
		if [[ ! -f $ZONEROOT/etc/vfstab.pre_p2v ]]; then
			safe_copy $ZONEROOT/etc/vfstab \
			    $ZONEROOT/etc/vfstab.pre_p2v
		fi
		safe_copy $tmpfile $ZONEROOT/etc/vfstab
	fi
	rm -f $tmpfile
}

#
# Delete or disable SMF services.
#
fix_smf()
{
	SMF_UPGRADE=/a/var/svc/profile/upgrade

	#
	# Fix network services if shared stack.
	#
	if [[ "$STACK_TYPE" == "shared" ]]; then
		vlog "$v_fixnetsvcs"

		NETPHYSDEF="svc:/network/physical:default"
		NETPHYSNWAM="svc:/network/physical:nwam"

		vlog "$v_enblsvc" "$NETPHYSDEF"
		zlogin -S $ZONENAME "echo /usr/sbin/svcadm enable $NETPHYSDEF \
		    >>$SMF_UPGRADE" </dev/null

		vlog "$v_dissvc" "$NETPHYSNWAM"
		zlogin -S $ZONENAME \
		    "echo /usr/sbin/svcadm disable $NETPHYSNWAM \
		    >>$SMF_UPGRADE" </dev/null

		# Disable routing svcs.
		vlog "$v_dissvc" 'svc:/network/routing/*'
		zlogin -S $ZONENAME \
		    "echo /usr/sbin/svcadm disable 'svc:/network/routing/*' \
		    >>$SMF_UPGRADE" </dev/null
	fi

	#
	# Disable well-known services that don't run in a zone.
	#
	vlog "$v_rminvalidsvcs"
	for i in $(egrep -hv "^#" \
	    /usr/lib/brand/solaris/smf_disable.lst \
	    /etc/brand/solaris/smf_disable.conf)
	do
		# Disable the svc.
		vlog "$v_dissvc" "$i"
		zlogin -S $ZONENAME \
		    "echo /usr/sbin/svcadm disable $i >>$SMF_UPGRADE" </dev/null
	done
}

#
# Remove well-known pkgs that do not work inside a zone.
#
rm_pkgs()
{
	for i in $(egrep -hv "^#" /usr/lib/brand/solaris/pkgrm.lst \
	    /etc/brand/solaris/pkgrm.conf)
	do
		$PKG info $i >/dev/null 2>&1
		if (( $? != 0 )); then
			continue
		fi

		vlog "$v_rmpkg" "$i"
		zlogin -S $ZONENAME LC_ALL=C \
		    /usr/bin/pkg -R /a uninstall -r $i </dev/null >&2 || \
		    error "$e_rmpkg" $i
	done
}

#
# Zoneadmd writes a one-line index file into the zone when the zone boots,
# so any information about installed zones from the original system will
# be lost at that time.  Here we'll warn the sysadmin about any pre-existing
# zones that they might want to clean up by hand, but we'll leave the zonepaths
# in place in case they're on shared storage and will be migrated to
# a new host.
#
warn_zones()
{
	zoneconfig=$ZONEROOT/etc/zones

	# if there is no /etc/zones/index file there's nothing to check
	[[ ! -a $zoneconfig/index ]] && return

	# verify that /etc/zones/index is actually a file
	if [[ -h $zoneconfig/index || ! -f $zoneconfig/index ]]; then
		error "$e_badfile" "/etc/zones/index"
		return
	fi

	NGZ=$(nawk -F: '{
		if (substr($1, 0, 1) == "#" || $1 == "global")
			continue

		if ($2 != "configured")
			printf("%s ", $1)
	}' $zoneconfig/index)

	# Return if there are no installed zones to warn about.
	[[ -z "$NGZ" ]] && return

	log "$v_rmzones" "$NGZ"

	NGZP=$(nawk -F: '{
		if (substr($1, 0, 1) == "#" || $1 == "global")
			continue

		if ($2 != "configured")
			printf("%s ", $3)
	}' $zoneconfig/index)

	log "$v_rmzonepaths"

	for i in $NGZP
	do
		log "    %s" "$i"
	done

	#
	# create an empty zones index file (a new one will be created
	# once the zone is booted) we don't use the safe_* functions
	# here because we've already validated etc/zones within the
	# zone.
	#
	rm -f $zoneconfig/index
	touch $zoneconfig/index
}

#
# Parse the command line options.
#
OPT_U=
OPT_V=
OPT_L=
sc_config=
leave_detached=false

while getopts "X:b:c:uv" opt
do
	case "$opt" in
	c)	sc_config="$OPTARG";;
	X)	#
		# The -X option is a brand-private option used for interaction
		# between brand scripts.
		#
		if [[ $OPTARG == "no-attach" ]]; then
	       		leave_detached=true
		else
			fail_internal "Invalid -X argument '%s'" "$OPTARG"
		fi
		;;
	u)	OPT_U="-u";;
	v)	OPT_V="-v";;
	*)	fail_usage "" ;;
	esac
done
shift OPTIND-1

(( $# != 2 )) && fail_usage "$f_missing_zone_zp"

if [[ $leave_detached == true ]]; then
	[[ -n $sc_config ]] &&
	    fail_internal "Invalid use of -X no-attach and -c"
	[[ -n $OPT_U ]] &&
	    fail_internal "Invalid use of -X no-attach and -u"
fi

# If fixing up the image fails, force an uninstall.
EXIT_CODE=$ZONE_SUBPROC_FATAL
init_zone zone "$1" "$2"
eval $(bind_legacy_zone_globals zone)

#
# failure should unmount the zone if necessary;
#
ZONE_IS_MOUNTED=0
trap trap_exit EXIT

start_log zone install "${save_args[@]}"

e_badinfo=$(gettext "Failed to get '%s' zone resource")
e_badfile=$(gettext "Invalid '%s' file within the zone")
v_mkdirs=$(gettext "Creating mount points")
v_change_var=$(gettext "Changing the pkg variant to nonglobal...")
e_change_var=$(gettext "Changing the pkg variant to nonglobal failed")
v_update=$(gettext "Updating the zone software to match the global zone...")
v_updatedone=$(gettext "Zone software update complete")
e_badupdate=$(gettext "Updating the Zone software failed")
v_adjust=$(gettext "Updating the image to run within a zone")
v_stacktype=$(gettext "Stack type '%s'")
v_rmhollowsvcs=$(gettext "Deleting global zone-only SMF services")
v_fixnetsvcs=$(gettext "Adjusting network SMF services")
v_rminvalidsvcs=$(gettext "Disabling invalid SMF services")
v_delsvc=$(gettext "Delete SMF svc '%s'")
v_enblsvc=$(gettext "Enable SMF svc '%s'")
e_enblsvc=$(gettext "enabling SMF svc '%s'")
v_dissvc=$(gettext "Disable SMF svc '%s'")
e_adminf=$(gettext "Unable to create admin file")
v_rmpkg=$(gettext "Remove package '%s'")
e_rmpkg=$(gettext "removing package '%s'")
v_rmzones=$(gettext "The following zones in this image will be unusable: %s")
v_rmzonepaths=$(gettext "These zonepaths could be removed from this image:")
v_exitgood=$(gettext "Postprocessing successful.")
f_missing_zone_zp=$(gettxt "Missing zone or zonepath argument.")
m_usage=$(gettext "/usr/lib/brand/solaris/p2v [-uv] [-c sysconfig] zone zonepath")

#
# Do some validation on the paths we'll be accessing
#
safe_dir etc
safe_dir etc/dfs
safe_dir etc/zones
safe_dir var
safe_dir var/log
safe_dir var/pkg

# If these paths exist, they must be directories.  If they don't exist,
# they will be created below.
safe_opt_dir etc/svc
safe_opt_dir system
safe_opt_dir system/volatile

# Now do the work to update the zone.

# Before booting the zone we may need to create a few mnt points, just in
# case they don't exist for some reason.
#
# Whenever we reach into the zone while running in the global zone we
# need to validate that none of the interim directories are symlinks
# that could cause us to inadvertently modify the global zone.
vlog "$v_mkdirs"
if [[ ! -f $ZONEROOT/tmp && ! -d $ZONEROOT/tmp ]]; then
	mkdir -m 1777 -p $ZONEROOT/tmp || exit $EXIT_CODE
fi
if [[ ! -h $ZONEROOT/etc && ! -f $ZONEROOT/etc/mnttab ]]; then
	touch $ZONEROOT/etc/mnttab || exit $EXIT_CODE
	chmod 444 $ZONEROOT/etc/mnttab || exit $EXIT_CODE
fi
if [[ ! -f $ZONEROOT/proc && ! -d $ZONEROOT/proc ]]; then
	mkdir -m 755 -p $ZONEROOT/proc || exit $EXIT_CODE
fi
if [[ ! -f $ZONEROOT/dev && ! -d $ZONEROOT/dev ]]; then
	mkdir -m 755 -p $ZONEROOT/dev || exit $EXIT_CODE
fi
if [[ ! -d $ZONEROOT/system/volatile ]]; then
	mkdir -m 755 -p $ZONEROOT/system/volatile || exit $EXIT_CODE
fi

# symlink: /etc/svc/volatile -> /system/volatile
if [[ ! -d $ZONEROOT/etc/svc ]]; then
	mkdir -m 755 -p $ZONEROOT/etc/svc || exit $EXIT_CODE
fi
if [[ -e $ZONEROOT/etc/svc/volatile || -h $ZONEROOT/etc/svc/volatile ]]; then
	rm -rf $ZONEROOT/etc/svc/volatile || exit $EXIT_CODE
fi
ln -s ../../system/volatile $ZONEROOT/etc/svc/volatile

# symlink: /var/run -> /system/volatile
if [[ -e $ZONEROOT/var/run || -h $ZONEROOT/var/run ]]; then
	rm -rf $ZONEROOT/var/run || exit $EXIT_CODE
fi
ln -s ../system/volatile $ZONEROOT/var/run

# Check for zones inside of image.
warn_zones

STACK_TYPE=$(zoneadm -z $ZONENAME list -p | nawk -F: '{print $7}')
if (( $? != 0 )); then
	error "$e_badinfo" "stacktype"
fi
vlog "$v_stacktype" "$STACK_TYPE"

# Note that we're doing this before update-on-attach has run.
fix_vfstab

#
# Mount the zone so that we can do all of the updates needed on the zone.
#
vlog "$v_mounting"
ZONE_IS_MOUNTED=1
zoneadm -z $ZONENAME mount -f || fatal "$e_badmount"

#
# Any errors in these functions are not considered fatal.  The zone can be
# be fixed up manually afterwards and it may need some additional manual
# cleanup in any case.
#

log "$v_adjust"
# cleanup SMF services
fix_smf
# remove invalid pkgs
rm_pkgs

# If anything below here fails, a subsequent attach can likely fix it up.
EXIT_CODE=$ZONE_SUBPROC_DETACHED

vlog "$v_unmount"
zoneadm -z $ZONENAME unmount || fatal "$e_badunmount"
ZONE_IS_MOUNTED=0

if is_brand_labeled; then
	# The labeled brand needs to mount the zone's root dataset back onto
	# ZONEROOT so we can finish processing.
	mount_active_be zone || fatal "$f_mount_active_be"
fi

#
# Update the image format before running other commands so that they are not
# thrown off by having old image metadata.
#
log "$v_update_format"
pkg -R "$ZONEROOT" update-format || pkg_err_check "$e_update_format"

# Change the pkging variant from global zone to non-global zone.
log "$v_change_var"
$PKG -R $ZONEROOT change-variant -I variant.opensolaris.zone=nonglobal || \
    pkg_err_check "$e_change_var"
# Set the property which tells the image to use the system publisher.
pkg -R $ZONEROOT set-property use-system-repo true
if [[ $? != 0 ]]; then
	log "\n$f_set_sysrepo_prop_fail"
	exit $EXIT_CODE
fi

if [[ $leave_detached == true ]]; then
	#
	# Unconditionally mount the active BE.  The caller is responsible
	# for unmounting if needed.
	#
	mount_active_be zone || fatal "$f_mount_active_be"
else

	#
	# Run update on attach.  State is currently 'incomplete' so use the
	# private force-update option.  This also leaves the zone in the
	# 'installed' state.  This is a known bug in 'zoneadm attach'.  We
	# change the zone state back to 'incomplete' for now but this can be
	# removed once 'zoneadm attach' is fixed.
	#
	log "$v_update"
	zoneadm -z $ZONENAME attach -X || fatal "$e_badupdate"
	zoneadm -z $ZONENAME mark incomplete || fatal "$e_badupdate"
	log "$v_updatedone"

	[[ -n $OPT_U ]] && reconfigure_zone $sc_config

	if is_brand_labeled; then
		mount_active_be zone || fatal "$f_mount_active_be"
	fi
fi

trap - EXIT
vlog "$v_exitgood"
finish_log zone
exit $ZONE_SUBPROC_OK
