#! /usr/bin/sh
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
# Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
#
#
# This is a clean script for removable disks
# 
# Following is the syntax for calling the script:
#	scriptname [-s|-f|-i|-I] devicename [-A|-D] username zonename zonepath
#
#    	-s for standard cleanup by a user
# 	-f for forced cleanup by an administrator
# 	-i for boot-time initialization (when the system is booted with -r) 
# 	-I to suppress error/warning messages; the script is run in the '-i'
#	   mode
#
# $1:	devicename - device to be allocated/deallocated, e.g., sr0
#
# $2:	-A if cleanup is for allocation, or -D if cleanup is for deallocation.
#
# $3:	username - run the script as this user, rather than as the caller.
#
# $4:	zonename - zone in which device to be allocated/deallocated
#
# $5:	zonepath - root path of zonename
#
# A clean script for a removable media device should prompt the user to 
# insert correctly labeled media at allocation time, and ensure that the
# media is ejected at deallocation time.
#
# Unless the clean script is being called for boot-time
# initialization, it may communicate with the user via stdin and
# stdout.  To communicate with the user via CDE dialogs, create a
# script or link with the same name, but with ".windowing" appended.
# For example, if the clean script specified in device_allocate is
# /etc/security/xyz_clean, that script must use stdin/stdout.  If a
# script named /etc/security/xyz_clean.windowing exists, it must use
# dialogs.  To present dialogs to the user, the dtksh script
# /etc/security/lib/wdwmsg may be used.
#
# This particular script, disk_clean, will work using stdin/stdout, or
# using dialogs.  A symbolic link disk_clean.windowing points to
# disk_clean.
#
 
# ####################################################
# ################  Local Functions  #################
# ####################################################
 
#
# Set up for windowing and non-windowing messages
#
msg_init()
{
    if [ `basename $0` != `basename $0 .windowing` ]; then
	WINDOWING="yes"
	case $MEDIATYPE in
	  cdrom)   TITLE="CD-ROM";;
	  rmdisk)  TITLE="Removable Disk";;
	  floppy)  TITLE="Floppy";;
	  *)       TITLE="Disk";;
	esac
	
	if [ "$MODE" = "allocate" ]; then
	    TITLE="$TITLE Allocation"
	else
	    TITLE="$TITLE Deallocation"
	fi
    else
	WINDOWING="no"
    fi
}

#
# Display a message for the user.  For windowing, user must press OK button 
# to continue. For non-windowing, no response is required.
#
msg() {
    if [ "$WINDOWING" = "yes" ]; then
	$WDWMSG "$*" "$TITLE" OK
    elif [ "$silent" != "y" ]; then
	echo "$*" > /dev/${MSGDEV}
    fi
}

ok_msg() {
	if [ "$WINDOWING" = "yes" ]; then
		$WDWMSG "$*" "$TITLE" READY
	else
		form=`gettext "Media in %s is ready. Please store safely."`
		printf "${form}\n" $DEVICE > /dev/${MSGDEV}
	fi
}

error_msg() {
	if [ "$WINDOWING" = "yes" ]; then
		$WDWMSG "$*" "$TITLE" ERROR
	else
		form=`gettext "%s: Error cleaning up device %s."`
		printf "${form}\n" $PROG $DEVICE > /dev/${MSGDEV}
	fi
}

#
# Ask the user an OK/Cancel question.  Return 0 for OK, 1 for Cancel.
#
okcancel() {
    if [ "$WINDOWING" = "yes" ]; then
	$WDWMSG "$*" "$TITLE" OK Cancel
    elif [ "$silent" != "y" ]; then
	get_reply "$* (y to continue, n to cancel) \c" y n
    fi
}

#
# Ask the user an Yes/No question.  Return 0 for Yes, 1 for No
#
yesno() {
    if [ "$WINDOWING" = "yes" ]; then
	$WDWMSG "$*" "$TITLE" Yes No
    elif [ "$silent" != "y" ]; then
	get_reply "$* (y/n) \c" y n
    fi
}

#
# Display an error message, put the device in the error state, and exit.
#
error_exit() {
	if [ "$silent" != "y" ]; then
		msg "$2" "$3" \
		    "\n\nDevice has been placed in allocation error state." \
		    "\nPlease inform system administrator."
	fi
	exit $DEVCLEAN_ERROR
}

#
# get_reply prompt choice ...
#
get_reply() {
	prompt=$1; shift
	while true
	do
		echo $prompt > /dev/tty
		read reply
		i=0
		for choice in $*
		do
			if [ "$choice" = "$reply" ]
			then
				return $i
			else
				i=`expr $i + 1`
			fi
		done
	done
}

#
# Display device access information after completion
# of "device file" type allocations.
#
alloc_device_msg() {
	/usr/sbin/dminfo -v -n $DEVICE | \
	    IFS=":" read device_name device_type device_list
	# sort the device_list for readability
	if [ "$MEDIATYPE" == "floppy" ]; then
		device_list="`echo $device_list | \
		    /usr/bin/tr ' ' '\n' | \
		    /usr/bin/sort | \
		    /usr/bin/tr -d ' '`"
	else
		device_list="`echo $device_list | \
		    /usr/bin/tr ' ' '\n' | \
		    /usr/bin/sed 's/\([sp]\)\([0-9]*\)$/ \1 \2/;' | \
		    /usr/bin/sort -t ' ' -k 1,1d -k 2,2d -k 3,3n | \
		    /usr/bin/tr -d ' '`"
	fi
	text1=`gettext "Device %s allocated for user %s in %s zone."`
	text2=`gettext "For direct access use device files."`
	column1=`gettext "Device Files"`
	if [ "$WINDOWING" = "yes" ]; then
		/usr/bin/zenity --width=400 --list \
		    --title="$TITLE" \
		    --text="`printf "$text1\n$text2" \
		    "$DEVICE" "$USER_NAME" "$ZONE_NAME"`" \
		    --column="$column1" \
		    $device_list \
		    >/dev/null 2>&1
	else
		printf "$text1\n" "$DEVICE" "$USER_NAME" "$ZONE_NAME" \
		    > /dev/${MSGDEV}
		printf "$text2\n" > /dev/${MSGDEV}
		printf "%s " $device_list > /dev/${MSGDEV}
		printf "\n" > /dev/${MSGDEV}
	fi
	return 0
}

#
# Display device access information after completion
# of "volume mount" type allocations.
#
alloc_mount_msg() {
	# remove extra output from rmmount
	mount_list=`echo "$@" | \
	    /usr/bin/sed -n -e 's/^[^ ]* \([^ ]*\) mounted at \(.*\)$/\1 \2/p'`
	# remove leading zone path for display of non-global mountpoint
	if [ "$ZONE_NAME" != "global" ]; then
		mount_list=`echo "$mount_list" | \
		    while IFS=" " read volume mountpoint ; \
		    do
			echo "$mountpoint" | \
			    /usr/bin/sed -e "s:^$ZONE_PATH/:/:" | \
			    read mountpoint
			echo "$volume" "$mountpoint"
		    done`
	fi
	text1=`gettext "Device %s mounted for user %s in %s zone."`
	text2=`gettext "Press OK to start File Browser."`
	text3=`gettext "Press Cancel to finish."`
	column1=`gettext "Volume"`
	column2=`gettext "Mount Point"`
	if [ "$WINDOWING" = "yes" ]; then
		echo "$mount_list" | \
		    while IFS=" " read volume mountpoint ; \
		    do
			# print one table cell per line for zenity
			printf "%s\n" "$volume" "$mountpoint"
		    done | \
		    /usr/bin/zenity --width=400 --list \
		    --title="$TITLE" \
		    --text="`printf "$text1\n$text2\n$text3" \
		    "$DEVICE" "$USER_NAME" "$ZONE_NAME"`" \
		    --column="$column1" --column="$column2" \
		    >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			echo "$mount_list" | \
			    while IFS=" " read volume mountpoint ; \
			    do
				# launch file browser for the mountpoint
				if [ "$ZONE_NAME" == "global" ]; then
					/usr/bin/gnome-open "$mountpoint" \
					    >/dev/null 2>&1
				else
					/usr/sbin/zlogin -l $USER_NAME \
					    $ZONE_NAME \
					    "DISPLAY=$DISPLAY ; \
					    export DISPLAY ; \
					    LANG=$LANG ; export LANG ; \
					    /usr/bin/gnome-open \
					    \"$mountpoint\"" \
					    >/dev/null 2>&1
				fi
			    done
		fi
	else
		printf "$text1\n" "$DEVICE" "$USER_NAME" "$ZONE_NAME" \
		    > /dev/${MSGDEV}
		printf "%-24s %s\n" "$column1" "$column2" > /dev/${MSGDEV}
		echo "$mount_list" | \
		    while IFS=" " read volume mountpoint ; \
		    do
			printf "%-24s %s\n" "$volume" "$mountpoint" \
			    > /dev/${MSGDEV}
		    done
	fi
	return 0
}

#
# Find the HAL storage UDI corresponding to a Device Allocation device
#
find_udi()
{
	da_device=$1
	/usr/sbin/dminfo -v -n $da_device | \
	    IFS=":" read da_device da_type da_files
	# The list of files in device_maps(4) is in an unspecified order.
	# To speed up the scanning below in most cases, filter the list
	# as follows:
	# First, try to select cdrom/rmdisk block devices:
	# 1) Select only block device files of the form "/dev/dsk/*".
	# 2) Sort the list of files in an order more likely to yield
	#    matches: first the fdisk(1M) partitions ("/dev/dsk/cNtNdNpN")
	#    then the format(1M) slices ("/dev/dsk/cNtNdNsN"), in ascending
	#    numeric order within each group.
	# If this list was empty, try to select floppy block devices, by
	# selecting only files of the form "/dev/disketteN"
	da_block_files="`echo $da_files | \
	    /usr/bin/tr ' ' '\n' | \
	    /usr/bin/sed '/^\/dev\/dsk\//!d; s/\([sp]\)\([0-9]*\)$/ \1 \2/;' | \
	    /usr/bin/sort -t ' ' -k 2,2d -k 3,3n | \
	    /usr/bin/tr -d ' '`"
	if [ -z "$da_block_files" ]; then
		da_block_files="`echo $da_files | \
		    /usr/bin/tr ' ' '\n' | \
		    /usr/bin/grep '^/dev/diskette[0-9][0-9]*'`"
	fi
	for da_file in $da_block_files ; do
		hal_udi_list="`/usr/sbin/hal-find-by-property \
		    --key block.device --string $da_file 2>/dev/null`"
		if [ $? -eq 0 ]; then
			for hal_udi in $hal_udi_list ; do
				hal_category=`/usr/sbin/hal-get-property \
				    --udi $hal_udi --key info.category \
				    2>/dev/null`
				if [ $? -eq 0 ] && \
				    [ "$hal_category" = "storage" ]; then
					echo "$hal_udi"
					return 0
				fi
			done
		fi
	done
	return 1
}

#
# Try to find a HAL property on a UDI.
# If found, print property value and return 0.
# If not found, return 1.
#
hal_get_prop()
{
	hal_prop=`/usr/sbin/hal-get-property --udi $1 --key $2 2>/dev/null`
	if [ $? -eq 0 ] && [ -n "$hal_prop" ]; then
		echo "$hal_prop"
		return 0
	fi
	return 1
}

#
# Find the HAL UDIs with a matching property key=value pair
#
hal_find_by_prop()
{
	udi_list=`/usr/sbin/hal-find-by-property \
	    --key $1 --string $2 2>/dev/null`
	if [ $? -eq 0 ] && [ -n "$udi_list" ]; then
		echo "$udi_list"
		return 0
	fi
	return 1
}

#
# Find the HAL storage UDI for a legacy symdev
#
find_storage()
{
	hal_find_by_prop storage.solaris.legacy.symdev "$1"
}

#
# Find the HAL volume UDIs corresponding to a HAL storage UDI
#
find_volumes()
{
	hal_find_by_prop block.storage_device "$1"
}

#
# Find the volume device for a HAL volume UDI
#
find_volume_device()
{
	hal_get_prop $1 block.device
}

#
# Find the volume fstype for a HAL volume UDI
#
find_volume_fstype()
{
	hal_get_prop $1 volume.fstype
}

#
# Find the volume label for a HAL volume UDI
#
find_volume_label()
{
	hal_get_prop $1 volume.label
}

#
# Find the partition slice for a HAL volume UDI
#
find_volume_slice()
{
	hal_get_prop $1 block.solaris.slice
}

#
# Find the mount point for a HAL volume UDI
#
find_mount_point()
{
	hal_get_prop $1 volume.mount_point
}

#
# Device Allocation behavior for rmmount:
# Process all volumes of a device one at a time, so that mount options
# for each one can be specified individually.
#
do_rmmount()
{
	if [ $# -ge 1 ] && [ "$1" == "-u" ]; then
		mount_mode=unmount
		shift
	else
		mount_mode=mount
	fi

	if [ $# -ge 2 ] && [ "$1" == "-o" ]; then
		mount_opts="$2"
		shift 2
	else
		mount_opts=""
	fi

	if [ $# -ge 1 ]; then
		symdev="$1"
		shift
	else
		return 1
	fi

	if [ $# -ge 1 ] && [ $mount_mode == mount ]; then
		mount_path="$1"
		shift
	fi

	[ $# -gt 0 ] && return 1

	hal_storage=`find_storage $symdev` || return 1

	hal_volumes=`find_volumes $hal_storage` || return 1
	hal_volumes=`echo "$hal_volumes" | /usr/bin/sort`

	mount_return=1
	for hal_volume in $hal_volumes; do
		block_device=`find_volume_device $hal_volume`
		if [ $? -ne 0 ]; then
			continue
		fi
		case $mount_mode in
		mount)
			vol_fstype=`find_volume_fstype $hal_volume`
			vol_opts=""
			if [ "$vol_fstype" == "pcfs" ]; then
				vol_opts="owner=$USER_NAME,group=$USER_GROUP"
			fi
			if [ -n "$mount_opts" ]; then
				if [ -n "$vol_opts" ]; then
					vol_opts="$mount_opts,$vol_opts"
				else
					vol_opts="$mount_opts"
				fi
			fi
			if [ -n "$vol_opts" ]; then
				vol_opts="-o $vol_opts"
			fi
			volume_label=`find_volume_label $hal_volume` ||
			    volume_label="$DEVICE"
			volume_label=`echo "$volume_label" | \
			    /usr/xpg4/bin/tr '/;|[:cntrl:][:space:]' '_____'`
			volume_slice=`find_volume_slice $hal_volume`
			if [ -n "$volume_slice" ]; then
				mount_attempts=2
			else
				mount_attempts=1
			fi
			i=0
			while [ $i -lt $mount_attempts ]; do
				if [ $i -eq 0 ]; then
					volume_dir="$volume_label"
				else
					volume_dir="$volume_label-$volume_slice"
				fi
				mount_point="$mount_path/$volume_dir"
				printf "%s " $symdev
				/usr/sbin/rmmount $vol_opts \
				    $block_device "$mount_point"
				if [ $? -eq 0 ]; then
					mount_return=0
					break
				fi
				i=$(( $i + 1 ))
			done
			;;
		unmount)
			mount_point=`find_mount_point $hal_volume`
			if [ $? -ne 0 ]; then
				continue
			fi
			printf "%s " $symdev
			/usr/sbin/rmmount -u "$mount_point"
			if [ $? -eq 0 ]; then
				mount_return=0
			fi
			rmdir "$mount_point" 2>/dev/null
			;;
		esac
	done

	return $mount_return
}

#
# Allocate a device.
# Ask the user to make sure the disk is properly labeled.
# Ask if the disk should be mounted.
#
do_allocate()
{
	if [ "$MEDIATYPE" = "floppy" ]; then
		# Determine if media is in drive
		eject_msg="`eject -q $SYMDEV 2>&1`"
		eject_status="$?"
		case $eject_status in
		1) # Media is not in drive
			okcancel "Insert disk in $DEVICE."
			if [ $? != 0 ]; then
				exit $DEVCLEAN_OK
			fi;;
		3) # Error 
			error_exit $DEVICE \
			    "Error checking for media in drive.";;
		esac
	else
		okcancel "Insert disk in $DEVICE."
		if [ $? != 0 ]; then
			exit $DEVCLEAN_OK
		fi
	fi
    
	yesno "Do you want $DEVICE mounted?"
	if [ $? != 0 ]; then
		alloc_device_msg
		exit $DEVCLEAN_OK
	fi

	if [ -d "$MOUNT_DIR" ]; then
		rmdir "$MOUNT_DIR"/* > /dev/null 2>&1
	else
		mkdir -p "$MOUNT_DIR"
	fi
	chown $USER_NAME "$MOUNT_DIR"
	chmod 700 "$MOUNT_DIR"

	# Do the actual mount.
	rmmount_msg="`do_rmmount $SYMDEV $MOUNT_DIR 2>&1`"
	rmmount_status="$?"
	if [ $rmmount_status -eq 0 ]; then
		EXIT_STATUS=$DEVCLEAN_MOUNTOK
	elif [ $rmmount_status -gt 0 -a $MEDIATYPE != cdrom ]; then
		# Try again in readonly mode. cdrom is always mounted ro, so
		# no need to try again.
		echo "Read-write mount of $DEVICE failed. Mounting read-only."
		rmmount_msg="`do_rmmount -o ro $SYMDEV $MOUNT_DIR 2>&1`"
		if [ $? -eq 0 ]; then
			EXIT_STATUS=$DEVCLEAN_MOUNTOK
		else
			EXIT_STATUS=$DEVCLEAN_BADMOUNT
		fi
	else
		EXIT_STATUS=$DEVCLEAN_BADMOUNT
	fi
	if [ $EXIT_STATUS -eq $DEVCLEAN_MOUNTOK ]; then
		alloc_mount_msg "$rmmount_msg"
	elif [ $EXIT_STATUS -eq $DEVCLEAN_BADMOUNT ]; then
		error_msg "Mount of $DEVICE failed. Device not allocated."
	fi

	# Set permissions on directory used by vold, sdtvolcheck, etc.
	if [ -d /tmp/.removable ]; then
		chown root /tmp/.removable
		chmod 777 /tmp/.removable
	fi
}


do_deallocate()
{
	if mount | /usr/xpg4/bin/grep -q "^${MOUNT_DIR}/" ; then
		# Do the actual unmount.
		rmmount_msg="`do_rmmount -u $SYMDEV 2>&1`"
		rmmount_status="$?"
		rmdir "$MOUNT_DIR"/* > /dev/null 2>&1
		rmdir "$MOUNT_DIR" > /dev/null 2>&1
	else
		rmmount_status=0
	fi

	case $rmmount_status in
	1) # still mounted
		error_exit $DEVICE "Error unmounting $DEVICE" "$rmmount_msg";;
	0) # not mounted
		# Eject the media
		if [ "$FLAG" = "f" ] ; then
			eject_msg="`eject -f $SYMDEV 2>&1`"
		else
			eject_msg="`eject $SYMDEV 2>&1`"
		fi
		eject_status="$?"
		case $eject_status in
		0|1|4) # Media has been ejected
			case $MEDIATYPE in
			floppy|cdrom|rmdisk)
				msg "Please remove the disk from $DEVICE.";;
			esac;;
		3) # Media didn't eject
			msg $DEVICE "Error ejecting disk from $DEVICE" \
			    "$eject_msg";;
		esac
	esac
}

#
# Reclaim a device
#
do_init()
{
	eject_msg="`eject -f $SYMDEV 2>&1`"
	eject_status="$?"

	rmdir "$MOUNT_DIR"/* > /dev/null 2>&1
	rmdir "$MOUNT_DIR" > /dev/null 2>&1

	case $eject_status in
	0) # Media has been ejected 
		if [ "$silent" != "y" ]; then
			ok_msg
		fi
		exit $DEVCLEAN_OK;;
	1) # Media not ejected
		if [ "$silent" != "y" ]; then
			error_msg
		fi
		exit $DEVCLEAN_OK;;
	3) # Error 
		if [ "$silent" != "y" ]; then
			error_msg
		fi
		msg $DEVICE "Error ejecting disk from $DEVICE" \
		"$eject_msg"
		exit $DEVCLEAN_SYSERR;;
	esac
}


# ####################################################
# ################ Begin main program ################
# ####################################################

trap "" INT TERM QUIT TSTP ABRT

# Device clean program exit codes
DEVCLEAN_OK=0
DEVCLEAN_ERROR=1
DEVCLEAN_SYSERR=2
DEVCLEAN_BADMOUNT=3
DEVCLEAN_MOUNTOK=4

EXIT_STATUS=$DEVCLEAN_OK

PATH="/usr/bin:/usr/sbin"
WDWMSG="/etc/security/lib/wdwmsg"
USAGE="Usage: disk_clean [-s|-f|-i|-I] devicename -[A|D] [username] [zonename] [zonepath]"

FLAG=i
#
# Parse the command line arguments
#
while getopts ifsI c
do
	case $c in
	i)
		FLAG=$c;;
	f)
		FLAG=$c;;
	s)
		FLAG=$c;;
	I)
		FLAG=i
		silent=y;;
	\?)
		echo $USAGE
		exit $DEVCLEAN_ERROR;;
      esac
done

shift `expr $OPTIND - 1`

DEVICE=$1

MODE="deallocate"
if [ "$2" = "-A" ]; then
	MODE="allocate"
elif [ "$2" = "-D" ]; then
	MODE="deallocate"
fi

if [ -n "$3" ]; then
	USER_NAME=$3
else
	USER_NAME=`/usr/xpg4/bin/id -u -nr`
fi
USER_GROUP=`/usr/bin/id -g -n $USER_NAME`

ZONE_NAME=$4

ZONE_PATH=$5

# e.g., "joeusr-cdrom0"
user_media="${USER_NAME}-${DEVICE}"
if [ "$ZONE_NAME" == "global" ] || [ "$ZONE_NAME" == "" ]; then
	MOUNT_DIR="/media/${user_media}"
else
	MOUNT_DIR="$ZONE_PATH/media/${user_media}"
fi

UDI=`find_udi $DEVICE`
SYMDEV=`hal_get_prop $UDI storage.solaris.legacy.symdev`
MEDIATYPE=`hal_get_prop $UDI storage.solaris.legacy.media_type`

msg_init

if [ "$MODE" = "allocate" ]; then
	MSGDEV=tty
  	do_allocate
else
    if [ "$FLAG" = "i" ] ; then
	MSGDEV=console
	do_init
    else
	MSGDEV=tty
	do_deallocate
    fi
fi

exit $EXIT_STATUS
