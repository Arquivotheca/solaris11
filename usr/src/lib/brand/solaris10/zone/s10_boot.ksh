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
# s10 boot script.
#
# The arguments to this script are the zone name and the zonepath.
#

. /usr/lib/brand/solaris10/common.ksh

ZONENAME=$1
ZONEPATH=$2
ZONEROOT=$ZONEPATH/root

w_missing=$(gettext "Warning: \"%s\" is not installed in the global zone\n")

arch=$(uname -p)
if [ "$arch" = "i386" ]; then
	ARCH32=i86
        ARCH64=amd64
elif [ "$arch" = "sparc" ]; then
        ARCH32=sparcv7
        ARCH64=sparcv9
else
        echo "Unsupported architecture: $arch"
        exit 2
fi

#
# Run cluster hook
#
call_cluster_hook boot "$@"  || exit $?

#
# Run the s10_support boot hook.
#
/usr/lib/brand/solaris10/s10_support boot $ZONENAME
if (( $? != 0 )); then
        exit 1
fi

BRANDDIR=/.SUNWnative/usr/lib/brand/solaris10;
FILEDIR=$BRANDDIR/files;
EXIT_CODE=1

#
# Replace the specified file in the booting zone with a wrapper script that
# invokes s10_isaexec_wrapper.  This is a convenience function that reduces
# clutter and code duplication.
#
# Parameters:
#	$1	The full path of the file to replace (e.g., /sbin/ifconfig)
#	$2	The access mode of the replacement file in hex (e.g., 0555)
#	$3	The name of the replacement file's owner (e.g., root:bin)
#	$4	Optional: the full path of the native file
#
# NOTE: The checks performed in the 'if' statement below are not generic: they
# depend on the success of the zone filesystem structure validation performed
# above to ensure that intermediate directories exist and aren't symlinks.
#
replace_with_native() {
	typeset path_dname="$ZONEROOT/"$(dirname $1)
	typeset native_path="$4"

	if [[ -n $native_path ]]; then
		[[ ! -f $native_path ]] && printf "$w_missing" \
		    "$native_path"
		native_path=$(dirname $4)
	else
		[[ ! -f $1 ]] && printf "$w_missing" "$1"
	fi

	if [ ! -h $path_dname -a -d $path_dname ]; then
		safe_replace "$ZONEROOT/$1" "$BRANDDIR/s10_isaexec_wrapper" \
		    "$2" "$3" remove "$native_path"
	fi
}

replace_with_native_py() {
	path_dname="$ZONEROOT/"$(dirname $1)

	[ ! -f $1 ] && printf "$w_missing" "$1"

	if [ ! -h $path_dname -a -d $path_dname ]; then
		safe_replace $ZONEROOT/$1 $BRANDDIR/s10_python_wrapper $2 $3 \
		    remove
	fi
}

#
# Create a new wrapper script that invokes s10_isaexec_wrapper in the
# brand (for a non-existing s10c file) pointing to the native brand file.
#
# This function assumes there is no s10 version of the replacement file,
# so there is nothing to back up.
#
# Parameters:
#	$1	The full path of the wrapper file to create
#	$2	The access mode of the replacement file in hex (e.g., 0555)
#	$3	The name of the replacement file's owner (e.g., root:bin)
#
wrap_with_native() {

	[ ! -f $1 ] && printf "$w_missing" "$1"

	path_dname="$ZONEROOT/"$(dirname $1)
	if [ ! -h $path_dname -a -d $path_dname -a ! -f $ZONEROOT/$1 ]; then
		safe_wrap $ZONEROOT/$1 $BRANDDIR/s10_isaexec_wrapper $2 $3
	fi
}

#
# Before we boot we validate and fix, if necessary, the required files within
# the zone.  These modifications can be lost if a patch is applied within the
# zone, so we validate and fix the zone every time it boots.
#

#
# BINARY REPLACEMENT
#
# This section of the boot script is responsible for replacing Solaris 10
# binaries within the booting zone with Nevada binaries.  This is a two-step
# process: First, the directory structure of the zone is validated to ensure
# that binary replacement will proceed safely.  Second, Solaris 10 binaries
# are replaced with Nevada binaries.
#
# Here's an example.  Suppose that you want to replace /usr/bin/zcat with the
# Nevada /usr/bin/zcat binary.  Then you should do the following:
#
#	1.  Go to the section below labeled "STEP ONE" and add the following
#	    two lines:
#
#		safe_dir /usr
#		safe_dir /usr/bin
#
#	    These lines ensure that both /usr and /usr/bin are directories
#	    within the booting zone that can be safely accessed by the global
#	    zone.
#	2.  Go to the section below labeled "STEP TWO" and add the following
#	    line:
#
#		replace_with_native /usr/bin/zcat 0555 root:bin
#
# Details about the binary replacement procedure can be found in the Solaris 10
# Containers Developer Guide.
#

#
# STEP ONE
#
# Validate that the zone filesystem looks like we expect it to.
#
safe_dir /lib
safe_dir /lib/svc
safe_dir /lib/svc/method
safe_dir /lib/svc/share
safe_dir /usr
safe_dir /usr/bin
safe_dir /usr/lib
safe_dir /usr/lib/autofs
safe_dir /usr/lib/fs
safe_dir /usr/lib/fs/autofs
safe_dir /usr/lib/fs/ufs
safe_dir /usr/lib/fs/zfs
safe_dir /usr/lib/inet
safe_dir /usr/lib/zfs
safe_dir /usr/sbin
safe_dir /usr/lib/ipf/$ARCH64
safe_dir /usr/sbin/$ARCH64
safe_dir /sbin
safe_dir /var
safe_dir /var/svc

#
# Some of the native networking daemons such as in.mpathd are
# expected under /lib/inet
#
mkdir -m 0755 -p $ZONEROOT/lib/inet
chown root:bin $ZONEROOT/lib/inet
safe_dir /lib/inet

#
# Some of the native services expect /system/volatile; link it.
# to /etc/svc/volatile.  /var/run will also be mounted on
# /etc/svc/volatile by s10_fs_minimal.  This allows native
# services to open doors in /var/run via system/volatile.
#
safe_dir /system
rm -f $ZONEROOT/system/volatile
ln -s ../etc/svc/volatile $ZONEROOT/system/volatile

#
# STEP TWO
#
# Replace Solaris 10 binaries with Nevada binaries.
#

#
# Replace various network-related programs with native wrappers.
#
replace_with_native /sbin/dhcpagent 0555 root:bin
replace_with_native /sbin/dhcpinfo 0555 root:bin
replace_with_native /sbin/ifconfig 0555 root:bin
replace_with_native /usr/bin/netstat 0555 root:bin
replace_with_native /usr/lib/inet/in.ndpd 0555 root:bin
replace_with_native /usr/sbin/in.routed 0555 root:bin
replace_with_native /usr/sbin/snoop 0555 root:bin
replace_with_native /usr/sbin/if_mpadm 0555 root:bin
replace_with_native /usr/lib/inet/in.mpathd 0555 root:bin /lib/inet/in.mpathd

#
# Replace IPFilter commands with native wrappers
#
replace_with_native /usr/lib/ipf/$ARCH64/ipftest 0555 root:bin
replace_with_native /usr/sbin/$ARCH64/ipf 0555 root:bin
replace_with_native /usr/sbin/$ARCH64/ipfs 0555 root:bin
replace_with_native /usr/sbin/$ARCH64/ipfstat 0555 root:bin
replace_with_native /usr/sbin/$ARCH64/ipmon 0555 root:bin
replace_with_native /usr/sbin/$ARCH64/ipnat 0555 root:bin
replace_with_native /usr/sbin/$ARCH64/ippool 0555 root:bin

#
# Create wrapper at /lib/inet/in.mpathd as well because native ifconfig
# looks up in.mpathd under /lib/inet.
#
wrap_with_native /lib/inet/in.mpathd 0555 root:bin

# Create native wrapper for /sbin/ipmpstat
wrap_with_native /sbin/ipmpstat 0555 root:bin

#
# Create ipmgmtd wrapper to native binary in s10 container
# and copy ipmgmt service method.
#
wrap_with_native /lib/inet/ipmgmtd 0555 root:bin
safe_copy /lib/svc/method/net-ipmgmt \
    $ZONEROOT/lib/svc/method/net-ipmgmt

#
# To handle certain IPMP configurations, we need updated
# net-physical method script and updated net_include.sh
#
filename=$ZONEROOT/lib/svc/method/net-physical
safe_backup $filename $filename.pre_p2v
safe_copy /usr/lib/brand/solaris10/s10_net_physical $filename
filename=$ZONEROOT/lib/svc/share/net_include.sh
safe_backup $filename $filename.pre_p2v
safe_copy /usr/lib/brand/solaris10/s10_net_include.sh $filename
filename=$ZONEROOT/sbin/umountall
safe_backup $filename $filename.pre_p2v
safe_copy /usr/lib/brand/solaris10/s10_umountall $filename

# 
# To make updates to /var, wrap the s10 fs-minimal script.
# Make sure the most recent version of fs minimal is backed up
# so that s10_fs_minimal uses is the up-to-date version.
# 
filename=$ZONEROOT/lib/svc/method/fs-minimal
if [[ ! -f $filename || -h $filename ]] ; then
	fail_fatal "$e_not_file" "$filename"
fi
grep THIS_IS_SOLARIS10_BRAND_FS_MINIMAL $filename >/dev/null 2>&1
if (( $? != 0 )) ; then
	safe_rm $filename.pre_p2v
	safe_backup $filename $filename.pre_p2v
fi
safe_copy /usr/lib/brand/solaris10/s10_fs_minimal $filename
#
# PSARC 2009/306 removed the ND_SET/ND_GET ioctl's for modifying
# IP/TCP/UDP/SCTP/ICMP tunables. If S10 ndd(1M) is used within an
# S10 container, the kernel will return EINVAL. So we need this.
#
replace_with_native /usr/sbin/ndd 0555 root:bin

#
# Replace various ZFS-related programs with native wrappers.  These commands
# either link with libzfs, dlopen libzfs or link with libraries that link
# or dlopen libzfs.  Commands which fall into these categories but which can
# only be used in the global zone are not wrapped.  The libdiskmgt dm_in_use
# code uses libfs, but only the zpool_in_use() -> zpool_read_label() code path.
# That code does not issue ioctls on /dev/zfs and does not need wrapping.
#
replace_with_native /sbin/zfs 0555 root:bin
replace_with_native /sbin/zpool 0555 root:bin
replace_with_native /usr/lib/fs/ufs/quota 0555 root:bin /usr/sbin/quota
replace_with_native /usr/lib/fs/zfs/fstyp 0555 root:bin
replace_with_native /usr/lib/zfs/availdevs 0555 root:bin
replace_with_native /usr/sbin/df 0555 root:bin
replace_with_native /usr/xpg4/bin/df 0555 root:bin
replace_with_native /usr/sbin/zstreamdump 0555 root:bin
replace_with_native_py /usr/lib/zfs/pyzfs.py 0555 root:bin

#
# Replace automount and automountd with native wrappers.
#
replace_with_native /usr/lib/fs/autofs/automount 0555 root:bin
replace_with_native /usr/lib/autofs/automountd 0555 root:bin

#
# Replace rstatd with native, as it depends on struct mib2_ipAddrEntry_t
#
replace_with_native /usr/lib/netsvc/rstat/rpc.rstatd root:bin

# Replace truss and mdb with their native counterparts.  The architecture
# specific versions are updated, as truss and mdb will exec these directly
# when the current binary does not match the target process.
#
replace_with_native /usr/bin/$ARCH32/mdb 0555 root:bin
replace_with_native /usr/bin/$ARCH64/mdb 0555 root:bin
replace_with_native /usr/bin/$ARCH32/truss 0555 root:bin
replace_with_native /usr/bin/$ARCH64/truss 0555 root:bin

#
# The class-specific dispadmin(1M) and priocntl(1) binaries must be native
# wrappers, and we must have all of the ones the native zone does.  This
# allows new scheduling classes to appear without causing dispadmin and
# priocntl to be unhappy.
#
rm -rf $ZONEROOT/usr/lib/class
mkdir $ZONEROOT/usr/lib/class || exit 1

find /usr/lib/class -type d -o -type f | while read x; do
	[ -d $x ] && mkdir -p -m 755 $ZONEROOT$x
	[ -f $x ] && wrap_with_native $x 0555 root:bin
done

#
# END OF STEP TWO
#

#
# Replace add_drv and rem_drv with /usr/bin/true so that pkgs/patches which
# install or remove drivers will work.  NOTE: add_drv and rem_drv are hard
# linked to isaexec so we want to remove the current executable and
# then copy true so that we don't clobber isaexec.
#
filename=$ZONEROOT/usr/sbin/add_drv
[ ! -f $filename.pre_p2v ] && safe_backup $filename $filename.pre_p2v
rm -f $filename
safe_copy $ZONEROOT/usr/bin/true $filename

filename=$ZONEROOT/usr/sbin/rem_drv
[ ! -f $filename.pre_p2v ] && safe_backup $filename $filename.pre_p2v
rm -f $filename
safe_copy $ZONEROOT/usr/bin/true $filename

exit 0
