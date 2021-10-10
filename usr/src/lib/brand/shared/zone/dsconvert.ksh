#! /bin/ksh

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


export PATH=/usr/bin:/usr/sbin

. /usr/lib/brand/shared/uninstall.ksh

function get_zone_state {
	typeset -n z=$1
	typeset fields

	zoneadm -z ${z.name} list -p | IFS=: read -A fields
	if (( ${#fields[@]} < 6 )); then
		error "%s: unknown data returned from zoneadm list -p" \
		    "${zone.name}"
		return 1
	fi
	z.state=${fields[2]}
	z.brand=${fields[5]}

	return 0
}

#
# run cmd undo rollback
#
# Runs the command given in the cmd array.  If all goes well, it adds the
# command in the undo array to the rollback array (of arrays) and returns 0.
# If the command does not return 0, the commands that exist in the rollback
# array are executed in the reverse of the order that they were added.
#
# In all cases, the exit value from the command in cmd is returned.
#
function run {
	typeset -n cmd=$1
	typeset -n undo=$2
	typeset -n rollback=$3

	if (( dryrun )); then
		print " " ${cmd[@]}
		return 0
	fi
	(( verbose )) && log "Running <%s>" "${cmd[*]}"
	( eval "${cmd[@]}" )
	typeset -i rv=$?
	if (( rv == 0 )); then
		# Push the undo array onto the rollback stack
		typeset -i rbsize=${#rollback[@]}
		typeset -a rollback[rbsize]
		set -A rollback[$rbsize] -- "${undo[@]}"
		return 0
	fi

	error "Command <%s> failed with exit status %d.  Rolling back." \
	    "${cmd[*]}" $rv

	typeset -i i
	for (( i = ${#rollback[@]} - 1; i >= 0; i-- )); do
		log "Executing rollback command <%s>" "${rollback[i][*]}"
		( eval "${rollback[i][@]}" )
	done
	return $rv
}

#
# get_active_be zone
#
# Overrides shared get_active_be.  Is used instead of brand-specific
# get_active_be because they know about the new layout, not the old.
#
function get_active_be {
	typeset -n zone=$1
	typeset -A uuid2gzbe
	typeset -i needs_selection=0

	#
	# solaris10 only supports a single for those that need to be converted.
	#
	if [[ ${zone.brand} == "solaris10" ]]; then
		zone.active_ds="${zone.path.ds}/ROOT/zbe-0"
		return 0
	fi

	#
	# The rest is solaris brand or something that uses the solaris brand
	#

	#
	# Load an associative array of global zone BEs.  Store current uuid
	# of GZBE in $active_gzbe.
	#
	# Initially the gzbes associative array is one dimensional.  The
	# following loop (beginning with zfs list) will add a second
	# dimension.  The fully populated uiid2gzbe structure will look like:
	#
	# uuid2gzbe[<gzbe uuid>]=<gzbe name>
	# uuid2gzbe[<gzbe uuid>][ngzbe name]=<ngz active>
	#
	typeset be uuid active junk
	typeset active_gzbe
	beadm list -H | while IFS=\; read be uuid active junk ; do
		uuid2gzbe[$uuid]=$be
		[[ $active == *N* ]] && active_gzbe=$uuid
	done
	if [[ -z $active_gzbe ]]; then
		error "%s: unable to get global zone BE UUID" "${zone.name}"
		return 1
	fi

	#
	# Look for the best match of zone BEs
	#
	typeset name parent active tab=$(printf "\t")
	typeset prop_parent="org.opensolaris.libbe:parentbe"
	typeset prop_active="org.opensolaris.libbe:active"

	typeset -A ngzbe
	zfs list -H -r -d 1 -t filesystem -o name,$prop_parent,$prop_active \
	    "${zone.path.ds}/ROOT" | while IFS=$tab read name parent active ; do

		# skip the non-BE top-level dataset
		[[ $name == "${zone.path.ds}/ROOT" ]] && continue

	    	if [[ -z ${uuid2gzbe[$parent]} ]]; then
	    		uuid2gzbe[$parent]='Missing Global Zone Boot Env'
		fi
		ngzbe[$(basename $name)].parent=$parent
		ngzbe[$(basename $name)].active=$active

		[[ $parent == "$active_gzbe" ]] || continue
		[[ $active == on ]] || continue
		vlog "Found active dataset %s" "$name"
		if [[ -n "$active_ds" ]]; then
			error "$f_multiple_ds"
			error "%s: Multiple active datasets" "${zone.name}"
			(( needs_selection=1 ))
			continue
		fi
		active_ds=$name
	done
	if [[ -n $active_ds ]]; then
		zone.active_ds=$active_ds
		return 0
	fi

	#
	# Active dataset was not found by property.  See if something is mounted
	# on the zone root.
	#
	active_ds=$(zfs list -H -o name ${zone.root})
	if [[ $active_ds == "${zone.path.ds}"/ROOT/* ]]; then
		zone.active_ds=$active_ds
		return 0
	fi

	#
	# If no active boot environment was found, offer a table of available
	# boot environments
	#
	error "%s: No active dataset" "${zone.name}"

	print "\nThe following unconverted boot environments exist this zone.\n"
	print -- "Boot Environment  NGZ Active  Global Zone Boot Environment" \
	    "GZ Active"
	print -- "----------------- ----------- ----------------------------" \
	    "---------"
	typeset be gzbe gzactive
	for be in ${!ngzbe[@]} ; do
		gzbe="${uuid2gzbe[${ngzbe[$be].parent}]}"
		if [[ $gzbe == "$active_gzbe" ]]; then
			gzactive=yes
		else
			gzactive=no
		fi
		printf "%-17s %-11s %-28s %s\n" "$be" "${ngzbe[$be].active}" \
		    "$gzbe" "$gzactive"
	done


	print "\nUse:\n"
	print "\tdsconvert -b <bootenv> ...\n"
       	print "to specify the boot environment to activate during conversion.\n"

	return 1
}

function usage {
	print "Usage: dsconvert [-hnv] [-b <BE>] zone\n\n" \
	    "    -h\t\tShow this help message\n" \
	    "    -n\t\tDry run - make no changes\n" \
	    "    -v\t\tVerbose\n" \
	    "    -b <BE>\tActivate the specified zone boot environment\n"
}

#
# main
#


typeset -i dryrun=0
bootenv=
m_conversion_complete="%s: Conversion complete"
while getopts :b:hnv opt ; do
	case $opt in
		b)	bootenv=$OPTARG
			;;
		h)	usage
			exit 0
			;;
		n)	(( dryrun = 1 ))
			m_conversion_complete="%s: Dryrun complete"
			function nopfn { echo " " "$*"; }
			nop=nopfn
			;;
		v)	(( verbose = 1 )) ;;	# used in this script
		?)	usage 1>&2
			exit 1
			;;
	esac
done
shift $(( OPTIND - 1 ))
if (( $# != 1 )); then
	usage 1>&2
	exit 1
fi
zonename=$1

zoneadm -z "$zonename" list >/dev/null 2>&1 || {
	error "%s: %s: No such zone configured" "$(basename "$0")" "$zonename"
	exit 1
}

init_zone zone "$zonename"
typeset -a cmd undo rollback
typeset -i has_bes=0
typeset active_ds=

init_dataset old_ROOT_ds "${zone.path.ds}/ROOT" >/dev/null 2>&1 || {
	error "%s: zone has no old style ROOT dataset." "$zonename"
	exit 1
}

get_zone_state zone || exit 1

#
# This check is used for the brief window where people may be trying to use
# an ON build with a build of the solaris brand that is too old.  Since the
# sysboot script was introduced with the dataset layout change, it makes for
# an easy check.
#
if [[ ${zone.brand} != solaris10 && ! -f /usr/lib/brand/solaris/sysboot ]]
then
	error "%s: %s must be updated before converting\n\t%s branded zones" \
	    "${zone.name}" pkg:/system/zones/brand/solaris "${zone.brand}"
	exit 1
fi

case "${zone.state}" in
	installed) : ;;
	configured)
		# Be sure that there's a dataset under the old ROOT
		typeset be_datasets
		set -A be_datasets $(zfs list -H -o name -d 1 -r \
		    "${zone.path.ds}/ROOT")
		if (( ${#be_datasets[@]} < 2 )); then
			log "%s: zone has no boot environments" "$zonename"
			(( hasbes=0 ))
		else
			get_ds_from_path "${zone.root}" active_ds || {
				log "%s: configured but no BE mounted at %s" \
				    "$zonename" "${zone.root}"
				(( hasbes=0 ))
			}
		fi
		;;
	*)	error "%s: Unable to convert zone in state %s" "${zonename}" \
		    "${zone.state}"
		exit 1
		;;
esac

if [[ -n $bootenv ]]; then
	if [[ ${zone.brand} == "solaris10" ]]; then
		error "%s: The -b option is not supported with the %s brand" \
		    "$zonename" "${zone.brand}"
		exit 1
	fi
	zone.active_ds="${zone.path.ds}/ROOT/$bootenv"
	if ! zfs list "${zone.active_ds}" >/dev/null 2>&1 ; then
		error "%s: Zone has no boot environment named %s" \
		    "$zonename" "$bootenv"
		exit 1
	fi
else
	get_active_be zone || exit 1
fi

#
# Look at zonecfg fs and dataset resources to see if anything under /export
# is added.  The message that comes out of zonecfg_has_export is not helpful
# in this particular case, so we ignore it.
#
typeset -i skip_export=0
zonecfg_has_export zone >/dev/null 2>&1 && (( skip_export = 1 ))

log "%s: Creating zone rpool dataset" "$zonename"
set -A cmd zfs create -o mountpoint=/rpool -o zoned=on "${zone.rpool_ds}"
set -A undo zfs destroy "${zone.rpool_ds}"
run cmd undo rollback || exit 1

if (( skip_export == 0 )); then
	log "%s: Creating zone export dataset" "$zonename"
	set -A cmd zfs create -o mountpoint=/export "${zone.rpool_ds}/export"
	set -A undo zfs destroy "${zone.rpool_ds}/export"
	run cmd undo rollback || exit 1

	log "%s: Creating zone export/home dataset" "$zonename"
	set -A cmd zfs create "${zone.rpool_ds}/export/home"
	set -A undo zfs destroy "${zone.rpool_ds}/export/home"
	run cmd undo rollback || exit 1
fi

set -A props -- -o mountpoint=legacy -o canmount=noauto
if [[ ${zone.brand} != solaris10 ]]; then
	a_push props -o "com.oracle.libbe:nbe_handle=on"
fi
log "%s: Creating zone ROOT dataset" "$zonename"
set -A cmd zfs create "${props[@]}" "${zone.ROOT_ds}"
set -A undo zfs destroy "${zone.ROOT_ds}"
run cmd undo rollback || exit 1

#
# Moving the dataset hierarchy is problematic when clones are involved.
# The easiest way to avoid the problem with clones is to use more of them.
#
log "%s: Cloning datasets from ROOT to rpool/ROOT" "$zonename"
set -A cmd zfs set -r canmount=off "${zone.path.ds}/ROOT"
set -A undo zfs set -r canmount=${old_ROOT_ds.props[canmount].value} \
    "${zone.path.ds}/ROOT"
run cmd undo rollback || exit 1

#
# Unconventional quoting of date format string is to silence SCCS keyword
# warnings.
#
snapname=$(basename $0)-$(TZ=UTC date "+%Y""%m%dT%H""%M""%SZ")
set -A cmd zfs snapshot -r "${zone.path.ds}/ROOT@$snapname"
set -A undo zfs destroy -rd "${zone.path.ds}/ROOT@$snapname"
run cmd undo rollback || exit 1

#
# Get a list of all the old datasets and their properties.  Once we
# have that, use that information to create clones.
#
typeset -a dslist
get_datasets -p ${zone.path.ds}/ROOT dslist || {
	error "%s: Unable to get list of datasets under %s" \
	    "${zone.name}" "${zone.path.ds}/ROOT"
	exit 1
}

#
# Go through the list and be sure they are all unmounted and won't become
# mounted.  This is needed because the clones that get created will need to use
# the mount points currently used by the originals.  Ignore the first item in
# the array because that is the ROOT dataset.
#
typeset -i i
for (( i = ${#dslist[@]} - 1; i > 1; i-- )); do

	# Before this conversion multiple datasets in the BE were broken.
	# Therefore, we don't deal with them here either.
	if [[ ${dslist[i].name} == ${zone.path.ds}/ROOT/*/* ]]; then
		# Force a rollback and exit
		set -A cmd error \
		    "%s: Unsupported non-root dataset %s in boot environment" \
		    "${zone.name}" "${dslist[i].name}" \; false
		set -A undo echo "this command will never run"
		run cmd undo rollback
		exit 1
	fi

	if [[ ${dslist[i].props[canmount].value} != no ]]; then
		set -A cmd zfs set canmount=off "${dslist[i].name}"
		set -A undo zfs set canmount=${dslist[i].props[canmount].value} \
	           "${dslist[i].name}"
		run cmd undo rollback || exit 1
	fi
	if [[ ${dslist[i].props[mounted].value} == yes ]]; then
		set -A cmd unmount "${dslist[i].name}"
		if [[ ${dslist[i].props[mountpoint].value} == legacy ]]; then
			# Create undo using existing mountpoint
			set -A undo mount -F zfs "${dslist[i].name}" \
			    "$(nawk -F '\t' -v "dev=${dslist[i].name}" \
			    '$1 == dev {print $2; exit}' /etc/mnttab)"
		else
			# This conversion process assumes no one uses temporary
			# mount points for zones before this conversion is
			# necessary.
			set -A undo zfs mount "${dslist[i].name}"
		fi
		run cmd undo rollback || exit 1
	fi
done

#
# The sources are all unmounted and along the way we verified that it is only
# root datasets.  Create clones.
#
for (( i = 1; i < ${#dslist[@]}; i++ )); do
       	set -A props -- -o mountpoint=/ -o canmount=noauto

	# preserve user properties
	for prop in "${!dslist[i].props[@]}" ; do
		# Skip non-user properties and inherited user properties
		[[ $prop == *:* ]] || continue
		[[ ${dslist[i].props[$prop].source} == @(local|received) ]] ||
		    continue

		a_push props -o "$prop=${dslist[i].props[$prop].value}"
	done

	src=${dslist[i].name}@$snapname
	new=${dslist[i].name/${zone.path.ds}/${zone.rpool_ds}}

	set -A cmd zfs clone "${props[@]}" "$src" "$new"
	set -a undo destroy_zone_dataset "$new"
	run cmd undo rollback || exit 1

	(( has_bes=1 ))
done

if (( has_bes )); then
	#
	# The layout is now correct, except for datasets that need to be
	# deleted.  Load the brand-specific common functions.  Note that
	# all of the non-solaris10 brands that this script support use
	# the solaris common.ksh.
	#
	if [[ ${zone.brand} == "solaris10" ]]; then
		. /usr/lib/brand/solaris10/common.ksh
	else
		. /usr/lib/brand/solaris/common.ksh
	fi

	# Clean up boot environment properties as needed
	(( dryrun )) || set_active_be zone "$(basename "${zone.active_ds}")"

	#
	# If we started this exercise with a zone that was attached, just run
	# the rough equivalent of the sysboot hook to mount it.  If it wasn't
	# attached, then we need to remount the active dataset at the zone
	# root.
	#
	log "%s: Remounting zone at %s" "$zonename" "${zone.root}"
	if [[ ${zone.state} == installed ]]; then
		set -A cmd mount_active_be -c zone
		set -A undo umount "${zone.root}"
		run cmd undo rollback || exit 1

		if [[ ${zone.brand} != solaris10 ]]; then
			log "\n%8s: zone %s needs to be updated with attach -u" \
			    NOTICE "$zonename"

			log "%8s: \"zoneadm -z %s detach\" to detach, then" \
			    Run "$zonename"
			log "%8s: \"zoneadm -z %s attach -u\" to attach.\n" \
			    Run "$zonename"
		fi
	else	# detached

		#
		# Before the dataset revision, each BE only supported a single
		# (root) dataset.  As such, we can use a rather simplistic
		# approach to mounting the active BE.
		#
		set -A cmd zfs set canmount=off "${zone.active_ds}"
		set -A undo zfs set canmount="$(/usr/sbin/zfs get -H \
		    -o value canmount "${zone.active_ds}")" "${zone.active_ds}"
		run cmd undo rollback || exit 1

		set -A cmd zfs set zoned=off "${zone.active_ds}"
		set -A undo zfs inherit zoned "${zone.active_ds}"
		run cmd undo rollback || exit 1

		set -A cmd zfs set mountpoint="${zone.root}" "${zone.active_ds}"
		set -A undo zfs set mountpoint=$(/usr/sbin/zfs get -H \
		    -o value mountpoint "${zone.active_ds}") \
		    "${zone.active_ds}"
		run cmd undo rollback || exit 1

		set -A cmd zfs set canmount=on "${zone.active_ds}"
		set -A undo zfs set canmount=off "${zone.active_ds}"
		run cmd undo rollback || exit 1

		set -A cmd zfs mount "${zone.active_ds}"
		set -A undo zfs unmount "${zone.active_ds}"
		run cmd undo rollback || exit 1

		log "\n%8s: zone %s converted, but is still detached" \
		    NOTICE "$zonename"
		if [[ ${zone.brand} == solaris10 ]]; then
			log "%8s: \"zoneadm -z %s attach\" to attach.\n" \
			    Run "$zonename"
		else
			log "%8s: \"zoneadm -z %s attach -u\" to attach.\n" \
			    Run "$zonename"
		fi
	fi
fi

#
# Clean up old datasets
#
log "%s: Removing obsolete datasets" "${zone.name}"
for (( i = ${#dslist[@]} - 1; i >= 0; i-- )); do
	(( verbose )) && log "%s: Destroying dataset %s" "${zone.name}" \
	    "${dslist[i].name}"
	(( dryrun )) || destroy_zone_dataset "${dslist[i].name}"
done

if (( skip_export == 0 )) && [[ -d ${zone.root}/export ]]; then
	log "%s: Migrating /export from the active boot environent" \
	    "${zone.name}"
	(( dryrun )) || migrate_export zone
	(( dryrun )) || migrate_rpool zone
fi

#
# Convert for dataset aliases
#
# If there are no delegated datasets, this does nothing.  If there are
# delegated datasets that don't have aliases defined (none should have
# aliases yet), the default aliases get populated into the zone configuration.
#
typeset -i errors=0
log "%s: Converting dataset aliases\n" "$zonename"
zonecfg -z "$zonename" "$(zonecfg -z $zonename info dataset | \
    nawk '$1 == "name:" { printf "select dataset name=%s; end;", $2 }')"
if (( $? != 0 )); then
	(( errors++ ))
	cat <<-NOMORE

	The automated conversion process is complete.

	Before the zone will boot zone configuration problems need to be
	corrected.  Run 'zonecfg -z $zonename' then use the verify subcommand
	within zonecfg to get a list of configuration problems that must be
	corrected within zonecfg.

	NOMORE
fi

log "%s: Verifying zone with zoneadm\n" "$zonename"
zoneadm -z "$zonename" verify
if (( $? != 0 )); then
	if (( errors == 0 )); then
		print "\nThe automated conversion process is complete."
	fi
	(( errors++ ))
	cat <<-NOMORE

	As displayed above, system configuration problems exist that must
	be corrected befor the zone will boot.  Before booting $zonename you
	will need to manually correct those problems.

	You may run 'zoneadm -z $zonename verify' at any time to re-verify the
	system configuration related to this zone.

	NOMORE
fi

#
# Check for potential /dev/zvol conflicts.  While this may emit warnings, it
# it should not cause the exit code to change from 0.
#
log "%s: Checking for potential configuration conflicts\n" "$zonename"
zonecfg -z "$zonename" verify -v
if (( $? != 0 )); then
	cat <<-NOMORE

	With Zone Dataset Aliasing, ZFS datasets that are delegated to
	$zonename appear within the zone as virtual ZFS pools.  One or more
	of the device resources that appears in the configuration for
	$zonename may hide the existence of similarly named ZFS volumes
	within these virtual pools.  See zonecfg(1M) and dev(7FS) for
	details.

	You may run 'zonecfg -z $zonename verify -v' at any time to perform
	this check again.

	NOMORE
fi

if (( errors != 0 )); then
	exit 1
fi

log "$m_conversion_complete" "$zonename"
exit 0
