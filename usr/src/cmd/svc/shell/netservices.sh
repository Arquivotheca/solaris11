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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#


LOG_FMRI=svc:/system/system-log
CMSD_FMRI=svc:/network/rpc/cde-calendar-manager
BIND_FMRI=svc:/network/rpc/bind
XSERVER_FMRI=svc:/application/x11/x11-server
SENDMAIL_FMRI=svc:/network/smtp:sendmail
CUPSSERVER_FMRI=svc:/application/cups/scheduler
CUPSRFC1179_FMRI=svc:/application/cups/in-lpd
TTDB_FMRI=svc:/network/rpc/cde-ttdbserver

usage()
{
	prog=`basename $0`
	echo "$prog: usage: $prog [ open | limited ]" >&2
	exit 2
}

#
# set_property fmri group property value action
#
# Sets the specified property in the specified property-group, creating
# the group and or property if necessary.
# The action after changing properties can be refresh, restart, or none.
#
set_property()
{
	fmri=$1
	group=$2
	prop=$3
	val=$4
	action=$5

	# Nothing to do if the service doesn't exist on this system
	svcprop -q $fmri || return 

	if svcprop -qp $group $fmri; then :; else
		if svccfg -s $fmri addpg $group application; then :; else
			echo "Failed to create property group \"$group\" \c"
			echo "for $fmri."
			exit 1
		fi
	fi

	if svccfg -s $fmri setprop $group/$prop = boolean: $val; then :; else
		echo "Failed to set property $group/$prop for $fmri"
		exit 1
	fi

	# Add :default if no instance specified
	if [ `expr $fmri : "svc:/.*:.*"` = 0 ]; then
		instance=$fmri:default
	else
		instance=$fmri
	fi

	case $action in
	refresh)
		svcadm refresh $instance
		;;
	restart)
		if [ "`svcprop -p restarter/state $instance`" = "online" ]
		then
			echo "restarting $instance"
			svcadm restart $instance
		fi
		;;
	esac
}

set_system_log()
{
	if [ "$1" = "local" ]; then
		val=false
	else
		val=true
	fi

	set_property $LOG_FMRI config log_from_remote $val restart
}

set_cmsd()
{
	svcprop -q $CMSD_FMRI:default || return
	if [ "$1" = "local" ]; then
		proto="ticlts"
	else
		proto="udp"
	fi

	inetadm -m $CMSD_FMRI:default proto=$proto
	svcadm refresh $CMSD_FMRI:default
}

set_rpcbind()
{
	if [ "$1" = "local" ]; then
		val=true
	else
		val=false
	fi

	set_property $BIND_FMRI config local_only $val refresh
}

set_xserver() {
	svcprop -q $XSERVER_FMRI || return
	if [ "$1" = "local" ]; then
		val=false
	else
		val=true
	fi

	set_property $XSERVER_FMRI options tcp_listen $val none
}

set_sendmail()
{
	if [ "$1" = "local" ]; then
		val=true
	else
		val=false
	fi

	set_property $SENDMAIL_FMRI config local_only $val restart
}

set_ttdbserver()
{
	svcprop -q $TTDB_FMRI:tcp || return
	if [ "$1" = "local" ]; then
		val=ticotsord
	else
		val=tcp
	fi
	inetadm -m $TTDB_FMRI:tcp proto="$val"
	svcadm refresh $TTDB_FMRI:tcp
}

set_printing() {
	use_cups=`svcprop -C -p general/active $CUPSSERVER_FMRI:default \
		  2>/dev/null`

	case "$1" in
	"open")
		cups_options="--remote-admin --remote-printers"
		cups_options="$cups_options --share-printers --remote-any"
		svc_operation="enable"
		;;
	"local")
		cups_options="--no-remote-admin --no-remote-printers"
		cups_options="$cups_options --no-share-printers --no-remote-any"
		svc_operation="disable"
		;;
	esac

	case "$use_cups" in
	"true")
		if [ -x /usr/sbin/cupsctl ] ; then
			# only run cupsctl with elevated privilege to avoid
			# being prompted for a password
			[ `/usr/bin/id -u` = 0 ] && 
				/usr/sbin/cupsctl $cups_options
		fi
		svcadm $svc_operation $CUPSRFC1179_FMRI
		;;
	*)
		;;
	esac
}

if [ $# -ne 1 ]; then
	usage
fi

case $1 in
	"open")
		profile=generic_open.xml
		keyword="open"
		;;
	"limited")
		profile=generic_limited_net.xml
		keyword="local"
		;;
	*)
		usage
		;;
esac

if [ ! -f /etc/svc/profile/$profile ]; then
	echo "/etc/svc/profile/$profile nonexistent. Exiting."
	exit 1
fi

#
# set service properties
#
set_system_log $keyword
set_cmsd $keyword
set_rpcbind $keyword
set_xserver $keyword
set_sendmail $keyword
set_ttdbserver $keyword
set_printing $keyword

#
# put the new profile into place, and apply it
#
# Create a hash entry so that manifest_import is aware of the
# profile being applied and does not reapply the profile on reboot.
#
ln -sf ./$profile /etc/svc/profile/generic.xml
svccfg delhash /etc/svc/profile/generic.xml > /dev/null 2>&1
SVCCFG_CHECKHASH="TRUE" svccfg apply /etc/svc/profile/generic.xml

#
# generic_open may not start inetd services on upgraded systems
#
if [ $profile = "generic_open.xml" ]
then
	svccfg apply /etc/svc/profile/inetd_generic.xml
fi

