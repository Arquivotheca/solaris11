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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#

. /usr/lib/brand/solaris/common.ksh

# Allows developers to override some things like PATH and PYTHONPATH
. /usr/lib/brand/solaris/developerenv.ksh

f_a_obs=$(gettext "-a publisher=uri option is obsolete.")
f_mktemp=$(gettext "Unable to make temporary filename.")
f_aimanifest_load=$(gettext "Unable to aimanifest load.")
f_aimanifest_add=$(gettext "Unable to aimanifest add.")
f_autoinstall=$(gettext "auto-install failed.")
m_ai_running=$(gettext "Running auto-install: '%s'")

m_image=$(gettext       "       Image: Preparing at %s.")
m_mannote=$(gettext     "        Note: Man pages can be obtained by installing pkg:/system/manual")

m_usage=$(gettext "\n        install [-h]\n        install [-m manifest] [-c profile.xml | dir]\n        install {-a archive|-d path} {-p|-u} [-s|-v] [-c profile.xml | dir]")

m_done=$(gettext      " done.")

trap_cleanup() {
	print "$m_inetrrupt"
	exit $EXIT_CODE
}

trap_exit() {
	vlog "Exiting with exit code $EXIT_CODE"
	finish_log zone
	exit $EXIT_CODE
}

EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
trap trap_cleanup INT

# Used by start_log()
set -A save_args "$0" "$@"

manifest=""
profile_dir=""
ZONENAME=""
ZONEPATH=""

# Setup i18n output
TEXTDOMAIN="SUNW_OST_OSCMD"
export TEXTDOMAIN

PKG=pkg

unset install_archive
unset source_dir
unset silent_mode
unset verbose_mode
unset sc_config
unset OPT_C
unset temp_manifest

while getopts "a:c:d:hm:pR:suvz:" opt; do
	case $opt in
		a)	# We're expecting a path to an archive
			if [[ ! -f $OPTARG ]]; then
				# If old style 'pub=uri' parameter then error.
				echo $OPTARG | egrep -s =
				if (( $? == 0 )); then
					fail_usage "$f_a_obs"
				fi
			fi
			install_archive="-a $OPTARG";;
		c)	ls "$OPTARG" >/dev/null 2>&1
			[[ -f $OPTARG ]] || [[ -d $OPTARG ]] || fatal "$f_arg_not_file_or_dir" "$OPTARG"
			sc_config="$OPTARG"
			OPT_C="-c $OPTARG" ;;
		d)	source_dir="-d $OPTARG";;
		h)	fail_usage "";;
		m)	manifest="$OPTARG" ;;
		p)	preserve_zone="-p";;
		R)	ZONEPATH="$OPTARG" ;;
		s)	silent_mode=1;;
		u)	unconfig_zone="-u";;
		v)	verbose_mode="-v";;
		z)	ZONENAME="$OPTARG" ;;
		*)	fail_usage "";;
	esac
done
shift $((OPTIND-1))

if [[ -z $ZONEPATH || -z $ZONENAME ]]; then
	print -u2 "Brand error: No zone path or name"
	exit $ZONE_SUBPROC_USAGE
fi
zone=
init_zone zone "$ZONENAME" "$ZONEPATH"
eval $(bind_legacy_zone_globals zone)

is_brand_labeled
brand_labeled=$?

# Configuration profile file must have .xml suffix
if [[ -f $sc_config && $sc_config != *.xml ]]; then
	fail_usage "$f_scxml" "$sc_config"
fi

# An image install can't use both -a AND -d...
[[ -n "$install_archive" && -n "$source_dir" ]] &&
    fail_usage "$f_incompat_options" "-a" "-d"

# The install can't be both verbose AND silent...
[[ -n $silent_mode && -n $verbose_mode ]] && \
    fail_usage "$f_incompat_options" "-s" "-v"

# The install can't both preserve and unconfigure
[[ -n $unconfig_zone && -n $preserve_zone ]] && \
    fail_usage "$f_incompat_options" "-u" "-p"

# AI zone manifest option isn`t allowed when installing from a system image.
if [[ -n "$install_archive" || -n "$source_dir" ]]; then
	[[ -n "$manifest" ]] && fail_usage \
	    "$f_incompat_options" "-a|-d" "-m"
fi

# p2v options aren't allowed when installing from a repo.
if [[ -z $install_archive && -z $source_dir ]]; then
	[[ -n $preserve_zone || -n $unconfig_zone ]] && \
		fail_usage "$f_incompat_options" "default" "-p|-u"
fi

start_log zone install "${save_args[@]}"
trap trap_exit EXIT

#
# Look for the 'entire' incorporation's FMRI in the current image; due to users
# doing weird machinations with their publishers, we strip off the publisher
# from the FMRI if it is present.
# It's ok to not find entire in the current image, since this means the user
# can install pre-release development bits for testing purposes.
#
entire_fmri=$(get_entire_incorp)

#
# If we're installing from an image, branch off to that installer.
# Set up ZFS dataset hierarchy for the zone root dataset.
#
if [[ -n $install_archive || -n $source_dir ]]; then
	/usr/lib/brand/solaris/image_install $ZONENAME $ZONEPATH \
	    $install_archive $source_dir $verbose_mode $silent_mode \
	    $unconfig_zone $preserve_zone $OPT_C
	EXIT_CODE=$?
	exit $EXIT_CODE
fi

log "$m_image\n" $ZONEROOT

enable_zones_services
if [[ $? -ne 0 ]]; then
	exit $ZONE_SUBPROC_NOTCOMPLETE
fi

# Use default AI zone manifest if none is given
if [[ ! -n $manifest ]]; then
	manifest=/usr/share/auto_install/manifest/zone_default.xml
fi

#
# Add packages to AI zone manifest for TX zones if appropriate.
# Add entire package if installed in GZ
# The environment variable AIM_MANIFEST contains the file where all the
# aimanifest changes will be made.  The load operation loads that manifest
# into the working file.  The add operation adds the entries to the
# working file.
#
if (( $brand_labeled == 0 )) || [[ -n $entire_fmri ]]; then
	temp_manifest=`mktemp -t manifest.xml.XXXXXX`
	if [[ -z $temp_manifest ]]; then
		print "$f_mktemp"
		exit $ZONE_SUBPROC_NOTCOMPLETE
	fi
	export AIM_MANIFEST=$temp_manifest
	aimanifest load $manifest
	if [[ $? -ne 0 ]]; then
		print "$f_aimanifest_load"
		exit $ZONE_SUBPROC_NOTCOMPLETE
	fi
	if (( $brand_labeled == 0 )); then
		aimanifest add \
		    /auto_install/ai_instance/software/software_data[@action="install"]/name \
		    pkg:/group/feature/trusted-desktop
		if [[ $? -ne 0 ]]; then
			print "$f_aimanifest_add"
			exit $ZONE_SUBPROC_NOTCOMPLETE
		fi
	fi
	if [[ -n $entire_fmri ]]; then
		aimanifest add \
		    /auto_install/ai_instance/software/software_data[@action="install"]/name \
		    pkg:///$entire_fmri
		if [[ $? -ne 0 ]]; then
			print "$f_aimanifest_add"
			exit $ZONE_SUBPROC_NOTCOMPLETE
		fi
	fi
	manifest=$temp_manifest
fi

#
# Before installing the zone, set up ZFS dataset for the zone root dataset,
# but don't create rpool/ROOT or rpool/export hierarchies since installer
# will create them.  Sets EXIT_CODE if datasets are created.
#
create_active_ds -r zone || fatal "$f_no_ds"

#
# If unconfig service is online, then call auto-install with the default
# profile or with the caller supplied profile.
# If unconfig service is offline or doesn't exist, then don't pass
# any profile to auto-install since this will cause SCI tool to start in
# zone on boot.  Previous sysconfig method handled below after install.
#
SC_ONLINE=$(svcprop -p restarter/state \
    svc:/milestone/unconfig:default 2> /dev/null)
set -A aicmd /usr/bin/auto-install -z "$ZONENAME" -Z "${zone.rpool_ds}" \
    -m "$manifest"
if (( $? == 0 )) && [[ $SC_ONLINE == "online" ]]; then
	if [[ -n $sc_config ]]; then
		# Do not quote $OPT_C as it should be "-c <something>"
		a_push aicmd $OPT_C
	else
		a_push aicmd -c \
		    /usr/share/auto_install/sc_profiles/enable_sci.xml
	fi
fi

#
# Run auto-install, saving the output in the log file.  Tricks are needed
# so that we can check the exit value from auto-install rather than the exit
# value from tee.
#

vlog "$m_ai_running" "${aicmd[*]}"
"${aicmd[@]}" || fail_fatal "$f_autoinstall"

if [[ -n $temp_manifest ]]; then
	rm $temp_manifest
fi

log "\n$m_mannote\n"

log "$m_done\n"

#
# If unconfig service is offline or doesn't exist, then use
# previous sysconfig method since that is still being used by the
# zone.  Copy sysidcfg file if given, but only copy if it isn't the
# new SC file enable_sci.xml.  The enable_sci.xml file causes
# sysid to generate warnings in a zone.
#
mount_active_be -c zone || fail_fatal "$f_mount_active_be"
if [[ $SC_ONLINE != "online" ]]; then
	touch $ZONEROOT/etc/.UNCONFIGURED
	if [[ -n $sc_config ]] && [[ $sc_config != \
	    "/usr/share/auto_install/sc_profiles/enable_sci.xml" ]]; then
		cp $sc_config $ZONEROOT/etc/sysidcfg
	fi
fi

#
# Labeled zones need to be able to modify /etc/gconf files, when gnome
# packages are installed in the zone.  Set up links in the zone to the
# global zone files -- this will provide default versions from the global
# zone, which can be modified by the zone, breaking the link.
if (( $brand_labeled == 0 )); then
	cd /etc/gconf
	for i in $(find .); do
		if [ ! -e $ZONEROOT/etc/gconf/$i ]; then
			if [ -d $i ]; then
				mkdir $ZONEROOT/etc/gconf/$i
			else
				ln -s /etc/gconf-global/$i \
				    $ZONEROOT/etc/gconf/$i
			fi
		fi
	done
fi

log "$m_complete\n\n" ${SECONDS}
if (( $brand_labeled != 0 )); then
	log "$m_postnote\n"
	log "$m_postnote2\n"
else
	# Umount the dataset on the root.
	umount $ZONEROOT || log "$f_zfs_unmount" "$ZONEPATH/root"
fi

finish_log zone
trap - EXIT
exit $ZONE_SUBPROC_OK
