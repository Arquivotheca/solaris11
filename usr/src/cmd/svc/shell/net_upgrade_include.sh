#!/bin/sh
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
# Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T.
# All rights reserved.
#

SVCPROP=/bin/svcprop
SVCCFG=/usr/sbin/svccfg
SVCADM=/usr/sbin/svcadm

HOSTNAME_UPGRADE_VERSION=2

unset inet_list		# list of IPv4 interfaces.
unset inet6_list	# list of IPv6 interfaces.
unset ipmp_list		# list of IPMP IPv4 interfaces.
unset ipmp6_list	# list of IPMP IPv6 interfaces.
unset inet_plumbed	# list of plumbed IPv4 interfaces.
unset inet6_plumbed	# list of plumbed IPv6 interfaces.
unset ipmp_created	# list of created IPMP IPv4 interfaces.
unset ipmp6_created	# list of created IPMP IPv6 interfaces.
unset inet_failed	# list of IPv4 interfaces that failed to plumb.
unset inet6_failed	# list of IPv6 interfaces that failed to plumb.
unset ipmp_failed	# list of IPMP IPv4 interfaces that failed creation..
unset ipmp6_failed	# list of IPMP IPv6 interfaces that failed creation.

#
# The following functions are the only public functions in this file:
#
# nu_rename_upgraded_file
# nu_upgrade_if
# nu_upgrade_from_etc_files
# nu_etc_files_need_upgrading
# nu_finalize_etc_upgrade
#

#
# nu_get_physical interface
#
# Return physical interface corresponding to the given interface.
#
nu_get_physical()
{
	ORIGIFS="$IFS"
	IFS="${IFS}:"
	set -- $1
	IFS="$ORIGIFS"

	echo $1
}

#
# nu_get_logical interface
#
# Return logical interface number.  Zero will be returned
# if there is no explicit logical number.
#
nu_get_logical()
{
	ORIGIFS="$IFS"
	IFS="${IFS}:"
	set -- $1
	IFS="$ORIGIFS"

	if [ -z "$2" ]; then
		echo 0
	else
		echo $2
	fi
}

#
# nu_comp_if if1 if2
#
# Compare interfaces.  Do the physical interface names and logical interface
# numbers match?
#
nu_comp_if()
{
	nu_comp_physical $1 $2 && [ `nu_get_logical $1` -eq `nu_get_logical $2` ]
}

#
# nu_comp_physical if1 if2
# 
# Do the two interfaces share a physical interface?
#
nu_comp_physical()
{
	[ "`nu_get_physical $1`" = "`nu_get_physical $2`" ]
}

#
# nu_in_list op item list
#
# Is "item" in the given list?  Use "op" to do the test, applying it to
# "item" and each member of the list in turn until it returns success.
#
nu_in_list()
{
	op=$1
	item=$2
	shift 2

	while [ $# -gt 0 ]; do
		$op $item $1 && return 0
		shift
	done

	return 1
}

#
# nu_get_hostname_ipmpinfo interface type
#
# Return all requested IPMP keywords from hostname file for a given interface.
#
# Example:
#	nu_get_hostname_ipmpinfo hme0 inet keyword [ keyword ... ]
#
nu_get_hostname_ipmpinfo()
{
	case "$2" in
		inet)
			file=/etc/hostname.$1
			;;
		inet6)
			file=/etc/hostname6.$1
			;;
		*)
			return
			;;
	esac

	[ -r "$file" ] || return 

	type=$2
	shift 2

	#
	# Read through the hostname file looking for the specified
	# keywords.  Since there may be several keywords that cancel
	# each other out, the caller must post-process as appropriate.
	#
	while read line; do
		[ -z "$line" ] && continue
		/usr/sbin/ifparse -s "$type" $line
	done < "$file" | while read one two; do
		for keyword in "$@"; do
			[ "$one" = "$keyword" ] && echo "$one $two"
		done
	done
}

#
# nu_get_grp_for_type interface type list
#
# Look through the set of hostname files associated with the same physical
# interface as "interface", and determine which group they would configure.
# Only hostname files associated with the physical interface or logical
# interface zero are allowed to set the group.
#
nu_get_grp_for_type()
{
	physical=`nu_get_physical $1`
	type=$2
	group=""

	#
	# The last setting of the group is the one that counts, which is
	# the reason for the second while loop.
	#
	shift 2
	for ifname in "$@"; do
		if nu_comp_if "$physical" $ifname; then 
			nu_get_hostname_ipmpinfo $ifname $type group
		fi
	done | while :; do
		read keyword grname || {
			echo "$group"
			break
		}
		group="$grname"
	done
}

#
# nu_get_grp interface
#
# If there is both an inet and inet6 version of an interface, the group
# could be set in either set of hostname files.  Since inet6 is configured
# after inet, if there's a setting in both files, inet6 wins.
#
nu_get_grp()
{
	group=`nu_get_grp_for_type $1 inet6 $inet6_list`
	[ -z "$group" ] && group=`nu_get_grp_for_type $1 inet $inet_list`
	echo $group
}

#
# nu_not_vrrp_if interface family
#
# Given the interface name and the address family (inet or inet6), determine
# whether this is a VRRP VNIC.
#
# This is used to determine whether to bring the interface up
#
nu_not_vrrp_if()
{
	macaddrtype=`/usr/sbin/dladm show-vnic $1 -o MACADDRTYPE \
	    -p 2>/dev/null`

	case "$macaddrtype" in
	'vrrp'*''$2'')
			vrrp=1
			;;
	*)
			vrrp=0
			;;
	esac
	return $vrrp
}

#
# nu_get_grp_ifname groupname
#
# Return the IPMP meta-interface name for the group, if it exists.
#
nu_get_grp_ifname()
{
	/usr/sbin/ipmpstat -gP -o groupname,group | \
	    while IFS=: read name ifname; do
		if [ "$name" = "$1" ]; then
			echo "$ifname"
			return
		fi
	done
}

# nu_process_dhcp_reqhost interface
#
# This function determines whether or not an interface is configured
# to request a specific host name from the DHCP server (using DDNS).
# If the interface is configured to request a host name, then this
# function will write the value of the hostname to stdout and
# return 0, otherwise the function will return 1.
#
nu_process_dhcp_reqhost()
{
	if [ -f /etc/dhcp.$1 ] && [ -f /etc/hostname.$1 ]; then
		set -- `shcat /etc/hostname.$1`
		[ $# -eq 2 -a "$1" = "inet" ] && echo $2 && return 0
	fi
	return 1
}

#
# nu_process_inet_hostname processor [ args ]
#
# Process an inet hostname file.  The contents of the file
# are taken from standard input. Each line is passed
# on the command line to the "processor" command.
# Command line arguments can be passed to the processor.
#
# Examples:
#	nu_process_inet_hostname /usr/sbin/ifconfig -P hme0 </etc/hostname.hme0
#	
#	nu_process_inet_hostname /usr/sbin/ifparse -f </etc/hostname.hme0
#
# If there is only line in an hostname file we assume it contains
# the old style address which results in the interface being brought up 
# and the netmask and broadcast address being set ($inet_oneline_epilogue).
#
# Note that if the interface is a VRRP interface, do not bring the address
# up ($inet_oneline_epilogue_no_up).
#
# If there are multiple lines we assume the file contains a list of
# commands to the processor with neither the implied bringing up of the
# interface nor the setting of the default netmask and broadcast address.
#
# Return non-zero if any command fails so that the caller may alert
# users to errors in the configuration.
#
inet_oneline_epilogue_no_up="netmask + broadcast +"
inet_oneline_epilogue="netmask + broadcast + up"

nu_process_inet_hostname()
{
	if [[ `echo $1 | grep "ifconfig"` ]] && \
	    nu_process_dhcp_reqhost $3; then
		:
	else
		#
		# Redirecting input from a file results in a sub-shell being
		# used, hence this outer loop surrounding the "multiple_lines"
		# and "ifcmds" variables.
		#
		while :; do
			multiple_lines=false
			ifcmds=""
			retval=0

			while read one rest; do
				if [ -n "$ifcmds" ]; then
					#
					# This handles the first N-1
					# lines of a N-line hostname file.
					#
					$* $ifcmds || retval=$?
					multiple_lines=true
				fi

				#
				# Strip out the "ipmp" keyword if it's the
				# first token, since it's used to control
				# interface creation, not configuration.
				#
				[ "$one" = ipmp ] && one=
				ifcmds="$one $rest"
			done

			#
			# If the hostname file is empty or consists of only
			# blank lines, break out of the outer loop without
			# configuring the newly plumbed interface.
			#
			[ -z "$ifcmds" ] && return $retval
			if [ $multiple_lines = false ]; then
				#
				# The traditional one-line hostname file.
				# Note that we only bring it up if the
				# interface is not a VRRP VNIC.
				#
				if nu_not_vrrp_if $2 $3; then
					estr="$inet_oneline_epilogue"
				else
					estr="$inet_oneline_epilogue_no_up"
				fi
				ifcmds="$ifcmds $estr"
			fi

			#
			# This handles either the single-line case or
			# the last line of the N-line case.
			#
			$* $ifcmds || return $?
			return $retval
		done
	fi
}

#
# nu_process_inet6_hostname processor [ args ]
#
# Process an inet6 hostname file.  The contents of the file
# are taken from standard input. Each line is passed
# on the command line to the "processor" command.
# Command line arguments can be passed to the processor.
#
# Examples:
#	nu_process_inet6_hostname /usr/sbin/ifconfig -P hme0 inet6 \
#	    < /etc/hostname6.hme0
#	
#	nu_process_inet6_hostname /usr/sbin/ifparse -f inet6 \
#	    < /etc/hostname6.hme0
#
# Return non-zero if any of the commands fail so that the caller may alert
# users to errors in the configuration.
#
nu_process_inet6_hostname()
{
    	retval=0
	while read one rest; do
		#
	    	# See comment in nu_process_inet_hostname for details.
		#
		[ "$one" = ipmp ] && one=
		ifcmds="$one $rest"

		if [ -n "$ifcmds" ]; then
			$* $ifcmds || retval=$?
		fi
	done
	return $retval
}

#
# nu_create_ipmp ifname groupname type
#
# Helper function for create_groupifname() that returns zero if it's able
# to create an IPMP interface of the specified type and place it in the
# specified group, or non-zero otherwise.
#
nu_create_ipmp()
{
	/usr/sbin/ifconfig $1 >/dev/null 2>&1 && return 1
	/usr/sbin/ifconfig $1 inet6 >/dev/null 2>&1 && return 1
	/usr/sbin/ifconfig -P $1 $3 ipmp group $2 2>/dev/null
}

#
# nu_create_grp_ifname groupname type 
#
# Create an IPMP meta-interface name for the group.  We only use this
# function if all of the interfaces in the group failed at boot and there
# were no /etc/hostname[6].<if> files for the IPMP meta-interface.
#
nu_create_grp_ifname()
{
	typeset -i indx 
	for (( indx=0; indx<=999; indx++ )); do
		if nu_create_ipmp ipmp$indx $1 $2; then 
			echo ipmp$indx
			return
		fi
	done
}

#
# nu_mv_ipmp_addrs family
#
# Process interfaces that failed to plumb.  Find the IPMP meta-interface
# that should host the addresses.  For IPv6, only static addresses defined
# in hostname6 files are moved, autoconfigured addresses are not moved.
#
# Example:
#	nu_mv_ipmp_addrs inet6
#
nu_mv_ipmp_addrs()
{
	type="$1"
	eval "failed=\"\$${type}_failed\""
	eval "list=\"\$${type}_list\""
	process_func="nu_process_${type}_hostname"
	processed=""

	if [ "$type" = inet ]; then
		typedesc="IPv4"
		zaddr="0.0.0.0"
		hostpfx="/etc/hostname"
	else
		typedesc="IPv6"
		zaddr="::"
		hostpfx="/etc/hostname6"
	fi

	echo "Moving addresses from missing ${typedesc} interface(s):\c" \
	    >/dev/msglog

	for ifname in $failed; do
		nu_in_list nu_comp_if $ifname $processed && continue

		group=`nu_get_grp $ifname`
		if [ -z "$group" ]; then
			nu_in_list nu_comp_physical $ifname $processed || { 
				echo " $ifname (not moved -- not" \
				    "in an IPMP group)\c" >/dev/msglog
				processed="$processed $ifname"
			}
			continue
		fi

		#
		# Lookup the IPMP meta-interface name.  If one doesn't exist,
		# create it.
		#
		grifname=`nu_get_grp_ifname $group`
		[ -z "$grifname" ] && grifname=`nu_create_grp_ifname $group $type`

		#
		# The hostname files are processed twice.  In the first
		# pass, we are looking for all commands that apply to the
		# non-additional interface address.  These may be
		# scattered over the hostname files for `ifname' and `ifname:0'.
		# We won't know whether the address represents a failover
		# address or not until we've read both the files.
		#
		# In the first pass through the hostname files, all
		# additional logical interface commands are removed.  The
		# remaining commands are concatenated together and passed
		# to ifparse to determine whether the non-additional
		# logical interface address is a failover address.  If it
		# as a failover address, the address may not be the first
		# item on the line, so we can't just substitute "addif"
		# for "set".  We prepend an "addif $zaddr" command, and
		# let the embedded "set" command set the address later.
		#
		/usr/sbin/ifparse -f $type `
			for item in $list; do
				nu_comp_if $ifname $item && $process_func \
				    /usr/sbin/ifparse $type < $hostpfx.$item 
			done | while read three four; do
				[ "$three" != addif ] && echo "$three $four \c"
			done` | while read one two; do
				[ -z "$one" ] && continue
				[ "$one $two" = "$inet_oneline_epilogue" ] && \
				    continue
				line="addif $zaddr $one $two"
				/usr/sbin/ifconfig -P $grifname $type $line \
				    >/dev/null
			done

		#
		# In the second pass, look for the the "addif" commands
		# that configure additional failover addresses.  Addif
		# commands are not valid in logical interface hostname
		# files.
		#
		if [ "$ifname" = "`nu_get_physical $ifname`" ]; then
			$process_func /usr/sbin/ifparse -f $type \
			    < $hostpfx.$ifname \
			| while read one two; do
				[ "$one" = addif ] && \
					/usr/sbin/ifconfig -P $grifname $type \
				    	    addif $two >/dev/null
			done
		fi

		nu_in_list nu_comp_physical $ifname $processed || { 
			processed="$processed $ifname"
			echo " $ifname (moved to $grifname)\c" > /dev/msglog
		}
	done
	echo "." >/dev/msglog
}

#
# nu_process_underif_hostname [ args ]
#
# Process the inet or inet6 hostname file for an underlying interface in an
# IPMP group. The goal is to create all the failover addresses directly on the
# IPMP interface, in order to avoid address migration when the address is
# later brought up. In some occasions, this function may accept two input
# hostname files, one for <if> and other for <if>:0, if there is a hostname
# file present for <if>:0.
#
# The lines that do not configure a failover address are fed to
# nu_process_inet_hostname or nu_process_inet6_hostname.
#
# Example:
#	nu_process_underif_hostname bge0 inet tester /etc/hostname.bge0
#	nu_process_underif_hostname bge0 inet tester /etc/hostname.bge0 \
#	    /etc/hostname.bge0:1
#
nu_process_underif_hostname()
{
	underif=$1
	type=$2
	group=$3
	hostfile=$4
	hostfile_lz=$5
	process_func="nu_process_${type}_hostname"
	retval=0
	set_list=
	addif_list=
	if [ "$type" = inet ]; then
		zaddr="0.0.0.0"
	else
		zaddr="::"
	fi

	phys=`nu_get_physical $underif`
	/usr/sbin/ifconfig -P $phys $type group $group || retval=$?

	#
	# Even if the group was not successfully created, we still need
	# to process the hostname file to configure the `underif', even
	# though it will not be in a group.
	#
	ipmpif=`nu_get_grp_ifname $group`

	hfiles="$hostfile $hostfile_lz"

	#
	# The IPMP group interface should exist at this point. If it doesn't,
	# a process must have unplumbed it accidentally, which will mean
	# additional problems. But, in the rare case that this happens,
	# `underif' will not be an underlying interface anymore. We will simply
	# call nu_process_inet[6]_hostname and exit this function
	#
	if [ -z "$ipmpif" ]; then
		shcat $hfiles | $process_func /usr/sbin/ifconfig -P $underif \
		    $type || retval=$?
		return $retval
	fi

	#
	# Parse the lines from the hostname files and get the data addresses
	# and test addresses in separate lists.
	# Note that the lines from both the input files for the 0th logical
	# interface (e.g. /etc/hostname[6].bge0 and /etc/hostname[6].bge0:0)
	# need to be parsed.
	#

	# Pass 1, isolate each addif block from the other tokens in the
	# file. Anything that is not an addif block is appended to a single
	# set address line.
	shcat $hfiles | while read line; do
		/usr/sbin/ifparse $type $line | while read one two; do
			if [ "$one" = "addif" ]; then
				addif_list="$addif_list\n$one $two"
			else
				set_list="$set_list $one $two"
			fi
		done
	done

	# Pass 2, Re-assemble the text with the set line first, then
	# re-parse the text looking for lines that apply to the IPMP interface.
	echo "$set_list\n $addif_list" | while read line; do
		/usr/sbin/ifparse -f $type $line | while read one two; do
			if [ "$one" != "addif" ]; then
				/usr/sbin/ifconfig -P $ipmpif $type addif \
				    $zaddr $one $two up >/dev/null || retval=$?
			else
				/usr/sbin/ifconfig -P $ipmpif $type $one $two \
				    >/dev/null || retval=$?
			fi
		done
	done

	# Pass 3, re-parse the lines looking for addresses that apply to
	# the underlying interface. 
	echo "$set_list up\n $addif_list" | while read line; do
		/usr/sbin/ifparse -s $type $line
	done | $process_func /usr/sbin/ifconfig -P $underif $type || retval=$?

	return $retval
}

#
# nu_configure_if type class interface_list
#
# Called by nu_upgrade_from_etc_files to configure all of the interfaces
# of type `type' (e.g., "inet6") in `interface_list' according to their
# /etc/hostname[6].* files.  `class' describes the class of interface
# (e.g., "IPMP"), as a diagnostic aid. For inet6 interfaces, the interface
# is also brought up. This fucntion is basically a driver function that
# calls one of nu_process_inet_hostname, nu_process_inet6_hostname, or
# nu_process_underif_hostname to process the interface.
#
nu_configure_if()
{
	fail=
	type=$1
	class=$2
	process_func=nu_process_${type}_hostname
	shift 2

	if [ "$type" = inet ]; then
		desc="IPv4"
		hostpfx="/etc/hostname"
	else
		desc="IPv6"
		hostpfx="/etc/hostname6"
	fi
	[ -n "$class" ] && desc="$class $desc"

	echo "configuring $desc interfaces:\c"
	while [ $# -gt 0 ]; do
		#
		# Determine whether the interface is an IPMP underlying
		# interface or not and then call the appropriate function
		# for this interface. IPMP underlying interfaces have
		# special handling and therefore, have their own function.
		#

		group=
		if [[ $class != IPMP ]]; then
			eval iflist="\$${type}_list"
			group=`nu_get_grp_for_type $1 $type $iflist`
		fi
		if [[ -z "$group" ]]; then
			$process_func /usr/sbin/ifconfig -P $1 $type \
			    < $hostpfx.$1 >/dev/null
		else
			lz_file=
			`echo $1 | grep -v ":" >/dev/null` && \
			    `echo $iflist | grep -w $1:0 >/dev/null` &&
			    lz_file=$hostpfx.$1:0
			nu_process_underif_hostname $1 $type $group \
			    $hostpfx.$1 $lz_file
		fi
		if [ $? -ne 0 ]; then
			fail="$fail $1"
		elif [ "$type" = inet6 ]; then
			#
			# only bring the interface up if it is not a
			# VRRP VNIC
			#
			if nu_not_vrrp_if $1 $type; then
			    	/usr/sbin/ifconfig $1 inet6 up || \
				    fail="$fail $1"
			fi
		fi
		echo " $1\c"
		[[ -n $lz_file ]] && echo " $1:0\c" && shift
		shift
	done
	echo "."

	[ -n "$fail" ] && warn_failed_ifs "configure $desc" "$fail"
}

#
# nu_delete_ipadm_config <interface>
#
# Deletes the existing configuration for interface. Returns 0, if no
# configuration is found or if the existing configuration is successfully
# deleted. Returns 1 otherwise.
#
# Before the upgrade, the /etc/hostname files were given preference over the
# ipadm configuration for the same interface. But when we do the conversion,
# it is not possible to combine persistent configuration from /etc/hostname
# files and ipadm config. So, we convert the /etc/hostname file after removing
# the existing ipadm config.
#
# `from-gz' interfaces are handled differently. Since these interfaces
# are never configured from /etc/hostname files, we leave the ipadm config
# and return 0 signaling that the /etc/hostname file should be ignored.
#
# During the upgrade process in a non-global exclusive-IP zone, the
# network/physical:default is started once before sysidnet runs, and restarted
# again by sysidnet once it finishes the network configuration.
# nu_delete_ipadm_config() will return 1 the first time, since no pre-existing
# config will be found for the `from-gz' interface. During the second time,
# we will find the interface configured with the `from-gz' information.
# Unlike the global zone, we do not want to remove this configuration.
# Instead, we should ignore any /etc/hostname.<if> file created by the sysidnet
# tool for this interface.
#
nu_delete_ipadm_config()
{
	ipadm show-if -po class,current $1 2>/dev/null |
	    IFS=':' read class current
	[ -z "$class" ] && return 0
	if [[ $current != *Z* ]]; then
		ipadm delete-$class $1
		[ $? -ne 0 ] && return 1
		echo "Deleted ipadm configuration for" \
		    "$1 during the upgrade"
		return 0
	fi
	return 1
}

#
# nu_rename_upgraded_file <file> <upgrade_result> <file_type>
#
# Function used by service instances to rename and add comments to files
# processed as part of an upgrade.
#
# Example:
#	nu_process_upgraded_file file 0 hostname6.<tun>
#
nu_rename_upgraded_file()
{
	typeset file=$1
	typeset suffix="converted"
	if [ $2 -ne 0 ]; then
		suffix="not-converted"
	fi
	typeset filetype=$3

	typeset ufile=/etc/OBSOLETE.$file.$suffix
	echo "#" >${ufile}
	echo "# Solaris no longer supports the /etc/$filetype files as a" \
	    "source for" >>${ufile}
	echo "# configuring network interfaces." >>${ufile}
	echo "#" >>${ufile}
	echo "# See ipadm(1M) for information on configuring network" \
	    " interfaces." >>${ufile}
	echo "#" >>${ufile}
	if [ $2 = 0 ]; then
		echo "# This file has been converted to its ipadm" \
		    "equivalent representation." >>${ufile}
	else
		echo "# Attempt to convert this file to its ipadm" \
		    "equivalent representation was" >>${ufile}
		echo "# not completely successful. Refer to the" \
		    "service log for details." >>${ufile}
	fi
	echo "#" >>${ufile}
	/usr/bin/cat /etc/$file >>${ufile}
	/usr/bin/rm -f /etc/$file
}

#
# nu_upgrade_if <intf_name> <net_type> <intf_file> <file_type>
#
# This function is used during upgrade when the /etc/hostname*.* file contents
# for IP tunnel and loopback interfaces are converted into ipadm persistent
# configuration.
#
# Example:
#	nu_upgrade_if lo0 inet hostname.lo0 hostname.<lo0>

#
nu_upgrade_if()
{
	[[ $1 != "lo0" ]] && /usr/sbin/ifconfig -P $1 $2 plumb
	/usr/sbin/ifconfig $1 $2 >/dev/null 2>&1
	if [ $? -ne 0 ]; then
		nu_rename_upgraded_file $3 1 $4
		return
	fi
	while read ifcmds; do
  		[ -n "$ifcmds" ] && /usr/sbin/ifconfig -P $1 $2 $ifcmds
	done < /etc/$3 > /dev/null
	/usr/sbin/ifconfig -P $1 $2 up
	nu_rename_upgraded_file $3 0 $4
}

#
# nu_upgrade_dhcp <intf> [1]
#
# Configures dhcp address on the given interface and returns the exit code of
# the ifconfig command.  If second arg is 1, the given interface is a primary
# interface.
#
nu_upgrade_dhcp()
{
	cmdline=`shcat /etc/dhcp\.$1`
	is_primary=$2

	dhcp_reqhost=`nu_process_dhcp_reqhost $1`
	[ -n "$dhcp_reqhost" ] && cmdline="$cmdline reqhost $dhcp_reqhost"
	if [ "$is_primary" = "1" ]; then
		echo "starting DHCP on primary interface $1"
		/usr/sbin/ifconfig -P $1 auto-dhcp primary $cmdline 
		retval=$?
	else
		echo "starting DHCP on interface $1"
		/usr/sbin/ifconfig -P $1 dhcp start wait 0 $cmdline 
		retval=$?
	fi
	nu_rename_upgraded_file dhcp.$1 0 "dhcp.<if>"
	return $retval
}

#
# nu_upgrade_from_etc_files
#
# Function used to upgrade the /etc/hostname.<if> and dhcp.<if>
# files to their ipadm equivalent configurations. This includes
# IPMP interfaces. It *does not* include loopback and tunnel interfaces.
# Those interfaces have their own upgrade procedures defined by
# their services.
#
# The upgrade processing follows these steps:
#
#  1. Derive list of IPv4 interfaces from /etc/hostname.<if> files
#     and create separate list for IPMP interfaces. Both physical and
#     logical interface names are included in these lists.
#  2. Derive list of IPv6 interfaces from /etc/hostname6.<if> files.
#     and create separate list for IPMP interfaces. Both physical and
#     logical interface names are included in these lists.
#  3. Plumb the IPv4 IPMP interfaces persistently and update the
#     'ipmp_created' and 'ipmp_failed' appropriately.
#  4. Plumb the IPv4 non-IPMP interfaces persistently and update the
#     'inet_plumbed' and 'inet_failed' appropriately.
#  5. Plumb the IPv6 non-IPMP interfaces persistently and update the
#     'inet6_plumbed' and 'inet6_failed' appropriately.
#  6. Plumb the IPv6 IPMP interfaces persistently and update the
#    'ipmp6_created' and 'ipmp6_failed' appropriately.
#  7. Process the hostname.<if> files for both the IPv4 and IPv6
#     IPMP interfaces.
#  8. Process the hostname.<if> files for both the IPv4 and IPv6
#     non-IPMP interfaces.
#  9. For the IPv4 and IPv6 interfaces that failed to plumb, find (or create)
#     IPMP meta-interfaces to host their data addresses.
# 10. Derive the list of interfaces that should be configured by DHCP
#     and determine the 'primary' interface in the process.
# 11. Persistently configure the 'primary' interface using DHCP.
# 12. Persistently configure the remaining interfaces using DHCP.
#
nu_upgrade_from_etc_files()
{
	#
	# All the IPv4 and IPv6 interfaces are plumbed before doing any
	# interface configuration.  This prevents errors from plumb failures
	# getting mixed in with the configured interface lists that the script
	# outputs.
	#
	#
	# Get the list of IPv4 interfaces to configure by breaking
	# /etc/hostname.* into separate args by using "." as a shell separator
	# character.
	#
	hostname_files="`echo /etc/hostname.*[0-9] 2>/dev/null`"
	if [ "$hostname_files" != "/etc/hostname.*[0-9]" ]; then
		ORIGIFS="$IFS"
		IFS="$IFS."
		set -- $hostname_files
		IFS="$ORIGIFS"
		while [ $# -ge 2 ]; do
			shift
			intf_name=$1
			while [ $# -gt 1 -a "$2" != "/etc/hostname" ]; do
				intf_name="$intf_name.$2"
				shift
			done
			shift

			phy_intf=`echo $intf_name | cut -f1 -d':'`
			# skip IP tunnel interfaces plumbed by net-iptun.
			if is_iptun $phy_intf; then
				continue
			fi
			# skip loopback plumbed by net-loopback
			if [[ $phy_intf == "lo0" ]]; then
				continue
			fi

			#
			# Delete any existing configuration for this
			# interface assuming that it was either created by
			# the legacy install tool or erroneously had
			# both ipadm legacy configurations pre-upgrade.
			#
			nu_delete_ipadm_config $intf_name
			if [ $? -ne 0 ]; then
				nu_rename_upgraded_file hostname.$intf 1 \
				    "hostname.<if>"
				continue
			fi
			read one rest < /etc/hostname.$intf_name
			if [ "$one" = ipmp ]; then
				ipmp_list="$ipmp_list $intf_name"
			else
				inet_list="$inet_list $intf_name"
			fi
		done
	fi
	
	#
	# Get the list of IPv6 interfaces to configure by breaking
	# /etc/hostname6.* into separate args by using "." as a shell separator
	# character.
	#
	hostname_files="`echo /etc/hostname6.*[0-9] 2>/dev/null`"
	if [ "$hostname_files" != "/etc/hostname6.*[0-9]" ]; then
		ORIGIFS="$IFS"
		IFS="$IFS."
		set -- $hostname_files
		IFS="$ORIGIFS"
		while [ $# -ge 2 ]; do
			shift
			intf_name=$1
			while [ $# -gt 1 -a "$2" != "/etc/hostname6" ]; do
				intf_name="$intf_name.$2"
				shift
			done
			shift

			phy_intf=`echo $intf_name | cut -f1 -d':'`
			# skip IP tunnel interfaces plumbed by net-iptun.
			if is_iptun $phy_intf; then
				continue
			fi
			# skip loopback plumbed by net-loopback
			if [[ $phy_intf == "lo0" ]]; then
				continue
			fi
	
			#
			# As with IPv4 case, delete any existing configuration
			# for this interface assuming that it was created by
			# legacy install tool or had both ipadm and legacy
			# configurations pre-upgrade.
			#
			nu_delete_ipadm_config $intf_name
			if [ $? -ne 0 ]; then
				nu_rename_upgraded_file hostname6.$intf 1 \
				    "hostname6.<if>"
				continue
			fi
			read one rest < /etc/hostname6.$intf_name
			if [ "$one" = ipmp ]; then
				ipmp6_list="$ipmp6_list $intf_name"
			else
				inet6_list="$inet6_list $intf_name"
			fi
		done
	fi
	
	#
	# Create all of the IPv4 IPMP interfaces.
	#
	if [ -n "$ipmp_list" ]; then
		set -- $ipmp_list
		while [ $# -gt 0 ]; do
		    	if /usr/sbin/ifconfig -P $1 ipmp; then
				ipmp_created="$ipmp_created $1"
			else
				ipmp_failed="$ipmp_failed $1"
			fi	
			shift
		done
		[ -n "$ipmp_failed" ] && warn_failed_ifs "create IPv4 IPMP" \
		    "$ipmp_failed"
	fi
	
	#
	# Step through the IPv4 interface list and try to plumb every
	# interface. Just in case the interface is plumbed in the active
	# configuration, unplumb first (e.g., sysidnet). Generate
	# list of plumbed and failed IPv4 interfaces.
	#
	if [ -n "$inet_list" ]; then
		set -- $inet_list
		while [ $# -gt 0 ]; do
			/usr/sbin/ifconfig $1 unplumb >/dev/null 2>&1
			/usr/sbin/ifconfig -P $1 plumb
			if /usr/sbin/ifconfig $1 inet >/dev/null 2>&1; then
				inet_plumbed="$inet_plumbed $1"
			else
				inet_failed="$inet_failed $1"
			fi
			shift
		done
		[ -n "$inet_failed" ] && warn_failed_ifs "plumb IPv4" \
		    "$inet_failed"
	fi
	
	#
	# Step through the IPv6 interface list and plumb every interface.
	# Generate list of plumbed and failed IPv6 interfaces.  Each plumbed
	# interface will be brought up later, after processing any contents of
	# the /etc/hostname6.* file.
	#
	if [ -n "$inet6_list" ]; then
		set -- $inet6_list
		while [ $# -gt 0 ]; do
			/usr/sbin/ifconfig $1 inet6 unplumb
			/usr/sbin/ifconfig -P $1 inet6 plumb
			if /usr/sbin/ifconfig $1 inet6 >/dev/null 2>&1; then
				inet6_plumbed="$inet6_plumbed $1"
			else
				inet6_failed="$inet6_failed $1"
			fi
			shift
		done
		[ -n "$inet6_failed" ] && warn_failed_ifs "plumb IPv6" \
		    "$inet6_failed"
	fi
	
	#
	# Create all of the IPv6 IPMP interfaces.
	#
	if [ -n "$ipmp6_list" ]; then
		set -- $ipmp6_list
		while [ $# -gt 0 ]; do
		    	if /usr/sbin/ifconfig -P $1 inet6 ipmp; then
				ipmp6_created="$ipmp6_created $1"
			else
				ipmp6_failed="$ipmp6_failed $1"
	 		fi
			shift
		done
		[ -n "$ipmp6_failed" ] && warn_failed_ifs "create IPv6 IPMP" \
		    "$ipmp6_failed"
	fi
	
	#
	# Process the /etc/hostname[6].* files for IPMP interfaces.  Processing
	# these before non-IPMP interfaces avoids accidental implicit IPMP
	# group creation.
	#
	[ -n "$ipmp_created" ] && nu_configure_if inet "IPMP" $ipmp_created
	[ -n "$ipmp6_created" ] && nu_configure_if inet6 "IPMP" $ipmp6_created
	
	#
	# Process the /etc/hostname[6].* files for non-IPMP interfaces.
	#
	[ -n "$inet_plumbed" ] && nu_configure_if inet "" $inet_plumbed
	[ -n "$inet6_plumbed" ] && nu_configure_if inet6 "" $inet6_plumbed
	
	#
	# For the IPv4 and IPv6 interfaces that failed to plumb, find
	# (or create) IPMP meta-interfaces to host their data addresses.
	#
	[ -n "$inet_failed" ] && nu_mv_ipmp_addrs inet
	[ -n "$inet6_failed" ] && nu_mv_ipmp_addrs inet6
	
	# Run DHCP if requested. Skip boot-configured interface.
	# Find the primary interface. Default to the first
	# interface if not specified. First primary interface found
	# "wins". Use care not to "reconfigure" a net-booted interface
	# configured using DHCP. Run through the list of interfaces
	# again, this time trying DHCP.
	#
	i4d_fail=
	firstif=
	primary=
	dhcp_files="`/usr/bin/ls -1 /etc/dhcp.*[0-9] 2>/dev/null | sort -u`"
	echo "$dhcp_files" | while IFS='.' read prefix ifname; do
		dhcp_list="$dhcp_list $ifname"
		[ -z "$firstif" ] && firstif=$ifname
		SP='(^| |    |$)'
		[ -z "$primary" ] && \
		    egrep "$SP*(primary)$SP" $prefix.$ifname >/dev/null && \
		    primary=$ifname
	done
	if [ -n "$primary" -o -n "$firstif" ]; then
		[ -z "$primary" ] && primary="$firstif"
		if [ "$_INIT_NET_IF" != "$primary" ]; then
			nu_upgrade_dhcp $primary 1
			# Exit code 4 means ifconfig timed out waiting for
			# dhcpagent
			[ $? -ne 0 ] && [ $? -ne 4 ] && \
			    i4d_fail="$i4d_fail $primary"
		fi
	fi
	for intf in $dhcp_list; do
		[ "$_INIT_NET_IF" = $intf ] || [ $intf = "$primary" ] && \
		    continue
		nu_upgrade_dhcp $intf
		# Exit code can't be timeout when wait is 0
		[ $? -ne 0 ] && i4d_fail="$i4d_fail $intf"
	done
	[ -n "$i4d_fail" ] && warn_failed_ifs "configure IPv4 DHCP" "$i4d_fail"

	#
	# Record the results of the conversion so that the service can
	# move the /etc files aside appropriately once the upgrade has
	# completed.
	#
	for intf in $inet_plumbed $ipmp_created; do
		nu_rename_upgraded_file hostname.$intf 0 "hostname.<if>"
	done

	for intf in $inet_failed $ipmp_failed; do
		nu_rename_upgraded_file hostname.$intf 1 "hostname.<if>"
	done

	for intf in $inet6_plumbed $ipmp6_created; do
		nu_rename_upgraded_file hostname6.$intf 0 "hostname6.<if>"
	done

	for intf in $inet6_failed $ipmp6_failed; do
		nu_rename_upgraded_file hostname6.$intf 1 "hostname6.<if>"
	done
}

#
# nu_etc_files_need_upgrading svc
#
# Do the services need to have their /etc files upgraded to their
# ipadm representations?
#
nu_etc_files_need_upgrading()
{
	svc=$1
	upgrade_version=`$SVCPROP -c -p upgrade/version $svc 2>/dev/null`
	[ "$upgrade_version" -lt "$HOSTNAME_UPGRADE_VERSION" ]
}

#
# nu_finalize_etc_upgrade svc
#
# Update the service upgrade version so that /etc file upgrades
# won't be performed again.
#
nu_finalize_etc_upgrade()
{
	svc=$1
	$SVCCFG -s $svc setprop \
	    upgrade/version = integer: $HOSTNAME_UPGRADE_VERSION
	$SVCADM refresh $svc
}
