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
# Only change PATH if you give full consideration to GNU or other variants
# of common commands having different arguments and output.
#
export PATH=/usr/bin:/usr/sbin
unset LD_LIBRARY_PATH

. /usr/lib/brand/shared/common.ksh

# Values for service tags.
STCLIENT=/usr/bin/stclient
ST_PRODUCT_NAME="Solaris 10 Containers"
ST_PRODUCT_REV="1.0"
ST_PRODUCT_UUID="urn:uuid:2f459121-dec7-11de-9af7-080020a9ed93"

w_sanity_detail=$(gettext "       WARNING: Skipping image sanity checks.")
f_sanity_detail=$(gettext  "Missing %s at %s")
f_sanity_sparse=$(gettext  "Is this a sparse zone image?  The image must be whole-root.")
f_sanity_vers=$(gettext  "The image release version must be 10 (got %s), the zone is not usable on this system.")
f_sanity_nopatch=$(gettext "Unable to determine the image's patch level.")
f_sanity_downrev=$(gettext "The image patch level is downrev for running in a solaris10 branded zone.\n(patchlist %s)")
f_need_newer_emul=$(gettext "The image requires a newer version of the solaris10 brand emulation.")
f_multiple_ds=$(gettext "Multiple active datasets.")
f_no_active_ds=$(gettext "No active dataset; the zone's ZFS root dataset must be configured as\n\ta zone boot environment.")
f_zfs_unmount=$(gettext "Unable to unmount the zone's root ZFS dataset (%s).\nIs there a global zone process inside the zone root?\nThe current zone boot environment will remain mounted.\n")
f_zfs_mount=$(gettext "Unable to mount the zone's ZFS dataset.")
incompat_options=$(gettext "mutually exclusive options.\n%s")

sanity_ok=$(gettext     "  Sanity Check: Passed.  Looks like a Solaris 10 image.")
sanity_fail=$(gettext   "  Sanity Check: FAILED (see log for details).")

e_s10_be_in_use=$(gettext "Boot environment %s already in use.")
e_badboot=$(gettext "Zone boot failed")
e_unconfig=$(gettext "sys-unconfig failed")
v_unconfig=$(gettext "Performing zone sys-unconfig")

v_no_tags=$(gettext "Service tags facility not present.")
e_bad_uuid=$(gettext "Failed to get zone UUID")
v_addtag=$(gettext "Adding service tag: %s")
v_deltag=$(gettext "Removing service tag: %s")
v_mkdirs=$(gettext "Creating mount points")
e_addtag_fail=$(gettext "Adding service tag failed (error: %s)")
f_cd=$(gettext "Unable to change working directory to %s");
e_not_file=$(gettext "%s is not a regular file")

function sanity_check {
	typeset dir="$1"
	res=0

	#
	# Check for some required directories and make sure this isn't a
	# sparse zone image.
	#
	checks="etc etc/svc var var/svc"
	for x in $checks; do
		if [[ ! -e $dir/$x ]]; then
			log "$f_sanity_detail" "$x" "$dir"
			res=1
		fi
	done
	# Files from SUNWcsr and SUNWcsu that are in sparse inherit-pkg-dirs.
	checks="lib/svc sbin/zonename usr/bin/chmod"
	for x in $checks; do
		if [[ ! -e $dir/$x ]]; then
			log "$f_sanity_detail" "$x" "$dir"
			log "$f_sanity_sparse"
			res=1
		fi
	done

	if (( $res != 0 )); then
		log "$sanity_fail"
		fatal "$install_fail" "$ZONENAME"
	fi

	if [[ "$SANITY_SKIP" == 1 ]]; then
		log "$w_sanity_detail"
		return
	fi

	#
	# Check image release to be sure its S10.
	#
	image_vers="unknown"
	if [[ -f $dir/var/sadm/system/admin/INST_RELEASE ]]; then
		image_vers=$(nawk -F= '{if ($1 == "VERSION") print $2}' \
		    $dir/var/sadm/system/admin/INST_RELEASE)
	fi

	if [[ "$image_vers" != "10" ]]; then
		log "$f_sanity_vers" "$image_vers"
		res=1
	fi

	#
	# Make sure we have the minimal KU patch we support.  These are the
	# KUs for S10u8.
	#
	if [[ $(uname -p) == "i386" ]]; then
		req_patch="141445-09"
	else
		req_patch="141444-09"
	fi

	for i in $dir/var/sadm/pkg/SUNWcakr*
	do
		if [[ ! -d $i || ! -f $i/pkginfo ]]; then
			log "$f_sanity_nopatch"
			res=1
		fi
	done

	#
	# Check the core kernel pkg for the required KU patch.
	#
	found=0
	for i in $dir/var/sadm/pkg/SUNWcakr*/pkginfo
	do
		patches=$(nawk -F= '{if ($1 == "PATCHLIST") print $2}' $i)
		for patch in $patches
		do
			if [[ $patch == $req_patch ]]; then
				found=1
				break
			fi
		done

		if (( $found == 1 )); then
			break
		fi
	done

	if (( $found != 1 )); then
		log "$f_sanity_downrev" "$patches"
		res=1
	fi

	#
	# Check the S10 image for a required version of the emulation.
	#
	VERS_FILE=/usr/lib/brand/solaris10/version
	s10vers_needs=0
	if [[ -f $dir/$VERS_FILE ]]; then
		s10vers_needs=$(/usr/bin/egrep -v "^#" $dir/$VERS_FILE)
	fi

	# Now get the current emulation version.
	emul_vers=$(/usr/bin/egrep -v "^#" $VERS_FILE)

	# Verify that the emulation can run this version of S10.
	if (( $s10vers_needs > $emul_vers )); then
		log "$f_need_newer_emul"
		res=1
	fi

	if (( $res != 0 )); then
		log "$sanity_fail"
		fatal "$install_fail" "$ZONENAME"
	fi

	vlog "$sanity_ok"
}

#
# get_active_be zone
#
# Gets the currently active BE, storing the name of the active BE's root
# dataset in in zone.active_ds.  At this time, solaris10 zones only support a
# single boot environment, which is named zbe-0.
#
# Arguments:
#
#  zone		zone structure initilialized with init_zone
#
# Return:
#
#  Always returns 0
#
function get_active_be {
	typeset -n zone="$1"
	zone.active_ds="${zone.ROOT_ds}/zbe-0"
	return 0
}

#
# set_active_be zone bename
#
# Sets the specified BE as the active boot environment.
#
# Arguments:
#
#   zone	A zone structure initialized with init_zone
#   bename	The name of the boot environment.  Should not contain "/".
#
function set_active_be {
	typeset -n zone="$1"
	typeset be="$2"

	if [[ $be != "zbe-0" ]]; then
		fail_internal "Active BE being set to %s, not zbe-0" "$be"
	fi

	zone.active_ds="${zone.ROOT_ds}/$be"

	return 0
}

#
# Whenever we reach into the zone while running in the global zone we
# need to validate that none of the interim directories are symlinks
# that could cause us to inadvertently modify the global zone.
#
function mk_zone_dirs {
	vlog "$v_mkdirs"
	if [[ ! -f $ZONEROOT/tmp && ! -d $ZONEROOT/tmp ]]; then
		mkdir -m 1777 -p $ZONEROOT/tmp || exit $EXIT_CODE
	fi
	if [[ ! -f $ZONEROOT/var/run && ! -d $ZONEROOT/var/run ]]; then
		mkdir -m 1755 -p $ZONEROOT/var/run || exit $EXIT_CODE
	fi
	if [[ ! -f $ZONEROOT/var/tmp && ! -d $ZONEROOT/var/tmp ]]; then
		mkdir -m 1777 -p $ZONEROOT/var/tmp || exit $EXIT_CODE
	fi
	if [[ ! -h $ZONEROOT/etc && ! -f $ZONEROOT/etc/mnttab ]]; then
		/usr/bin/touch $ZONEROOT/etc/mnttab || exit $EXIT_CODE
		/usr/bin/chmod 444 $ZONEROOT/etc/mnttab || exit $EXIT_CODE
	fi
	if [[ ! -f $ZONEROOT/proc && ! -d $ZONEROOT/proc ]]; then
		mkdir -m 755 -p $ZONEROOT/proc || exit $EXIT_CODE
	fi
	if [[ ! -f $ZONEROOT/dev && ! -d $ZONEROOT/dev ]]; then
		mkdir -m 755 -p $ZONEROOT/dev || exit $EXIT_CODE
	fi
	if [[ ! -h $ZONEROOT/etc && ! -h $ZONEROOT/etc/svc && \
	    ! -d $ZONEROOT/etc/svc ]]; then
		mkdir -m 755 -p $ZONEROOT/etc/svc/volatile || exit $EXIT_CODE
	fi
}

#
# We're sys-unconfig-ing the zone.  This will normally halt the zone, however
# there are problems with sys-unconfig and it can hang when the zone is booted
# to milestone=none.  Sys-unconfig also sometimes hangs halting the zone.
# Thus, we take some care to workaround these sys-unconfig limitations.
#
# On entry we expect the zone to be booted.  We use sys-unconfig -R to make it
# think its working on an alternate root and let the caller halt the zone.
#
function sysunconfig_zone {
	/usr/sbin/zlogin -S $ZONENAME /usr/sbin/sys-unconfig -R /./ \
	    >/dev/null 2>&1
	if (( $? != 0 )); then
		error "$e_unconfig"
		return 1
	fi

	return 0
}

#
# Get zone's uuid for service tag.
#
function get_inst_uuid {
        typeset ZONENAME="$1"

	ZONEUUID=`zoneadm -z $ZONENAME list -p | nawk -F: '{print $5}'`
	[[ $? -ne 0 || -z $ZONEUUID ]] && return 1

	INSTANCE_UUID="urn:st:${ZONEUUID}"
	return 0
}

#
# Add a service tag for a given zone.  We use two UUIDs-- the first,
# the Product UUID, comes from the Sun swoRDFish ontology.  The second
# is the UUID of the zone itself, which forms the instance UUID.
#
function add_svc_tag {
        typeset ZONENAME="$1"
        typeset SOURCE="$2"

	if [ ! -x $STCLIENT ]; then
		vlog "$v_no_tags"
		return 0
	fi

	get_inst_uuid "$ZONENAME" || (error "$e_bad_uuid"; return 1)

	vlog "$v_addtag" "$INSTANCE_UUID"
	$STCLIENT -a \
	    -p "$ST_PRODUCT_NAME" \
	    -e "$ST_PRODUCT_REV" \
	    -t "$ST_PRODUCT_UUID" \
	    -i "$INSTANCE_UUID" \
	    -P "none" \
	    -m "Sun" \
	    -A `uname -p` \
	    -z "$ZONENAME" \
	    -S "$SOURCE" >/dev/null 2>&1

	err=$?

	# 226 means "duplicate record," which we can ignore.
	if [[ $err -ne 0 && $err -ne 226 ]]; then
		error "$e_addtag_fail" "$err"
		return 1
	fi
	return 0
}

#
# Remove a service tag for a given zone.
#
function del_svc_tag {
        typeset ZONENAME="$1"

	if [ ! -x $STCLIENT ]; then
		vlog "$v_no_tags"
		return 0
	fi

	get_inst_uuid "$ZONENAME" || (error "$e_bad_uuid"; return 1)

	vlog "$v_deltag" "$INSTANCE_UUID"
        $STCLIENT -d -i "$INSTANCE_UUID" >/dev/null 2>&1
	return 0
}

#
# convert_s10_zonepath_to_be <directory>
#
# In Solaris 10, if a zone is on a ZFS dataset, the zone root is contained
# within the root subdirectory of the zonepath dataset.  In contrast, on
# Solaris 11, the root subdirectory is a mountpoint for a boot environment
# dataset.  This function uses rm and mv to transform a Solaris 10 style
# zonepath dataset into a Solaris 11 style boot environment dataset.  This
# approach is used instead of copying to a new dataset to minimize I/O.
#
# This function assumes that a snapshot of the dataset exists and can be used
# for restoring files that really shouldn't have been deleted.
#
function convert_s10_zonepath_to_be {
	typeset dir="$1"
	typeset newroot file
	typeset -i removed=0

	vlog "Converting %s from a Solaris 10 zonepath to a boot environment" \
	    "$dir"

	[[ -z "$dir" ]] && fail_internal "$f_int_missing_arg" "dir"
	[[ -d "$dir" ]] || fail_internal "'%s' is not a directory" "$dir"

	#
	# A temporary directory is created to avoid conflicts of
	# "mv $dir/root/* $dir/root" when $dir/root/root exists.  Really,
	# mktemp is just used to create a directory name that is extremely
	# unlikely to collide with anything else.  In the extremely unlikely
	# event of a collision between $newroot and a file in $dir/root, a
	# "mv" will fail in an unexploitable way and an error will be
	# returned.
	#
	newroot=$(mktemp -d "$dir/attach.XXXXXX") || {
		error "$e_tmpdir" "$dir/attach.XXXXXX"
		return 1
	}
	rmdir "$newroot" || {
		error "$e_rmdir" "$newroot"
		return 1
	}

	mv "$dir/root" "$newroot" || {
		error "$e_mv" "$dir/root" "$newroot"
		return 1
	}

	for file in "$dir"/* "$dir"/.* ; do
		[[ $(basename "$file") == "." ]] && continue
		[[ $(basename "$file") == ".." ]] && continue
		[[ $file == "$newroot" ]] && continue
		(( removed == 0 )) && \
	    		vlog "Removed files preserved in boot env snapshot."
		(( removed=1 ))
		vlog "Removing '%s' from zonepath" "${file#$dir/}"
		rm -rf "$file"
		if [[ -e "$file" || -h "$file" ]]; then
			error "$e_rm" "$file"
			return 1
		fi
	done
	for file in "$newroot"/* "$newroot"/.* ; do
		[[ $(basename "$file") == "." ]] && continue
		[[ $(basename "$file") == ".." ]] && continue
		mv "$file" "$dir" || {
			error "$e_mv" "$file" "$dir"
			return 1
		}
	done

	# $dir was previously a zonepath with permissions of 700.
	chown root:sys "$dir" || {
		error "$f_chown" "$dir"
		return 1
	}
	chmod 755 "$dir" || {
		error "$f_chmod" "$dir"
		return 1
	}

	# This temporary directory should be empty - get rid of it.
	rmdir "$newroot" || {
		error "$e_rmdir" "$newroot"
		return 1
	}

	return 0
}

#
# discover_active_be zone
#
#
function discover_active_be {
	typeset -n zone=$1

	if [[ ${#allowed_bes[@]} == 1 ]]; then
		vlog "Activating only allowed boot environment %s" \
		    "${!allowed_bes[0]}"
		if ! zfs rename "${zone.ROOT_ds}/${!allowed_bes[0]}" \
		    "${zone.ROOT_ds}/zbe-0"; then
			error "$e_s10_be_in_use" "zbe-0"
			return 1
		fi
	elif /usr/sbin/zfs list -H -o name ${zone.ROOT_ds}/zbe-0 \
	    >/dev/null 2>&1; then
	    	# Otherwise, if zbe-0 exists, use it
		vlog "Activating default boot environment zbe-0"
	else
		typeset dsn= candidate=
		/usr/sbin/zfs list -H -o name -r -d 1 \
		    ${zone.ROOT_ds} | while read dsn ; do
			[[ $dsn == "${zone.ROOT_ds}" ]] && continue
			vlog "Found candidate boot environment %s" "$dsn"
			if [[ -n $candidate ]]; then
				error "$f_multiple_ds"
				return 1
			fi
			candidate=$dsn
		done
		if [[ -z "$candidate" ]]; then
			error "$e_no_active_be"
			return 1
		fi
		vlog "Activating only candidate environment %s" \
		    "$candidate"
		if ! zfs rename "$candidate" "${zone.ROOT_ds}/zbe-0"; then
			error "$e_s10_be_in_use" "zbe-0"
			return 1
		fi
	fi

	mount_active_be -b zbe-0 zone
	return $?
}

#
# Unpack flar into current directory (which should be zoneroot).  The flash
# archive is standard input.  See flash_archive(4) man page.
#
# We can't use "flar split" since it will only unpack into a directory called
# "archive".  We need to unpack in place in order to properly handle nested
# fs mounts within the zone root.  This function does the unpacking into the
# current directory.
#
# This code is derived from the gen_split() function in /usr/sbin/flar so
# we keep the same style as the original.
#
# Globals:
#
#  EXIT_CODE	Set to ZONE_SUBPROC_FATAL if datasets are created but not
#		populated with archive contents.  Set to ZONE_SUBPROC_DETACHED
#		if archive extraction is complete.
#
function install_flar {
	typeset -n zone=$1
	typeset result
	typeset archiver_command
	typeset archiver_arguments
	typeset identification=$(mktemp /tmp/identification.XXXXXX)

	# Read cookie
	read -r input_line
	if (( $? != 0 )); then
		log "$not_readable" "$install_media"
		return 1
	fi
	# The cookie has format FlAsH-aRcHiVe-m.n where m and n are integers.
	if [[ ${input_line%%-[0-9]*.[0-9]*} != "FlAsH-aRcHiVe" ]]; then
		log "$not_flar"
		return 1
	fi

	while [[ true ]]
	do
		# We should always be at the start of a section here
		read -r input_line
		if [[ ${input_line%%=*} != "section_begin" ]]; then
			log "$bad_flar"
			return 1
		fi
		section_name=${input_line##*=}

		# If we're at the archive, we're done skipping sections.
		if [[ "$section_name" == "archive" ]]; then
			break
		fi

		#
		# Save identification section to a file so we can determine
		# how to unpack the archive.
		#
		if [[ "$section_name" == "identification" ]]; then
			/usr/bin/rm -f $identification
			while read -r input_line
			do
				if [[ ${input_line%%=*} == \
				    "section_begin" ]]; then
					/usr/bin/rm -f $identification
					log "$bad_flar"
					return 1
				fi

				if [[ $input_line == \
				    "section_end=$section_name" ]]; then
					break;
				fi
				echo $input_line >> $identification
			done

			continue
		fi

		#
		# Otherwise skip past this section; read lines until detecting
		# section_end.  According to flash_archive(4) we can have
		# an arbitrary number of sections but the archive section
		# must be last.
		#
		success=0
		while read -r input_line
		do
			if [[ $input_line == "section_end=$section_name" ]];
			then
				success=1
				break
			fi
			# Fail if we miss the end of the section
			if [[ ${input_line%%=*} == "section_begin" ]]; then
				/usr/bin/rm -f $identification
				log "$bad_flar"
				return 1
			fi
		done
		if (( success == 0 )); then
			#
			# If we get here we read to the end of the file before
			# seeing the end of the section we were reading.
			#
			/usr/bin/rm -f $identification
			log "$bad_flar"
			return 1
		fi
	done

	typeset origdir=$(pwd)

	# Check for an archive made from a ZFS root pool.
	if egrep -s "^rootpool=" $identification ; then
		rm -f $identification
		#
		# The solaris10 brand only knows how to deal with zbe-0.
		# Reporting the error here avoids a long delay as the flar
		# is extracted to only get an error message during
		# set_active_be().
		# 
		if /usr/sbin/zfs list "${zone.ROOT_ds}/zbe-0" >/dev/null 2>&1
		then
			ds_empty "${zone.ROOT_ds}/zbe-0" || {
				error "$e_s10_be_in_use" "zbe-0"
				return 1
			}
			zfs destroy -r "${zone.ROOT_ds}/zbe-0" || {
				error "$e_zfs_destroy" "${zone.ROOT_ds}/zbe-0"
				return 1
			}
		fi

		# Sets EXIT_CODE.
		extract_zfs zone cat /dev/stdin
		result=$?

		#
		# If this fails, we will have already gotten more useful
		# errors out of extract_zfs.
		#
		cd "${zone.root}" >/dev/null 2>&1

		post_unpack

		cd "$origdir"

		return $result
	fi

	vlog "%s" "$stage1 $insrc | install_flar"

	# Sets EXIT_CODE
	create_active_ds zone || fatal "$f_no_active_ds"
	mount_active_be -c zone || fatal "$f_zfs_mount"

	get_fs_info
	mnt_fs
	if (( $? != 0 )); then
		umnt_fs >/dev/null 2>&1
		rm -f $fstmpfile $fscpiofile $fspaxfile
		fatal "$mount_failed"
	fi

	cd ${zone.root} || fatal "$f_cd" "${zone.root}"

	# Get the information needed to unpack the archive.
	archiver=$(get_archiver $identification)
	if [[ $archiver == "pax" ]]; then
		# pax archiver specified
		archiver_command="/usr/bin/pax"
		if [[ -s $fspaxfile ]]; then
			archiver_arguments="-r@/ -p e -c \
			    $(/usr/bin/cat $fspaxfile)"
		else
			archiver_arguments="-r@/ -p e"
		fi
	elif [[ $archiver == "cpio" || -z $archiver ]]; then
		# cpio archived specified OR no archiver specified - use default
		archiver_command="/usr/bin/cpio"
		archiver_arguments="-icdP@/umfE $fscpiofile"
	else
		# unknown archiver specified
		log "$unknown_archiver" $archiver
		cd "$origdir"
		return 1
	fi

	if [[ ! -x $archiver_command ]]; then
		/usr/bin/rm -f $identification
		log "$cmd_not_exec" $archiver_command
		cd "$origdir"
		return 1
	fi

	compression=$(get_compression $identification)

	# We're done with the identification file
	/usr/bin/rm -f $identification

	# Extract archive
	if [[ $compression == "compress" ]]; then
		/usr/bin/zcat | \
		    $archiver_command $archiver_arguments 2>/dev/null
	else
		$archiver_command $archiver_arguments 2>/dev/null
	fi
	result=$?

	post_unpack

	cd "$origdir"

	(( result != 0 )) && return 1

	EXIT_CODE=$ZONE_SUBPROC_DETACHED
	return 0
}
