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
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
#

# NOTE: this script runs in the global zone and touches the non-global
# zone, so care should be taken to validate any modifications so that they
# are safe.

. /usr/lib/brand/solaris10/common.ksh

LOGFILE=
MSG_PREFIX="p2v: "
EXIT_CODE=1

function usage {
	echo "$0 [-s] [-m msgprefix] [-u] [-v] [-b patchid]* [-c sysidcfg] zonename" >&2
	exit $EXIT_CODE
}

# Clean up on interrupt
function trap_cleanup {
	msg=$(gettext "Postprocessing cancelled due to interrupt.")
	error "$msg"

	if (( zone_is_running != 0 )); then
		error "$e_shutdown" "$ZONENAME"
		/usr/sbin/zoneadm -z $ZONENAME halt
	fi

	#
	# Delete temporary files created during the hollow package removal
	# process.
	#
	rm -f $hollow_pkgs $hollow_file_list $hollow_dir_list

	finish_log zone

	exit $EXIT_CODE
}

#
# Disable any existing live-upgrade configuration.
# We have already called safe_dir to validate the etc/lu directory.
#
function fix_lu {
	ludir=$ZONEROOT/etc/lu

	[[ ! -d $ludir ]] && return

	safe_rm etc/lutab
	safe_rm etc/lu/.BE_CONFIG
	safe_rm etc/lu/.CURR_VARS
	safe_rm etc/lu/ludb.local.xml
	for i in $ludir/ICF* $ludir/vtoc* $ludir/GRUB*
	do
		nm=$(basename $i)
		safe_rm etc/lu/$nm
	done
}

#
# For an exclusive stack zone, verify the zonecfg network configuration
# is consistent with the /etc/hostname* files inside the zone.
#
function fix_net {
	typeset file_list
	typeset net_list
	typeset anet_list
	typeset iface_list

	typeset file	# Example: /zones/myzone/root/etc/hostname.e1000g0:0
	typeset iface	#	   e1000g0
	typeset device	#	   e1000g

	[[ "$STACK_TYPE" == "shared" ]] && return

	#
	# Create lists of /etc/hostname* file names inside the zone
	# and interface names from zonecfg net and anet resources.
	# Make sure list items are separated by a space character.
	#
	file_list=$(/usr/bin/ls $ZONEROOT/etc/hostname.* \
	    $ZONEROOT/etc/hostname6.* 2>/dev/null)
	[[ -n $file_list ]] && file_list=$(print $file_list)
	net_list=$(LC_ALL=C /usr/sbin/zonecfg -z $ZONENAME info net \
		2>/dev/null | grep "physical: " | /usr/bin/cut -f2 -d " ")
	[[ -n $net_list ]] && net_list=$(print $net_list)
	anet_list=$(LC_ALL=C /usr/sbin/zonecfg -z $ZONENAME info anet \
		2>/dev/null | grep "linkname: " | /usr/bin/cut -f2 -d " ")
	[[ -n $anet_list ]] && anet_list=$(print $anet_list)

	#
	# Walk the list of /etc/hostname* files looking for interfaces
	# not covered in the zonecfg file.
	#
	iface_list=""
	for file in $file_list; do

		# Extract interface name from end of file name
		iface=${file#$ZONEROOT/etc/hostname?(6).}

		# Strip logical interface number that may exist
		iface=${iface%:+([0-9])}

		# Isolate device name
		device=${iface%%+([0-9])}

		# Skip virtual and ppp interfaces.
		[[ " xx lo ip.tun ip6.tun ip.6to4tun vni ppp " \
		    == *\ $device\ * ]] && continue

		# Do basic validity checks on interface and device names
		if [[ $iface == $device ||
		    $device != @([a-zA-Z])*([a-zA-Z0-9.]) ]]; then
			vlog "$v_invalidiface" ${file#$ZONEROOT}
			continue
		fi

		# Build list of unique interfaces names we found.
		[[ " $iface_list " != *\ $iface\ * ]] && \
		    iface_list="$iface_list $iface"

		# Is the interface name we extracted included in
		# net or anet resource interface names?
		if [[ " $net_list $anet_list " != *\ $iface\ * ]]; then
		    vlog "$v_noresource" ${file#$ZONEROOT}
		fi
	done

	#
	# Walk the list of resources looking for interface names
	# not covered by the zone's /etc/hostname* files.
	#
	for iface in $net_list; do
		if [[ " $iface_list " != *\ $iface\ * ]]; then
			vlog "$v_nonethostname" $iface
		fi
	done
	for iface in $anet_list; do
		if [[ " $iface_list " != *\ $iface\ * ]]; then
			vlog "$v_noanethostname" $iface
		fi
	done
}

#
# Disable all of the shares since the zone cannot be an NFS server.
# Note that we disable the various instances of the svc:/network/shares/group
# SMF service in the fix_smf function.
#
function fix_nfs {
	zonedfs=$ZONEROOT/etc/dfs

	[[ ! -d $zonedfs ]] && return

	if [[ -h $zonedfs/dfstab || ! -f $zonedfs/dfstab ]]; then
		error "$e_badfile" "/etc/dfs/dfstab"
		return
	fi

	tmpfile=$(mktemp -t)
	if [[ $? == 1 || -z "$tmpfile" ]]; then
		error "$e_tmpfile"
		return
	fi

	/usr/bin/nawk '{
		if (substr($1, 0, 1) == "#") {
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
	}' $zonedfs/dfstab >>$tmpfile

	if (( $? == 0 )); then
		if [[ ! -f $zonedfs/dfstab.pre_p2v ]]; then
			safe_copy $zonedfs/dfstab $zonedfs/dfstab.pre_p2v
		fi
		safe_copy $tmpfile $zonedfs/dfstab
		chown root:sys $zonedfs/dfstab || \
		    fail_fatal "$f_chown" "$zonedfs/dfstab"
		chmod 644 $zonedfs/dfstab || \
		    fail_fatal "$f_chmod" "$zonedfs/dfstab"
	fi
	/usr/bin/rm -f $tmpfile
}

#
# Comment out most of the old mounts since they are either unneeded or
# likely incorrect within a zone.  Specific mounts can be manually
# reenabled if the corresponding device is added to the zone.
#
function fix_vfstab {
	if [[ -h $ZONEROOT/etc/vfstab || ! -f $ZONEROOT/etc/vfstab ]]; then
		error "$e_badfile" "/etc/vfstab"
		return
	fi

	tmpfile=$(mktemp -t)
	if [[ $? == 1 || -z "$tmpfile" ]]; then
		error "$e_tmpfile"
		return
	fi

	/usr/bin/nawk '{
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
		chown root:sys $ZONEROOT/etc/vfstab || \
		    fail_fatal "$f_chown" "$ZONEROOT/etc/vfstab"
		chmod 644 $ZONEROOT/etc/vfstab || \
		    fail_fatal "$f_chmod" "$ZONEROOT/etc/vfstab"
	fi
	/usr/bin/rm -f $tmpfile
}

#
# Collect the data needed to delete SMF services.  Since we're p2v-ing a
# physical image there are SMF services which must be deleted.
#
function get_smf_services_to_delete {
	#
	# Start by getting the svc manifests that are delivered by hollow
	# pkgs then use 'svccfg inventory' to get the names of the svcs
	# delivered by those manifests.  The svc names are saved into a
	# temporary file.
	#

	SMFTMPFILE=$(mktemp -t smf.XXXXXX)
	if [[ $? == 1 || -z "$SMFTMPFILE" ]]; then
		error "$e_tmpfile"
		return
	fi

	for i in $ZONEROOT/var/sadm/pkg/*
	do
		pkg=$(/usr/bin/basename $i)
		[[ ! -f $ZONEROOT/var/sadm/pkg/$pkg/save/pspool/$pkg/pkgmap ]] \
		    && continue

		/usr/bin/egrep -s "SUNW_PKG_HOLLOW=true" \
		    $ZONEROOT/var/sadm/pkg/$pkg/pkginfo || continue

		for j in $(/usr/bin/nawk '{if ($2 == "f" &&
		    substr($4, 1, 17) == "var/svc/manifest/") print $4}' \
		    $ZONEROOT/var/sadm/pkg/$pkg/save/pspool/$pkg/pkgmap)
		do
			svcs=$(/usr/sbin/zlogin $ZONENAME /usr/sbin/svccfg \
			    inventory /$j)
			for k in $svcs
			do
				echo $k /$j >> $SMFTMPFILE
			done
		done
	done
}

#
# Delete or disable SMF services.
# Zone is booted to milestone=none when this function is called.
# Use the SMF data collected by get_smf_services_to_delete() to delete the
# services.
#
function fix_smf {
	#
	# Zone was already booted to milestone=none, wait until SMF door exists.
	#
	integer i

	for i in 0 1 2 3 4 5 6 7 8 9
	do
		[[ -r $ZONEROOT/etc/svc/volatile/repository_door ]] && break
		sleep 5
	done

	if (( i == 9 )) && \
	    [[ ! -r $ZONEROOT/etc/svc/volatile/repository_door ]]
	then
		#
		# The zone never booted, something is wrong.
		#
		error "$e_nosmf"
		error "$e_bootfail"
		/usr/bin/rm -f $SMFTMPFILE
		return 1
	fi

	insttmpfile=$(mktemp -t instsmf.XXXXXX)
	if [[ $? == 1 || -z "$insttmpfile" ]]; then
		error "$e_tmpfile"
		/usr/bin/rm -f $SMFTMPFILE
		return 1
	fi

	vlog "$v_rmhollowsvcs"
        while read fmri mfst
	do
		# Delete the svc.
		vlog "$v_delsvc" "$fmri"
		echo "/usr/sbin/svccfg delete -f $fmri"
		echo "/usr/sbin/svccfg delhash -d $mfst"
		echo "rm -f $mfst"
	done < $SMFTMPFILE > $ZONEROOT/tmp/smf_rm

	/usr/sbin/zlogin -S $ZONENAME /bin/sh /tmp/smf_rm >/dev/null 2>&1

	/usr/bin/rm -f $SMFTMPFILE

	# Get a list of the svcs that now exist in the zone.
	LANG=C /usr/sbin/zlogin -S $ZONENAME /usr/bin/svcs -aH | \
	    /usr/bin/nawk '{print $3}' >>$insttmpfile

	[[ -n $LOGFILE ]] && \
	    printf "[$(date)] ${MSG_PREFIX}${v_svcsinzone}\n" >&2
	[[ -n $LOGFILE ]] && cat $insttmpfile >&2

	#
	# Fix network services if shared stack.
	#
	if [[ "$STACK_TYPE" == "shared" ]]; then
		vlog "$v_fixnetsvcs"

		NETPHYSDEF="svc:/network/physical:default"
		NETPHYSNWAM="svc:/network/physical:nwam"

		/usr/bin/egrep -s "$NETPHYSDEF" $insttmpfile
		if (( $? == 0 )); then
			vlog "$v_enblsvc" "$NETPHYSDEF"
			/usr/sbin/zlogin -S $ZONENAME \
			    /usr/sbin/svcadm enable $NETPHYSDEF || \
			    error "$e_dissvc" "$NETPHYSDEF"
		fi

		/usr/bin/egrep -s "$NETPHYSNWAM" $insttmpfile
		if (( $? == 0 )); then
			vlog "$v_dissvc" "$NETPHYSNWAM"
			/usr/sbin/zlogin -S $ZONENAME \
			    /usr/sbin/svcadm disable $NETPHYSNWAM || \
			    error "$e_enblsvc" "$NETPHYSNWAM"
		fi

		for svc in $(/usr/bin/egrep network/routing $insttmpfile)
		do
			# Disable the svc.
			vlog "$v_dissvc" "$svc"
			/usr/sbin/zlogin -S $ZONENAME \
			    /usr/sbin/svcadm disable $svc || \
			    error "$e_dissvc" $svc
		done
	fi

	#
	# Disable well-known services that don't run in a zone.
	#
	vlog "$v_rminvalidsvcs"
	for svc in $(/usr/bin/egrep -hv "^#" \
	    /usr/lib/brand/solaris10/smf_disable.lst \
	    /etc/brand/solaris10/smf_disable.conf)
	do
		# Skip svcs not installed in the zone.
		/usr/bin/egrep -s "$svc:" $insttmpfile || continue

		# Disable the svc.
		vlog "$v_dissvc" "$svc"
		/usr/sbin/zlogin -S $ZONENAME /usr/sbin/svcadm disable $svc || \
		    error "$e_dissvc" $svc
	done

	#
	# Since zones can't be NFS servers, disable all of the instances of
	# the shares svc.
	#
	for svc in $(/usr/bin/egrep network/shares/group $insttmpfile)
	do
		vlog "$v_dissvc" "$svc"
		/usr/sbin/zlogin -S $ZONENAME /usr/sbin/svcadm disable $svc || \
		    error "$e_dissvc" $svc
	done

	/usr/bin/rm -f $insttmpfile

	return 0
}

#
# Remove well-known pkgs that do not work inside a zone.
#
function rm_pkgs {
	/usr/bin/cat <<-EOF > $ZONEROOT/tmp/admin || fatal "$e_adminf"
	mail=
	instance=overwrite
	partial=nocheck
	runlevel=nocheck
	idepend=nocheck
	rdepend=nocheck
	space=nocheck
	setuid=nocheck
	conflict=nocheck
	action=nocheck
	basedir=default
	EOF

	for i in $(/usr/bin/egrep -hv "^#" /usr/lib/brand/solaris10/pkgrm.lst \
	    /etc/brand/solaris10/pkgrm.conf)
	do
		[[ ! -d $ZONEROOT/var/sadm/pkg/$i ]] && continue

		vlog "$v_rmpkg" "$i"
		/usr/sbin/zlogin -S $ZONENAME \
		    /usr/sbin/pkgrm -na /tmp/admin $i >&2 || error "$e_rmpkg" $i
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
function warn_zones {
	zoneconfig=$ZONEROOT/etc/zones

	[[ ! -d $zoneconfig ]] && return

	if [[ -h $zoneconfig/index || ! -f $zoneconfig/index ]]; then
		error "$e_badfile" "/etc/zones/index"
		return
	fi

	NGZ=$(/usr/bin/nawk -F: '{
		if (substr($1, 0, 1) == "#" || $1 == "global")
			continue

		if ($2 == "installed")
			printf("%s ", $1)
	}' $zoneconfig/index)

	# Return if there are no installed zones to warn about.
	[[ -z "$NGZ" ]] && return

	log "$v_rmzones" "$NGZ"

	NGZP=$(/usr/bin/nawk -F: '{
		if (substr($1, 0, 1) == "#" || $1 == "global")
			continue

		if ($2 == "installed")
			printf("%s ", $3)
	}' $zoneconfig/index)

	log "$v_rmzonepaths"

	for i in $NGZP
	do
		log "    %s" "$i"
	done
}

#
# ^C Should cleanup; if the zone is running, it should try to halt it.
#
integer zone_is_running=0
trap trap_cleanup INT

# used by start_log
set -A save_args "$0" "$@"

#
# Parse the command line options.
#
OPT_U=
OPT_V=
OPT_M=
OPT_L=
sc_sysidcfg=
while getopts "c:uvm:" opt
do
	case "$opt" in
		c)	sc_sysidcfg="$OPTARG" ;;
		u)	OPT_U="-u";;
		v)	OPT_V="-v";;
		m)	MSG_PREFIX="$OPTARG"; OPT_M="-m \"$OPTARG\"";;
		*)	usage;;
	esac
done
shift OPTIND-1

(( $# < 1 )) && usage

(( $# > 2 )) && usage

[[ -n $LOGFILE ]] && exec 2>>$LOGFILE

init_zone zone "$1" "$2"
eval $(bind_legacy_zone_globals zone)

# Clear the child dataset list - solaris10 should not create them.
set -A zone.new_be_datasets

start_log zone install "${save_args[@]}"

e_badinfo=$(gettext "Failed to get '%s' zone resource")
e_badfile=$(gettext "Invalid '%s' file within the zone")
v_invalidiface=$(gettext "Invalid interface name in '%s'")
v_noresource=$(gettext "No zonecfg net or anet resource found for %s")
v_nonethostname=$(gettext \
    "No /etc/hostname\* file found for zonecfg net physical=%s")
v_noanethostname=$(gettext \
    "No /etc/hostname\* file found for zonecfg anet linkname=%s")
v_adjust=$(gettext "Updating the image to run within a zone")
v_stacktype=$(gettext "Stack type '%s'")
v_booting=$(gettext "Booting zone to single user mode")
e_bootfail=$(gettext "Failed to boot zone to single user mode.")
e_nosmf=$(gettext "SMF repository unavailable.")
v_svcsinzone=$(gettext "The following SMF services are installed:")
v_rmhollowsvcs=$(gettext "Deleting SMF services from hollow packages")
v_fixnetsvcs=$(gettext "Adjusting network SMF services")
v_rminvalidsvcs=$(gettext "Disabling invalid SMF services")
v_delsvc=$(gettext "Delete SMF svc '%s'")
v_enblsvc=$(gettext "Enable SMF svc '%s'")
e_enblsvc=$(gettext "enabling SMF svc '%s'")
v_dissvc=$(gettext "Disable SMF svc '%s'")
e_dissvc=$(gettext "disabling SMF svc '%s'")
e_adminf=$(gettext "Unable to create admin file")
v_rmpkg=$(gettext "Remove package '%s'")
e_rmpkg=$(gettext "removing package '%s'")
v_rmzones=$(gettext "The following zones in this image will be unusable: %s")
v_rmzonepaths=$(gettext "These zonepaths could be removed from this image:")
v_halting=$(gettext "Halting zone")
v_sc_config=$(gettext "Copying sysconfig file to zone")
v_migrate_export=$(gettext "Migrating /export out of boot environment")
e_shutdown=$(gettext "Shutting down zone %s...")
e_badhalt=$(gettext "Zone halt failed")
v_exitgood=$(gettext "Postprocessing successful.")
e_exitfail=$(gettext "Postprocessing failed.")

#
# Do some validation on the paths we'll be accessing
#
safe_dir /etc
safe_dir /var
safe_dir /var/sadm
safe_dir /var/sadm/install
safe_dir /var/sadm/pkg
safe_opt_dir /etc/dfs
safe_opt_dir /etc/lu
safe_opt_dir /etc/zones

mk_zone_dirs

# Now do the work to update the zone.

# Check for zones inside of image.
warn_zones

log "$v_adjust"

#
# Any errors in these functions are not considered fatal.  The zone can be
# be fixed up manually afterwards and it may need some additional manual
# cleanup in any case.
#

STACK_TYPE=$(/usr/sbin/zoneadm -z $ZONENAME list -p | \
    /usr/bin/nawk -F: '{print $7}')
if (( $? != 0 )); then
	error "$e_badinfo" "stacktype"
fi
vlog "$v_stacktype" "$STACK_TYPE"

fix_lu
if [[ -z $OPT_U ]]; then
	fix_net
fi
fix_nfs
fix_vfstab

vlog "$v_booting"

#
# Boot the zone so that we can do all of the SMF updates needed on the zone's
# repository.
#

zone_is_running=1

/usr/sbin/zoneadm -z $ZONENAME boot -f -- -m milestone=none
if (( $? != 0 )); then
	error "$e_badboot"
	/usr/bin/rm -f $SMFTMPFILE
	fatal "$e_exitfail"
fi

get_smf_services_to_delete

#
# Remove all files and directories installed by hollow packages.  Such files
# and directories shouldn't exist inside zones.
#
hollow_pkgs=$(mktemp -t .hollow.pkgs.XXXXXX)
hollow_file_list=$(mktemp $ZONEROOT/.hollow.pkgs.files.XXXXXX)
hollow_dir_list=$(mktemp $ZONEROOT/.hollow.pkgs.dirs.XXXXXX)
[ -f "$hollow_pkgs" -a -f "$hollow_file_list" -a -f "$hollow_dir_list" ] || {
	error "$e_tmpfile"
	rm -f $hollow_pkgs $hollow_file_list $hollow_dir_list
	fatal "$e_exitfail"
}
for pkg_name in $ZONEROOT/var/sadm/pkg/*; do
	grep 'SUNW_PKG_HOLLOW=true' $pkg_name/pkginfo >/dev/null 2>&1 && \
	    basename $pkg_name >>$hollow_pkgs
done
/usr/bin/nawk -v hollowpkgs=$hollow_pkgs -v filelist=$hollow_file_list \
    -v dirlist=$hollow_dir_list '
	BEGIN {
		while (getline p <hollowpkgs > 0)
			pkgs[p] = 1;
		close(hollowpkgs);
	}
	{
		# fld is the field where the pkg names begin.
		# nm is the file/dir entry name.
		if ($2 == "f") {
			fld=10;
			nm=$1;
		} else if ($2 == "d") {
			fld=7;
			nm=$1;
		} else if ($2 == "s" || $2 == "l") {
			fld=4;
			split($1, a, "=");
			nm=a[1];
		} else {
			next;
		}

		# Determine whether the file or directory is delivered by any
		# non-hollow packages.  Files and directories can be
		# delivered by multiple pkgs.  The file or directory should only
		# be removed if it is only delivered by hollow packages.
		for (i = fld; i <= NF; i++) {
			if (pkgs[get_pkg_name($i)] != 1) {
				# We encountered a non-hollow package.  Skip
				# this entry.
				next;
			}
		}

		# The file or directory is only delivered by hollow packages.
		# Mark it for removal.
		if (fld != 7)
			print nm >>filelist
		else
			print nm >>dirlist
	}

	# Get the clean pkg name from the fld entry.
	function get_pkg_name(fld) {
		# Remove any pkg control prefix (e.g. *, !)
		first = substr(fld, 1, 1)
		if (match(first, /[A-Za-z]/)) {
			pname = fld
		} else {
			pname = substr(fld, 2)
		}

		# Then remove any class action script name
		pos = index(pname, ":")
		if (pos != 0)
			pname = substr(pname, 1, pos - 1)
                return (pname)
        }
' $ZONEROOT/var/sadm/install/contents
/usr/sbin/zlogin -S $ZONENAME "cat /$(basename $hollow_file_list) | xargs rm -f"
/usr/sbin/zlogin -S $ZONENAME "sort -r /$(basename $hollow_dir_list) | \
    xargs rmdir >/dev/null 2>&1"
rm -f $hollow_pkgs $hollow_file_list $hollow_dir_list

# cleanup SMF services
fix_smf || failed=1

# remove invalid pkgs
[[ -z $failed ]] && rm_pkgs

if [[ -z $failed && -n $OPT_U ]]; then
	vlog "$v_unconfig"

	sysunconfig_zone
	if (( $? != 0 )); then
		failed=1
	fi
fi

# Migrate /export to the non-BE dataset(s).
vlog "$v_migrate_export"
migrate_export zone
migrate_rpool zone

vlog "$v_halting"
/usr/sbin/zoneadm -z $ZONENAME halt
if (( $? != 0 )); then
	error "$e_badhalt"
	failed=1
fi
zone_is_running=0

# Copy in sysidcfg file after zone is halted
if [[ -n $sc_sysidcfg ]]; then
	vlog "$v_sc_config"
	safe_copy $sc_sysidcfg $ZONEROOT/etc/sysidcfg
fi

if [[ -n $failed ]]; then
	fatal "$e_exitfail"
fi

vlog "$v_exitgood"
finish_log zone
exit 0
