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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

#
# Only change PATH if you give full consideration to GNU or other variants
# of common commands having different arguments and output.
#
export PATH=/usr/bin:/usr/sbin
unset LD_LIBRARY_PATH

#
# backtrace [message]
#
# Used to generate a backtrace (newest on top) of functions up to the caller.
# Intended for use when internal errors are encountered.
#
function backtrace {
	typeset msg="$*"
	typeset -a args

	#
	# Use "set -u" followed by an unset variable reference to force
	# a backtrace.
	#
	set -A args -- $(exec 2>&1; set -u; unset bt; $bt)
	if [[ -n "$msg" ]]; then
		print -u2 -- "${msg}:"
	else
		print -u2 "Backtrace:"
	fi
	#
	# The ksh backtrace format is unlike that seen in common debuggers and
	# other languages, so the logic below transforms it.  That is, we go
	# from a message like the following (but all on one line).  do_{a,b,c}
	# are functions that led to the backtrace and ./foo is the script name.
	#
	#   ./foo[17]: do_a[6]: do_b[10]: do_c[14]: backtrace: line 47: bt:
	#    parameter not set
	#
	# To:
	#	do_c[14]:
	#	do_b[10]:
	#	do_a[6]:
	#	./foo[17]:
	#
	typeset -i i
	#
	# Skip errors about this function as we are reporting on the path
	# that led to this function, not the function itself.  From the example
	# above, we remove the arguments that make up "backtrace: line 47: bt:
	# parameter not set"
	#
	for (( i = ${#args[@]} - 1; i >= 0; i-- )); do
		[[ "${args[i]}" == "${.sh.fun}:" ]] && break
	done
	# Print a backtrace, newest on top
	for (( i-- ; i >= 0; i-- )); do
		print -u2 "\t${args[i]}"
	done
}
#
# Send the error message to the screen and to the logfile.
#
function error {
	typeset fmt="$1"
	shift
	[[ -z "$fmt" ]] && fail_internal "format argument undefined"

	printf -- "${MSG_PREFIX}ERROR: ${fmt}\n" "$@"
	[[ -n $LOGFILE ]] && printf "[$(date)] ERROR: ${fmt}\n" "$@" >&2
}

function fatal {
	typeset fmt="$1"
	shift
	[[ -z $EXIT_CODE ]] \
	    && fail_internal 'fatal (%s) called with undefined $EXIT_CODE' \
	    "$(printf -- "$fmt" "$@")"

	error "$fmt" "$@"
	exit $EXIT_CODE
}

function fail_fatal {
	typeset fmt="$1"
	shift

	error "$fmt" "$@"
	EXIT_CODE=$ZONE_SUBPROC_FATAL
	exit $ZONE_SUBPROC_FATAL
}

#
# This function name is misleading!
#
# If a brand script exits with $ZONE_SUBPROC_NOTCOMPLETE, this means the
# operation did not complete and can be retried.  It causes zoneadm to
# return the zone to the state it was when the operation started.  It
# DOES NOT set the state to incomplete, as one would expect.
#
function fail_incomplete {
	typeset fmt="$1"
	[[ -z "$fmt" ]] && fail_internal "format argument undefined"

	printf "ERROR: " 1>&2
	printf -- "$@" 1>&2
	printf "\n" 1>&2
	EXIT_CODE=$ZONE_SUBPROC_NOTCOMPLETE
	exit $ZONE_SUBPROC_NOTCOMPLETE
}

function fail_usage {
	#
	# format is optional argument to fail_usage: do not fail_internal if
	# none given
	#
	printf -- "$@" 1>&2
	printf "\n" 1>&2
	printf -- "$m_brnd_usage" 1>&2
	[[ -z $m_usage ]] && fail_internal "m_usage undefined"
	printf -- "$m_usage\n" 1>&2
	EXIT_CODE=$ZONE_SUBPROC_USAGE
	exit $ZONE_SUBPROC_USAGE
}

#
# fail_internal is intended to be used in places where we are checking for
# logic errors, much as assert is used in C.
#
function fail_internal {
	typeset fmt=$1
	shift
	backtrace "Unrecoverable internal error"

	error "$fmt" "$@"
	EXIT_CODE=$ZONE_SUBPROC_FATAL
	exit $ZONE_SUBPROC_FATAL
}

#
# Send the provided printf()-style arguments to the screen and to the logfile.
#
function log {
	typeset fmt="$1"
	shift
	[[ -z "$fmt" ]] && fail_internal "format argument undefined"

	printf -- "${MSG_PREFIX}${fmt}\n" "$@"
	[[ -n $LOGFILE ]] && printf "[$(date)] ${MSG_PREFIX}${fmt}\n" "$@" >&2
}

#
# Print provided text to the screen if the shell variable "OPT_V" is set.
# The text is always sent to the logfile.
#
function vlog {
	typeset fmt="$1"
	shift
	[[ -z "$fmt" ]] && fail_internal "format argument undefined"

	[[ -n $OPT_V ]] && printf -- "${MSG_PREFIX}${fmt}\n" "$@"
	[[ -n $LOGFILE ]] && printf "[$(date)] ${MSG_PREFIX}${fmt}\n" "$@" >&2
}

# Check that zone is not in the ROOT dataset.
function fail_zonepath_in_rootds {
	typeset gzrootds=$(get_ds_from_path /)

	if [[ -z $gzrootds ]]; then
		#
		# This is an internal error because we shouldn't have made it
		# this far if the system wasn't ZFS root.
		#
		fail_internal "Unable to find dataset mounted at /"
	fi

	[[ $1 == "$gzrootds"/* ]] && fail_fatal "$f_zfs_in_root"
}

#  Return success if system is labeled (aka Trusted Extensions).
function is_system_labeled {
	[[ -x /bin/plabel ]] || return 1
  	/bin/plabel >/dev/null 2>&1 && return 0
	return 1
}

#
# Validate that the directory is safe.
#
# It is possible for a malicious zone root user to modify a zone's filesystem
# so that modifications made to the zone's filesystem by administrators in the
# global zone modify the global zone's filesystem.  We can prevent this by
# ensuring that all components of paths accessed by scripts are real (i.e.,
# non-symlink) directories.
#
# NOTE: The specified path should be an absolute path as would be seen from
# within the zone.  Also, this function does not check parent directories.
# If, for example, you need to ensure that every component of the path
# '/foo/bar/baz' is a directory and not a symlink, then do the following:
#
#	safe_dir /foo
#	safe_dir /foo/bar
#	safe_dir /foo/bar/baz
#
function safe_dir {
	typeset dir="$1"

	if [[ -h $ZONEROOT/$dir || ! -d $ZONEROOT/$dir ]]; then
		fatal "$e_baddir" "$dir"
	fi
}

# Like safe_dir except the dir doesn't have to exist.
function safe_opt_dir {
	typeset dir="$1"

	[[ ! -e $ZONEROOT/$dir ]] && return

	if [[ -h $ZONEROOT/$dir || ! -d $ZONEROOT/$dir ]]; then
		fatal "$e_baddir" "$dir"
	fi
}

# Only make a copy if we haven't already done so.
function safe_backup {
	typeset src="$1"
	typeset dst="$2"

	if [[ ! -h $src && ! -h $dst && ! -d $dst && ! -f $dst ]]; then
		/usr/bin/cp -p $src $dst || fatal "$e_badfile" "$src"
	fi
}

# Make a copy even if the destination already exists.
function safe_copy {
	typeset src="$1"
	typeset dst="$2"

	if [[ ! -h $src && ! -h $dst && ! -d $dst ]]; then
		/usr/bin/cp -p $src $dst || fatal "$e_badfile" "$src"
	fi
}

# Make a recursive copy
function safe_copy_rec {
	typeset src="$1"
	typeset dst="$2"

	if [[ ! -h $src && ! -h $dst && ! -d $dst ]]; then
		/usr/bin/cp -pr $src $dst || fatal "$e_badfile" "$src"
	fi
}

# Move a file
function safe_move {
	typeset src="$1"
	typeset dst="$2"

	if [[ ! -h $src && ! -h $dst && ! -d $dst ]]; then
		/usr/bin/mv $src $dst || fatal "$e_badfile" "$src"
	fi
}

function safe_rm {
	if [[ ! -h $ZONEROOT/$1 && -f $ZONEROOT/$1 ]]; then
		rm -f "$ZONEROOT/$1"
	fi
}

#
# Replace the file with a wrapper pointing to the native brand code.
# However, we only do the replacement if the file hasn't already been
# replaced with our wrapper.  This function expects the cwd to be the
# location of the file we're replacing.
#
# Some of the files we're replacing are hardlinks to isaexec so we need to 'rm'
# the file before we setup the wrapper while others are hardlinks to rc scripts
# that we need to maintain.
#
function safe_replace {
	typeset filename="$1"
	typeset runname="$2"
	typeset mode="$3"
	typeset own="$4"
	typeset rem="$5"
	typeset nativedir="$6"

	if [ -h $filename -o ! -f $filename ]; then
		return
	fi

	egrep -s "Solaris Brand Replacement" $filename
	if [ $? -eq 0 ]; then
		return
	fi

	safe_backup $filename $filename.pre_p2v
	if [ $rem = "remove" ]; then
		rm -f $filename
	fi

	cat <<-END >$filename || exit 1
	#!/bin/sh -p
	#
	# Solaris Brand Replacement
	#
	# Attention.  This file has been replaced with a new version for
	# use in a virtualized environment.  Modification of this script is not
	# supported and all changes will be lost upon reboot.  The
	# {name}.pre_p2v version of this file is a backup copy of the
	# original and should not be deleted.
	#
	END

	echo "__S10_BRAND_NATIVE_PATH=$nativedir;" >>$filename || exit 1
	echo ". $runname \"\$@\"" >>$filename || exit 1

	chmod $mode $filename
	chown $own $filename
}

function safe_wrap {
	typeset filename="$1"
	typeset runname="$2"
	typeset mode="$3"
	typeset own="$4"

	if [ -f $filename ]; then
		log "$e_cannot_wrap" "$filename"
		exit 1
	fi

	cat <<-END >$filename || exit 1
	#!/bin/sh
	#
	# Solaris Brand Wrapper
	#
	# Attention.  This file has been created for use in a
	# virtualized environment.  Modification of this script
	# is not supported and all changes will be lost upon reboot.
	#
	END

	echo ". $runname \"\$@\"" >>$filename || exit 1

	chmod $mode $filename
	chown $own $filename
}

#
# Read zonecfg fs entries and save the relevant data, one entry per
# line.
# This assumes the properties from the zonecfg output, e.g.:
#	fs:
#		dir: /opt
#		special: /opt
#		raw not specified
#		type: lofs
#		options: [noexec,ro,noatime]
#
# and it assumes the order of the fs properties as above.
#
function get_fs_info {
	zonecfg -z $ZONENAME info fs | nawk '{
		if ($1 == "options:") {
			# Remove brackets.
			options=substr($2, 2, length($2) - 2);
			printf("%s %s %s %s\n", dir, type, special, options);
		} else if ($1 == "dir:") {
			dir=$2;
		} else if ($1 == "special:") {
			special=$2;
		} else if ($1 == "type:") {
			type=$2
		}
	}' >> $fstmpfile
}

#
# Mount zonecfg fs entries into the zonepath.
#
function mnt_fs {
	if [ ! -s $fstmpfile ]; then
		return;
	fi

	# Sort the fs entries so we can handle nested mounts.
	sort $fstmpfile | nawk -v zonepath=$ZONEPATH '{
		if (NF == 4)
			options="-o " $4;
		else
			options=""

		# Create the mount point.  Ignore errors since we might have
		# a nested mount with a pre-existing mount point.
		cmd="/usr/bin/mkdir -p " zonepath "/root" $1 " >/dev/null 2>&1"
		system(cmd);

		cmd="/usr/sbin/mount -F " $2 " " options " " $3 " " \
		    zonepath "/root" $1;
		if (system(cmd) != 0) {
			printf("command failed: %s\n", cmd);
			exit 1;
		}
	}' >>$LOGFILE
}

#
# Unmount zonecfg fs entries from the zonepath.
#
function umnt_fs {
	if [[ -z $fstmpfile || ! -s $fstmpfile ]]; then
		return
	fi

	# Reverse sort the fs entries so we can handle nested unmounts.
	sort -r "$fstmpfile" | nawk -v zonepath=$ZONEPATH '{
		cmd="/usr/sbin/umount " zonepath "/root" $1
		if (system(cmd) != 0) {
			printf("command failed: %s\n", cmd);
		}
	}' >>$LOGFILE
}

#
# get_dataset path [outvar]
#
# Find the dataset mounted at a given path. The implementation is tolerant
# of the path not being an exact match of the entry in /etc/mnttab (e.g. an
# extra / somewhere) but the supplied path must be a mount point of a ZFS
# dataset.  If a second argument is provided, it must be the name of a variable
# into which the result will be stored.
#
# On success, returns 0.  In the one argument form, the dataset is printed
# to stdout.  In the two argument form, the dataset name is stored in the
# variable by the name of the second argument.
# If no match is found, returns 1.
#
function get_ds_from_path {
	typeset path=$1
	typeset dsn mountpoint

	/usr/sbin/zfs list -H -o name,mountpoint "$path" 2>/dev/null | \
	    IFS=$'\t' read dsn mountpoint
	[[ -z $dsn || -z $mountpoint ]] && return 1

	# If mountpoint=legacy, consult /etc/mnttab.
	if [[ $mountpoint == legacy ]]; then
		mountpoint=$(nawk -F$'\t' -v "dsn=$dsn" \
		    '$1 == dsn { print $2; exit }' /etc/mnttab)
		[[ -z $mountpoint ]] && return 1
	fi

	[[ $mountpoint -ef $path ]] || return 1

	if [[ -n "$2" ]]; then
		typeset -n res=$2
		res=$dsn
	else
		print -- "$dsn"
	fi
	return 0
}

#
# create_zone_rpool [-e] [-r] zone
#
# Establishes the standard dataset hierarchy under <zonepath>/rpool
#
# Arguments:
#	-e	Do not create rpool/export hierarchy
#	-r	Do not create rpool/ROOT or rpool/export hierarchy
#	zone	a zone structure created with init_zone
#
# Globals:
#
#  EXIT_CODE	Set to ZONE_SUBPROC_FATAL if any dataset that could collide
#		with a future install or attach operation is created.
#
function create_zone_rpool {
	typeset opt
	typeset -i skipexport=0
	typeset -i skipROOT=0

	while getopts :er opt; do
		case $opt in
			e) skipexport=1 ;;
			r) skipROOT=1 ;;
			?) fail_internal "$f_int_bad_opt" "$OPTARG" ;;
		esac
	done
	shift $(( OPTIND - 1 ))
	case $# in
		0)	fail_internal "$f_int_missing_arg" zonepath_dataset ;;
		1)	typeset -n zone=$1 ;;
		*)	shift;
			fail_internal "$f_int_bad_arg" "$*"
			;;
	esac

	#
	# rpool
	#
	typeset ds
	init_dataset ds "${zone.rpool_ds}" >/dev/null 2>&1
	if (( $? != 0 )); then
		zfs create -o zoned=on -o mountpoint=/rpool \
		    "${zone.rpool_ds}" || {
			log "$f_zfs_create" "${zone.ROOT_ds}"
			return 1
		}
	else
		zfs_set zoned=on ds || return 1
		zfs_set canmount=on ds || return 1
		zfs_set mountpoint=/rpool ds || return 1
		# Dealing with existing rpool.  Perhaps someone intentionally
		# got rid of export dataset.  We shouldn't make it come back.
		(( skipexport = 1 ))
	fi

	if (( $skipROOT != 0 )); then
		return 0
	fi

	#
	# If the zone configuration already has export in it, don't create it.
	# Message that is logged in the event that it exists is inappropriate
	# for this function, so send it to the bit bucket.
	#
	zonecfg_has_export zone >/dev/null 2>&1 && (( skipexport = 1 ))

	#
	# rpool/ROOT
	#
	init_dataset ds "${zone.ROOT_ds}" >/dev/null 2>&1
	if (( $? != 0 )); then
		zfs create -o canmount=noauto -o mountpoint=legacy \
		    "${zone.ROOT_ds}" || {
			log "$f_zfs_create" "${zone.ROOT_ds}"
			return 1
		}
	else
		zfs inherit zoned "${zone.ROOT_ds}"
		typeset prop
		for prop in canmount=noauto mountpoint=legacy; do
			zfs_set "$prop" ds || return 1
		done
	fi
	# zfs_set doesn't do -r so use zfs command directly.
	zfs set -r canmount=noauto "${zone.ROOT_ds}" || return 1

	#
	# rpool/export
	#
	if (( skipexport == 0 )); then
		zfs create -o mountpoint=/export "${zone.rpool_ds}/export" || {
			log "$f_zfs_create" "${zone.rpool_ds}/export"
			return 1
		}
		EXIT_CODE=$ZONE_SUBPROC_FATAL
		zfs create "${zone.rpool_ds}/export/home" || {
			log "$f_zfs_create" "${zone.rpool_ds}/export/home"
			return 1
		}
	fi

	return 0
}

#
# create_active_ds [-s snapshot] [-r] zone
#
# Set up ZFS dataset hierarchy for the zone root dataset and the datasets
# listed in zone.new_be_datasets.  If an active dataset is being created
# as a clone of another BE (that is, -s is used), the caller is responsible for
# cloning any child datasets and zone.new_be_datasets is ignored.
#
# Arguments and options:
#
# -s snapshot	If specified, the active dataset is cloned from this snapshot.
#		With -s, the caller is responsible for cloning any child
#		datasets.  That is, /var is not created automatically.
# -r		If specified, only create rpool, not ROOT, export or zbe
# zone		zone structure created with init_zone.
#
# Globals:
#
#  EXIT_CODE	Set to ZONE_SUBPROC_FATAL if any ZBE or rpool/export is
#		created.
#
function create_active_ds {
	typeset snapshot opt
	typeset -i skipROOT=0

	while getopts :s:r opt; do
		case $opt in
			s) snapshot="$OPTARG" ;;
			r) skipROOT=1 ;;
			?) fail_internal "$f_int_bad_opt" "$OPTARG" ;;
		esac
	done
	shift $(( OPTIND - 1 ))
	case $# in
		0) fail_internal "$f_int_missing_arg" zone ;;
		1) : ;;
		*) fail_internal "$f_int_bad_arg" "$*" ;;
	esac
	typeset -n zone=$1

	#
	# Find the zone's current dataset.  This should have been created by
	# zoneadm.
	#
	[[ -z "${zone.path.ds}" ]] && fail_fatal "$f_no_ds"

	# Check that zone is not in the ROOT dataset.
	fail_zonepath_in_rootds "${zone.path.ds}"

	#
	# Create the zone's rpool, rpool/ROOT, rpool/export, etc.  If creating
	# from a snapshot (part of cloning process) assume the caller has
	# already created it.
	#
	if [[ -z "$snapshot" ]]; then
		if (( $skipROOT == 1 )); then
			# Does not set EXIT_CODE.
			create_zone_rpool -r zone || return 1
			return 0
		fi
		# Sets EXIT_CODE.
		create_zone_rpool zone || return 1

	fi

	#
	# Create the new active dataset with "zfs create" or "zfs clone",
	# depending on whether a snapshot was passed.  If the create or clone
	# operation fails 100 times, it's likely it will never succeed.
	#
	typeset bename dsname
	typeset -a zfs_prop_options
	#
	# mountpoint=/ is safe because create_zone_rpool verifies zoned=on
	# for parent.
	#
	typeset -a be_props
	set -A be_props -- -o canmount=noauto -o mountpoint=/

	typeset -a sl_opt
	if is_system_labeled; then
		# On TX, reset the mlslabel upon cloning
		set -A sl_opt -- -o mlslabel=none
	fi

	typeset -i i
	typeset be_prefix
	if [[ ${zone.brand} == "solaris10" ]]; then
		be_prefix=zbe
	else
		be_prefix=solaris
	fi
	for (( i = 0 ; i < 100 ; i++ )); do
		bename=$(printf "%s-%d" "$be_prefix" $i)
		dsname="${zone.ROOT_ds}/$bename"

		if [[ -n "$snapshot" ]]; then
			vlog "Creating active_ds $dsname from $snapshot"
			zfs clone "${be_props[@]}" "${sl_opt[@]}" "$snapshot" \
			    "$dsname" >/dev/null 2>&1 && break
		else
			vlog "Creating active_ds $dsname"
			zfs create "${be_props[@]}" "$dsname" \
			    >/dev/null 2>&1 && break
		fi
		bename=
		dsname=
	done
	[[ -z $bename ]] && return 1
	EXIT_CODE=$ZONE_SUBPROC_FATAL

	# If clone wasn't used, create the child datasets, if any.
	if [[ -z $snapshot ]]; then
		typeset child
		for child in ${zone.new_be_datasets[@]}; do
			vlog "Creating child dataset: %s" "$child"
			zfs create -o mountpoint="/$child" -o canmount=noauto \
			    "$dsname/$child" || return 1
		done
	fi

	# Activate the BE.
	set_active_be zone "$bename" || return 1
}

#
# Perform validation and cleanup in the zoneroot after unpacking the archive.
#
function post_unpack {
	#
	# Check if the image was created with a valid libc.so.1.
	#
	hwcap=$(moe -v -32 $ZONEROOT/lib/libc.so.1 2>&1)
	if (( $? != 0 )); then
		vlog "$f_hwcap_info" "$hwcap"
		fail_fatal "$f_sanity_hwcap"
	fi

	( cd "$ZONEROOT" && \
	    find . \( -type b -o -type c \) -exec rm -f "{}" \; )
}

#
# Determine flar compression style from identification file.
#
function get_compression {
	typeset ident=$1
	typeset line=$(grep "^files_compressed_method=" $ident)

	print ${line##*=}
}

#
# Determine flar archive style from identification file.
#
function get_archiver {
	typeset ident=$1
	typeset line=$(grep "^files_archived_method=" $ident)

	print ${line##*=}
}

#
# Get the archive base.
#
# We must unpack the archive in the right place within the zonepath so
# that files are installed into the various mounted filesystems that are set
# up in the zone's configuration.  These are already mounted for us by the
# mntfs function.
#
# Archives can be made of either a physical host's root file system or a
# zone's zonepath.  For a physical system, if the archive is made using an
# absolute path (/...) we can't use it.  For a zone the admin can make the
# archive from a variety of locations;
#
#   a) zonepath itself: This will be a single dir, probably named with the
#      zone name, it will contain a root dir and under the root we'll see all
#      the top level dirs; etc, var, usr...  We must be above the ZONEPATH
#      when we unpack the archive but this will only work if the the archive's
#      top-level dir name matches the ZONEPATH base-level dir name.  If not,
#      this is an error.
#
#   b) inside the zonepath: We'll see root and it will contain all the top
#      level dirs; etc, var, usr....  We must be in the ZONEPATH when we unpack
#      the archive.
#
#   c) inside the zonepath root: We'll see all the top level dirs, ./etc,
#      ./var, ./usr....  This is also the case we see when we get an archive
#      of a physical sytem.  We must be in ZONEROOT when we unpack the archive.
#
# Note that there can be a directory named "root" under the ZONEPATH/root
# directory.
#
# This function handles the above possibilities so that we reject absolute
# path archives and figure out where in the file system we need to be to
# properly unpack the archive into the zone.  It sets the ARCHIVE_BASE
# variable to the location where the achive should be unpacked.
#
function get_archive_base {
	stage1=$1
	archive=$2
	stage2=$3

	vlog "$m_analyse_archive"

	base=$($stage1 $archive | $stage2 2>/dev/null | nawk -F/ '{
		# Check for an absolute path archive
		if (substr($0, 1, 1) == "/")
			exit 1

		if ($1 != ".")
			dirs[$1] = 1
		else
			dirs[$2] = 1
	}
	END {
		for (d in dirs) {
			cnt++
			if (d == "bin")  sawbin = 1
			if (d == "etc")  sawetc = 1
			if (d == "root") sawroot = 1
			if (d == "var")  sawvar = 1
		}

		if (cnt == 1) {
			# If only one top-level dir named root, we are in the
			# zonepath, otherwise this must be an archive *of*
			# the zonepath so print the top-level dir name.
			if (sawroot)
				print "*zonepath*"
			else
				for (d in dirs) print d
		} else {
			# We are either in the zonepath or in the zonepath/root
			# (or at the top level of a full system archive which
			# looks like the zonepath/root case).  Figure out which
			# one.
			if (sawroot && !sawbin && !sawetc && !sawvar)
				print "*zonepath*"
			else
				print "*zoneroot*"
		}
	}')

	if (( $? != 0 )); then
		umnt_fs
		fatal "$e_absolute_archive"
	fi

	if [[ "$base" == "*zoneroot*" ]]; then
		ARCHIVE_BASE=$ZONEROOT
	elif [[ "$base" == "*zonepath*" ]]; then
		ARCHIVE_BASE=$ZONEPATH
	else
		# We need to be in the dir above the ZONEPATH but we need to
		# validate that $base matches the final component of ZONEPATH.
		bname=$(basename $ZONEPATH)

		if [[ "$bname" != "$base" ]]; then
			umnt_fs
			fatal "$e_mismatch_archive" "$base" "$bname"
		fi
		ARCHIVE_BASE=$(dirname $ZONEPATH)
	fi
}

#
# Unpack cpio archive into zoneroot.
#
function install_cpio {
	stage1=$1
	archive=$2

	get_archive_base "$stage1" "$archive" "cpio -it"

	cpioopts="-idmP@/fE $fscpiofile"

	vlog "cd \"$ARCHIVE_BASE\" && $stage1 \"$archive\" | cpio $cpioopts"

	# Ignore errors from cpio since we expect some errors depending on
	# how the archive was made.
	( cd "$ARCHIVE_BASE" && $stage1 "$archive" | cpio $cpioopts )

	post_unpack

	return 0
}

#
# Unpack pax archive into zoneroot.
#
function install_pax {
	typeset archive=$1
	typeset filtopt

	get_archive_base "cat" "$archive" "pax"

	if [[ -n $fspaxfile && -s $fspaxfile ]]; then
		filtopt="-c $(/usr/bin/cat $fspaxfile)"
	fi

	vlog "cd \"$ARCHIVE_BASE\" && pax -r@/ -p e -f \"$archive\" $filtopt"

	# Ignore errors from pax since we expect some errors depending on
	# how the archive was made.
	( cd "$ARCHIVE_BASE" && pax -r@/ -p e -f "$archive" $filtopt )

	post_unpack

	return 0
}

#
# Unpack UFS dump into zoneroot.
#
function install_ufsdump {
	archive=$1

	vlog "cd \"$ZONEROOT\" && ufsrestore rf \"$archive\""

	#
	# ufsrestore goes interactive if you ^C it.  To prevent that,
	# we make sure its stdin is not a terminal.
	#
	( cd "$ZONEROOT" && ufsrestore rf "$archive" < /dev/null )
	result=$?

	post_unpack

	return $result
}

#
# Copy directory hierarchy into zoneroot.
#
function install_dir {
	source_dir=$1

	cpioopts="-pPdm@/"

	first=1
	filt=$(for i in $(cat $fspaxfile)
		do
			echo $i | egrep -s "/" && continue
			if [[ $first == 1 ]]; then
				printf "^%s" $i
				first=0
			else
				printf "|^%s" $i
			fi
		done)

	list=$(cd "$source_dir" && ls -d * | egrep -v "$filt")
	flist=$(for i in $list
	do
		printf "%s " "$i"
	done)
	findopts="-xdev ( -type d -o -type f -o -type l ) -print"

	vlog "cd \"$source_dir\" && find $flist $findopts | "
	vlog "cpio $cpioopts \"$ZONEROOT\""

	# Ignore errors from cpio since we expect some errors depending on
	# how the archive was made.
	( cd "$source_dir" && find $flist $findopts | \
	    cpio $cpioopts "$ZONEROOT" )

	post_unpack

	return 0
}

#
# This is a common function for laying down a zone image from a variety of
# different sources.  This can be used to either install a fresh zone or as
# part of zone migration during attach.
#
# The first argument specifies the type of image: archive, directory or stdin.
# The second argument specifies the image itself.  In the case of stdin, the
# second argument specifies the format of the stream (cpio, flar, etc.).
# Any validation or post-processing on the image is done elsewhere.
#
# This function calls a 'sanity_check' function which must be provided by
# the script which includes this code.
#
# Globals:
#    EXIT_CODE		Set to ZONE_SUBPROC_DETACHED on successful return.
#			May be set to ZONE_SUBPROC_FATAL on failure.  If no
#			datasets are created before failure, EXIT_CODE is
#			unchanged.
#
function install_image {
	typeset -n zone=$1
	typeset intype=$2
	typeset insrc=$3

	if [[ -z ${zone.name} || -z $intype || -z $insrc ]]; then
		fail_internal "Missing argument to install_image.  Got: '%s'" \
		    "$*"
	fi

	typeset filetype="unknown"
	typeset filetypename="unknown"
	typeset filetypeprefx=
	typeset stage1="cat"

	if [[ "$intype" == "directory" ]]; then
		if [[ "$insrc" == "-" ]]; then
			# Indicates that the existing zonepath is prepopulated.
			filetype="existing"
			filetypename="existing"
		else
			if [[ "$(echo $insrc | cut -c 1)" != "/" ]]; then
				fatal "$e_path_abs" "$insrc"
			fi

			if [[ ! -e "$insrc" ]]; then
				log "$e_not_found" "$insrc"
				fatal "$e_install_abort"
			fi

			if [[ ! -r "$insrc" ]]; then
				log "$e_not_readable" "$insrc"
				fatal "$e_install_abort"
			fi

			if [[ ! -d "$insrc" ]]; then
				log "$e_not_dir"
				fatal "$e_install_abort"
			fi

			sanity_check $insrc

			filetype="directory"
			filetypename="directory"
		fi

	else
		# Common code for both archive and stdin stream.

		if [[ "$intype" == "archive" ]]; then
			if [[ $insrc != /* ]]; then
				log "$e_path_abs" "$insrc"
				fatal "$e_install_abort"
			elif [[ ! -f "$insrc" ]]; then
				log "$e_not_found" "$insrc"
				fatal "$e_install_abort"
			fi
			ftype="$(LC_ALL=C file "$insrc" | cut -d: -f 2)"

			#
			# If it is a compressed stream, extract the first
			# megabyte into a temporary file to figure out what
			# kind of data is in the file.
			#
			case "$ftype" in
			*bzip2*)
				stage1=bzcat
				filetypeprefx="bzipped "
				;;
			*gzip*)	stage1=gzcat
				filetypeprefix="gzipped "
				;;
			esac

			if [[ $stage1 != cat ]]; then
				typeset tastefile=$(mktemp)
				[[ -n $tastefile ]] || fatal "$e_tmpfile"

				"$stage1" "$insrc" | dd of=$tastefile \
				    bs=1024k count=1 2>/dev/null
				ftype="$(LC_ALL=C file "$tastefile" \
				    | cut -d: -f 2)"
				rm -f "$tastefile"
			fi
		elif [[ $intype == stdin ]]; then
			# For intype == stdin, the insrc parameter specifies
			# the stream format coming on stdin.
			ftype="$insrc"
			insrc="-"
		else
			fail_internal "intype '%s' is invalid" "$intype"
		fi


		# Setup vars for the archive type we have.
		case "$ftype" in
		*cpio*)	filetype="cpio"
			filetypename="cpio archive"
			;;
		*ufsdump*)
			if [[ ${zone.brand} != solaris10 ]]; then
				log "$e_unsupported_archive" "$ftype" \
				    "${zone.brand}"
				fatal "$e_install_abort"
			fi
			filetype="ufsdump"
			filetypename="ufsdump archive"
			;;
		flar|flash|*Flash\ Archive*)
			if [[ ${zone.brand} != solaris10 ]]; then
				log "$e_unsupported_archive" "$ftype" \
				    "${zone.brand}"
				fatal "$e_install_abort"
			fi
			filetype="flar"
			filetypename="flash archive"
			;;
		tar|*USTAR\ tar\ archive)
			filetype="tar"
			filetypename="tar archive"
			;;
		pax|*USTAR\ tar\ archive\ extended\ format*)
			filetype="xustar"
			filetypename="pax (xustar) archive"
			;;
		zfs|*ZFS\ snapshot\ stream*)
			filetype="zfs"
			filetypename="ZFS send stream"
			;;
		*)	log "$e_unsupported_archive" "$ftype" "${zone.brand}"
			fatal "$e_install_abort"
			;;
		esac
	fi

	# compressed archives only supported for cpio and zfs
	if [[ $stage1 != cat ]]; then
		filetypename="${filetypeprefx}$filetypename"
		if [[ $filetype != cpio && $filetype != zfs ]]; then
			log "$e_unsupported_archive" "$filetypename" \
			    "${zone.brand}"
			fatal "$e_install_abort"
		fi
	fi

	vlog "$filetypename"

	if [[ $filetype != @(existing|zfs|flar) ]]; then
		#
		# Since we're not using a pre-existing ZFS dataset layout, or
		# an archive containing a dataset layout, create the zone
		# datasets and mount them.
		#

		# Sets EXIT_CODE.
		create_active_ds zone || fatal "$f_no_active_ds"
		mount_active_be -c zone || fatal "$f_no_active_ds"

		# If the brand supports candidate zbes, tag this as a candidate.
		if [[ -n $PROP_CANDIDATE ]]; then
			zfs set "$PROP_CANDIDATE=on" "${zone.active_ds}" ||
			    return 1
		fi
	fi

	fstmpfile=$(/usr/bin/mktemp -t -p /var/tmp)
	if [[ -z "$fstmpfile" ]]; then
		fatal "$e_tmpfile"
	fi

	# Make sure we always have the files holding the directories to filter
	# out when extracting from a CPIO or PAX archive.  We'll add the fs
	# entries to these files in get_fs_info()
	fscpiofile=$(/usr/bin/mktemp -t -p /var/tmp fs.cpio.XXXXXX)
	if [[ -z "$fscpiofile" ]]; then
		rm -f $fstmpfile
		fatal "$e_tmpfile"
	fi

	# Filter out these directories.
	cat >>$fscpiofile <<-EOF
	dev/*
	devices/*
	devices
	proc/*
	tmp/*
	var/run/*
	system/contract/*
	system/object/*
	system/volatile/*
	rpool/boot/*
	rpool/boot
	rpool/etc/*
	rpool/etc
	EOF

	fspaxfile=$(/usr/bin/mktemp -t -p /var/tmp fs.pax.XXXXXX)
	if [[ -z "$fspaxfile" ]]; then
		rm -f $fstmpfile $fscpiofile
		fatal "$e_tmpfile"
	fi

	print -n "dev devices proc tmp var/run system/contract system/object" \
	    "system/volatile rpool/boot rpool/etc" >>$fspaxfile

	# Set up any fs mounts so the archive will install into the correct locations.
	if [[ $filetype != @(existing|zfs|flar) ]]; then
		get_fs_info
		mnt_fs
		if (( $? != 0 )); then
			umnt_fs >/dev/null 2>&1
			rm -f $fstmpfile $fscpiofile $fspaxfile
			fatal "$mount_failed"
		fi
	fi

	if [[ $filetype == existing ]]; then
		if [[ -z ${zone.zbe_cloned_from} ]]; then
			log "$no_installing"
		else
			log "$from_clone" "${zone.zbe_cloned_from}"
		fi
	else
		log "$installing"
	fi

	#
	# Install the image into the zonepath.
	#
	unpack_result=0
	if [[ "$filetype" == "cpio" ]]; then
		install_cpio "$stage1" "$insrc"
		unpack_result=$?

	elif [[ "$filetype" == "flar" ]]; then
		# Sets EXIT_CODE.
		$stage1 $insrc | install_flar zone
		unpack_result=$?

	elif [[ "$filetype" == "xustar" ]]; then
		install_pax "$insrc"
		unpack_result=$?

	elif [[ "$filetype" == "tar" ]]; then
		vlog "cd \"${zone.root}\" && tar -xf \"$insrc\""
		# Ignore errors from tar since we expect some errors depending
		# on how the archive was made.
		( cd "${zone.root}" && tar -xf "$insrc" )
		unpack_result=0
		post_unpack

	elif [[ "$filetype" == "ufsdump" ]]; then
		install_ufsdump "$insrc"
		unpack_result=$?

	elif [[ "$filetype" == "directory" ]]; then
		install_dir "$insrc"
		unpack_result=$?

	elif [[ "$filetype" == "zfs" ]]; then
		# Sets EXIT_CODE.
		extract_zfs zone "$stage1" "$insrc"
		unpack_result=$?
	fi

	# Clean up any fs mounts used during unpacking.
	umnt_fs
	rm -f $fstmpfile $fscpiofile $fspaxfile

	chmod 700 "${zone.path}"

	(( unpack_result != 0 )) && fatal "$f_unpack_failed"

	# Verify this is a valid image.
	mount_active_be -C zone
	sanity_check "${zone.root}"

	#
	# We are now far enough along that the admin may be able to fix up an
	# extracted/copied image in the event that an attach fails.  Instead of
	# deleting the new datasets, mark them as pinned so the error path
	# doesn't delete them.  Spit a message if pinning fails, but don't
	# abort the operation.
	#
	if [[ $filetype != existing ]]; then
		pin_datasets "${zone.path.ds}" || error "$f_pin"
	fi
	EXIT_CODE=$ZONE_SUBPROC_DETACHED

	return 0
}

#
# extract_zfs zone filter file
#
# Receive the zfs stream from the specified file.  The stream is passed through
# the specified filter, such as gzcat or bzcat.  If no filter is needed,
# use "cat" as the filter.  zone should have been initialized by init_zone.
# File can be a regular file or /dev/stdin.
#
# On successful creation, the active ZBE is mounted on the zone root.
#
# Globals:
#
#  EXIT_CODE	Set to ZONE_SUBPROC_FATAL while temporary extraction dataset
#  		exists.  Set to ZONE_SUBPROC_DETACHED on success.
#
# Returns the return value from "zfs receive".  May exit with fatal errors.
#
function extract_zfs {
	typeset -n zone=$1
	typeset stage1=$2
	typeset insrc=$3

	#
	# Receive the stream into a temporary dataset then move the datasets
	# into place.  Be careful while doing this that the recieved datasets
	# don't get mounted.
	#
	zfs create -o zoned=on "${zone.path.ds}/installtmp" || fatal "$f_no_ds"

	#
	# Be sure that an uninstall is forced if we are interrupted before
	# the install or attach completes.
	#
	EXIT_CODE=$ZONE_SUBPROC_FATAL

	typeset -a cmd
	set -A cmd zfs receive -F -u -x zoned "${zone.path.ds}/installtmp/ds"

	vlog "$stage1 $insrc | ${cmd[*]}"
	# $stage1 intentionally not quoted to support commands with arguments.
	$stage1 "$insrc" | "${cmd[@]}"
	typeset unpack_result=$?

	rationalize_datasets zone "${zone.path.ds}/installtmp/ds" || \
	    fatal "Invalid data received"

	#
	# If the destroy fails, this will trigger a failure in
	# zoneadm verify.
	#
	zfs destroy "${zone.path.ds}/installtmp" || \
	    fatal "Failed to destroy temporary dataset."

	[[ $unpack_result == 0 ]] && EXIT_CODE=$ZONE_SUBPROC_DETACHED

	return $unpack_result
}

#
# rationalize_datasets zone dsname
#
# dsname is the top-level dataset that should contain at least a BE and maybe
# other zone datasets in an unknown hierarchy.  rationalize_datasets looks
# through the dataset and data hierarchy found there to find a way to
# shoehorn it into the proper dataset layout.
#
# Upon successful conversion:
#
#    - $dsname will no longer exist
#    - zoned, mountpoint, canmount will have been set properly
#    - the active BE will have been determined and mounted.
#    - 0 is returned
#
# If the rationalization fails, 1 is returned and the zone and $dsname are in
# an undetermined state.
#
function rationalize_datasets {
	typeset -n zone=$1
	typeset topdsn=$2
	typeset dsn

	typeset rpooldsn ROOTdsn

	#
	# This forms the logic of guessing where things are at.
	#

	# Build an associative array of datasets in the source area.
	typeset -A dsa
	get_datasets -A "$topdsn" dsa || return 1

	#
	# Look for any of the following dataset layouts.  The layouts are
	# describe from the perspective of the source system.  In the table,
	# <rootpool> refers to the the root pool, typically "rpool", on the
	# source system.  <zpds> refers to the zone path dataset - the dataset
	# mounted at the zonepath.
	#
	# Source Layout      Dataset provided as arg to zfs send Check# Notes
	# ------ ----------- ----------------------------------- ------ -----
	# GZ     global      $rootpool                             2	1
	# GZ     global      $rootpool/ROOT                        3	1
	# GZ     global      $rootpool/ROOT/$be                    4	2
	# NGZ    s10         $zpds                                 5	2
	# NGZ    s11x2010.11 $zpds                                 1	1
	# NGZ    s11         $zpds/rpool                           2	1
	# NGZ    any !s10    $zpds/rpool/ROOT                      3	1
	#                    $zpds/ROOT					1
	# NGZ    any         $zpds/rpool/ROOT/$be                  4	2
	#                    $zpds/ROOT/$be				2
	#
	# The Layout column refers to the following dataset hierarchies:
	#
	#   GZ global:  Same across s10 and s11, expcept s11 adds
	#		rpool/export/home
	#	rpool
	#	rpool/ROOT
	#	rpool/ROOT/$be
	#	rpool/export
	#
	#   NGZ s10:
	#	$zpds	(A single BE exists in the "root" subdirectory.)
	#
	#   NGZ s11express:  Applies to Solaris 11 Express 2010.11.
	#	$zpds
	#	$zpds/ROOT
	#	$zpds/ROOT/$be
	#
	#   NGZ s11:
	#	$zpds
	#	$zpds/rpool
	#	$zpds/rpool/ROOT
	#	$zpds/rpool/ROOT/$be
	#	$zpds/rpool/export
	#	$zpds/rpool/export/home
	#
	#   any:
	#	Any of the above layouts are supported.
	#
	#   any !s10:
	#	Any of the above layouts except NGZ s10 are supported.
	#
	# Notes:
	#
	#   1.  The archive must be created with "zfs send -R", "zfs send -r",
	#	or "zfs send -rc".  Note that "zfs send -r" first appears in
	#	Solaris 11.
	#   2.  The archive may be created with any of the options specified
	#	in Note 1 (assuming support in that Solaris release) or
	#	without the -R or -r[c] options to zfs send.
	#

	# Check 1
	if [[ -n ${dsa[$topdsn/rpool]} && \
	    -n ${dsa[$topdsn/rpool/ROOT]} ]]; then
		rpooldsn=$topdsn/rpool
		ROOTdsn=$topdsn/rpool/ROOT
	# Check 2
	elif [[ -n ${dsa[$topdsn/ROOT]} ]]; then
		rpooldsn=$topdsn
		ROOTdsn=$topdsn/ROOT
	# Check for 3, 4, and 5 - We need to mount it to figure it out.
	else
		typeset dir=$(mktemp -d)
		# We know that it is zoned from the way it was received.
		zfs set canmount=noauto "$topdsn" && \
		    zfs set mountpoint=/ $topdsn ||
		    vlog "Unable to set properties for mounting %s" "$topdsn"
		if zfs_tmpmount "$topdsn" "$dir" >/dev/null 2>&1; then
			if [[ -d $dir/usr && -d $dir/var && -d $dir/etc ]]
			then
				# Looks like the inside of a BE (Check 4)
				rpooldsn=
				ROOTdsn=$(dirname "$topdsn")
			elif [[ ${zone.brand} == "solaris10" && \
			    -d $dir/root/usr && -d $dir/root/var && \
			    -d $dir/root/etc ]]; then
				# Looks like an S10 zonepath dataset (Check 5)
				rpooldsn=
				ROOTdsn=$(dirname "$topdsn")
				# Fix it to look like Check 4, above.
				convert_s10_zonepath_to_be "$dir" || {
					umount -f "$dir" >/dev/null 2>&1
					return 1
				}
			else
				# Must be a ROOT at $topdsn (Check 3)
				rpooldsn=
				ROOTdsn=$topdsn
			fi
			umount -f "$dir" >/dev/null 2>&1
		else
			# Must be a ROOT at $topdsn (Check 3)
			rpooldsn=
			ROOTdsn=$topdsn
		fi
		rmdir "$dir" >/dev/null 2>&1
	fi

	# Create rpool and rpool/ROOT if it doesen't already exist.
	create_zone_rpool -e zone || return 1

	if [[ -n $rpooldsn ]]; then
		#
		# Now look for datasets that collide
		#
		typeset -a collide_ds
		typeset -a move_ds
		typeset -A seen_ds
		/usr/sbin/zfs list -H -o name -t filesystem,volume -r \
		    "$rpooldsn" | while read dsn; do
			[[ $dsn == "$rpooldsn" ]] && continue
			[[ $dsn == "$rpooldsn"/ROOT ]] && continue

			# dataset name relative to rpooldsn
			typeset rdsn=${dsn#$rpooldsn/}
	
			if /usr/sbin/zfs list "${zone.rpool_ds}/$rdsn" \
			    >/dev/null 2>&1; then
				#
				# keep track of collisions that can be deleted
				# for possible removal in reverse order
				#
				if ds_empty "${zone.rpool_ds}/$rdsn"; then
					a_push collide_ds "$rdsn"
					continue
				fi
				log "$e_ds_conflict" "$rdsn" \
				    "${zone.rpool_ds}/$rdsn"
				(( collide++ ))
				continue
			fi

			#
			# ZBEs will be handled below, as they need to be
			# tagged all at once after any colliding ZBEs are
			# renamed.
			#
			[[ $dsn == "$rpooldsn"/ROOT/* ]] && continue

			#
			# If the parent of this dataset has already been added
			# to the move list (or not added because its parent was
			# added), it will be moved with its parent. Don't try
			# to move it after it is already gone.
			#
			seen_ds[$rdsn]=1
			[[ -n ${seen_ds[$(dirname "$rdsn")]} ]] && continue

			a_push move_ds "$rdsn"
		done
	
		if (( collide != 0 )); then
			return 1
		fi

		for dsn in "${collide_ds[@]}"; do
			vlog "Removing empty dataset '%s' due to collision" \
			    "$dsn"
			zfs destroy "$dsn"
		done

		for dsn in "${move_ds[@]}"; do
			vlog "Dataset '%s' received from archive" \
			    "${zone.rpool_ds}/$dsn"
			zfs rename "$rpooldsn/$dsn" "${zone.rpool_ds}/$dsn" ||
			    return 1
		done
	fi

	#
	# The zone's rpool dataset (if any) has been migrated, except for the
	# be(s) found in $ROOTdsn.  Merge them in with any existing zbes.
	#
	typeset -a bes
	typeset be newbe
	typeset -A zone.allowed_bes	# used by discover_active_be
	typeset -A allowed_bes
	typeset -A bemap
	typeset -i i
	typeset be_prefix
	if [[ ${zone.brand} == "solaris10" ]]; then
		be_prefix=zbe
	else
		be_prefix=solaris
	fi
	tag_candidate_zbes "$ROOTdsn" bes allowed_bes || return 1
	for be in "${bes[@]}"; do
		# Use the next available BE name
		for (( i = 0 ; i < 100 ; i++ )); do
			newbe=$be_prefix-$i

			#
			# Try to claim this BE.  If it fails, it probably means
			# that there is already a BE by that name and we should
			# try to claim the next one.
			#
			if /usr/sbin/zfs rename "$ROOTdsn/$be" \
			    "${zone.ROOT_ds}/$newbe" >/dev/null 2>&1; then
				break
			fi
			newbe=
		done
		if [[ -z $newbe ]]; then
			error "$e_be_move_failed" "$be"
			return 1
		fi

		[[ -n ${allowed_bes[$be]} ]] && zone.allowed_bes[$newbe]=1
	done
	#
	# The only datasets that may still exist under $topdsn are $rpooldsn
	# and $ROOTdsn.  Verify that.  If it all checks out, destroy $topdsn
	# and all of its children and call it a day.
	#
	typeset -i errors=0 delete_topdsn=0
	/usr/sbin/zfs list -H -o name -t filesystem,volume "$topdsn" \
	    2>/dev/null | while read dsn; do
		# Assuming there are no errors, $topdsn should be deleted.
		(( delete_topdsn=1 ))
		[[ $dsn == "$topdsn" ]] && continue
		[[ $dsn == "$rpooldsn" ]] && continue
		[[ $dsn == "$ROOTdsn" ]] && continue
		log "$e_unexpected_ds" "${dsn#$topdsn/}"
		(( errors++ ))
	done
	(( errors == 0 )) || return 1

	(( delete_topdsn )) && zfs destroy -r "$topdsn"

	fix_zone_rpool_props zone || return 1

	#
	# If the zone doesn't have /export, set up the /export dataset
	# hierarchy.  Since this isn't strictly necessary for the zone
	# to work, do not fail the attach if creation fails.
	#
	if ! zonecfg_has_export zone && ! /usr/sbin/zfs list -H -o name \
	    "${zone.rpool_ds/export}" >/dev/null 2>&1; then
		vlog "Creating /export"
		if ! zfs create -o mountpoint=/export "${zone.rpool_ds}/export"
		then
			log "$f_zfs_create" "${zone.rpool_ds}/export"
		else
			zfs create "${zone.rpool_ds}/export/home" ||
			    log "$f_zfs_create" "${zone.rpool_ds}/export/home"
		fi
	fi

	discover_active_be zone || return 1
	mount_active_be -C zone || return 1

	return 0
}

#
# ds_empty <datasetname>
#
# Returns 0 if the dataset has no snapshots and there are no files or
# directories in the dataset.  Assumes the dataset is a filesystem.
#
function ds_empty {
	typeset dsn="$1"

	# If any snapshots or descendant datasets exist, it's not empty.
	typeset -i children
	children=$(/usr/sbin/zfs list -Hr -d 1 -t snapshot -o name "$dsn" | \
	    awk 'END {print NR}')
	if (( children > 1 )); then
		vlog "Dataset %s has %d snapshots" "$dsn" $(( children - 1 ))
		return 1
	fi

	#
	# If it's already mounted, look inside it.  Be careful not to descend
	# into datasets mounted on it to avoid false positives.  Note that we
	# ignore mount points for already mounted datasets.  This is important
	# for the case where an empty BE that contains a separate /var is
	# mounted.
	#
	if [[ "$(zfs list -H -o mounted "$dsn")" == yes ]]; then
		typeset mntpt
		mntpt=$(zfs list -H -o mountpoint "$dsn") || return 1

		# Only look at the first line or two to see if it is empty.
		# If it contains only directories, those are likely
		# mountpoints or ancestors of mountpoints.
		find "$mntpt" -mount -type d -o -print | awk 'NR > 1 { exit 1 }
		    END { if (NR == 0) { exit 0 } else { exit 1}}'
		if (( $? != 0 )); then
			vlog "Dataset %s mounted at %s is not empty" "$dsn" \
			    "$mntpt"
			return 1
		fi
		return 0
	fi


	# Mount it to see if there are any files in it
	typeset dir=$(mktemp -d)
	zfs_tmpmount "$dsn" "$dir" || {
		vlog "Unable to mount dataset %s on %s" "$dsn" "$dir"
		rmdir "$dir" || \
		    vlog "Unable to clean up temporary directory at %s" "$dir"
		return 1
	}

	typeset contents
	contents=$(ls -A "$dir")
	umount "$dir" && rmdir $dir || \
	    vlog "Unable to clean up temporary mount of %s at %s" "$dsn" "$dir"
	[[ -n "$contents" ]] && return 1

	return 0
}

#
# fix_zone_rpool_props zone
#
# Troll through the zone's rpool dataset and fix the properties on datasets
# such that the BE's are likely to mount.
#
function fix_zone_rpool_props {
	typeset -n zone=$1

	vlog "Fixing properties on zone datasets"

	zfs set zoned=on "${zone.rpool_ds}"
	typeset dsn
	zfs list -H -o name -d 1 -t filesystem,volume "${zone.rpool_ds}" | \
	    while read dsn; do
		[[ $dsn == "${zone.rpool_ds}" ]] && continue
		zfs inherit -r zoned $dsn || return 1
	done

	typeset be_dsn=
	zfs list -H -o name -t filesystem "${zone.ROOT_ds}" | \
	    while read dsn; do
		[[ $dsn == "${zone.ROOT_ds}" ]] && continue
		if [[ $dsn != "${zone.ROOT_ds}"/*/* ]]; then
			be_dsn=$dsn
		else
			# Fix mountpoint .../zbe-0/var -> /var
			zfs set mountpoint=${dsn#$be_dsn} "$dsn" || return 1
		fi
		zfs set canmount=noauto "$dsn" || return 1
	done
}

#
# attach_datasets -m install_media -t install_type zone
#
# Attaches datasets then performs any required installation tasks.
#
# Options and arguments
#
# -m install_media	If install_media is '-', attempt to find a ZBE to
#			attach.  The selection is performed by the brand's
#			discover_active_be() function.
# -t install_type	Can be any value accepted by install_image.
# zone			zone structure initialized by init_zone.
#
# Globals
#   EXIT_CODE		Depending on the level of success, may be set to
#			ZONE_SUBPROC_DETACHED (returns 0), ZONE_SUBPROC_FATAL
#			(returns 1, datasets partially extracted)
#
# Return values
#
#   0 on success, else exits.
#   Exits with failure if:
#    - zonepath is in the global zone's ROOT dataset.
#    - active BE could not be found
#    - the ZFS properties on the active BE could not be set
#    - the active BE could not be mounted on the zoneroot
#
function attach_datasets {
	typeset opt
	typeset install_media= inst_type=
	while getopts :m:t: opt; do
		case $opt in
			m) install_media=$OPTARG ;;
			t) inst_type=$OPTARG ;;
			?) fail_internal "$f_int_bad_opt" "$OPTARG" ;;
		esac
	done
	[[ -z "$install_media" ]] && fail_internal "$f_int_missing_opt" m
	[[ -z "$inst_type" ]] && fail_internal "$f_int_missing_opt" t
	shift $(( OPTIND - 1 ))
	case $# in
		0) fail_internal "$f_int_missing_arg" "zone" ;;
		1) : ;;
		*) fail_internal "$f_int_bad_arg" "$*" ;;
	esac
	typeset -n zone=$1

	# Validate that the zonepath is not in the root dataset.
	fail_zonepath_in_rootds "${zone.path.ds}"

	#
	# Fix mountpoint and other properties for ZBEs detached using the
	# old scheme.
	#
	if ! convert_old_detached_zbes zone; then
		# So long as any failed conversions didn't leave anything
		# mounted on the zone root, allow the attach to continue.
		get_ds_from_path "${zone.root}" && fatal "$f_detach_convert"
	fi

	if [[ "$install_media" == "-" ]]; then
		discover_active_be zone || return 1
	elif [[ $inst_type == zbe ]]; then
		claim_zbe zone "$install_media" || return 1
		inst_type=directory
		install_media=-
	fi

	#
	# The zone's datasets are now in place.
	#
	log "$m_attaching"
	# Sets EXIT_CODE.
	install_image zone "$inst_type" "$install_media"

	return 0
}

#
# claim_zbe zone
#
# This function exists here only to give a clear error message in the event
# that attach_datasets() calls brand-specific functionality not appropriate
# to this brand.  Brands that support claim_zbe() must define it.
#
# claim_zbe will be called if zoneadm is invoked as
#
#   zoneadm -z <zone> attach -z <zbe>
#
# As such, any brand that doesn't support -z should have bailed before calling
# attach_datasets.
#
function claim_zbe {
	# If we make it to here, it is programmer error.
	fail_internel "%s not defined from this brand" "$0"
}

#
# convert_s10_zonepath_to_zbe dir
#
# This function exists here only to give a clear error message in the event
# that attach_datasets() calls brand-specific functionality not appropriate
# to this brand.
#
function convert_s10_zonepath_to_be {
	# Anyone that has called this from common code should have already
	# checked the brand.
	fail_internal "$s10_zbe_not_supported"
}

#
# tag_candidate_zbes ROOTdsn [be_array_name [curgz_assoc_array_name]]
#
# This generic function only returns the list of zbes found in the specified
# dataset.  A brand-specific function may exist for brands that have more
# sophisticated zbe management needs.
#
#   ROOTdsn             The name of a dataset that contains zbes.
#   be_array_name       If specified, this variable will contain an array
#                       of candidate zbes on return.
#   curgz_assoc_array_name Only used by some brands, not implemented in this
#			implementation.  Intended to return the list zbes
#			associated with the current global zone in an
#			associative array.
#
# Returns 0 if all went well and at least one zbe exists, else 1.
#
function tag_candidate_zbes {
	(( $# < 2 )) && return 0

	typeset ROOTdsn=$1
	typeset -n bes=$2

	typeset dsn
	/usr/sbin/zfs list -H -o name -r -d 1 -t filesystem "$ROOTdsn" \
	    2>/dev/null | while read dsn; do
		[[ $dsn == "$ROOTdsn" ]] && continue
		a_push bes "$(basename "$dsn")"
	done
	if (( ${#bes[@]} == 0 )); then
		error "$e_no_active_be"
	fi
	return 0
}

#
# convert_old_detached_zbes zone
#
# Earlier releases left detached datasets mounted on the zone root.  This
# function cleans those up, if needed.
#
# Arguments:
#
#  zone		zone structure initialized by init_zone.
#
# Return:
#
#  0	Nothing unexpected happened.  There is no longer anything mounted on
#	the zone root
#  1	One or more ZBEs could not be converted.
#
function convert_old_detached_zbes {
	typeset -n zone=$1
	typeset retval=0
	typeset first=true	# Is this the first ZBE converted?

	#
	# Look at each ZBE.  Ignore zfs list result, as it is OK to call this
	# on a zone that has no ZBEs
	#
	/usr/sbin/zfs list -H -o name,mountpoint -r -d 1 "${zone.ROOT_ds}" \
	    2>/dev/null | while IFS=$'\t' read dsn zbe_mntpt; do

		# Skip the ROOT dataset
		[[ $dsn == "${zone.ROOT_ds}" ]] && continue;

		#
		# If the ZBE's mount point is already set to /, this doesn't
		# look like a detached zbe.  Because the currently configured
		# zone root may be different than the zone root on some other
		# host where this storage may have previously been presented,
		# all the references to the zone root are based on the mount
		# point of the BE's top level dataset rather than the currently
		# configured zone root.
		#
		[[ $zbe_mntpt == / ]] && continue

		log "$m_convert_detached" "$(basename "$dsn")"

		#
		# Before doing anything that causes unmounts, get a list of
		# datasets that exist under the ZBE's top dataset, as well as
		# their properties.  This will be used when fixing up
		# properties later.
		#
		typeset -a dsslist	# indexed array of datasets in dsn
		typeset -A dsnbydir	# associative array indexed by mntpt
		get_datasets -p "$dsn" dsslist || fatal "$f_no_active_ds"
		typeset -i i errors=0
		for (( i = 0; i < ${#dsslist[@]}; i++ )); do
			typeset -n dss=dsslist[$i]	# ref to current item

			# Ignore things that don't get mounted
			[[ ${dss.props[type].value} == filesystem ]] || \
			    continue

			# figure out where it is mounted
			mountpt=${dss.props[mountpoint].value}

			# Legacy mountpoints do not need to be fixed.
			[[ $mountpt == legacy ]] && continue

			# Make mountpoint relative to BE root
			if [[ $mountpt == "$zbe_mntpt" ]]; then
				mountpt=/
			elif [[ $mountpt == ${zbe_mntpt}/* ]]; then
				mountpt=${mountpt#${zbe_mntpt}}
			fi
			if [[ -n ${dsnbydir[$mountpt]} ]]; then
				error "$e_ds_mnt_multiply_defined" "$mountpt"
				(( errors++ ))
				mountpt=
			fi
			if [[ -n $mountpt ]]; then
				dsnbydir[$mountpt]=$i
			fi
		done

		#
		# Allow progression through all ZBEs, converting those that
		# can be converted.
		#
		if (( errors != 0 )); then
			retval=1
			continue
		fi

		if $first; then
			first=false
			# Set up proper attributes on the ROOT dataset.
			typeset rootds rpoolds
			init_dataset rpoolds "${zone.rpool_ds}"
			init_dataset rootds "${zone.ROOT_ds}"

			#
			# Unmount the BE so that we can fix up mounts.  Note
			# that if file systems are mounted with temporary mount
			# points, the persistent mountpoint property is hidden.
			#
			unmount_be zone || return 1

			if ! zfs_set zoned=on rpoolds ||
			    ! zfs_set canmount=noauto rootds ||
			    ! zfs_set mountpoint=legacy rootds; then
				# If datasets above ZBEs can't be fixed,
				# return immediately.  zfs_set has already
				# given an error message.
				return 1
			fi
		fi

		#
		# Walk through any remaining datasets and fix mount points
		#
		typeset -a mntlist
		get_sorted_subscripts dsnbydir mntlist
		for dir in "${mntlist[@]}"; do
			typeset -n ds=dsslist[${dsnbydir[$dir]}]
			refresh_dataset ds
			if ! fix_ds_mountpoint ds "$dir"; then
				retval=1
			fi
		done
	done

	return $retval
}

#
# get_datasets [-A] [-t type] [-p] dataset array_name
#
# Updates indexed array (or associative array with -A) named array_name with
# the names of datasets found under the given dataset, including the given
# dataset.  Use of an array generated with this function is preferable to "for
# ds in $(zfs list -r ...)" because this is tolerant of dataset names that
# contain spaces.
#
# Example:
#
#    typeset -a array
#    get_datasets [options] $dataset array
#    for ds in "${array[@]}"; do
#	...
#    done
#
# Returns 0 on success or 1 if dataset was not found.
# Note: No error messages are printed if no dataset is found.
#
function get_datasets {
	#
	# Option and argument processing
	#
	typeset opt var dstype=filesystem assoc
	typeset -i getprops=0
	while getopts :Apt: opt; do
		case $opt in
		A)	assoc=1 ;;
		p)	getprops=1 ;;
		t)	dstype=$OPTARG ;;
		?)	fail_internal "$f_int_bad_opt" "$OPTARG" ;;
		esac
	done
	shift $(( $OPTIND - 1 ))
	[[ -z "$1" ]] && fail_internal "$f_int_missing_arg" dataset
	[[ -z "$2" ]] && fail_internal "$f_int_missing_arg" array

	typeset dataset="$1"
	typeset -n array="$2"
	unset array
	[[ -n $assoc ]] && typeset -A array

	#
	# Build the list of datasets
	#
	typeset ds
	typeset -i index=0
	/usr/sbin/zfs list -H -o name -t $dstype -r "$dataset" 2>/dev/null \
	    | while read ds; do
		if (( getprops )); then
			if [[ -n $assoc ]]; then
				array[$ds]=
				init_dataset "array[$ds]" "$ds"
			else
				array[$index]=
				init_dataset "array[$index]" "$ds"
			fi
		else
			if [[ -n $assoc ]]; then
				array[$ds]=$ds
			else
				array[$index]="$ds"
			fi
		fi
		(( index++ ))
	done

	if (( index == 0 )); then
		return 1
	fi
	return 0
}

#
# snapshot_zone_rpool zone snapformat snapname
#
# Creates a recursive snapshot of the specified zone.
#
# Arguments
#
#   zone	A zone, initialized with init_zone.
#   snapformat	A printf-friendly string that includes %d in it.
#   snapname	Upon return, this variable will contain the name of the
#		snapshot.  This should be the name of the variable, without
#		a $.
#
# Globals:
#   PATH	Must contain /sbin or /usr/sbin.
#
# Return
#   0		Success, $snapname can be trusted
#   1		Fail, $snapname may have garbage
#   exit	If an internal error occurs
#
function snapshot_zone_rpool {
	#
	# Option/Argument processing
	#
	[[ -z "$1" ]] && fail_internal "$f_int_missing_arg" zone
	[[ -z "$2" ]] && fail_internal "$f_int_missing_arg" snapformat
	[[ -z "$3" ]] && fail_internal "$f_int_missing_arg" snapname
	typeset -n zone="$1"
	typeset snap_fmt="$2"
	typeset -n snapname=$3

	#
	# Find a name that works for the snapshot
	#
	typeset rpool_ds=${zone.rpool_ds}
	typeset -i i
	for (( i=0; i < 100; i++ )); do
		snapname=$(printf -- "$snap_fmt" $i)
		zfs snapshot -r "$rpool_ds@$snapname" >/dev/null 2>&1 \
		    && return 0
	done

	# No name found, fail
	return 1
}

#
# clone_zone_rpool srczone dstzone snapname
#
# Clones the active BE dataset and other non-BE datasets from one zone to
# another.  If srczone and dstzone are the same zone, the only effect is that
# a new boot environment is created.
#
# Upon successful return, the specified snapshot will have been marked for
# deffered destruction with 'zfs destroy -d'
#
# Options and Arguments
#
#   srczone	A zone structure, initialized with init_zone.
#   dstzone	A zone structure, initialized with init_zone.
#   snapname	The name of the snapshot (part after @) from
#		snapshot_zone_rpool.
#
# Globals:
#
#   EXIT_CODE	Set to $ZONE_SUBPROC_FATAL if one or more datasets have been
#   		created.
#
# Return
#   0		Success
#   1		Fail
#
function clone_zone_rpool {
	typeset -n s="$1" d="$2"
	typeset snapname="$3"

	typeset -a dslist props
	typeset -i propcnt=0
	typeset dsname newdsname snap
	typeset dss newdss		# dataset structure
	typeset -i clone_made clone_reqd
	typeset -a sl_opt

	#
	# When cloning a BE within a zone, s and d will refer to the same
	# zone.  create_active_ds will adjust d.active_ds, which is the
	# same as s.active_ds.  To be sure that cloning of the source BE's
	# child datasets happen, we need to remember what the initial active
	# BE was.
	#
	typeset src_active_ds=${s.active_ds}

	#
	# In order to see the persistent value of mountpoints, datasets
	# must be unmounted.
	#
	# Note that this causes problems for cloning from snapshots, which is
	# still awaiting implementation for non-native brands.
	#
	unmount_be s || return 1
	get_datasets -t filesystem,volume "${s.rpool_ds}" dslist || return 1

	if is_system_labeled; then
		# On TX, reset the mlslabel upon cloning
		set -A sl_opt -- -o mlslabel=none
	fi

	for dsname in "${dslist[@]}"; do
		init_dataset dss "$dsname"
		newdsname=${dss.name/${s.path.ds}/${d.path.ds}}
		snap="${dss.name}@$snapname"
		clone_made=0
		clone_reqd=0

		# zvols are not supported inside of a boot environment
		if [[ ${dss.name} == "${s.ROOT_ds}/"* ]]; then
			typeset dstype
			#
			# The following zfs call should only fail if some
			# is removing or renaming datasets while this is
			# running.  If someone is doing that, abort the clone
			# operation because it's likely that something will
			# break.
			#
			dstype=$(zfs get -H -o value type "${dss.name}") || \
			    return 1
			if [[ $dstype == volume ]]; then
				error "$e_volume_in_bootenv" "${dss.name}"
				return 1
			fi
		fi

		#
		# Filter through the datasets to throw away snapshots that
		# will not be cloned.  Set other flags that will be needed
		# for post-processing.
		#
		case "${dss.name}" in
		$src_active_ds)	# Clone the active boot env
			typeset -i i
			# The BE name need not be the same in src and dst.
			# Find the first available BE name by cloning.
			# Sets EXIT_CODE.
			create_active_ds -s "$snap" d || return 1
			(( clone_made=1 ))
			newdsname=${dss.name/${src_active_ds}/${d.active_ds}}
			;;
		$src_active_ds/*)	# Clone the active boot env nested ds
			# Rejigger the name to match the BE name picked above
			newdsname=${dss.name/${src_active_ds}/${d.active_ds}}
			(( clone_reqd=1 ))
			;;
		${s.ROOT_ds}/*)		# Do not clone inactive BE
			# If we are just creating a new BE in an existing zone,
			# don't worry about this dataset.
			[[ ${s.name} == ${d.name} ]] && continue
			vlog "Not cloning %s: not part of source active BE" \
			    "$snap"
			continue
			;;
		*) 			# Clone everything else, if needed.
			# If we are just creating a new BE in an existing zone,
			# don't worry about this dataset.
			[[ ${s.name} == ${d.name} ]] && continue
			#
			# It is possible that the destination zonepath already
			# exists and is at least partially populated due to the
			# same zone in some other boot environment.  If non-BE
			# datasets already exist, reuse them.
			#
			if /usr/sbin/zfs list "$newdsname" >/dev/null 2>&1; then
				vlog "Not cloning %s: dataset already exists" \
				    "$newdsname"
				continue
			fi
			;;
		esac

		if (( clone_made == 0 )); then
			zfs list "$newdsname" >/dev/null 2>&1
			if (( $? == 0 && clone_reqd )); then
				error "$e_dataset_exists" "$newdsname"
				return 1
			fi
			vlog "Cloning $snap to $newdsname"
			zfs clone "${sl_opt[@]}" "$snap" "$newdsname" || return 1
			EXIT_CODE=$ZONE_SUBPROC_FATAL
			(( clone_made=1 ))
		fi

		#
		# Force the zone's rpool to be zoned and everything else
		# to inherit the zoned property.
		#
		init_dataset newdss "$newdsname"
		if [[ $newdsname == "${d.rpool_ds}" ]]; then
			zfs_set zoned=on newdss || return 1
		else
			zfs inherit zoned "$newdsname" || return 1
		fi
		#
		# Locally set properties to match those found on the source.
		#
		typeset prop
		zoned_src=${newdss.props[zoned].source}
		for prop in mountpoint canmount; do
			if [[ "${dss.props[$prop].source}" == \
			    @(local|received) ]]; then
				zfs_set $prop="${dss.props[$prop].value}" \
				    newdss || return 1
			fi
		done
	done

	# Remount the source zone.  Just complain if it can't be remounted
	# as the next boot, clone, etc. will succeed even if it's not mounted.
	mount_active_be -c s || log "$e_mount1_failed" "${s.name}"

	# Mount the new zone
	[[ -d ${d.root} ]] || mkdir -m 755 "${d.root}"
	mount_active_be -c d || return 1

	# Perform a deferred destruction of snapshots.  Any snapshot that
	# wasn't cloned will be immediately destroyed.
	zfs destroy -rd "${s.rpool_ds}@$snapname" || return 1

	return 0
}

#
# initializes a new dataset structure
#
# Example:
#	typeset dss
#	init_dataset dss rpool
#	print "${dss.name} mounted at ${dss.props[mountpoint].value}"
#
# After calling init_dataset, dss looks like
#
#	dss.name=rpool
#	dss.props[mountpoint].value=/rpool
#	dss.props[mountpoint].source=local
#	...
#
# Returns 0 if one or more properties were found on the dataset, else 1.
#
function init_dataset {
	typeset -n dss="$1"
	dss="$2"
	dss.name="$2"
	dss.props=
	typeset -A dss.props

	refresh_dataset dss
	return $?
}

function refresh_dataset {
	typeset -n dss="$1"
	typeset -r tab="$(printf "\t")"
	typeset prop src val
	typeset -i rv=1

	/usr/sbin/zfs get -Hp -o property,source,value all "${dss.name}" \
	    | while IFS=$tab read prop src val; do
		dss.props[$prop].value="$val"
		dss.props[$prop].source="$src"
		(( rv=0 ))
	done
	(( rv == 0 )) || error "refresh of ${dss.name} failed"
	return $rv
}

#
# init_zfs_fs varname [path]
#
# Allocate a new zfs_fs structure
#
function init_zfs_fs {
	typeset -n ref=$1
	ref=
	ref.ds=

	# When this variable is set to a value, cache the dataset
	function ref.set {
		get_ds_from_path "${.sh.value}" ${.sh.name}.ds
	}
	[[ -n "$2" ]] && ref="$2"
}

#
# init_zone zone zonename [zonepath]
#
# Initialize a zone structure with the following useful members.
#
#  brand	The zone's brand.
#  path		The zonepath.  See -p option below.
#  path.ds	The zonepath dataset name.  Automatically updated when zonepath
#		is updated if a dataset is mounted on the zonepath.  This
#		member should not be updated directly.
#  root		Read-only.  The zoneroot.  Automatically derived from zonepath.
#  rpool_ds	Read-only.  The name of the dataset that contains the zone
#		rpool.  Automatically derived from path.ds
#  ROOT_ds	Read-only.  The name of the dataset that contains boot
#		environments.  Automatically derived from path.ds
#  new_be_datasets List of datasets that will be created when a new empty
#		boot environment is created.  For example, if each BE should
#		get a separate /var, this list will contain one element: var.
#
# Other members are commonly initialized as needed by other functions.  For
# example,
#
#  active_ds	The name of the dataset that should be mounted on the zone
#		root.  This is updated by brand-specific get_active_be() and
#		set_active_be() functions.
#  allowed_bes	During attach, this associative array may be initialized
#		to signal set_active_be() that it can only choose from these
#		boot environments when deciding on which one to make active.
#
# Options and arguments:
#
#  zone		The name of the variable that will contain the structure.
#  zonename	The name of the zone.
#  zonepath	The zonepath.  If this option is not provided, the value for
#		zonepath will be looked up in the zone configuration, if it
#		exists.
#
function init_zone {
	#
	# Argument and option processing
	#
	typeset opt

	[[ -z "$1" ]] && fail_internal "$f_int_missing_arg" zone
	[[ -z "$2" ]] && fail_internal "$f_int_missing_arg" zonename
	typeset -n ref=$1
	ref=$2
	ref.name=$2
	shift 2

	ref.path=
	init_zfs_fs ref.path

	# Called after init_zfs_fs to make use of discipline function.
	[[ -n $1 ]] && ref.path=$1

	#
	# Set up remaining members
	#
	if [[ -z "${ref.path}" ]]; then
		set -- $(zonecfg -z "$ref" info zonepath 2>/dev/null)
		ref.path=$2
	fi
	set -- $(zonecfg -z "$ref" info brand 2>/dev/null)
	ref.brand=$2

	# root is always zonepath/root
	typeset -r ref.root=
	function ref.root.get {
		typeset -n pathref=${.sh.name%.root}.path
		.sh.value="$pathref/root"
	}

	# rpool dataset is always zonepath_ds/rpool
	typeset -r ref.rpool_ds=
	function ref.rpool_ds.get {
		typeset -n pathdsref=${.sh.name%.rpool_ds}.path.ds
		if [[ -z "$pathdsref" ]]; then
			.sh.value=
		else
			.sh.value="$pathdsref/rpool"
		fi
	}

	# ROOT dataset is always zonepath_ds/rpool/ROOT
	typeset -r ref.ROOT_ds=
	function ref.ROOT_ds.get {
		typeset -n pathdsref=${.sh.name%.ROOT_ds}.path.ds
		if [[ -z "$pathdsref" ]]; then
			.sh.value=
		else
			.sh.value="$pathdsref/rpool/ROOT"
		fi
	}

	# If a new empty BE is created, which datasets should be in it?
	# This list may be overridden.
	set -A ref.new_be_datasets var
}

#
# bind_legacy_zone_globals zone
#
# Generates the commands to bind legacy globals to a specific zone's members.
# Output should be passed to eval.
#
# Example:
#
#   typeset zone=
#   init_zone zone z1
#   eval $(bind_legacy_zone_globals zone)
#
function bind_legacy_zone_globals {
	[[ -z "$1" ]] && fail_internal "$f_int_missing_arg" zone
	cat <<-EOF
	typeset -n ZONENAME="$1.name";
	typeset -n ZONEPATH="$1.path";
	typeset -n ZONEPATH_DS="$1.path.ds";
	typeset -n ZONEROOT="$1.root";
	typeset -n ACTIVE_DS="$1.active_ds";
	EOF
}

#
# a_push array_name item ...
#
# Push item(s) onto an index array
#
function a_push {
	typeset -n array=$1
	typeset -i len=${#array[@]}
	shift;
	typeset item
	for item in "$@"; do
		array[len++]="$item"
	done
}

#
# get_sorted_subscripts associative_array_name indexed_array_name
#
# The specification for ksh93 is silent about the order of ${!array[@]}.
# This function provides a guaranteed way to get the subscripts of an
# associative array in order.
#
# Example:
#	typeset -A a_array
#	typeset -a i_array
#	a_array[foo/bar]=stuff
#	a_array[foo]=otherstuff
#	get_sorted_subscripts a_array i_array
#	for subscript in "${i_array[@]}"; do
#		print "a_array[$subscript] = ${a_array[$subscript]}"
#	done
#
function get_sorted_subscripts {
	typeset -n a="$1" i="$2"

	set -s -- "${!a[@]}"
	set -A i "$@"
}

#
# zfs_set property=value dss
#
# Sets the property, or generates a clear message that it can't.
#
# Arguments:
#  property=value	Passed directly to "zfs set".  dss.props[property].* is
#			updated.
#  dss			The name of a dataset structure, initialized with
#			init_dataset.
#
# Example:
#	typeset dss
#	init_dataset dss "zones/z1/rpool"
#	zfs_set zoned=on dss
#
# Returns 0 on succes, else 1
#
function zfs_set {
	[[ -z "$1" ]] && fail_internal "$f_int_missing_arg" "prop=value"
	[[ -z "$2" ]] && fail_internal "$f_int_missing_arg" "dataset"
	typeset propval="$1"
	typeset -n dss="$2"		# dataset structure

	[[ -z "${dss.name}" ]] && fail_internal "uninitialized ds"

	vlog "  setting ZFS property %s on %s" "$propval" "${dss.name}"
	/usr/sbin/zfs set "$propval" "${dss.name}" || {
		error "$e_zfs_set" "$propval" "${dss.name}"
		return 1
	}

	#
	# Update the property on the dataset.  Note that setting some
	# properties (e.g. zoned) may cause others to change (e.g. mounted), so
	# this is imperfect.  It is best to use update_dataset when you really
	# care about getting an accurate snapshot of the properties.
	# update_dataset is not called here to avoid a lot of overhead when
	# the caller has many properties to set.
	#
	dss.props[${propval%%=}].value="${propval#=}"
	dss.props[${propval%%=}].source=local
	return 0
}

#
# zfs [zfs(1M) args]
#
# On its own, zfs(1M) only tells you that something failed, it doesn't tell
# you anything about the options and arguments that were passed to it.  This
# serves as a wrapper around zfs(1M) to be more verbose about what zfs(1M)
# failed to do.
#
# To avoid unnecessary error messages for the times that zfs failures are
# expected (e.g. part of a test condition for existence of a dataset), use
# "/usr/sbin/zfs" instead of "zfs".
#
function zfs {
	/usr/sbin/zfs "$@"
	typeset -i rv=$?
	(( rv == 0 )) || error "$e_cmd_failed" "zfs $*" $rv
	return $rv
}

#
# fix_ds_mountpoint dataset mountpoint
#
# Updates the dataset's mountpoint, zoned, and canmount properties so that the
# dataset is mountable in a zone.  If the values in the dataset structure
# indicate that no changes are needed, no changes are made.  If changes are
# made the dataset structure is refreshed to match the current state of the
# dataset according to zfs(1M).
#
# The dataset must not be mounted when this function is called.
#
# Arguments:
#   dataset	The name of a dataset structure, initilized with init_dataset.
#   mountpoint	The new value for the mountpoint property.  For zoned datasets
#		this should be relative to the zone root.
#
# Returns 0 on succes, else 1.
#
function fix_ds_mountpoint {
	case $# in
		0|1) fail_internal "$f_int_missing_arg" "dataset or dir" ;;
		2) : ;;
		*) fail_internal "$f_int_bad_arg" "$*" ;;
	esac
	typeset -n dss="$1"
	typeset mountpoint="$2"
	typeset -i dirty=0

	#
	# If nothing needs to be fixed, don't fix it.
	#
	if [[ "${dss.props[mountpoint].value}" == "$mountpoint" && \
	    "${dss.props[zoned].value}" == on && \
	    "${dss.props[zoned].source}" == inherited* && \
	    "${dss.props[canmount].value}" == noauto ]]; then
		#
		# Currently we can only verify mountpoints if a dataset is not
		# mounted.  The lack of ability to get the persistent value of
		# mountpoint from zfs(1M) is a bit of a problem:
		#
		#  - If it is mounted with source of "temporary" we can't get
		#    to the persistent value to be sure that it will be mounted
		#    at the right place next time.
		#  - If the mounted property has a source of "-" and it is
		#    zoned, that means one of two things:
		#
		#      i) The zone virtual platform must be up and it is mounted
		#         relative to the zone root.  The mountpoint property
		#	  that is seen from the global zone includes the
		#	  zone root.  This function doesn't expect to be called
		#	  to fix a mountpoint in a running zone.
		#     ii) It is not mounted and the mountpoint can be trusted.
		#
		[[ "${dss.props[mounted].value}" == no ]] && return 0

		#
		# It is mounted, making it impossible or unwise to muck with
		# the mount point.
		#
		error "$e_no_mntpt_change_for_mounted" "${dss.name}"
		return 1
	fi

	#
	# We can't fix the mountpoint on a mounted dataset without causing
	# an unmount.
	#
	if [[ "${dss.props[mountpoint].value}" == mounted ]]; then
		error "$e_no_mntpt_change_for_mounted" "${dss.name}"
		return 1
	fi

	vlog "$m_fix_ds_mountpoint" "${dss.name}" \
	    "${dss.props[mountpoint].value}" "$mountpoint"

	# Fix the zoned property if it is not inherited.
	if [[ "${dss.props[zoned].source}" != inherited* ]]; then
		if [[ ${dss.props[zoned].value} != on ]]; then
			vlog "Inheriting property zoned on ${dss.name}"
			zfs inherit zoned "${dss.name}" || return 1
		fi
		(( dirty=1 ))
	fi

	#
	# Verify that the value is now zoned.  If the parent dataset wasn't
	# zoned then this dataset is not zoned and a basic assumption of the
	# zone dataset structure is broken.  Note that we aren't using the
	# cached value in $dss because the zoned property may have changed
	# above.
	#
	typeset zonedval
	zonedval=$(zfs get -H -o value zoned "${dss.name}") || return 1
	if [[ $zonedval != on ]]; then
		error "$e_parent_not_zoned" "${dss.name}"
		return 1
	fi
	# All BE datasets should have canmount=noauto
	typeset cm="${dss.props[canmount].value}"
	if [[ "$cm" != noauto ]]; then
		zfs_set canmount=noauto dss || return 1
		(( dirty=1 ))
	fi

	#
	# Now that we are sure that mucking with the mountpoint won't cause
	# mounts in the global zone, update the mountpoint property.
	#
	if [[ "${dss.props[mountpoint].value}" != "$mountpoint" ]]; then
		zfs_set mountpoint="$mountpoint" dss || return 1
		(( dirty=1 ))
	fi

	if (( dirty )); then
		refresh_dataset dss || return 1
	fi
	return 0
}

#
# zfs_tmpmount dataset mountpoint
#
# Mount the specified dataset using a ZFS temporary mount.  The mountpoint
# is created if necessary.  The zfs mountpoint property must not be "legacy"
# or "none" and the canmount property must not be "no" for this to succeed.
#
# Special protection against devices files is needed for datasets mounted by
# the global zone that are delegated to non-global zones.  The temporary
# mount option "nodevices" overrides the "devices" zfs property.  This
# provides protection that wouldn't be afforded by simply setting the zfs
# "devices" property to "off".  This is not a concern for datasets that are
# mounted from within the zone because the zone=<zonename> property implies
# that device special files are disallowed.
#
# Arguments:
#  dataset	The name of a dataset. This may be a string or a dataset
#		structure initialized with init_dataset.
#  mountpoint	The place where it gets mounted.
#
# Returns 0 on success, else 1.
#
function zfs_tmpmount {
	typeset dsname="$1"
	typeset dir="$2"

	vlog "Mounting $dsname at $dir with ZFS temporary mount"
	[[ -d "$dir" ]] || mkdir -m 755 -p "$dir"
	zfs mount -o nodevices,mountpoint="$dir" "$dsname" || {
		error "$e_temp_mount_failed" "$dsname" "$dir"
		return 1
	}
	return 0
}

#
# mount_be_ds -r root [-m mountpoint] dataset_structure_name
#
# Uses the ZFS Temporary Mount feature to mount the specified dataset.
#
# -m mountpoint			The place where the dataset will be mounted,
#				relative root option.  If this value and
#				the mountpoint property in the dataset are
#				in conflict, the dataset will be modified to
#				have its mountpoint set to this value.
# -r root			The root of the the zone.  This plus mountpoint
#				(or mountpoint property on dataset) determines
#				where the mount will occur.  Required.
# dataset_structure_name 	The name of a structure initialized with
#				init_dataset.  Before any action is taken,
#				the properties on this dataset will be
#				refreshed to ensure they match the current
#				state of the system.
#
function mount_be_ds {
	typeset root= mntpt= opt=

	#
	# Argument processing
	#
	while getopts :m:r: opt; do
		case "$opt" in
		m)	mntpt=$OPTARG ;;
		r)	root=$OPTARG ;;
		?)	fail_internal "$f_int_bad_opt" "$OPTARG" ;;
		esac
	done
	shift $(( OPTIND - 1 ))
	[[ -z "$root" ]] && fail_internal "$f_int_missing_opt" r
	[[ -z "$1" ]] && fail_internal "$f_int_missing_arg" dataset
	typeset -n dss=$1
	shift
	(( $# == 0 )) || fail_internal "$f_int_bad_arg" "$*"

	vlog "Preparing to mount %s at %s%s" "${dss.name}" "${root}" "${mntpt}"

	#
	# Real work
	#

	# Verify that all the properties are OK prior to mounting
	refresh_dataset dss || return 1

	#
	# Temporary mounts hide the persistent value of the mountpoint
	# property.  As such, assume that if it is mounted somewhere under
	# $root, all is well.
	#
	if [[ "${dss.props[mountpoint].source}" == temporary ]]; then
		if [[ -z "$mntpt" \
		    && "${dss.props.[mountpoint].value}" == "$root"/* ]]; then
			return 0
		fi

		# Ask zfs for an exact match
		[[ "$(get_ds_from_path "${root}${mntpt}")" \
		    == "${dss.name}" ]] && return 0
	fi

	#
	# Fix up the mountpoint, zoned, and canmount properties
	#
	if [[ -z "$mntpt" ]]; then
		mntpt="${dss.props[mountpoint].value}"
	fi
	fix_ds_mountpoint dss "$mntpt" || {
		error "$e_mount1_failed" "${dss.name}"
		return 1
	}

	# Use zfs(1M) to mount it.
	zfs_tmpmount "${dss.name}" "${root}${mntpt}" || return 1

	return 0
}

#
# mount_be -c root_dataset mountpoint
#
# Mounts the specified boot environment at the specified mountpoint.
# In addition to mounting the root dataset, child datasets that have
# have canmount=noauto and a path as a mountpoint are mounted.
#
function mount_be {
	typeset -A dsa
	typeset -A mnt
	typeset line
	typeset -i dscnt=0
	typeset dir
	typeset -i mount_children=0
	typeset zfslist_r		# -r for zfs list, if needed
	typeset extra_vlog=

	while getopts :c opt; do
		case $opt in
		c)	(( mount_children=1 ))
			zfslist_r=-r
			extra_vlog=" (including child datasets)"
			;;
		?)	fail_internal "$f_int_bad_opt" "$OPTARG"
			;;
		esac
	done
	shift $(( OPTIND - 1 ))
	typeset rootds="$1" root="$2"

	vlog "Mounting boot environment in $rootds at ${root}${extra_vlog}"

	#
	# Find all of the datasets under the root dataset, store them in the dsa
	# associative array.  stderr and return value from zfs list command are
	# ignored.  Instead, there is a check to be sure that the root dataset
	# was added in the body of the while loop.
	#
	zfs list -H -o name -t filesystem $zfslist_r "$rootds" 2>/dev/null \
	    | while read line; do
		dsa["$line"].dss=
		init_dataset "dsa[$line].dss" "$line"

		# We know where rootds needs to be mounted, so skip checks.
		[[ $line == "$rootds" ]] && continue

		#
		# Be sure mountpoint and canmount are OK.  Informational
		# messages are given rather than errors to align with
		# behavior in beadm.
		#
		typeset dir="${dsa[$line].dss.props[mountpoint].value}"
		typeset cm="${dsa[$line].dss.props[canmount].value}"

		if [[ "$dir" == legacy || "$dir" == none ]]; then
			log "$m_not_mounting_mountpoint" "$d" "$dir"
			unset dsa[$line]
			continue
		fi
		# If canmount=on it will be set to noauto when it is mounted.
		if [[ $cm == off ]]; then
			log "$m_not_mounting_canmount" "$d" "$cm"
			unset dsa[$line]
			continue
		fi
	done

	#
	# Be sure the root dataset was found
	#
	if [[ ${dsa[$rootds].dss.name} != "$rootds" ]]; then
		error "$e_no_such_dataset" "$rootds"
		return 1
	fi

	#
	# In most circumstances, only the root gets mounted.  However, if the
	# zone is intended to be left in the installed state, such as with
	# sysboot or halt, the entire BE is mounted.
	#
	if (( mount_children == 0 )); then
		if [[ ${dsa[$rootds].dss.props[mountpoint].value} != "$root" \
		    || ${dsa[$rootds].dss.props[mounted].value} != yes ]]; then
			mount_be_ds -r "$root" -m / dsa[$rootds].dss || return 1
		else
			vlog "${.sh.fun} $rootds already on $root"
		fi
		return 0
	fi

	#
	# Mount the file systems.
	#
	typeset -a umount_on_error
	typeset -i errors=0
	get_sorted_subscripts dsa subs
	for dir in "${subs[@]}"; do
		mount_be_ds -r "$root" "dsa[$dir].dss" || {
			(( errors++ ))
			break
		}
		a_push umount_on_error "$dir"
	done

	# If no errors, we are done.
	if (( errors == 0 )); then
		return 0
	fi

	# The mount process was not error-free.  Unmount whatever was mounted.
	for (( i = ${#umount_on_error[@]} - 1 ; i >= 0 ; i-- )); do
		zfs unmount "${umount_on_error[i]}" \
		    || error "e_unmount_failed" "${umount_on_error[i]}"
	done
	return 1
}

#
# mount_active_be [-b bootenv] [-c] zoneref
#
# Mounts the active boot environment at the zoneroot.
#
# Arguments and Options:
#
#  -b bootenv	Set this boot environment as the active boot environment
#		before mounting.
#  -c		Mount the complete dataset, including children of the root
#		dataset.  If the wrong BE is currently mounted, it will be
#		unmounted first.
#  -C		Similar to -c, but the BE that is already partially or fully
#		mounted is assumed to be the correct one.  No attempt will
#		be made to unmount the mounted zone root.  This should be used
#		when we know that the right BE was already partially mounted
#		and we just need the child datasets to be mounted too.
#  zoneref	A zone structure initialized by init_zone
#
# Returns 0 on success, else 1
#
function mount_active_be {
	typeset mount_children= unmount_children= be= opt

	while getopts :b:cC opt; do
		case $opt in
		b)	be=$OPTARG ;;
		c)	mount_children=-c ;;
		C)	mount_children=-c
			unmount_children=-C
			;;
		?)	fail_internal "$f_int_bad_opt" "$OPTARG" ;;
		esac
	done
	shift $(( OPTIND - 1 ))
	typeset -n zone="$1"

	if [[ -n $be ]]; then
		set_active_be zone "$be" || return 1
	elif [[ -z "${zone.active_ds}" ]]; then
		get_active_be zone || return 1
	fi

	#
	# The unmount is required until such a time as mount_be is able to
	# get the persistent mountpoint property out of zfs(1M).
	#
	unmount_be $unmount_children zone || return 1
	mount_be $mount_children "${zone.active_ds}" \
	    "${zone.root}" || return 1
	return 0
}

#
# unmount_be zoneref
#
# Unmounts the zone mounted at zoneref.root.  This is expected to be called
# at a time when the zone has no active virtual platform.  As such, it should
# only have local mounts.  In the case of a zone being halted, this function
# should have no work to do.  During sysboot, attach, and clone, this function
# is likely to unmount all datasets in a BE.
#
# Options and Arguments:
#
#   -C		Only unmount ZFS datasets that are children of the dataset
#		mounted on the zone root.
#   zoneref	A zone structure initialized by init_zone
#
# Returns 0 if everything under the zoneroot was unmounted, else 1
#
function unmount_be {
	typeset -a mounts
	typeset tab=$(printf "\t")
	typeset dev dir fstype junk opt
	typeset -i zfs_children_only=0

	while getopts :C opt; do
		case $opt in
		C)	(( zfs_children_only=1 ))
			if [[ -z "${zone.active_ds}" ]]; then
				get_active_be zone || return 1
			fi
			;;
		?)	fail_internal "$f_int_bad_opt" $opt ;;
		esac
	done
	shift $(( OPTIND - 1 ))

	typeset -n zone=$1
	typeset root=${zone.root}

	[[ -z "$root" ]] && fail_internal "zoneroot is null"

	# Read /etc/mnttab
	while IFS=$tab read dev dir fstype junk; do
		set -- $line
		if (( zfs_children_only )); then
			[[ $fstype != zfs ]] && continue
			[[ $dir == "$root" ]] && continue
			# Do not umount added fs resources
			[[ $dev == ${zone.active_ds}/* ]] || continue
		fi
		if [[ "$dir" == "$root" ]]; then
			a_push mounts "$dir"
			continue
		fi
		if [[ "$dir" == "$root"/* ]]; then
			a_push mounts "$dir"
			continue
		fi
	done < /etc/mnttab

	(( ${#mounts[@]} == 0 )) && return 0

	# Sort
	set -s -- "${mounts[@]}"
	set -A mounts "$@"

	# Unmount in reverse sorted order
	typeset -i i rv=0
	for (( i = ${#mounts[@]} - 1; i >= 0; i-- )); do
		vlog "Unmounting ${mounts[i]}"
		#
		# If a graceful umount fails, it may be an indication that some
		# global zone process is still active on the file system's
		# contents.  We should allow the umount to fail so that we
		# don't pull the rug out from a process that may be doing
		# delicate operations in the zone's BE.
		#
		umount "${mounts[i]}" || {
			rv=1
			error "$e_unmount_failed" "${mounts[i]}"
		}
	done

	return $rv
}

#
# detach_zone zone
#
# Unmount the ZBE then copy the zone configuration to SUNWdetached.xml.
#
# Arguments:
#
#  zone		A zone structure initialized with init_zone
#
# Returns:
#  0 on success, exits with $ZONE_SUBPROC_NOTCOMPLETE on error
#

function detach_zone {
	typeset -n zone=$1

	unmount_be zone || fail_incomplete "$f_unmount_be"

	cp /etc/zones/${zone.name}.xml ${zone.path}/SUNWdetached.xml

	return 0
}

#
# Determines if any part of the zone's /export hierarchy comes from
# fs or dataset resources in the zone configuration.
#
# Returns 0 (true) if export is provided by the zone configuration.
#
function zonecfg_has_export {
	typeset -n zone=$1
	typeset dir
	for dir in $(zonecfg -z "${zone.name}" info fs | \
	    nawk '$1 == "dir:" { print $2 }' | LC_ALL=C sort); do
		if [[ $dir == /export || $dir == /export/* ]]; then
			log "$m_manual_export_migrate" "${zone.root}/export" \
			    "zonecfg fs $dir"
			return 0
		fi
	done
	typeset dsname line
	zonecfg -z "${zone.name}" info dataset | \
	    nawk '$1 == "name:" { print $2}' | \
	    while read line; do
		zfs list -H -o name,mountpoint "$line" 2>/dev/null
	    done | while read dsname dir; do
		if [[ $dir == /export || $dir == /export/* ]]; then
			log "$m_manual_export_migrate" "${zone.root}/export" \
			    "zonecfg dataset $dsname"
			return 0
		fi
	done

	return 1
}

#
# umount_destroy_rmdir dirname dsname
#
# Cleans up the specified mount, destroys the dataset, and removes the mount
# point.  Calls error() with a useful message on first failure and returns 1.
# Returns 0 on success.
#
function umount_destroy_rmdir {
	typeset dir=$1
	typeset dsname=$2

	umount "$dir" || {
		error "$e_unmount_failed" "$dir"
		return 1
	}
	zfs destroy "$dsname" || {
		error "$e_zfs_destroy" "$dsname"
		return 1
	}
	rmdir "$dir" || {
		error "$f_rmdir" "$dir"
		return 1
	}
	return 0
}

#
# During p2v and v2v, move any contents from the BE's /export to the non-BE
# /export.  Same goes for /export/home.
#
# If existing contents are found in .../rpool/export or .../rpool/export/home,
# a message is displayed indicating manual migration is required and a the
# return value is 0.  If migration is attempted but unsuccessful, the return
# value is 1.  If migration is successful, the return value is 0.
#
# In the event that the zone's existing /export and/or /export/home exists but is
# not a directory (e.g. is a symlink) the corresponding dataset(s) are destroyed
# so as to not be in the way for any migration.  If /export exists and is not
# a directory, both the export and export/home datasets are destroyed and no
# migration takes place.
#
function migrate_export {
	typeset -n zone=$1
	typeset dir
	typeset -i destroy_export=0 destroy_exporthome=0

	# If /export doesn't exist or is empty there is no work to do.
	[[ -d ${zone.root}/export ]] || return 0
	[[ -z "$(ls -A ${zone.root}/export)" ]] && return 0

	#
	# If zonecfg fs or dataset resources specify a file system to mount
	# anywhere under /export, assume that they don't want /export migrated.
	#
	zonecfg_has_export zone && return 0

	#
	# Mount /export and /export home under a temporary directory.
	# Note that it the zone's export dataset is mounted at $dir/export
	# not at $dir to make it so that mv(1) can be used for a very simple
	# migration process.
	#
	dir=$(mktemp -d)
	zfs_tmpmount "${zone.rpool_ds}/export" "$dir/export" || {
		rmdir $dir
		error "$e_export_migration_failed"
		return 1
	}
	zfs_tmpmount "${zone.rpool_ds}/export/home" "$dir/export/home" || {
		umount "$dir/export"
		rmdir "$dir/export" "$dir"
		error "$e_export_migration_failed"
		return 1
	}

	#
	# Check to see if the existing .../rpool/export dataset hierarchy
	# contains anything.  If so, don't clobber it.
	#
	(cd "$dir" && find export) | \
	    nawk '$0 !~ /^(export|export\/home)$/ {exit 1}' || {
		umount "$dir/export/home"
		umount "$dir/export"
		rmdir "$dir/export" 2>/dev/null
		rmdir "$dir"
		log "$m_manual_export_migrate" "${zone.root}/export" \
		    "${zone.rpool_ds}/export"
		return 0
	}

	#
	# It is possible that /export and/or /export/home exsists but is not a
	# directory.  If so, the corresponding dataset should be deleted so
	# that migration (if any) doesn't choke trying to put a directory on
	# top of a symlink or other non-directory.
	#
	if [[ -h "${zone.root}/export/home" ]]; then
		(( destroy_exporthome = 1 ))
	elif [[ -e "${zone.root}/export/home" && \
	    ! -d "${zone.root}/export/home" ]]; then
		(( destroy_exporthome = 1 ))
	fi

	if [[ -h "${zone.root}/export" ]]; then
		(( destroy_export = 1 ))
		(( destroy_exporthome = 1 ))
	elif [[ -e "${zone.root}/export" && ! -d "${zone.root}/export" ]]
	then
		(( destroy_export = 1 ))
		(( destroy_exporthome = 1 ))
	fi

	if (( destroy_exporthome )); then
		umount_destroy_rmdir "$dir/export/home" \
		    "${zone.rpool_ds}/export/home" || {
			error "$e_export_migration_failed"
			return 1
		}
	fi

	if (( destroy_export )); then
		umount_destroy_rmdir "$dir/export" \
		    "${zone.rpool_ds}/export" || {
			error "$e_export_migration_failed"
			return 1
		}
		# Nothing left to migrate to.  Finish cleanup and return.
		rmdir $dir
		return 0
	fi

	# Odd quoting below to prevent SCCS keyword expansion & warnings.
	typeset bkup
	bkup=${zone.root}/export.backup.$(TZ=UTC date +%Y""%m""%dT""%H""%M""%SZ)
	if [[ -e "$bkup" ]]; then
		#
		# There's no legitimate reason that we should have a
		# collision - this is likely an attack by the provider
		# of the archive.
		#
		umount "$dir/export/home"
		umount "$dir/export"
		rmdir "$dir/export" 2>/dev/null
		rmdir "$dir"
		fatal "$f_backup_dir_exists" "$bkup"
	fi

	log "$m_migrating_data" "$(zfs list -H -o name "${zone.root}/export")" \
	    "$(zfs list -H -o name "$dir/export")"

	#
	# cpio insists on printing the number of blocks transferred on stderr.
	# This output only serves to confuse so prevent it from being displayed
	# if cpio is otherwise successful.
	#
	typeset cpioout rv
	cpioout=$( cd "${zone.root}" && find export | \
	    LC_ALL=C cpio -pdumP@/ "$dir" 2>&1)
	rv=$?
	if [[ $cpioout != [0-9]*" blocks" || $rv != 0 ]]; then
		print -- "$cpioout"
		(( destroy_exporthome )) || umount "$dir/export/home"
		umount "$dir/export"
		rmdir "$dir/export" 2>/dev/null
		rmdir "$dir"
		error "$e_export_migration_failed"
		return 1
	fi

	mv ${zone.root}/export "$bkup"
	log "$m_backup_saved" /export "/$(basename "$bkup")"

	# Migration was successful.  Even if a umount fails, still return 0.
	(( destroy_exporthome )) || umount "$dir/export/home"
	umount "$dir/export"
	rmdir "$dir/export" 2>/dev/null
	rmdir "$dir"

	return 0
}

#
# migrate_rpool zone
#
# Migrates the contents of the /rpool directory out of the ZBE into the rpool
# dataset.
#
# Arguments
#
#  zone		A zone data structure initialized with init_zone.
#
# Returns
#
# 0 on success, 1 on failure.
#
function migrate_rpool {
	typeset -n zone=$1
	typeset dir

	[[ -d ${zone.root}/rpool ]] || return 0
	[[ -z "$(ls -A ${zone.root}/rpool)" ]] && return 0

	dir=$(mktemp -d)
	zfs_tmpmount "${zone.rpool_ds}" "$dir/rpool" || {
		rmdir $dir/rpool >/dev/null 2>&1
		rmdir $dir
		error "$e_rpool_migration_failed"
		return 1
	}

	typeset bkup
	bkup=${zone.root}/rpool.backup.$(TZ=UTC date +%Y""%m""%dT""%H""%M""%SZ)
	if [[ -e $bkup ]]; then
		#
		# There's no legitimate reason that we should have a
		# collision - this is likely an attack by the provider
		# of the archive.
		#
		umount $dir/rpool
		rmdir $dir/rpool
		rmdir $dir
		fatal "$f_backup_dir_exists" "$bkup"
	fi

	log "$m_migrating_data" "$(zfs list -H -o name "${zone.root}/rpool")" \
	    "$(zfs list -H -o name "$dir/rpool")"

	#
	# cpio insists on printing the number of blocks transferred on stderr.
	# This output only serves to confuse so prevent it from being displayed
	# if cpio is otherwise successful.
	#
	typeset cpioout rv
	cpioout=$( cd "${zone.root}" && find rpool | \
	    LC_ALL=C cpio -pdumP@/ "$dir" 2>&1)
	rv=$?
	if [[ $cpioout != [0-9]*" blocks" || $rv != 0 ]]; then
		print -- "$cpioout"
		umount "$dir/rpool"
		rmdir "$dir/rpool"
		rmdir "$dir"
		error "$e_rpool_migration_failed"
		return 1
	fi

	mv ${zone.root}/rpool "$bkup"
	log "$m_backup_saved" /rpool "/$(basename "$bkup")"

	# Migration was successful.  Even if a umount fails, still return 0.
	umount "$dir/rpool"
	rmdir "$dir/rpool"
	rmdir "$dir"
	return 0
}

CLUSTER_HOOK="/usr/cluster/lib/sc/zc_handler"

function call_cluster_hook {
	if [[ -f $CLUSTER_HOOK ]]; then
		$CLUSTER_HOOK "$@"
		return $?
	else
		return $ZONE_SUBPROC_OK
	fi
}

#
# start_log zone subcommand [command line]
#
# Sets up the environment for logging functions to log to a log file.  The log
# is created as /var/log/zones/zoneadm.<timestamp>.<zone>.<subcommand>.
# However, see the effect of the ZONEADM_LOGFILE environment variable.
#
# Example:
#
#	init_zone zone "$1" "$2"
#	start_log zone attach "$0" "$@"
#
# Arguments
#
# zone		A zone structure initialized with init_zone.
# subcommand	The subcommand of zoneadm that is running.
# command line	The command line of the script calling this function.  This
#		array will be logged to indicate what is running
#
# Globals
#
#  LOGFILE		Set to the name of the log file.
#  ZONEADM_LOGFILE	If this environment variable is set to a writable file,
#			logging is done to that file.  If this environment
#			variable did not already refer to a writable file,
#			it is set to the value of LOGFILE.  Thus, in situations
#			where one brand script calls another (perhaps via
#			another invocation of zoneadm), there is a single
#			log file created.
#  LOGGING_COMMAND	A string that represents the command line.  Used by
#			finish_log().
#			start_log().
#  FINISH_LOG		Set to true or false.  If ZONEADM_LOGFILE is set
#			to a writable file when start_log() is called,
#			FINISH_LOG is set to false.  This affects behavior
#			of finish_log().
#  stderr (fds 2 & 3)	stderr is copied to file descriptor 3, then stderr
#			is redirected for append to $LOGFILE.
#
function start_log {
	typeset subcommand zonename

	(( $# < 2 )) && fail_internal "Too few arguments to start_log"
	typeset -n zone=$1
	typeset subcommand=$2
	typeset zonename=${zone.name}
	shift 2
	LOGGING_COMMAND="$*"

	[[ -z $zonename ]] && fail_internal "zone structure not initialized"

	if [[ -n $ZONEADM_LOGFILE && -f $ZONEADM_LOGFILE &&
	    -w $ZONEADM_LOGFILE ]]; then
		#
		# Some other script that called this one already set things
		# up.  Continue to use existing $ZONEADM_LOGFILE as $LOGFILE.
		#
		FINISH_LOG=false
		LOGFILE=$ZONEADM_LOGFILE
	else
		if [[ ! -d /var/log/zones ]]; then
			mkdir -m 755 /var/log/zones ||
			    fatal "$f_mkdir" /var/log/zones
		fi
		FINISH_LOG=true

		#
		# Use a subshell to set noclobber, then try to create
		# a unique log file without the ugly file name generated
		# by mktemp.
		#
		typeset name timestamp
		timestamp=$(TZ=GMT date +"%Y""%m""%dT""%H""%M""%"SZ)
		name=/var/log/zones/zoneadm.$timestamp.$zonename.$subcommand
		LOGFILE=$(set -o noclobber
			try=$name
			i=0
			while (( i++ < 100 )); do
				exec 2>$try
				if (( $? == 0 )); then
					print "$try"
					break
				fi
				try=$name.$try
			done)
		[[ -z $LOGFILE || ! -f $LOGFILE ]] &&
		    fail_internal "Cannot create unique log file"
		ZONEADM_LOGFILE=$LOGFILE
	fi

	#
	# Before redirecting stderr to $LOGFILE, save a copy of it to fd 3
	# so that it can be restored by finish_log().
	#
	exec 3>&2
	exec 2>>$LOGFILE

	vlog "==== Starting: %s ====" "$*"
	[[ $FINISH_LOG == true ]] && log "$m_log_progress_to" "$LOGFILE"
}
#
# The following export is performed in the global scope to force the
# environment variable to exist.  If it is exported from within the function,
# it just puts it into the global scope and not into the environment.
#
export ZONEADM_LOGFILE

#
# finish_log zone [logfilevar]
#
# Finish logging started by start_log().
#
# Arguments
#
#  zone		A zone structure initialized with init_zone.
#  logfilevar	The name of a variable to contain the name of the
#		resulting log file.
#
# Globals
#
#  LOGFILE		The name of the log file.  Unset before return.
#  ZONEADM_LOGFILE	If FINISH_LOG is true, this environment variable
#			is unset before return.
#  LOGGING_COMMAND	The command that is being logged.  Set by start_log().
#  FINISH_LOG		If set to true and ${zone.root}/var/log exists as a
#			directory, $LOGFILE is copied to the same path in the
#			zone.  If necessary, /var/log/zones is created in the
#			zone.
#
function finish_log {
	typeset -n zone=$1
	typeset newlog
	typeset logfile=$LOGFILE

	[[ -z $LOGFILE ]] && return

	vlog "==== Completed: %s  ====" "$LOGGING_COMMAND"

	# Stop logging to $LOGFILE and restore stderr.
	exec 2<&3
	exec 3<&-
	unset LOGFILE
	[[ $FINISH_LOG == true ]] || return

	#
	# If the operation ended such that there is no zone mounted
	# (e.g. uninstall, failed install, etc.) do not attempt to copy
	# it into the zone.
	#
	[[ -z ${zone.root} ]] && return
	[[ -d ${zone.root}/var/log ]] || return
	safe_dir /var
	safe_dir /var/log

	if [[ ! -d ${zone.root}/var/log/zones ]]; then
		# If the log file can't be safely copied into the zone,
		# give up on copying it there.
		if [[ -e ${zone.root}/var/log/zones ||
		    -h ${zone.root}/var/log/zones ]]; then
			error "$e_baddir" /var/log/zones
			return
		fi
		mkdir -m 755 ${zone.root}/var/log/zones ||
		    fatal "$f_mkdir" ${zone.root}/var/log/zones
	fi

	safe_copy "$logfile" "${zone.root}${logfile}"
	if [[ -n $2 ]]; then
		typeset -n out=$2
		out=$logfile
	fi
	log "$m_log_copied_to" "${zone.root}$logfile"
}

#
# pin_datasets topds
#
# Keeps track of which decendants of topds exist at a point in time by
# tracking the guid property of each dataset.  Note that as datasets
# are renamed, the guid stays the same and as such datasets stay pinned
# across renames.  See also delete_unpinned_datasets() and unpin_datasets().
#
# Arguments
#
#   topds	The name of the top dataset to pin.  Most likely this is
#		a zonepath dataset.
#
# Globals
#
#   DATASET_PINS	An associative array mapping the guid property
#			to the name of pinned dataset.  The name is
#			not actually important - it is an arbitrary
#			value assigned to the array element.
#
# Return
#   0		Success - at least one dataset is pinned.
#   1		Failure
#
unset DATASET_PINS
typeset -A DATASET_PINS
function pin_datasets {
	typeset topdsn=$1
	typeset guid dsn
	typeset retval=1

	vlog "Pinning datasets under %s" "$topdsn"

	/usr/sbin/zfs get -Hrp -o value,name guid "$topdsn" 2>/dev/null |
	    while IFS=$'\t' read guid dsn; do
		vlog "Pinning %s" "$dsn"
		DATASET_PINS[$guid]="$dsn"
		retval=0
	done

	return $retval
}

#
# unpin_datasets topds
#
# Undoes the work of pin_datasets() for all datasets that are descendants
# of topds.
#
# Arguments
#
#   topds	The name of the top dataset to pin.  Most likely this is
#		a zonepath dataset.
#
# Globals
#
#   DATASET_PINS	An associative array mapping the guid property
#			to the name of pinned dataset.
#
# Return
#   0		Success
#   1		Failure - nothing was unpinned
#
function unpin_datasets {
	typeset topdsn=$1
	typeset retval=1

	vlog "Unpinning datasets under %s" "$topdsn"

	/usr/sbin/zfs get -Hrp -o value,name guid "$topdsn" 2>/dev/null |
	    while IFS=$'\t' read guid dsn; do
		[[ -z ${DATASET_PINS[$guid]} ]] && continue
		vlog "Unpinning %s" "$dsn"
		unset DATASET_PINS[$guid]
		retval=0
	done

	return $retval
}

#
# delete_unpinned_datasets topds
#
# Deletes each dataset under topds that is not pinned by pin_datasets().  As a
# safety measure, if topds is not pinned, nothing will be deleted and 1 will be
# returned.
#
# Note:	This function does not handle the case of snapshot collisions on
#	promotion.  However, if it is just used as a cleanup function after
#	typical failures in install/attach operations, this should not be a
#	problem.
# Note: This function does not use the same functions used by uninstall
#	because they do recursive removals and may exit while executing.
#	This function is intended to be safe to call from an exit handler
#	and functions it calls should not prematurely cause the exit handler
#	to exit.
#
# Arguments
#
#   topds	The name of the top dataset to pin.  Most likely this is
#		a zonepath dataset.
#
# Globals
#
#   DATASET_PINS	An associative array mapping the guid property
#			to the name of pinned dataset.
#
# Return
#   0		Success
#   1		Failure or partial failure.
#
function delete_unpinned_datasets {
	typeset topdsn=$1
	typeset -i ispinned=0

	vlog "Destroying datasets under %s that are not pinned" "$topdsn"

	typeset name guid
	typeset -A todestroy
	/usr/sbin/zfs get -Hrp -o value,name guid "$topdsn" 2>/dev/null |
	    while IFS=$'\t' read guid name; do

		# Be sure it is pinned before allowing anything to be deleted.
		if [[ $name == $topdsn ]]; then
			if [[ -z ${DATASET_PINS[$guid]} ]]; then
				error "$e_not_pinned" "$topdsn"
				return 1
			fi
			(( ispinned=1 ))
		fi

		# Do not destroy pinned datasets.
		[[ -n ${DATASET_PINS[$guid]} ]] && continue

		# To minimize the chance of snapshot collisions during clone
		# promotion, remove all snapshots that we can ASAP.
		if [[ $name == *@* ]]; then
			/usr/sbin/zfs destroy "$name" >/dev/null 2>&1
			if (( $? == 0 )); then
				vlog "Destroyed unpinned snapshot %s" "$name"
				continue
			fi
		fi
		todestroy["$name"]=$name
	done

	# If no work to be done, return immediately.
	(( ${#todestroy[@]} == 0 )) && return 0

	#
	# Be sure that if there is anything to do that it is pinned.  If
	# we detect that it is not pinned at this point, it means there is
	# a logic error.
	#
	(( ispinned == 0 )) && fail_internal "$e_not_pinned" "$topdsn"

	#
	# Destroy the datasets in reverse order.  Because of clones that
	# exist within the received datasets, there may be some failures.
	# Don't worry about that so long as each iteration makes progress.
	#
	while (( ${#todestroy[@]} != 0 )); do
		typeset progress=false
		typeset -a names
		get_sorted_subscripts todestroy names
		for name in "${names[@]}"; do
			name=${names[$i]}
			/usr/sbin/zfs destroy "$name" >/dev/null 2>&1 ||
			    continue
			vlog "Destroyed unpinned dataset %s" "$name"
			progress=true
			unset todestroy[$name]
			unset DATASET_PINS[$guid]
		done

		if [[ $progress != true ]]; then
			for name in "${names[@]}"; do
				log "$e_destroy_unpinned" "$name"
			done
			return 1
		fi
	done

	return 0
}

# Setup i18n output
TEXTDOMAIN="SUNW_OST_OSCMD"
export TEXTDOMAIN

e_cannot_wrap=$(gettext "%s: error: wrapper file already exists")
e_baddir=$(gettext "Invalid '%s' directory within the zone")
e_badfile=$(gettext "Invalid '%s' file within the zone")
e_path_abs=$(gettext "Pathname specified to -a '%s' must be absolute.")
e_not_found=$(gettext "%s: error: file or directory not found.")
e_install_abort=$(gettext "Installation aborted.")
e_not_readable=$(gettext "Cannot read directory '%s'")
e_not_dir=$(gettext "Error: must be a directory")
e_unsupported_archive=$(gettext "Archive format '%s' not supported by this brand.  See %s(5) for supported archive types.")
e_absolute_archive=$(gettext "Error: archive contains absolute paths instead of relative paths.")
e_mismatch_archive=$(gettext "Error: the archive top-level directory (%s) does not match the zonepath (%s).")
e_tmpfile=$(gettext "Unable to create temporary file")
e_tmpdir=$(gettext "Unable to create temporary directory %s")
e_rmdir=$(gettext "Unable to remove directory %s")
e_rm=$(gettext "Unable to remove %s")
e_mv=$(gettext "Unable to rename '%s' to '%s'")
e_root_full=$(gettext "Zonepath root %s exists and contains data; remove or move aside prior to install.")
e_temp_mount_failed=$(gettext "ZFS temporary mount of %s on %s failed.")
e_no_such_dataset=$(gettext "Error: %s: No such dataset.")
e_ds_mnt_multiply_defined=$(gettext "Error: multiple datasets list %s as mountpoint.")
e_unmount_failed=$(gettext "unable to unmount %s.")
e_mount1_failed=$(gettext "Error: could not mount %s.")
e_parent_not_zoned=$(gettext "Error: parent dataset of %s is not zoned.")
e_export_migration_failed=$(gettext "Error: migration of /export from active boot environment to the zone's\nrpool/export dataset failed.  Manual cleanup required.")
e_rpool_migration_failed=$(gettext "Error: migration of data in /rpool from active boot environment to the zone's\nrpool dataset failed.  Manual cleanup required.")
e_zfs_destroy=$(gettext "Error: cannot destroy dataset %s")
e_file_conflict=$(gettext "Received file %s collides in datasets %s.")
e_ds_conflict=$(gettext "Received dataset %s collides with existing dataset %s.")
e_unexpected_ds=$(gettext "Unexpected dataset %s found in receive stream.")
e_be_move_failed=$(gettext "Failed to move be dataset %s.")
f_no_ds=$(gettext "The zonepath must be a ZFS dataset.\nThe parent directory of the zonepath must be a ZFS dataset so that the\nzonepath ZFS dataset can be created properly.")
e_no_active_be=$(gettext "Error: No active boot environment found.")
e_no_mntpt_change_for_mounted=$(gettext "Error: Cannot change mountpoint because %s is mounted")
e_zfs_set=$(gettext "Error: Cannot set zfs property %s on %s")
e_zfs_inherit=$(gettext "Error: Cannot inherit zfs property %s on %s")
e_cmd_failed=$(gettext "Error: Command <%s> exited with status %d")
e_not_pinned=$(gettext "Dataset %s is not pinned")
e_destroy_unpinned=$(gettext "Unable to destroy unpinned dataset '%s'.")
e_dataset_exists=$(gettext "Dataset '%s' already exists")
s10_zbe_not_supported=$(gettext "Solaris 10 style boot environments not supported by this brand.")
f_mkdir=$(gettext "Unable to create directory %s.")
f_chmod=$(gettext "Unable to chmod directory %s.")
f_chown=$(gettext "Unable to chown directory %s.")
f_rmdir=$(gettext "Unable to remove directory %s.")
f_hwcap_info=$(gettext "HWCAP: %s\n")
f_sanity_hwcap=$(gettext \
"The image was created with an incompatible libc.so.1 hwcap lofs mount.\n"\
"       The zone will not boot on this platform.  See the zone's\n"\
"       documentation for the recommended way to create the archive.")
f_int_bad_opt=$(gettext "Internal error: bad option -%s")
f_int_missing_opt=$(gettext "Internal error: missing option -%s")
f_int_missing_arg=$(gettext "Internal error: missing argument %s")
f_int_bad_opt_combo=$(gettext "Internal error: incompatible options -%s and %-s")
f_int_bad_arg=$(gettext "Internal error: extra argument %s")
f_mount=$(gettext "Error: error mounting zone root dataset.")
f_ds_config=$(gettext "Failed to configure dataset %s: could not set %s.")
f_backup_dir_exists=$(gettext "Backup directory %s already exists.")
f_zfs_snapshot=$(gettext "Failed to snapshot source zone.")
f_zone_clone=$(gettext "Failed to clone zone.")
f_zfs_create=$(gettext "Failed to create dataset %s.")
f_zfs_snapshot_of=$(gettext "Failed to create snapshot of %s.")
f_detach_convert=$(gettext "Conversion of detached datasets failed.")
f_mount_active_be=$(gettext "Unable to mount zone root dataset.")
f_unmount_be=$(gettext "Unable to unmount boot environment.")
f_pin=$(gettext "Failed to mark existing datasets for preservation.")
f_unpin=$(gettext "Failed to remove preservation mark from pre-existing datasets.")

m_interrupt=$(gettext "Cleaning up due to interrupt.  Please be patient.")
m_attaching=$(gettext "Attaching...")
m_brnd_usage=$(gettext "brand-specific usage: ")
m_analyse_archive=$(gettext "Analysing the archive")
m_fix_ds_mountpoint=$(gettext "Changing mountpoint of dataset %s from %s to %s.")
m_not_mounting_mountpoint=$(gettext "Not mounting %s because mountpoint is '%s'.")
m_not_mounting_canmount=$(gettext "Not mounting %s because canmount is '%s'.")
m_manual_export_migrate=$(gettext "Manual migration of export required.  Potential conflicts in\n%s and %s.")
m_backup_saved=$(gettext "A backup copy of %s is stored at %s.\nIt can be deleted after verifying it was migrated correctly.")
m_migrating_data=$(gettext "Migrating data\n\tfrom: %s\n\t  to: %s")
m_convert_detached=$(gettext "Converting detached zone boot environment '%s'.")
m_log_progress_to=$(gettext "Progress being logged to %s")
m_log_copied_to=$(gettext "Log saved in non-global zone as %s")

not_readable=$(gettext "Cannot read file '%s'")
not_flar=$(gettext "Input is not a flash archive")
bad_flar=$(gettext "Flash archive is a corrupt")
bad_zfs_flar=$(gettext "Flash archive contains a ZFS send stream.\n\tRecreate the flar using the -L option with cpio or pax.")
f_unpack_failed=$(gettext "Unpacking the archive failed")
unknown_archiver=$(gettext "Archiver %s is not supported")
cmd_not_exec=$(gettext "Required command '%s' not executable!")
installing=$(gettext    "    Installing: This may take several minutes...")
no_installing=$(gettext "    Installing: Using existing zone boot environment")
from_clone=$(gettext "    Installing: Using clone of zone boot environment '%s'")

#
# Exit values used by the script, as #defined in <sys/zone.h>
#
#	ZONE_SUBPROC_OK
#	===============
#	Installation was successful
#
#	ZONE_SUBPROC_DETACHED
#	=====================
#	The install or attach operation left a not yet attached zone within
#	the zonepath dataset.  It is ok to try to attach the zone or mark it
#	incomplete and try again.
#
#	ZONE_SUBPROC_USAGE
#	==================
#	Improper arguments were passed, so print a usage message before exiting
#
#	ZONE_SUBPROC_NOTCOMPLETE
#	========================
#	Installation did not complete, but another installation attempt can be
#	made without an uninstall
#
#	ZONE_SUBPROC_FATAL
#	==================
#	Installation failed and an uninstall will be required before another
#	install can be attempted
#
ZONE_SUBPROC_OK=0
ZONE_SUBPROC_DETACHED=252
ZONE_SUBPROC_USAGE=253
ZONE_SUBPROC_NOTCOMPLETE=254
ZONE_SUBPROC_FATAL=255

