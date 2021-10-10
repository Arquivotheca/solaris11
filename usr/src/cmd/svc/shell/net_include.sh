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
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T.
# All rights reserved.
#

NET_INADDR_ANY="0.0.0.0"
NET_IN6ADDR_ANY_INIT="::0"

# Print warnings to console
warn_failed_ifs() {
	echo "Failed to $1 interface(s):$2" >/dev/msglog
}

#
# shcat file
#   Simulates cat in sh so it doesn't need to be on the root filesystem.
#
shcat() {
        while [ $# -ge 1 ]; do
                while read i; do
                        echo "$i"
                done < $1
                shift
        done
}

net_record_err()
{
	message=$1
	err=$2

	echo "$message" | smf_console
	if [ $err -ne 0 ]; then
		echo "Error code = $err" | smf_console
	fi
}

#
# net_reconfigure is called from the network/physical service (by the
# net-physical and net-nwam method scripts) to perform tasks that only
# need to be done during a reconfigure boot.  This needs to be
# isolated in a function since network/physical has two instances
# (default and nwam) that have distinct method scripts that each need
# to do these things.
#
net_reconfigure ()
{
	#
	# Is this a reconfigure boot?  If not, then there's nothing
	# for us to do.
	#
	reconfig=$(svcprop -c -p system/reconfigure \
	    system/svc/restarter:default 2>/dev/null)
	if [ $? -ne 0 -o "$reconfig" = false ]; then
		return 0
	fi

	#
	# Ensure that the datalink-management service is running since
	# manifest-import has not yet run for a first boot after
	# upgrade.  We wouldn't need to do that if manifest-import ran
	# earlier in boot, since there is an explicit dependency
	# between datalink-management and network/physical.
	#
	svcadm enable -ts network/datalink-management:default

	#
	# There is a bug in SMF which causes the svcadm command above
	# to exit prematurely (with an error code of 3) before having
	# waited for the service to come online after having enabled
	# it.  Until that bug is fixed, we need to have the following
	# loop to explicitly wait for the service to come online.
	#
	i=0
	while [ $i -lt 30 ]; do
		i=$(expr $i + 1)
		sleep 1
		state=$(svcprop -p restarter/state \
		    network/datalink-management:default 2>/dev/null)
		if [ $? -ne 0 ]; then
			continue
		elif [ "$state" = "online" ]; then
			break
		fi
	done
	if [ "$state" != "online" ]; then
		echo "The network/datalink-management service \c"
		echo "did not come online."
		return 1
	fi

	#
	# Initialize the set of physical links, and validate and
	# remove all the physical links which were removed during the
	# system shutdown.
	#
	/usr/sbin/dladm init-phys
	return 0
}

#
# Check for use of the default "Port VLAN Identifier" (PVID) -- VLAN 1.
# If there is one for a given interface, then warn the user and force the
# PVID to zero (if it's not already set).  We do this by generating a list
# of interfaces with VLAN 1 in use first, and then parsing out the
# corresponding base datalink entries to check for ones without a
# "default_tag" property.
#
update_pvid()
{
	datalink=/etc/dladm/datalink.conf

	(
		# Find datalinks using VLAN 1 explicitly
		# configured by dladm
		/usr/bin/nawk '
			/^#/ || NF < 2 { next }
			{ linkdata[$1]=$2; }
			/;vid=int,1;/ {
				sub(/.*;linkover=int,/, "", $2);
				sub(/;.*/, "", $2);
				link=linkdata[$2];
				sub(/name=string,/, "", link);
				sub(/;.*/, "", link);
				print link;
			}' $datalink
	) | ( /usr/bin/sort -u; echo END; cat $datalink ) | /usr/bin/nawk '
	    /^END$/ { state=1; }
	    state == 0 { usingpvid[++nusingpvid]=$1; next; }
	    /^#/ || NF < 2 { next; }
	    {
		# If it is already present and has a tag set,
		# then believe it.
		if (!match($2, /;default_tag=/))
			next;
		sub(/name=string,/, "", $2);
		sub(/;.*/, "", $2);
		for (i = 1; i <= nusingpvid; i++) {
			if (usingpvid[i] == $2)
				usingpvid[i]="";
		}
	    }
	    END {
		for (i = 1; i <= nusingpvid; i++) {
			if (usingpvid[i] != "") {
				printf("Warning: default VLAN tag set to 0" \
				    " on %s\n", usingpvid[i]);
				cmd=sprintf("dladm set-linkprop -p " \
				    "default_tag=0 %s\n", usingpvid[i]);
				system(cmd);
			}
		}
	    }'
}

#
# service_exists fmri
#
# returns success (0) if the service exists, 1 otherwise.
#
service_exists()
{
	/usr/sbin/svccfg -s $1 listpg > /dev/null 2>&1
	if [ $? -eq 0 ]; then
		return 0;
	fi
	return 1;
}

#
# service_is_enabled fmri
#
# returns success (0) if the service is enabled (permanently or
# temporarily), 1 otherwise.
#
service_is_enabled()
{
	#
	# The -c option must be specified to use the composed view
	# because the general/enabled property takes immediate effect.
	# See Example 2 in svcprop(1).
	#
	# Look at the general_ovr/enabled (if it is present) first to
	# determine the temporarily enabled state.
	#
	tstate=$(/usr/bin/svcprop -c -p general_ovr/enabled $1 2>/dev/null)
	if [ $? -eq 0 ]; then
		[ "$tstate" = "true" ] && return 0
		return 1
	fi

        state=$(/usr/bin/svcprop -c -p general/enabled $1 2>/dev/null)
	[ "$state" = "true" ] && return 0
	return 1
}

#
# service_is_in_maintenance fmri
#
# return success (0) if the service is in maintenance state, 1 otherwise.
#
service_is_in_maintenance()
{
	state=$(/usr/bin/svcs -Ho STATE $1 2>/dev/null)
	[ "$state" = "maintenance" ] && return 0
	return 1
}

#
# is_valid_v4addr addr
#
# Returns 0 if a valid IPv4 address is given, 1 otherwise.
#
is_valid_v4addr()
{ 
	echo $1 | /usr/xpg4/bin/awk 'NF != 1 { exit 1 } \
	$1 !~ /^((25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])\.){3}\
(25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])$/ \
	{ exit 1 }'
	return $?
}

#
# is_valid_v6addr addr
#
# Returns 0 if a valid IPv6 address is given, 1 otherwise.
#
is_valid_v6addr()
{
	echo $1 | /usr/xpg4/bin/awk 'NF != 1 { exit 1 } \
	# 1:2:3:4:5:6:7:8
	$1 !~ /^([a-fA-F0-9]{1,4}:){7}[a-fA-F0-9]{1,4}$/ &&
	# 1:2:3::6:7:8
	$1 !~ /^([a-fA-F0-9]{1,4}:){0,6}:([a-fA-F0-9]{1,4}:){0,6}\
[a-fA-F0-9]{1,4}$/ && 
	# 1:2:3::
	$1 !~ /^([a-fA-F0-9]{1,4}:){0,7}:$/ &&
	# ::7:8
	$1 !~ /^:(:[a-fA-F0-9]{1,4}){0,6}:[a-fA-F0-9]{1,4}$/ && 
	# ::f:1.2.3.4
	$1 !~ /^:(:[a-fA-F0-9]{1,4}){0,5}:\
((25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])\.){3}\
(25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])$/ &&
	# a:b:c:d:e:f:1.2.3.4
	$1 !~ /^([a-fA-F0-9]{1,4}:){6}\
((25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])\.){3}\
(25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])$/ \
	{ exit 1 }'
	return $?
}

#
# is_valid_addr addr
#
# Returns 0 if a valid IPv4 or IPv6 address is given, 1 otherwise.
#
is_valid_addr()
{
	is_valid_v4addr $1 || is_valid_v6addr $1
}

#
# netmask2plen netmask
#
# Converts a subnet mask to its CIDR prefixlen.
#
netmask2plen ()
{
	mask=$1
	plen=0
	ocnt=1
	done=0

	pmask=$mask
	while [ $ocnt -le 4 -a "$pmask" != "" ]
	do
		octet=`echo $pmask | /usr/bin/cut -f 1-1 -d '.'`
		ocnt=`expr $ocnt + 1`

		if [ $octet -lt 0 -o $octet -ge 256 ]; then
			echo "Failed to compute prefixlen from subnet mask."
			echo "$mask is not a valid subnet mask."
			return 1
		fi

		if [ $done -ne 0 -a $octet -ne 0 ]; then
			echo "Failed to compute prefixlen from" \
			    "subnet mask."
			echo "$mask is a non-contiguous subnet mask."
			return 1
		fi

		case $octet in
		'255' )
			plen=`expr $plen + 8`
			;;
		'0' )
			;;
		'254' )
			plen=`expr $plen + 7`
			;;
		'252' )
			plen=`expr $plen + 6`
			;;
		'248' )
			plen=`expr $plen + 5`
			;;
		'240' )
			plen=`expr $plen + 4`
			;;
		'224' )
			plen=`expr $plen + 3`
			;;
		'192' )
			plen=`expr $plen + 2`
			;;
		'128' )
			plen=`expr $plen + 1`
			;;
		*)
			echo "Failed to compute prefixlen from" \
			    "subnet mask."
			echo "$mask is a non-contiguous subnet mask."
			return 1
			;;
		esac

		if [ $octet -ne 255 ]; then
			done=1
		fi

		tmask=`echo $pmask | /usr/bin/cut -f 2- -d '.'`
		if [ "$tmask" = "$pmask" ]; then
			pmask=""
		else
			pmask=$tmask
		fi
	done
	if [ "$pmask" != "" ]; then
		echo "Failed to compute prefixlen from subnet mask."
		echo "$mask is not a valid subnet mask."
		return 1
	fi

	echo $plen
	return 0
}

#
# nwam_get_loc_prop location property
#
# echoes the value of the property for the given location
# return:
#	0 => property is set
#	1 => property is not set
#
nwam_get_loc_prop()
{
	value=`/usr/sbin/netcfg "select loc \"$1\"; get -V $2" 2>/dev/null`
	rtn=$?
	echo $value
	return $rtn
}

#
# nwam_get_loc_list_prop location property
#
# echoes a space-separated list of the property values for the given location
# return:
#	0 => property is set
#	1 => property is not set
#
nwam_get_loc_list_prop()
{
	clist=`/usr/sbin/netcfg "select loc \"$1\"; get -V $2" 2>/dev/null`
	rtn=$?
	#
	# netcfg gives us a comma-separated list;
	# need to convert commas to spaces.
	#
	slist=$(echo $clist | sed -e s/","/" "/g)
	echo $slist
	return $rtn
}

is_iptun ()
{
	intf=$(echo $1 | cut -f1 -d:)
	# Is this a persistent IP tunnel link?
	/usr/sbin/dladm show-iptun -P $intf > /dev/null 2>&1
	if [ $? -eq 0 ]; then
		return 0
	fi
	# Is this an implicit IP tunnel (i.e., ip.tun0)
	ORIGIFS="$IFS"
	IFS="$IFS."
	set -- $intf
	IFS="$ORIGIFS"
	if [ $# -eq 2 -a \( "$1" = "ip" -o "$1" = "ip6" \) ]; then
		#
		# It looks like one, but another type of link might be
		# using a name that looks like an implicit IP tunnel.
		# If dladm show-link -P finds it, then it's not an IP
		# tunnel.
		#
		/usr/sbin/dladm show-link -Pp $intf > /dev/null 2>&1
		if [ $? -eq 0 ]; then
			return 1
		else
			return 0
		fi
	fi
	return 1
}
