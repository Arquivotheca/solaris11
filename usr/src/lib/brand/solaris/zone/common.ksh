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

#
# Only change PATH if you give full consideration to GNU or other variants
# of common commands having different arguments and output.
#
export PATH=/usr/bin:/usr/sbin
unset LD_LIBRARY_PATH

. /usr/lib/brand/shared/common.ksh

PROP_PARENT="org.opensolaris.libbe:parentbe"
PROP_ACTIVE="org.opensolaris.libbe:active"
PROP_BE_HANDLE="com.oracle.libbe:nbe_handle"
PROP_CANDIDATE="com.oracle.zoneadm:candidate_zbe"

f_incompat_options=$(gettext "cannot specify both %s and %s options")
f_sanity_detail=$(gettext  "Missing %s at %s")
sanity_ok=$(gettext     "  Sanity Check: Passed.  Looks like a Solaris system.")
sanity_fail=$(gettext   "  Sanity Check: FAILED (see log for details).")
sanity_fail_vers=$(gettext  "  Sanity Check: the Solaris image (release %s) is not an OpenSolaris image and cannot be installed in this type of branded zone.")
install_fail=$(gettext  "        Result: *** Installation FAILED ***")
f_zfs_in_root=$(gettext "Installing a zone inside of the root pool's 'ROOT' dataset is unsupported.")
f_no_gzbe=$(gettext "unable to determine global zone boot environment.")
f_multiple_ds=$(gettext "multiple active datasets.")
f_no_active_ds=$(gettext "no active dataset.")
f_zfs_unmount=$(gettext "Unable to unmount the zone's root ZFS dataset (%s).\nIs there a global zone process inside the zone root?\nThe current zone boot environment will remain mounted.\n")
f_zfs_mount=$(gettext "Unable to mount the zone's ZFS dataset.")

f_sysrepo_fail=$(gettext "Unable to enable svc:/application/pkg/system-repository, please enable the service manually.")
f_zones_proxyd_fail=$(gettext "Unable to enable svc:/application/pkg/zones-proxyd, please enable the service manually.")
f_set_sysrepo_prop_fail=$(gettext "Unable to set the use-system-repo property.")
# Used by install and attach during argument processing
f_arg_not_dir=$(gettext "Argument %s is not a directory")
f_arg_not_file=$(gettext "Argument %s is not a regular file")
f_arg_not_file_or_dir=$(gettext "Argument %s is not a file or directory")
f_scxml=$(gettext "Configuration profile %s must have an .xml suffix")

m_brnd_usage=$(gettext "brand-specific usage: ")

v_reconfig=$(gettext "Performing zone system configuration")
e_reconfig=$(gettext "System configuration failed")
v_mounting=$(gettext "Mounting the zone")
e_badmount=$(gettext "Zone mount failed")
v_unmount=$(gettext "Unmounting zone")
e_badunmount=$(gettext "Zone unmount failed")
e_exitfail=$(gettext "Postprocessing failed.")
v_update_format=$(gettext "Updating image format")
e_update_format=$(gettext "Updating image format failed")

m_complete=$(gettext    "        Done: Installation completed in %s seconds.")
m_postnote=$(gettext    "  Next Steps: Boot the zone, then log into the zone console (zlogin -C)")
m_postnote2=$(gettext "              to complete the configuration process.")

m_zbe_discover_failed=$(gettext "Unable to determine which boot environment to activate.  Candidates are:\n")
m_zbe_discover_header=$(gettext "Zone Boot Environment Active Global Zone Boot Environment\n--------------------- ------ ------------------------------------")
missing_gzbe=$(gettext "Missing Global Zone Boot Environment")
m_again_with_dash_z=$(gettext "Use the following command to attach a specific zone boot environment:\n%s")
e_no_be_0=$(gettext "Zone has no boot environments")
e_no_be_1=$(gettext "Zone has no boot environment with any of these names: %s")


is_brand_labeled() {
	if [[ -z $ALTROOT ]]; then
		AR_OPTIONS=""
	else
		AR_OPTIONS="-R $ALTROOT"
	fi
	brand=$(/usr/sbin/zoneadm $AR_OPTIONS -z $ZONENAME \
		list -p | awk -F: '{print $6}')
	[[ $brand == "labeled" ]] && return 0
	return 1
}

function sanity_check {
	typeset dir="$1"
	shift
	res=0

	#
	# Check for some required directories.
	#
	checks="etc etc/svc var var/svc"
	for x in $checks; do
		if [[ ! -e $dir/$x ]]; then
			log "$f_sanity_detail" "$x" "$dir"
			res=1
		fi
	done
	if (( $res != 0 )); then
		log "$sanity_fail"
		fatal "$install_fail" "$ZONENAME"
	fi

	# Check for existence of pkg command.
	if [[ ! -x $dir/usr/bin/pkg ]]; then
		log "$f_sanity_detail" "usr/bin/pkg" "$dir"
		log "$sanity_fail"
		fatal "$install_fail" "$ZONENAME"
	fi

	#
	# XXX There should be a better way to do this.
	# Check image release.  We only work on the same minor release as the
	# system is running.  The INST_RELEASE file doesn't exist with IPS on
	# OpenSolaris, so its presence means we have an earlier Solaris
	# (i.e. non-OpenSolaris) image.
	#
	if [[ -f "$dir/var/sadm/system/admin/INST_RELEASE" ]]; then
		image_vers=$(nawk -F= '{if ($1 == "VERSION") print $2}' \
		    $dir/var/sadm/system/admin/INST_RELEASE)
		vlog "$sanity_fail_vers" "$image_vers"
		fatal "$install_fail" "$ZONENAME"
	fi

	vlog "$sanity_ok"
}

function get_current_gzbe {
	#
	# If there is no alternate root (normal case) then set the
	# global zone boot environment by finding the boot environment
	# that is active now.
	# If a zone exists in a boot environment mounted on an alternate root,
	# then find the boot environment where the alternate root is mounted.
	#
	CURRENT_GZBE=$(beadm list -H | nawk -v alt=$ALTROOT -F\; '{
		if (length(alt) == 0) {
		    # Field 3 is the BE status.  'N' is the active BE.
		    if ($3 !~ "N")
			next
		} else {
		    # Field 4 is the BE mountpoint.
		    if ($4 != alt)
		next
		}
		# Field 2 is the BE UUID
		print $2
	    }')
	[[ -z "$CURRENT_GZBE" ]] && fatal "$f_no_gzbe"
	return 0
}

#
# get_active_be zone
#
# Finds the active boot environment for the given zone.
#
# Arguments:
#
#  zone		zone structure initialized with init_zone
#
# Globals:
#
#  CURRENT_GZBE	Current global zone boot environment.  If not already set,
#		it will be set.
#
# Returns:
#
#  0 on success, else 1.
#
function get_active_be {
	typeset -n zone=$1
	typeset active_ds=
	typeset tab=$(printf "\t")

	if [[ -z "$CURRENT_GZBE" ]]; then
		get_current_gzbe
	fi

	typeset name parent active
	zfs list -H -r -d 1 -t filesystem -o name,$PROP_PARENT,$PROP_ACTIVE \
	    "${zone.ROOT_ds}" | while IFS=$'\t' read name parent active ; do
		[[ $parent == "$CURRENT_GZBE" ]] || continue
		[[ $active == on ]] || continue
		vlog "Found active dataset %s" "$name"
		if [[ -n "$active_ds" ]]; then
			error "$f_multiple_ds"
			return 1
		fi
		active_ds=$name
	done
	if [[ -z $active_ds ]]; then
		error "$f_no_active_ds"
		return 1
	fi

	zone.active_ds=$active_ds
}

#
# claim_zbe zone zbe
#
# If the zbe belongs to the existing gzbe or the zbe was extracted from an
# archive and has not yet been attached, set it as the active zbe.  Otherwise,
# clone it and set the clone to the active zbe.
#
# Globals:
#
#  EXIT_CODE	On success, set as described in clone_zbe.
#
# Returns 0 on success, !0 on failure.
#
function claim_zbe {
	typeset -n zone=$1
	typeset zbe=$2
	typeset dss

	init_dataset dss "${zone.ROOT_ds}/$zbe" || return 1

	if [[ -z "$CURRENT_GZBE" ]]; then
		get_current_gzbe
	fi

	if [[ ${dss.props[$PROP_CANDIDATE].value} == "$CURRENT_GZBE" ||
	    ${dss.props[$PROP_PARENT].value} == "$CURRENT_GZBE" ]]; then
		mount_active_be -b "$zbe" zone
		return $?
	fi

	# Sets EXIT_CODE.
	clone_zbe zone "$zbe"
	return $?
}

#
# clone_zbe [-u] zone zbe
#
# Clones a zbe within a zone and sets the new ZBE as the active BE for this
# zone.
#
# Options and arguments:
#
#   zone 	zone structure initialized with init_zone
#   zbe		Name of the zone boot environment to clone.
#
# Globals:
#
#   EXIT_CODE	On success, set as described in clone_zone_rpool.
#
# Returns:
#
#   0 Success, and members of the zone structure have been updated.
#		zone.active_ds		Updated with the dataset that is
#					the root of the zbe.
#		zone.zbe_cloned_from	Set to the name of the zbe passed in
#   1 Failure.  Error message has been logged.
#
function clone_zbe {
	typeset -n zone=$1
	typeset zbe=$2
	typeset -i i
	typeset snapname
	typeset dsn=${zone.ROOT_ds}/$zbe

	[[ -z $zbe ]] && fail_internal "zbe not specified"
	/usr/sbin/zfs list -o name "$dsn" >/dev/null 2>&1 || \
	    fail_internal "Dataset '%s' does not exist" "$dsn"

	typeset now=$(date +%Y-%m-%d-%H:%M:%S)
	snapname=$now
	for (( i=0; i < 100; i++ )); do
		/usr/sbin/zfs snapshot -r "$dsn@$snapname" >/dev/null 2>&1 && \
		    break
		snapname=$(printf "%s-%02d" "$now" $i)
	done
	if (( i == 100 )); then
		error "$f_zfs_snapshot_of" "$dsn"
		return 1
	fi

	# Clone, activate, and mount ZBE
	zone.active_ds=${zone.ROOT_ds}/$zbe
	# Sets EXIT_CODE.  "zone" appears twice as it is the source and target.
	clone_zone_rpool zone zone "$snapname" || return 1
	zone.zbe_cloned_from=$zbe
	return 0
}

#
# discover_active_be zone
#
# Looks for the ZBE that is best suited to be the active ZBE.
#
# The caller may optionally constrain the list of ZBEs that is considered for
# activation by populating the zone.allowed_bes associative array.  In such a
# case,  If zone.allowed_bes is a non-empty associative array, only BEs in that
# array are considered.
#
# After selecting which ZBE is the best candidate for activation, care will
# be taken not to "steal" the ZBE from another global zone.  If the chosen
# zbe has $PROP_PARENT matching the UUID of an extant global zone BE, it is
# the chosen ZBE is cloned and this new clone is the ZBE that is selected for
# activation.
#
# Note, however, that a ZBE that appears in zone.allowed_bes is
# never cloned.  It is assumed that zone.allowed_bes contains a set of ZBEs
# that was received from an archive and any existing values in $PROP_PARENT
# on these ZBEs are stale.
#
# Arguments
#
#  zone		A zone structure initialized with init_zone.
#
# Globals
#
#  EXIT_CODE	If a ZBE is cloned, set as described in clone_zbe().  Else,
#		if a list of datasets is displayed, set to
#		ZONE_SUBPROC_DETACHED.
#
# Returns:
#
#   0	Success.  The discovered active_be has been activated (see
#	set_active_be() for details) and has been mounted on the zone root.
#   1	Active dataset could not be found and an error message has been
#   	printed.
#
function discover_active_be {
	typeset -n zone=$1
	shift
	typeset -A uuid2gzbe
	typeset -i needs_selection=0

	#
	# Load an associative array of global zone BEs.  Store current uuid
	# of GZBE in $active_gzbe.
	#
	# uuid2gzbe[<gzbe uuid>]=<gzbe name>
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
	# Load an associative array of non-global zone BEs and arrays of
	# likely candidates.
	#
	# ngzbe[<ngzbe name>].parent=<gzbe uuid>
	# ngzbe[<ngzbe name>].active=<on|off|->
	# ngzbe[<ngzbe name>].mountpoint=</|zoneroot>
	#
	typeset name mountpoint parent active candidate
	typeset -A ngzbe
	typeset -a activezbe	# NGZ BEs that are active
	typeset -a this_gz	# NGZ BEs that match this GZ BE
	typeset -a this_gz_active	# match GZ BE and is active
	zfs list -H -r -d 1 -t filesystem -o \
	    name,mountpoint,$PROP_PARENT,$PROP_ACTIVE,$PROP_CANDIDATE \
	    "${zone.ROOT_ds}" | \
	    while IFS=$'\t' read name mountpoint parent active candidate
	    do

		# skip the non-BE top-level dataset
		[[ $name == "${zone.ROOT_ds}" ]] && continue

		typeset curbe=$(basename "$name")

		# skip BEs that are not in the allowed_bes list
		if (( ${#zone.allowed_bes[@]} != 0 )) &&
		    [[ -z ${zone.allowed_bes[$curbe]} ]]; then
			vlog "Ignoring dataset %s: %s not in allowed_bes" \
			    "$name" "$(basename "$name")"
			continue
		fi

	    	if [[ -z ${uuid2gzbe[$parent]} ]]; then
	    		uuid2gzbe[$parent]=$missing_gzbe
		fi
		ngzbe[$curbe].parent=$parent
		ngzbe[$curbe].active=$active
		ngzbe[$curbe].mountpoint=$mountpoint
		ngzbe[$curbe].candidate=$candidate

		# Update some arrays used in decision process.
		if [[ $parent == "$active_gzbe" ]]; then
		       	a_push this_gz "$curbe"
			[[ $active == on ]] && a_push this_gz_active "$curbe"
		fi
		[[ $active == on ]] && a_push activezbe "$curbe"
	done
	#
	# If there are no BEs, return an error
	#
	if (( ${#ngzbe[@]} == 0 )); then
		if (( ${#zone.allowed_bes[@]} == 0 )); then
			error "$e_no_be_0"
		else
			error "$e_no_be_1" "${!zone.allowed_bes[*]}"
		fi
		return 1
	fi

	# If there was only one ZBE active for this GZ, activate it.
	if (( ${#this_gz_active[@]} == 1 )); then
		mount_active_be -c -b "${this_gz_active[0]}" zone
		return $?
	fi

	# If there was only one ZBE associated with this GZ, activate it.
	if (( ${#this_gz[@]} == 1 )); then
		mount_active_be -c -b "${this_gz[0]}" zone
		return $?
	fi

	# If there was only one ZBE that was active, clone and/or activate it.
	if (( ${#activezbe[@]} == 1 )); then
		typeset zbe="${activezbe[0]}"
		if [[ ${#zone.allowed_bes[@]} != 0 ]]; then
			mount_active_be -c -b "$zbe" zone
			return $?
		fi
		# If the zbe is not associated with any gzbe, do not clone it.
		if [[ ${uuid2gzbe[${ngzbe[$zbe].parent}]} == "$missing_gzbe" ]]
		then
			mount_active_be -c -b "$zbe" zone
			return $?
		fi
		clone_zbe zone "$zbe"
		return $?
	fi

	# If there was only one ZBE, clone and/or activate it
	if (( ${#ngzbe[@]} == 1 )); then
		#
		# We really want the name of index 0, but a subscript of 0
		# is not supported.  Since we know that there is only one
		# item in the associative array, the name of all the items
		# is equivalent to the name of the first item.
		#
		typeset zbe="${!ngzbe[@]}"
		if [[ ${#zone.allowed_bes[@]} ]]; then
			mount_active_be -c -b "$zbe" zone
			return $?
		fi
		# Sets EXIT_CODE.
		clone_zbe zone "$zbe"
		return $?
	fi

	log "$m_zbe_discover_failed"
	log "$m_zbe_discover_header"
	typeset zbe
	for zbe in "${!ngzbe[@]}" ; do
		typeset uuid=${ngzbe[$zbe].parent}
		typeset bename=${uuid2gzbe[$uuid]}
		if [[ $bename == $missing_gzbe ]]; then
			typeset cuuid=${ngzbe[$zbe].candidate}
			if [[ $cuuid != $missing_gzbe && \
			    -n ${uuid2gzbe[$cuuid]} ]]; then
				bename="Extracted for ${uuid2gzbe[$cuuid]}"
			else
		       		bename=$uuid
			fi
		fi
		log "%-21s %-6s %s" "$zbe" "${ngzbe[$zbe].active}" "$bename"
	done

	log "%s" ""

	#
	# Install and attach need different messages.  m_usage_dash_z should
	# be defined in the brand's install and attach scripts.
	#
	if (( ${#ATTACH_Z_COMMAND[@]} != 0 )); then
		EXIT_CODE=ZONE_SUBPROC_DETACHED
	       	log "$m_again_with_dash_z" "${ATTACH_Z_COMMAND[*]}"
	fi

	return 1
}

#
# set_active_be zone bootenv
#
# Sets the active boot environment for the zone.  This includes updating the
# zone structure and setting the required properties ($PROP_PARENT,
# $PROP_ACTIVE) on the top-level BE datasets.
#
function set_active_be {
	typeset -n zone="$1"
	typeset be=$2
	typeset name canmount parent active candidate

	[[ -z $be ]] && fail_internal "zbe not specified"

	if [[ -z "$CURRENT_GZBE" ]]; then
	       	get_current_gzbe
	fi

	#
	# Turn off the active property on BE's with the same GZBE and ensure
	# that there aren't any BE datasets that will mount automatically.
	#
	zfs list -H -r -d 1 -t filesystem -o \
	    name,canmount,$PROP_PARENT,$PROP_ACTIVE,$PROP_CANDIDATE \
	    ${zone.ROOT_ds} | \
	    while IFS=$'\t' read name canmount parent active candidate ; do
		# skip the ROOT dataset
		[[ $name ==  "${zone.ROOT_ds}" ]] && continue
		# The root of each BE should only be mounted explicitly.
		if [[ $canmount != noauto ]]; then
			zfs set canmount=noauto "$name" || \
			    fail_internal "$e_zfs_set" canmount "$name"
		fi
		#
		# If this was extracted from an archive within this GZ,
		# finish the association process.  In the unlikely event
		# that these property updates fail, manual cleanup may
		# be required, but it should not prevent the attach.
		#
		if [[ $candidate == "$CURRENT_GZBE" ]]; then
			zfs inherit "$PROP_CANDIDATE" "$name" ||
			    log "$e_zfs_inherit" "$PROP_CANDIDATE" "$name"
			#
			# Setting the parent to this gzbe makes it possible
			# for beadm to clean up the zbes within the zone
			# once one of the candidate zbe's is attached.
			#
			if [[ $parent != "$CURRENT_GZBE" ]]; then
				zfs set "$PROP_PARENT=$CURRENT_GZBE" "$name" ||
				    log "$e_zfs_set" "$PROP_PARENT" "$name"
				parent=$CURRENT_GZBE
			fi
		fi

		# Deactivate BEs for this GZ that are not being set to active.
		[[ $parent == "$CURRENT_GZBE" ]] || continue
		[[ $active == on ]] || continue
		[[ $name ==  "${zone.ROOT_ds}/$be" ]] && continue
		vlog "Deactivating active dataset %s" "$name"
		zfs set $PROP_ACTIVE=off "$name" || return 1
	done

	zone.active_ds="${zone.ROOT_ds}/$be"
	zfs set "$PROP_PARENT=$CURRENT_GZBE" ${zone.active_ds} \
	    || return 1
	zfs set "$PROP_ACTIVE=on" ${zone.active_ds} || return 1
	zfs set "$PROP_BE_HANDLE=on" "${zone.rpool_ds}" || return 1

	typeset origin
	zfs list -H -o name,origin "${zone.active_ds}" | while read name origin
	do
		if [[ $origin == "${zone.ROOT_ds}"/* ]]; then
			vlog "Promoting active dataset '%s'" "${zone.active_ds}"
			zfs promote "${zone.active_ds}"
		fi
	done

	return 0
}

#
# Run system configuration inside a zone.
#
function reconfigure_zone {
	typeset sc_config=$1
	vlog "$v_reconfig"

	vlog "$v_mounting"
	ZONE_IS_MOUNTED=1
	zoneadm -z $ZONENAME mount -f || fatal "$e_badmount"

	# If unconfig service exists and is online then use sysconfig
	SC_ONLINE=$(svcprop -p restarter/state \
	    svc:/milestone/unconfig:default 2> /dev/null)
	if [[ -n $sc_config ]]; then
		sc_config_base=$(basename "$sc_config")
		# Remove in case $sc_config_base is a directory
		safe_dir "/system"
		safe_dir "/system/volatile"
		rm -rf "$ZONEPATH/lu/system/volatile/$sc_config_base"
		safe_copy_rec $sc_config \
		    "$ZONEPATH/lu/system/volatile/$sc_config_base"
		zlogin -S $ZONENAME "_UNCONFIG_ALT_ROOT=/a \
		    /usr/sbin/sysconfig configure -g system \
		    -c /system/volatile/$sc_config_base --destructive" \
		    </dev/null >/dev/null 2>&1
	else
		zlogin -S $ZONENAME "_UNCONFIG_ALT_ROOT=/a \
		    /usr/sbin/sysconfig configure -g system --destructive" \
		    </dev/null >/dev/null 2>&1
	fi

	if (( $? != 0 )); then
		error "$e_reconfig"
		failed=1
	fi

	vlog "$v_unmount"
	zoneadm -z $ZONENAME unmount || fatal "$e_badunmount"
	ZONE_IS_MOUNTED=0

	[[ -n $failed ]] && fatal "$e_exitfail"
}

#
# Emits to stdout the fmri for the supplied package,
# stripped of publisher name and other junk.
#
get_pkg_fmri() {
	typeset pname=$1
	typeset pkg_fmri=
	typeset info_out=

	info_out=$(LC_ALL=C $PKG info pkg:/$pname 2>/dev/null)
	if [[ $? -ne 0 ]]; then
		return 1
	fi
	pkg_fmri=$(echo $info_out | grep FMRI | cut -d'@' -f 2)
	echo "$pname@$pkg_fmri"
	return 0
}

#
# Emits to stdout the entire incorporation for this image,
# stripped of publisher name and other junk.
#
get_entire_incorp() {
	get_pkg_fmri entire
	return $?
}

#
# Handle pkg exit code.  Exit 0 means Command succeeded, exit 4 means
# No changes were made - nothing to do.  Any other exit code is an error.
#
pkg_err_check() {
	typeset res=$?
	(( $res != 0 && $res != 4 )) && fail_fatal "$1"
}

#
# Enable the services needed to perform packaging operations inside a zone.
#
enable_zones_services() {
	/usr/sbin/svcadm enable -st svc:/application/pkg/system-repository
	if [[ $? -ne 0 ]]; then
		error "$f_sysrepo_fail"
		return 1
	fi
	/usr/sbin/svcadm enable -st svc:/application/pkg/zones-proxyd
	if [[ $? -ne 0 ]]; then
		error "$f_zones_proxyd_fail"
		return 1
	fi
	return 0
}

#
# tag_candidate_zbes ROOTdsn [be_array_name [curgz_assoc_array_name]]
#
# Tags each dataset that is a child of ROOTdsn with
# $PROP_CANDIDATE=$CURRENT_GZBE.
#
# Arguments:
#   ROOTdsn		The name of a dataset that contains zbes.
#   be_array_name	If specified, this variable will contain an array
#			of candidate zbes on return.
#   curgz_assoc_array_name If specified and any zbes have $PROP_PARENT that
#			matches $CURRENT_GZBE, curgz_assoc_array_name will
#			contain that list.  Otherwise, curgz_assoc_array_name
#			will be updated to reflect all of the zbes found.  Note
#			that curgz_assoc_array_name is an associative (not
#			indexed) array with keys that match the zbe name.  The
#			value assigned to each key is not significant.
#
# Returns 0 if there is at least one zbe found
# Returns 1 if there are no zbes or there is a failure updating properties.
#
function tag_candidate_zbes {
	typeset ROOTdsn=$1
	if [[ -n $2 ]]; then
		typeset -n bes=$2
	fi
	typeset -a bes
	if [[ -n $3 ]]; then
		typeset -n curgzbes=$3
	fi
	typeset -A curgzbes
	typeset dsn parent

	if [[ -z "$CURRENT_GZBE" ]]; then
		get_current_gzbe
	fi

	/usr/sbin/zfs list -H -o name -r -d 1 -t filesystem "$ROOTdsn" \
	    2>/dev/null | while read dsn ; do
		[[ $dsn == "$ROOTdsn" ]] && continue
		a_push bes "$(basename "$dsn")"

		zfs set "$PROP_CANDIDATE=$CURRENT_GZBE" "$dsn" || return 1

		# See if the zbe is already associated with the GZBE
		parent=$(zfs get "$PROP_PARENT" "$dsn")
		if [[ $parent == "$CURRENT_GZBE" ]]; then
			curgzbes[$(basename "$dsn")]=1
		fi
	done
	if (( ${#bes[@]} == 0 )); then
		error "$e_no_active_be"
		return 1
	fi

	#
	# If there were no zbes that already had a parent of $CURRENT_GZBE,
	# mark all of the found zbes as being allowed.
	#
	if (( ${#curgzbes[@]} == 0 )); then
		typeset be
		for be in "${bes[@]}"; do
			curgzbes[$be]=1
		done
	fi

	return 0
}
