#!/bin/ksh
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

# This script provides a simple GUI for managing labeled zones.
# It provides contextual menus which provide appropriate choices.
# It must be run in the global zone as root.

# These arguments are accepted, and will result in non-interactive
# (text-only) mode:
#
#	txzonemgr [-c | -d[f]| -Z <file>]
#
#	-c	create default zones
#	-d	destroy all zones; prompts for confirmation unless
#		the -f flag is also specified
#	-f	force
#	-Z	create zones using list in specified file
#

# DISP - use GUI (otherwise use non-interactive mode)
DISP=1
# CREATEDEF - make default zones (non-interactive)
CREATEDEF=0
# DESTROYZONES - tear down all zones (non-interactive)
DESTROYZONES=0
# FORCE - force
FORCE=0
# ZONELIST - create zones using list in  specified file
ZONELIST=

NSCD_PER_LABEL=0
NSCD_INDICATOR=/var/tsol/doors/nscd_per_label
if [ -f $NSCD_INDICATOR ] ; then
	NSCD_PER_LABEL=1
fi

myname=$(basename $0)

TXTMP=/tmp/txzonemgr
TNZONECFG=/etc/security/tsol/tnzonecfg
PUBZONE=public
INTZONE=internal
SNAPSHOT=snapshot

PATH=/usr/bin:/usr/sbin:/usr/lib export PATH
title="Labeled Zone Manager 2.3"

msg_defzones=$(gettext "Create default zones using default settings?")
msg_confirmkill=$(gettext "OK to destroy all zones?")
msg_continue=$(gettext "(exit to resume $(basename $0) when ready)")
msg_getlabel=$(gettext "Select a label for the")
msg_getremote=$(gettext "Select a remote host or network from the list below:")
msg_getnet=$(gettext "Select a network configuration for the")
msg_getzone=$(gettext "Select a zone from the list below:
(select global for zone creation and shared settings)")
msg_getcmd=$(gettext "Select a command from the list below:")
msg_getmin=$(gettext "Select the minimum network label for the")
msg_getmax=$(gettext "Select the maximum network label for the")
msg_badip=$(gettext " is not a valid IP address")
msg_getIPaddrOrDHCP=$(gettext "Enter hostname, 'dhcp', or IP address/[prefix length]:")
msg_getIPaddr=$(gettext "Enter hostname or IP address/[prefix length]:")


process_options()
{
	typeset opt optlist

	optlist='cdfZ:'

	while getopts "$optlist" opt
	do
		case $opt in
		c)	CREATEDEF=1
			DISP=0
			;;
		d)	DESTROYZONES=1
			DISP=0
			;;
		f)	FORCE=1
			;;
		Z)	ZONELIST=$OPTARG
			if [ ! -f $ZONELIST ]; then
				gettext "invalid file name -$ZONELIST\n"
				usage
				return 2
			fi
			DISP=0
			;;
		*)	gettext "invalid option -$OPTARG\n"
			usage
			return 2
			;;
		esac
	done

	if [[ $ZONELIST ]]; then
		if [ $CREATEDEF -eq 1 \
		    -o $DESTROYZONES -eq 1 \
		     -o $FORCE -eq 1 ] ; then
			gettext "cannot combine -Z  with other options\n"
			usage
			return 2
		fi
	fi
	if [ $CREATEDEF -eq 1 -a $DESTROYZONES -eq 1 ] ; then
		gettext "cannot combine options -c and -d\n"
		usage
		return 2
	fi
	if [ $CREATEDEF -eq 1 -a $FORCE -eq 1 ] ; then
		gettext "option -f not allowed with -c\n"
		usage
		return 2
	fi
	if [ $FORCE -eq 1 -a $CREATEDEF -eq 0 -a $DESTROYZONES -eq 0 ] ; then
		gettext "option -f specified without any other options\n"
		usage
		return 2
	fi

	shift $((OPTIND - 1))
	if [ "x$1" != "x" ] ; then
		usage
		return 2
	fi

	return 0
}

usage() {
	gettext "usage: $myname [-c | -d[f] | -Z <file>]\n"
}

consoleCheck() {
	if [ $zonename != global ] ; then
		zconsole=$(pgrep -f "zlogin -C $zonename")
		if [ $? != 0 ] ; then
			console="Zone Console...\n"
		fi
	fi
}

labelCheck() {
	curlabel=$(tncfg -z $zonename info label 2>/dev/null)
	if [[ $curlabel ]] ; then
		curlabel=$(echo $curlabel|cut -f2 -d=)
		hexlabel=$(atohexlabel "${curlabel}")
		label=
		if [ $zonename = global ] ; then
			addcipsohost="Add Multilevel Access to Remote Host...\n"
			removecipsohost="Remove Multilevel Access to Remote Host...\n"
			mlpType=both
			setmlps="Configure Multilevel Ports...\n"
		else
			addcipsohost=
			removecipsohost=
			setmlps=
			ipType=$(zonecfg -z $zonename info ip-type|cut -d" " -f2)
			if [ $zonestate = configured -o \
			    $zonestate = installed  ] ; then
				setmlps="Configure Multilevel Ports...\n"
				net=$(zonecfg -z $zonename info net)
				if [ $ipType = exclusive ] ; then
					net=$(zonecfg -z $zonename info anet)
					mlpType=private
				elif [[ -n $net ]] ; then
					mlpType=both
				else
					mlpType=shared
				fi
				addnet="Configure Network Interfaces...\n"
			fi
		fi
		addremotehost="Add Single-level Access to Remote Host...\n"
		remotes=$(tncfg -S files -t $unlab_template info host 2>/dev/null)
		if [[ -n $remotes ]] ; then
			removeremotehost="Remove Single-level Access to Remote Host...\n"
		else
			removeremotehost=
		fi
	else
		label="Select Label...\n"
		addremotehost=
		removeremotehost=
		addcipsohost=
		removecipsohost=
		setmlps=
	fi
}

cloneCheck() {
	set -A zonelist
	integer clone_cnt=0
	for p in $(zoneadm list -ip) ; do
		z=$(echo "$p"|cut -d : -f2)
		s=$(echo "$p"|cut -d : -f3)
		if [ $z = $zonename ] ; then
			continue
		elif [ $s = "installed" ] ; then
			zonelist[clone_cnt]=$z
			clone_cnt+=1
		fi
	done
	if [ $clone_cnt -gt 0 ] ; then
		clone="Clone...\n"; \
	fi
}

relabelCheck() {
	macstate=$(zonecfg -z $zonename info|grep win_mac_write)
	if [[ -n "$macstate" ]] ; then
		permitrelabel="Deny Relabeling\n"
	else
		permitrelabel="Permit Relabeling\n"
	fi
}

autobootCheck() {
	bootmode=$(zonecfg -z $zonename info autoboot)
	if [[ $bootmode == 'autoboot: true' ]] ; then
		autoboot="Set Manual Booting\n"
	else
		autoboot="Set Automatic Booting\n"
	fi
}

newZone() { 
		if [[ ! -n $zonename ]] ; then
			zonename=$(zenity --entry \
			    --title="$title" \
			    --width=330 \
			    --entry-text="" \
			    --text="Enter Zone Name: ")

			if [[ ! -n $zonename ]] ; then
				zonename=global
				return
			fi
		fi
		zonecfg -z $zonename "create -t SYStsoldef;\
		     set zonepath=/zone/$zonename"
}

removeZoneBEs() {
	zfs list -H $ZDSET/$zonename 1>/dev/null 2>&1
	if [ $? = 0 ] ; then
		for zbe in $(zfs list -rHo name $ZDSET/$zonename|grep ROOT/zbe) ; do
			zfs destroy -rRf $zbe
		done
	fi
}

updateTemplate () {
	if [ $hostType = cipso ] ; then
		tncfg -S files -t $cipso_template "set host_type=cipso;\
		    set doi=1;\
		    set min_label=${minlabel};\
		    set max_label=${maxlabel}"
	else
		tncfg -S files -t $unlab_template "set host_type=unlabeled;\
		    set doi=1;\
		    set min_label=${minlabel};
		    set max_label=${maxlabel};
		    set def_label=${hexlabel}"
	fi
}

setTNdata () {
	err=$(tncfg -z $zonename set label=$hexlabel 2>&1)
	if [ $? != 0 ] ; then
		if [ $DISP -eq 0 ]; then
			echo $err
		else
			x=$(zenity --error \
			    --title="$title" \
			    --text="$err")
		fi
		return 1
	fi
	#
	# Add matching entries in tnrhtp if necessary
	#
	minlabel=admin_low
	maxlabel=admin_high
	hostType=cipso
	updateTemplate

	hostType=unlabeled
	updateTemplate
}

selectLabel() {
	hexlabel=$(tgnome-selectlabel \
		--title="$title" \
		--text="$msg_getlabel $zonename zone:" \
		--min="${DEFAULTLABEL}"  \
		--default="${DEFAULTLABEL}"  \
		--max=$(chk_encodings -X) \
		--accredcheck=yes \
		--mode=sensitivity \
		--format=internal \
		2>/dev/null)
	if [ $? = 0 ] ; then
		setTNdata
		if [ $? -ne 0 ] ; then
			return 1
		fi
	fi	
}

getLabelRange() {
	deflabel=$(tncfg -S files -t $cipso_template info min_label|cut -d= -f2)
	minlabel=$(tgnome-selectlabel \
		--title="$title" \
		--text="$msg_getmin $zonename zone:" \
		--min="admin_low" \
		--max="$hexlabel" \
		--default="$deflabel" \
		--accredcheck=no \
		--mode=sensitivity \
		--format=internal \
		2>/dev/null)
	[ $? != 0 ] && return
	
	deflabel=$(tncfg -S files -t $cipso_template info max_label|cut -d= -f2)
	maxlabel=$(tgnome-selectlabel \
		--title="$title" \
		--text="$msg_getmax $zonename zone:" \
		--min="${minlabel}"  \
		--max="admin_high" \
		--default="$deflabel" \
		--accredcheck=no \
		--mode=sensitivity \
		--format=internal \
		2>/dev/null)
	[ $? != 0 ] && return

	hostType=cipso
	updateTemplate
}


encryptionValues() {
	echo $(zfs get 2>&1 | grep encryption | sed -e s/^.*YES// -e s/\|//g)
}

getPassphrase() {
	pass1=$(zenity --entry --title="$title" --text="Enter passphrase:" \
	    --width=330 --hide-text)
	pass2=$(zenity --entry --title="$title" --text="Re-enter passphrase:" \
	    --width=330 --hide-text)
	if [[ "$pass1" != "$pass2" ]]; then
		zenity --error --title="$title" \
			--text="Passphrases do not match"
		return ""
	fi
	file=$(mktemp)
	echo "$pass1" > $file
	echo "$file"
}

createZDSET() {
	options=$1
	pool=${2%%/*}

	# First check if ZFS encrytption support is available
	pversion=$(zpool list -H -o version $pool)
	cversion=$(zpool upgrade -v | grep Crypto | awk '{ print $1 }')
	if (( cversion == 0 || pversion < cversion )); then
		zfs create $options $ZDSET
		return
	fi

	encryption=$(zenity --list --title="$title" --height=320 \
		--text="Select cipher for encryption of all labels:" \
		--column="encryption" $(encryptionValues))

	if [[ $? != 0 || $encryption == "off" ]]; then
		zfs create $options $ZDSET
		return
	fi

	format=$(zenity --list --title="$title" \
		--text "Select encryption key source:" \
		--column="Key format and location" \
		"Passphrase" "Generate Key in file")
	[ $? != 0 ] && exit 

	if [[ $format == "Passphrase" ]]; then
		file=$(getPassphrase)
		if [[ $file == "" ]]; then
			exit
		fi
		keysource="passphrase,file://$file"
		removefile=1;
	elif [[ $format == "Generate Key in file" ]]; then
		file=$(zenity --file-selection \
			--title="$title: Location of key file" \
			--save --confirm-overwrite)
		[ $? != 0 ] && exit 
		if [[ $encryption == "on" ]]; then
			keylen=128
		else
			t=${encryption#aes-} && keylen=${t%%-*}
		fi
		pktool genkey keystore=file keytype=aes \
		    keylen=$keylen outkey=$file
		keysource="raw,file:///$file"
	fi

	options="$options -o encryption=$encryption -o keysource=$keysource"
	zfs create $options $ZDSET
	if (( removefile == 1 )); then
		zfs set keysource=passphrase,prompt $ZDSET
		rm $file
	fi
}

#
# This function automates NFS sharing of the minimum labeled
# home directory with higher lableled zones. Reading the lowest
# labeled home directory is required by updatehome(1).
#
# An auto_home_<zonename> automap entry is created and stored in
# /var/tsol/doors/automount, which is then readable to all zones.
#
# Although zone administrators can configure their own automount
# entries, it is done automatically here to make things easier.
#
auto_home() {
	zoneadm -z $zonename ready
	if [ $? != 0 ]; then
		return
	fi
	zonepath=$(zoneadm -z $zonename list -p|cut -d : -f4)
	ZONE_ETC_DIR=${zonepath}/root/etc

# If we are cloning a zone with its own name service
# then we need to delete the existing account and update
# the user's clearance. It is necessary to initialize the
# zone's auto_home file so that userdel(1M) can locate the
# cloned home directory to delete it.

	if [ $NSCD_PER_LABEL = 1 -a $1 = clone ]; then
		if [ $LOGNAME != "root" ]; then
			echo "$LOGNAME\tlocalhost:/export/home/$LOGNAME" >> \
			    $ZONE_ETC_DIR/auto_home_$zonename
		fi
	fi

	# Find the labeled zone corresponding to the minimum label

	minzone_entry=$(grep ${DEFAULTLABEL} $TNZONECFG)
	if [[ ! -n $minzone_entry ]]; then
		zoneadm -z $zonename halt
		return
	fi
	minzone=$(echo $minzone_entry|cut -d: -f1)
	am=auto_home_$minzone

	if [ $minzone = $zonename ]; then
		zoneadm -z $zonename halt
		# If the current zone has the minimum label,
		# check if its home directories can be shared.

		# An explicit IP address assignment is required
		# for a zone to be a multilevel NFS server.
		
		if [ $ipaddr = ...  -o $ipaddr = 127.0.0.1 ] ; then
			return
		fi
		validateIPaddr
		if [[ -z $ipaddr ]] ; then
			return
		fi

		# Save the automount entry for other zones to import

		echo "+$am" > $AUTOMOUNTDIR/$am
		echo "*	${ipaddr}:/export/home/&" \
		    >> $AUTOMOUNTDIR/$am 

		# Configure multilevel NFS ports if
		# this hasn't been done already.
	
		tncfg -z $zonename "add mlp_private=111/tcp;\
		    add mlp_private=111/udp;\
		    add mlp_private=2049/tcp" >/dev/null 2>&1
	else
		# If an automount map exists, then copy it into the higher
		# labeled zone.

		if [ -f $AUTOMOUNTDIR/$am ]; then
			cp $AUTOMOUNTDIR/$am $ZONE_ETC_DIR/$am
			mountpoint="/zone/${minzone}/home"
			
			# Add map to auto_master if necessary

			entry=$(grep ^$mountpoint $ZONE_ETC_DIR/auto_master)
			if [ $? != 0 ] ;then
				entry="$mountpoint	$am	-nobrowse"
				echo $entry >> $ZONE_ETC_DIR/auto_master
			fi
		else
			rm $ZONE_ETC_DIR/$am 2>/dev/null
		fi
		zoneadm -z $zonename halt
	fi
}

parseNet() {
	ipaddr="..."
	shift 1
	while (( $# > 1)) do
		case $1 in
			"lower-link:")
				physical=$2
				;;
			"physical:")
				physical=$2
				;;
			"address:")
				if [ $2 != "..." ]; then
					ipaddr=$2
				fi
				;;
			"allowed-address:")
				if [ $2 != "..." ]; then
					ipaddr=$2
				fi
				;;
			"defrouter:")
				defrouter=$2
				;;
		esac
		shift 2
	done
#
#	If address is a hostname, return IP address
#
	alpha=$(echo $ipaddr|grep ^[A-z])
	if [[ ! -z $alpha ]]; then
		ipaddr=$(getent hosts $ipaddr|nawk '{print $1}')
		if [[ -z $ipaddr ]] ; then
			ipaddr="..."
		fi
	fi
}

printsysconfig() {
	fmtsvc='<service name="%s" version="1" type="service">\n<instance name="%s" enabled="true">\n'
	fmtgrp='<property_group name="%s" type="%s">\n'
	fmtval='<propval name="%s" type="%s" value="%s"/>\n'
	fmtendsvc='</property_group>\n</instance>\n</service>\n'

	print '<!DOCTYPE service_bundle SYSTEM "/usr/share/lib/xml/dtd/service_bundle.dtd.1">'
	print '<service_bundle type="profile" name="system configuration">'
	printf "$fmtsvc" milestone/config default
	printf "$fmtgrp" configuration application
	printf "$fmtval" configure boolean true
	printf "$fmtval" interactive_config boolean false
	printf "$fmtval" config_groups astring $1
	printf "$fmtendsvc"

	ldap=$(svcprop -p general/enabled ldap/client)
	if [ $ldap = true ] ; then
		printf "$fmtsvc" network/ldap/client default
		printf "$fmtgrp" config application
		searchbase=$(svcprop -p config/search_base network/ldap/client)
		printf "$fmtval" search_base astring "$searchbase"
		serverlist=$(svcprop -p config/server_list network/ldap/client)
		printf "$fmtval" server_list astring "$serverlist"
		profName=$(svcprop -p config/profile network/ldap/client)
		printf "$fmtval" profile astring $profName
		print '</property_group>'

		printf "$fmtgrp" cred application
		profDN=$(svcprop -p cred/bind_dn network/ldap/client)
		printf "$fmtval" bind_dn astring $profDN
		profPwd=$(ldapclient list | \
		    grep "^NS_LDAP_BINDPASSWD" | cut -d " " -f2)
		printf "$fmtval" bind_passwd astring $profPwd
		printf "$fmtendsvc"

		printf "$fmtsvc" system/name-service/switch default
		printf "$fmtgrp" config application
		printf "$fmtval" default astring "files ldap"
		printf "$fmtval" netgroup astring "ldap"
		printf "$fmtendsvc"
	fi
 
	ipType=$(zonecfg -z $zonename info ip-type|cut -d" " -f2)
	if [ $ipType = exclusive ] ; then
		hostname=$(zenity --entry \
		    --title="$title" \
		    --width=330 \
		    --text="${zonename}: Enter Hostname: ")
		[ $? != 0 ] && return
	else
		net=$(zonecfg -z $zonename info net)
		if [[ -n $net ]]; then
			net=$(echo $net|sed 's/ not specified/: .../g')
			parseNet $net
			validateIPaddr
			getent=$(getent hosts $ipaddr)
			if [ $? != 0 ]; then
				hostname=$(zenity --entry \
				    --title="$title" \
				    --width=330 \
				    --text="${zonename}: Enter Hostname: ")
				[ $? != 0 ] && return
			else
				hostname=$(echo $getent|nawk '{print $2}')
			fi
		else
			hostname=$(hostname)
			ipaddr=127.0.0.1
		fi
	fi
	printf "$fmtsvc" system/identity node
	printf "$fmtgrp" config application
	printf "$fmtval" nodename astring $hostname
	printf "$fmtendsvc"

	locale=$(svcprop -p environment/LANG system/environment:init 2>/dev/null)
	if [ $? = 0 ]; then
		printf "$fmtsvc" system/environment init
		printf "$fmtgrp" environment application
		printf "$fmtval" LANG astring $locale
		printf "$fmtendsvc"
	fi

	timezone=$(svcprop -p timezone/localtime system/timezone:default)
	printf "$fmtsvc" system/timezone default
	printf "$fmtgrp" timezone application
	printf "$fmtval" localtime astring $timezone
	printf "$fmtendsvc"

	printf "$fmtsvc" system/console-login default
	printf "$fmtgrp" ttymon application
	printf "$fmtval" terminal_type astring vt100
	printf "$fmtendsvc"

	if [ $NSCD_PER_LABEL = 1 -a $LOGNAME != "root" ]; then
		printf "$fmtsvc" system/config-user default
		printf "$fmtgrp" root_account application
		rootpwd=$(grep "^root:" /etc/shadow|cut -d : -f2)
		printf "$fmtval" password astring $rootpwd
		printf "$fmtval" type astring role
		print '</property_group>'
		printf "$fmtgrp" user_account application
		printf "$fmtval" login astring gfaden
		userpwd=$(grep "^${LOGNAME}:" /etc/shadow|cut -d : -f2)
		printf "$fmtval" password astring $userpwd
		printf "$fmtval" roles astring root

		getent passwd $LOGNAME|while IFS=: \
		    read name pw uid gid descr home shell; do
			printf "$fmtval" uid astring $uid
			printf "$fmtval" gid astring $gid
			printf "$fmtval" description astring "$descr"
			printf "$fmtval" shell astring $shell
		done
		printf "$fmtendsvc"
	fi
	print '</service_bundle>'
}


# $1 specifies how much to configure.
# possible values are "system,identity,network,location,users"

# $2 specifies if the unconfiguration should be destructive
# possible values are --destructive and null

initialize() {
	zonepath=$(zoneadm -z $zonename list -p|cut -d : -f4)
        zoneadm -z $zonename mount
	zonestate=$(zoneadm -z $zonename list -p | cut -d : -f 3)
	if [ $zonestate != mounted ] ; then
		gettext "error getting zone $zonename mounted.\n"
		return
	fi

	# Determine if the system has been previously configured
	# by checking for a root entry in /etc/user_attr.
	# An unconfigured zone can't be unconfigured.

	configured=$(grep ^root $zonepath/root/etc/user_attr)
	if [ $? = 1 ]; then
		ZONE_PROFILE_DIR=$zonepath/root/etc/svc/profile/site
		SCPROFILE=${ZONE_PROFILE_DIR}/sc_profile.xml

		# The SC profile will contain encrypted passwords copied
		# from the global zone /etc/shadow. So it must be protected.

		touch $SCPROFILE
		chmod 0400 $SCPROFILE
		printsysconfig system >$SCPROFILE
	else
		# For updates, the SC profile needs a unique name 
		# to distinguish it from the previous profile

		ZONE_PROFILE_DIR=$zonepath/lu/system/volatile
		SCPROFILE_TMP=$(mktemp ${ZONE_PROFILE_DIR}/sc_profile-XXX)
		SCPROFILE=${SCPROFILE_TMP}.xml
		mv $SCPROFILE_TMP $SCPROFILE
		printsysconfig $1 >$SCPROFILE
		SC_CONFIG_BASE=$(basename $SCPROFILE)
		zlogin -S $zonename "export _UNCONFIG_ALT_ROOT=/a; \
		    /usr/sbin/sysconfig configure -g $1 \
		    -c /system/volatile/$SC_CONFIG_BASE $2"
	fi
	zoneadm -z $zonename unmount
}

clone() {
	image=$1
	if [[ -z $image ]] ; then
		msg_clone=$(gettext "Clone the $zonename zone using a
snapshot of one of the following halted zones:")
		image=$(zenity --list \
		    --title="$title" \
		    --text="$msg_clone" \
		    --height=300 \
		    --width=330 \
		    --column="Installed Zones" ${zonelist[*]})
	fi

	if [[ -n $image ]] ; then
		removeZoneBEs
		zoneadm -z $zonename clone $image
		zonestate=$(zoneadm -z $zonename list -p | cut -d : -f 3)
		if [ $zonestate != installed ] ; then
			gettext "error cloning zone $zonename.\n"
			return 1
		fi
		auto_home clone
		if [ $NSCD_PER_LABEL = 0 ] ; then
			sharePasswd $zonename
		else
			unsharePasswd $zonename
			# change "system" to "users" when partial
			# reconfiguration is supported
			initialize system --destructive
		fi
	fi
}

install() {
	removeZoneBEs
	mkdir -p $TXTMP/$zonename
	SCPROFILE=$TXTMP/$zonename/sc_profile.xml

	# The SC profile will contain encrypted passwords copied
	# from the global zone /etc/shadow. So it must be protected.

	touch $SCPROFILE
	chmod 0400 $SCPROFILE
	printsysconfig system >$SCPROFILE
	if [ $DISP -eq 0 ] ; then
		gettext "installing zone $zonename ...\n"
		zoneadm -z $zonename install -c $SCPROFILE
	else
		# sleep is needed here to avoid occasional timing
		# problem with gnome-terminal display...
		sleep 2
		gnome-terminal \
		    --title="$title: Installing $zonename zone" \
		    --command "zoneadm -z $zonename install -c $SCPROFILE" \
		    --disable-factory \
		    --hide-menubar
	fi

	rm -rf $TXTMP/$zonename
	zonestate=$(zoneadm -z $zonename list -p | cut -d : -f 3)
	if [ $zonestate != installed ] ; then
		gettext "error installing zone $zonename.\n"
		return 1
	fi

	auto_home install
	if [ $NSCD_PER_LABEL = 0 ] ; then
		sharePasswd $zonename
	else
		unsharePasswd $zonename
	fi
}

delete() {
	delopt=$*

	# if there is an entry for this zone in tnzonecfg, remove it
	# before deleting the zone.

	curlabel=$(tncfg -z $zonename info label 2>/dev/null)
	if [[ $curlabel ]] ; then
		tncfg -z $zonename delete -F 2>/dev/null
		tncfg -S files -t $unlab_template delete -F 2>/dev/null
		tncfg -S files -t $cipso_template delete -F 2>/dev/null
	fi
	zonecfg -z $zonename delete -F

	removeZoneBEs
	for snap in $(zfs list -Ho name -t snapshot|grep "\@${zonename}_snap") ; do
		zfs destroy -R $snap
	done
}

validateIPaddr () {
	cidr=$(echo $ipaddr|grep /|cut -f2 -d/)
	if [[ -n $cidr ]]; then
		# remove the optional cidr suffix
		# which is input to getNetmask()

		ipaddr=$(echo $ipaddr|cut -f1 -d/)
	fi

	OLDIFS=$IFS
	IFS=.
	integer octet_cnt=0
	integer dummy
	set -A octets $ipaddr
	IFS=$OLDIFS
	if [ ${#octets[*]} == 4 ] ; then
		while (( octet_cnt < ${#octets[*]} )); do
			dummy=${octets[octet_cnt]}
			if [ $dummy = ${octets[octet_cnt]} ] ; then
				if (( dummy >= 0 && \
				    dummy < 256 )) ; then
					octet_cnt+=1
					continue
				else
					x=$(zenity --error \
					    --title="$title" \
					    --text="$ipaddr $msg_badip")
					ipaddr=
					return
				fi
			fi
		done
	else
		x=$(zenity --error \
		    --title="$title" \
		    --text="$ipaddr $msg_badip")
		ipaddr=
	fi
}

getAllZoneNICs(){
	integer az_cnt=0
	integer nic_cnt=0
	set -A nics $(ipadm show-addrprop -p zone -o addrobj,current)
	while (( nic_cnt < ${#nics[*]} )); do
		addrObj=${nics[nic_cnt]}
		nic_cnt+=1
		zoneType=${nics[nic_cnt]}
		nic_cnt+=1
		if [[ $addrObj == */v6 ]]; then
			continue
		elif [ $zoneType = all-zones ]; then
			aznics[az_cnt]=$addrObj
			az_cnt+=1
		fi
        done
}

getNetmask() {
	# first check if the user already specified it as a suffix

	if [[ -n $cidr ]]; then

		# The cidr prefix is extracted in validateIPaddr.
		# If it was provided, use it to compute a netask
		# and return it in the variable $netmask.

		netmask=$(perl -e 'use Socket;($c1,$c2,$c3,$c4) = unpack("C4",pack("N",~((1<<(32-$ARGV[0]))-1)));print "$c1.$c2.$c3.$c4\n";' $cidr)
	else
		# if we don't have a netmask yet, ask the user
		# and do the opposite computation

		netmask=$(zenity --entry \
		    --title="$title" \
		    --width=330 \
		    --text="$ipaddr: Enter netmask: " \
		    --entry-text 255.255.255.0)
		[ $? != 0 ] && return;

		cidr=$(perl -e 'use Socket; print unpack("%32b*",inet_aton($ARGV[0])), "\n";' $netmask)
	fi
}

addZoneNet() {
	getIPaddr "${msg_getIPaddr}"
	if [[ -z $ipaddr ]] ; then
		return;
	fi
	getNetmask
	if [[ -z $cidr ]] ; then
		return;
	fi
	updateTnrhdb $cipso_template
	if [ $? != 0 ] ; then
		return 1
	fi
	zonecfg -z $zonename "add net; \
	    set address=${ipaddr}/${cidr}; \
	    set physical=$nic; \
	    end"
	ipType=shared
	zoneNetChanged=1
}

updateTnrhdb() {
	if [[ -n $2 ]] ; then
		newipaddr="${ipaddr}/$2"
	else
		newipaddr="$ipaddr"
	fi
	err=$(tncfg -S files -t $1 add host="${newipaddr}" 2>&1)
	if [ $? != 0 ] ; then
		x=$(zenity --warning \
		    --title="$title" \
		    --text="$err")
		return 1
	fi
	return
}

getIPaddr() {
        hostname=$(zenity --entry \
            --title="$title" \
	    --width=330 \
	    --text="${1}")

	if [[ ! -n $hostname ]]; then
		ipaddr=
		return
	fi

	alpha=$(echo $hostname|grep ^[A-z])
	if [[ ! -z $alpha ]]; then
		if [ $hostname = dhcp ]; then
			ipaddr=
			return
		fi
		ipaddr=$(getent hosts $hostname|nawk '{print $1}')
		if [[ -z $ipaddr ]] ; then
			ipaddr=$(zenity --entry \
			    --title="$title" \
			    --text="Enter IP address for ${hostname}: ")
			[ $? != 0 ] && return
		fi
	else
		# hostname is numeric so treat as IP address
		ipaddr=$hostname
		hostname=
	fi
	validateIPaddr
	if [[ -z $ipaddr ]] ; then
		return
	fi
}

createInstance() {
	integer inst_cnt=0
	instance=v4static

	persistent=$(ipadm show-if -po persistent $nic)
	if [[ $persistent == *4* ]]; then
		tempFlag=
	else
		tempFlag=-t
	fi
	getIPaddr "${msg_getIPaddr}"
	if [[ -n $hostname ]]; then
		if [ $hostname = dhcp ]; then
			nic="${nic}/v4dhcp"
			ipadm create-addr $tempFlag -T dhcp $nic
			svcadm restart network/service
			return
		fi
	fi
        if [[ -z $ipaddr ]] ; then
               return;
	fi
	getNetmask
	while (( 1 )) do
		x=$(ipadm show-addr ${nic}/${instance}$inst_cnt 2>/dev/null)
		if [ $? = 1 ]; then
			nic="${nic}/${instance}$inst_cnt"
			break
		else
			inst_cnt+=1
		fi
	done
	updateTnrhdb $cipso_template
	if [ $? != 0 ] ; then
		return
	fi
	if [[ -n $hostname ]]; then
		grep $hostname /etc/inet/hosts >/dev/null
		if [ $? -eq 1 ] ; then
			print "$ipaddr\t$hostname" >> /etc/inet/hosts
		fi
	fi
	msg=$(ipadm create-addr $tempFlag -T static -a local=${ipaddr}/$cidr $nic 2>&1)
	if [ $? = 1 ]; then
		$(zenity --info \
		    --title="$title" \
		    --text="$msg" )
	fi
}
		    
createVNIC() {
	if [ $zonename == global ] ; then
		vnicname=$(zenity --entry \
		    --title="$title" \
		    --width=330 \
		    --entry-text="" \
		    --text="Enter VNIC Name: ")

		if [[ ! -n $vnicname ]] ; then
			return
		fi
		x=$(dladm show-vnic|grep "^$vnicname " )
		if [[ ! -n $x ]] ; then
			dladm create-vnic -l $nic $vnicname
		fi
		ipadm create-ip $vnicname
	else
		getIPaddr "${msg_getIPaddrOrDHCP}"
		if [[ -n $hostname ]]; then
			if [ $hostname = dhcp ]; then
				zonecfg -z $zonename "set ip-type=exclusive;
				    add anet; \
				    set lower-link=$nic; \
				    end"
				zoneNetChanged=1
				return;
			fi
		fi
		if [[ -z $ipaddr ]] ; then
			return;
		fi
		getNetmask
		if [[ -z $cidr ]] ; then
			return;
		fi
		updateTnrhdb $cipso_template
		if [ $? != 0 ] ; then
			return
		fi
		zonecfg -z $zonename "set ip-type=exclusive; \
		    add anet; \
		    set lower-link=$nic; \
		    set allowed-address=${ipaddr}/${cidr}; \
		    end"
		ipType=exclusive
		zoneNetChanged=1
	fi
	nic=$vnicname
}

shareInterface() {
	persistent=$(ipadm show-addr -po persistent $nic)
	if [[ $persistent == *U* ]]; then
		tempFlag=
	else
		tempFlag=-t
	fi
	ipadm set-addrprop $tempFlag -p zone=$1 $nic
	sleep 1
}

addTnrhdb() {
	getIPaddr "${msg_getIPaddr}"
        if [[ -z $ipaddr ]] ; then
               return;
	fi
	updateTnrhdb $1 $cidr
}
	
removeTnrhdb() {
	while (( 1 )) do
		remotes=$(tncfg -S files -t $1 info host|\
		    cut -f2 -d= 2>/dev/null)
		if [ $1 = cipso ] ; then
			templateHeading="Remove Multilevel Access to:"
		else
			templateHeading="Remove Single-level Access by $zonename Zone"
		fi
		if [[ -n $remotes ]] ; then
			ipaddr=$(zenity --list \
			    --title="$title" \
			    --text="$msg_getremote" \
			    --height=250 \
			    --width=300 \
			    --column="${templateHeading}" \
			    $remotes)

			if [[ -n $ipaddr ]] ; then
				tncfg -S files -t $1 remove host=${ipaddr}
			else
				return
			fi
		else
			return
		fi
	done
}

manageMLPs () {
	integer mlp_cnt
	integer mlpOp_cnt
	integer type_cnt

	cmds[0]="Add MLP"
	cmds[1]="Delete MLP"

	types[0]="private"
	types[1]="shared"

	while (( 1 )) do
		mlp_cnt=0
		set -A mlpOps
		mlpOps[0]="0\nSet Label Range\n${types[0]}\nany\n..."
		mlpOp_cnt=1
		case $1 in
		    "private")
			type_cnt=0
			type_limit=1
			;;
		    "shared")
			type_cnt=1
			type_limit=2
			;;
		    "both")
			type_cnt=0
			type_limit=2
			;;
		esac
		while (( type_cnt < type_limit )); do
			mlpOps[mlpOp_cnt]="\n$mlpOp_cnt\n${cmds[0]}\n${types[type_cnt]}\ntcp\n..."
			mlpOp_cnt+=1

			mlpOps[mlpOp_cnt]="\n$mlpOp_cnt\n${cmds[0]}\n${types[type_cnt]}\nudp\n..."
			mlpOp_cnt+=1

			set -A mlps $(tncfg -z $zonename info mlp_${types[type_cnt]}|cut -f2 -d=)

			while (( mlp_cnt < ${#mlps[*]} )); do
				mlp=$(echo ${mlps[mlp_cnt]}|cut -f1 -d/)
				proto=$(echo ${mlps[mlp_cnt]}|cut -f2 -d/)

				mlpOps[mlpOp_cnt]="\n$mlpOp_cnt\n${cmds[1]}\n${types[type_cnt]}\n${proto}\n${mlp}"
				mlpOp_cnt+=1
				mlp_cnt+=1
			done
			mlp_cnt=0
			type_cnt+=1
		done

		msg1="zone = ${zonename}"
		msg2=$(tncfg -S files -t $cipso_template info min_label)
		msg3=$(tncfg -S files -t $cipso_template info max_label)
		mlpOp=$(print "${mlpOps[*]}"|zenity --list \
		    --title="$title" \
		    --text="${msg1}\n${msg2}\n${msg3}\n${msg_getcmd}" \
		    --height=300 \
		    --width=400 \
		    --column="#" \
		    --column="Command" \
		    --column="MLP Type" \
		    --column="Protocol" \
		    --column="Port(s)" \
		    --hide-column=1
		)
		
		# User picked cancel or no selection
		if [[ -z $mlpOp ]] ; then
			return
		fi

		if [ $mlpOp = 0 ]; then
			getLabelRange
			continue
		else
			cmd=$(print "${mlpOps[$mlpOp]}"|tr '\n' ';' |cut -d';' -f 3)
			type=$(print "${mlpOps[$mlpOp]}"|tr '\n' ';' |cut -d';' -f 4) 
			proto=$(print "${mlpOps[$mlpOp]}"|tr '\n' ';' |cut -d';' -f 5) 
			mlp=$(print "${mlpOps[$mlpOp]}"|tr '\n' ';' |cut -d';' -f 6) 
		fi
		case $cmd in
		    ${cmds[0]} )
			mlp=$(zenity --entry \
			    --title="$title" \
			    --width=330 \
			    --entry-text="" \
			    --text="Enter $type MLP (port[-port2]):")

			if [[ ! -n $mlp ]] ; then
				return
			else
				err=$(tncfg -z $zonename add mlp_${type}=${mlp}/${proto} 2>&1)
				if [ $? != 0 ] ; then
					x=$(zenity --error \
					    --title="$title" \
					    --text="$err")
					return
				fi
			fi
			;;	
		    ${cmds[1]} )
			tncfg -z $zonename remove mlp_${type}=${mlp}/${proto}
			;;
		esac
	done
}

unsharePasswd() {
	zonecfg -z $1 remove fs dir=/etc/passwd >/dev/null 2>&1 | grep -v such
	zonecfg -z $1 remove fs dir=/etc/shadow >/dev/null 2>&1 | grep -v such
}

sharePasswd() {
	passwd=$(zonecfg -z $1 info|grep /etc/passwd)
	if [ $? -eq 1 ] ; then
		zonecfg -z $1 "add fs; \
		    set special=/etc/passwd; \
		    set dir=/etc/passwd; \
		    set type=lofs; \
		    add options ro; \
		    end; \
		    add fs; \
		    set special=/etc/shadow; \
		    set dir=/etc/shadow; \
		    set type=lofs; \
		    add options ro; \
		    end"
	fi
}

# This routine is a toggle -- if we find it configured for global nscd,
# change to nscd-per-label and vice-versa.
#
# The user was presented with only the choice to CHANGE the existing
# configuration.

manageNscd() {
	if [ $NSCD_PER_LABEL -eq 0 ] ; then
		# this MUST be a regular file for svc-nscd to detect
		touch $NSCD_INDICATOR
		NSCD_OPT="Unconfigure per-zone name service"
		NSCD_PER_LABEL=1
		for i in $(zoneadm list -i | grep -v global) ; do
			zoneadm -z $i halt >/dev/null 2>&1
			unsharePasswd $i
			zonestate=$(zoneadm -z $i list -p | cut -d : -f 3)
			if [ $zonestate = installed ] ; then
				zonename=$i
				
				# change "system" to "users" when partial
				# reconfiguration is supported
				initialize system --destructive
			fi
		done
		zonename=global
	else
		rm -f $NSCD_INDICATOR
		NSCD_OPT="Configure per-zone name service"
		NSCD_PER_LABEL=0
		for i in $(zoneadm list -i | grep -v global) ; do
			zoneadm -z $i halt >/dev/null 2>&1
			sharePasswd $i
		done
	fi
}

manageZoneInterface() {
	if [ $ipType = exclusive ] ; then
		remove="Remove Virtual Interface\n"
	else
		remove="Remove IP instance\n"
	fi
		
        net=$(echo $net|sed 's/ not specified/: .../g')
	parseNet $net
	
	nic=$(zenity --list \
	    --title="$title" \
	    --text="Select an interface from the list below:" \
	    --height=200 \
	    --width=500 \
	    --column="Interface" \
	    --column="Type" \
	    --column="Zone Name" \
	    --column="IP Address" \
	    --column="Default Router" \
	    $physical $ipType $zonename $ipaddr $defrouter)

	if [[ -z $nic ]] ; then
		return
	fi

	if [ $defrouter = "..." ] ; then
		setdefrouter="Set default router...\n"
	else
		setdefrouter=
	fi

	command=$(print ""\
	    $setdefrouter \
	    $remove \
	    | zenity --list \
	    --title="$title" \
	    --text="Select a command from the list below:" \
	    --height=300 \
	    --column="Interface: $nic" )
	if  [[ -z $command ]]; then
		return
	fi
	case $command in
	    " Set default router...")
		getIPaddr "${msg_getIPaddr}"
		if [[ -z $ipaddr ]] ; then
			return;
		fi
		if [ $ipType = exclusive ] ; then
			zonecfg -z $zonename "\
			    select anet lower-link=${nic};\
			    set defrouter=${ipaddr};
			    end"
		else
			zonecfg -z $zonename "\
			    select net physical=${nic};\
			    set defrouter=${ipaddr};
			    end"
		fi
		;;
	    " Remove IP instance")
		zonecfg -z $zonename remove net physical=${nic}
		addr=$(echo $ipaddr|cut -d/ -f1)
		tncfg -S files -t $cipso_template remove host=${addr} 1>/dev/null 2>&1
		;;
	    " Remove Virtual Interface")
		zonecfg -z $zonename "\
		    remove anet lower-link=$nic;\
		    set ip-type=shared"
		addr=$(echo $ipaddr|cut -d/ -f1)
		tncfg -S files -t $cipso_template remove host=${addr} 1>/dev/null 2>&1
		;;
	esac
	zoneNetChanged=1

}

manageZoneNets () {
	zoneNetChanged=0
	while (( 1 )) do
	if [[ -n $net ]] ; then
		manageZoneInterface
		if [[ -z $nic ]] ; then
			break
		fi
	else
		ncmds[0]="Only use all-zones IP instances"
		ncmds[1]="Add an IP instance"
		ncmds[2]="Add a virtual interface (VNIC)"

		stacks[0]="Shared Stack"
		stacks[1]="Exclusive Stack"

		getAllZoneNICs
		netOps[0]="1\n${ncmds[0]}\nShared Stack\n${aznics[*]}"

		integer nic_cnt=0
		integer netOp_cnt=2

		set -A nics $(dladm show-phys -po link,media|grep Ethernet|cut -f1 -d:)
		while (( nic_cnt < ${#nics[*]} )); do
			netOps[netOp_cnt - 1]="\n$netOp_cnt\n${ncmds[1]}\n${stacks[0]}\n${nics[nic_cnt]}"
			netOp_cnt+=1
			netOps[netOp_cnt - 1]="\n$netOp_cnt\n${ncmds[2]}\n${stacks[1]}\n${nics[nic_cnt]}"
			netOp_cnt+=1
			nic_cnt+=1
		done

		netOp=$(print "${netOps[*]}"|zenity --list \
		    --title="$title" \
		    --text="$msg_getnet $zonename zone:" \
		    --height=300 \
		    --width=500 \
		    --column="#" \
		    --column="Network Configuration " \
		    --column="IP Type" \
		    --column="Available Interfaces" \
		    --hide-column=1
		)
		
		# User picked cancel or no selection
		if [[ -z $netOp ]] ; then
			break
		fi

		# All-zones is the default, so just return
		if [ $netOp = 1 ] ; then
			break
		fi

		cmd=$(print "${netOps[$netOp - 1]}"|tr '\n' ';' |cut -d';' -f 3)
		nic=$(print "${netOps[$netOp - 1]}"|tr '\n' ';' |cut -d';' -f 5) 
		case $cmd in
		    ${ncmds[1]} )
			addZoneNet;
			;;	
		    ${ncmds[2]} )
			createVNIC
			;;
		esac
	fi
	if [ $ipType = exclusive ] ; then
		net=$(zonecfg -z $zonename info anet)
	else
		net=$(zonecfg -z $zonename info net)
	fi
	done
	if [ $zoneNetChanged = 1 -a $zonestate = installed ]; then
		# change "system" to "network,identity" when partial
		# reconfiguration is supported
		initialize system
	fi
}

manageInterface () {
	# Clear list of commands

	share=
	newlogical=
	newvnic=
	unplumb=
	bringup=
	bringdown=

	if [[ $nic == */* ]]; then
		linktype=logical
	else
		linktype=$(dladm show-link -po class $nic)
	fi

	case $linktype in
	phys )
		newlogical="Create IP Instance...\n";
		newvnic="Create Virtual Interface (VNIC)...\n";
		unplumb="Remove Physical Interface\n" ;
		column="Interface: $nic"
		;;
	logical )
		unplumb="Remove IP Instance\n"
		state=$(ipadm show-addr -po state $nic)
		column="IP Instance: $nic"
		if [ $state = down ] ; then
			bringup="Bring Up\n"
		else
			bringdown="Bring Down\n"
		fi
		zone=$(ipadm show-addrprop -cp zone -o current $nic)
		if [ $zone != all-zones ] ; then
			share="Share with Shared-IP Zones\n"
		else 
			share="Remove from Shared-IP Zones\n"
		fi
		;;
	vnic )
		newlogical="Create IP Instance...\n";
		unplumb="Remove Virtual Interface\n" ;
		column="Virtual Interface: $nic"
		;;
	esac


	command=$(print ""\
	    $share \
	    $newlogical \
	    $newvnic \
	    $unplumb \
	    $bringup \
	    $bringdown \
	    | zenity --list \
	    --title="$title" \
	    --text="Select a command from the list below:" \
	    --height=300 \
	    --column="$column")

	case $command in
	    " Create IP Instance...")
		createInstance;;
	    " Create Virtual Interface (VNIC)...")
		createVNIC ;;	
	    " Share with Shared-IP Zones")
		shareInterface all-zones;;
	    " Remove from Shared-IP Zones")
		shareInterface global;;
	    " Remove IP Instance")
		addr=$(ipadm show-addr -po addr $nic|cut -d/ -f1)
		shareInterface global
		ipadm delete-addr $nic
		tncfg -S files -t $cipso_template remove host=${addr} 1>/dev/null 2>&1
		;;
	    " Remove Physical Interface")
		ipadm delete-ip $nic
		;;
	    " Remove Virtual Interface")
		ipadm delete-ip $nic
		dladm delete-vnic $nic;;
	    " Bring Up")
		ipadm up-addr -t $nic;;
	    " Bring Down")
		ipadm down-addr -t $nic;;
	    *) return;;
	esac
}

manageNets() {
	while ((1)) do
		integer cnt=0
		set -A  objs
		i=$(ipadm show-if -po ifname)
		a=$(ipadm show-addr -po addrobj)
		nl=""
		for j in $(echo $i $a|tr " " \\n|sort); do
			if [[ $j == lo0* ]]; then
				continue
			elif [[ $j == tun* ]]; then
				continue
			elif [[ $j == */* ]]; then
				addr=$(ipadm show-addr -po addr $j 2>/dev/null)
				if [[ ! -n $addr ]]; then
					continue
				fi
				intf=$(echo $j|cut -d/ -f1)
				inst=$(echo $j|cut -d/ -f2)
				state=$(ipadm show-addr -po state $j)
				type=$(ipadm show-addr -po type $j)
				zone=$(ipadm show-addrprop -cp zone -o current $j)

				objs[cnt]="$nl$j\n$intf\n$type\n$inst\n$addr\n$zone\n$state"
			else
				state=$(dladm show-link -po state $j)
				class=$(dladm show-link -po class $j)
				persistent=$(ipadm show-if -po persistent $j)
				if [ $class = vnic ]; then
					over="over $(dladm show-link -po over $j)"
				elif [[ $persistent == *4* ]]; then
					over=persistent
				else
					over=temporary
				fi
				objs[cnt]="$nl$j\n$j\n$class\n$over\n...\nglobal\n$state"
			fi
			nl="\n"
			cnt+=1
		done
		nic=$(print "${objs[*]}"|zenity --list \
		    --title="$title" \
		    --text="Select an interface from the list below:" \
		    --height=300 \
		    --width=570 \
		    --column="AddrObj" \
		    --column="Interface" \
		    --column="Type" \
		    --column="Instance" \
		    --column="IP Address" \
		    --column="Zone Name" \
		    --column="State" \
		    --hide-column=1)

		if [[ -z $nic ]] ; then
			return
		fi
		manageInterface
	done
}

createLDAPclient() {
	ldaptitle="$title: Create LDAP Client"
	ldapdomain=$(zenity --entry \
	    --width=400 \
	    --title="$ldaptitle" \
	    --text="Enter Domain Name: ")
	if [[ -n $ldapdomain ]] ; then
	ldapserver=$(zenity --entry \
	    --width=400 \
	    --title="$ldaptitle" \
	    --text="Enter Hostname of LDAP Server: ")
	else
		return
	fi
	if [[ -n $ldapserver ]] ; then
	ldapserveraddr=$(zenity --entry \
	    --width=400 \
	    --title="$ldaptitle" \
	    --text="Enter IP address of LDAP Server $ldapserver: ")
	else
		return
	fi
	ldappassword=""
	while [[ -z ${ldappassword} || "x$ldappassword" != "x$ldappasswordconfirm" ]] ; do
	    ldappassword=$(zenity --entry \
		--width=400 \
		--title="$ldaptitle" \
		--hide-text \
		--text="Enter LDAP Proxy Password:")
	    ldappasswordconfirm=$(zenity --entry \
		--width=400 \
		--title="$ldaptitle" \
		--hide-text \
		--text="Confirm LDAP Proxy Password:")
	done
	ldapprofile=$(zenity --entry \
	    --width=400 \
	    --title="$ldaptitle" \
	    --text="Enter LDAP Profile Name: ")
	whatnext=$(zenity --list \
	    --width=400 \
	    --height=250 \
	    --title="$ldaptitle" \
	    --text="Proceed to create LDAP Client?" \
	    --column=Parameter --column=Value \
	    "Domain Name" "$ldapdomain" \
	    "Hostname" "$ldapserver" \
	    "IP Address" "$ldapserveraddr" \
	    "Password" "$(print "$ldappassword" | sed 's/./*/g')" \
	    "Profile" "$ldapprofile")
	[ $? != 0 ] && return

	grep "^${ldapserveraddr}[^0-9]" /etc/hosts > /dev/null
	if [ $? -eq 1 ] ; then
		print "$ldapserveraddr $ldapserver" >> /etc/hosts
	fi

	tncfg -S files -t cipso add host=${ldapserveraddr} 1>/dev/null 2>&1

	proxyDN=$(print $ldapdomain|awk -F"." \
	    "{ ORS = \"\" } { for (i = 1; i < NF; i++) print \"dc=\"\\\$i\",\" }{ print \"dc=\"\\\$NF }")

	zenity --info \
	    --title="$ldaptitle" \
	    --width=500 \
	    --text="global zone will be LDAP client of $ldapserver"

	ldapout=$TXTMP/ldapclient.$$

	ldapclient init -a profileName="$ldapprofile" \
	    -a domainName="$ldapdomain" \
	    -a proxyDN"=cn=proxyagent,ou=profile,$proxyDN" \
	    -a proxyPassword="$ldappassword" \
	    "$ldapserveraddr" >$ldapout 2>&1

	if [ $? -eq 0 ] ; then
	    ldapstatus=Success
	else
	    ldapstatus=Error
	fi

	zenity --text-info \
	    --width=700 \
	    --height=300 \
	    --title="$ldaptitle: $ldapstatus" \
	    --filename=$ldapout

	rm -f $ldapout


}

tearDownZones() {
	if [ $DISP -eq 0 ] ; then
		if [ $FORCE -eq 0 ] ; then
			gettext "OK to destroy all zones [y|N]? "
			read ans
			printf "%s\n" "$ans" \
			    | /usr/xpg4/bin/grep -Eq "$(locale yesexpr)"
			if [ $? -ne 0 ] ; then
				gettext "canceled.\n"
				return 1
			fi
		fi
		gettext "destroying all zones ...\n"
	else
		killall=$(zenity --question \
		    --title="$title" \
		    --width=330 \
		    --text="$msg_confirmkill")
		if [ $? != 0 ]; then
			return
		fi
	fi

	for p in $(zoneadm list -cp|grep -v global:) ; do
		zonename=$(echo "$p"|cut -d : -f2)
		if [ $DISP -eq 0 ] ; then
			gettext "destroying zone $zonename ...\n"
		fi
		zoneadm -z $zonename halt 1>/dev/null 2>&1
		zoneadm -z $zonename uninstall -F 1>/dev/null 2>&1
		unlab_template="${zonename}_unlab"
		cipso_template="${zonename}_cipso"
		delete -rRf
	done
	zonename=global
}

createDefaultZones() {
	# If GUI display is not used, skip the dialog
	if [ $DISP -eq 0 ] ; then
		createDefaultPublic $PUBZONE $DEFAULTLABEL
		if [ $? -ne 0 ] ; then
			return 1
		fi
		createSnapshot $PUBZONE $SNAPSHOT
		createDefaultInternal $INTZONE
		return
	fi

	msg_choose1=$(gettext "Choose one:")
	defpub=$(gettext "$PUBZONE zone only")
	defboth=$(gettext "$PUBZONE and $INTZONE zones")
	defskip=$(gettext "Main Menu...")
	command=$(echo ""\
	    "$defpub\n" \
	    "$defboth\n" \
	    "$defskip\n" \
	    | zenity --list \
	    --title="$title" \
	    --text="$msg_defzones" \
	    --column="$msg_choose1" \
	    --height=400 \
	    --width=330 )

	case $command in
	    " $defpub")
		createDefaultPublic $PUBZONE $DEFAULTLABEL
		if [ $? -ne 0 ] ; then
			return 1
		fi
		createSnapshot $PUBZONE $SNAPSHOT;;

	    " $defboth")
		createDefaultPublic $PUBZONE $DEFAULTLABEL
		if [ $? -ne 0 ] ; then
			return 1
		fi
		createSnapshot $PUBZONE $SNAPSHOT
		createDefaultInternal $INTZONE;;

	    *)
		return;;
	esac
}

createDefaultPublic() {
	zonename=$1
	if [ $DISP -eq 0 ] ; then
		gettext "creating default $zonename zone ...\n"
	fi
	newZone	
	zone_cnt+=1 
	hexlabel="$2"
	unlab_template="${zonename}_unlab"
	cipso_template="${zonename}_cipso"
	setTNdata
	if [ $? -ne 0 ] ; then
		return 1
	fi
	install
	if [ $? -ne 0 ] ; then
		return 1
	fi

	if [ $DISP -eq 0 ] ; then
		gettext "booting zone $zonename ...\n"
		zoneadm -z $zonename boot
	else
		zoneadm -z $zonename boot &
		gnome-terminal \
		    --disable-factory \
		    --title="Zone Console: $zonename $msg_continue" \
		    --command "zlogin -C $zonename"
	fi
}

createSnapshot() {
	zoneadm -z $1 halt
	zonename=$2
	newZone	
	zone_cnt+=1 
	zonecfg -z $zonename set autoboot=false
	clone $1
	zoneadm -z $1 boot &
}

createDefaultInternal() {
	zonename=$1
	if [ $DISP -eq 0 ] ; then
		gettext "creating default $zonename zone ...\n"
	fi
	newZone	
	zone_cnt+=1 

	hexlabel=$INTLABEL
	unlab_template="${zonename}_unlab"
	cipso_template="${zonename}_cipso"
	setTNdata
	if [ $? -ne 0 ] ; then
		return 1
	fi

	clone $SNAPSHOT
	if [ $DISP -eq 0 ] ; then
		gettext "booting zone $zonename ...\n"
	else
		gnome-terminal \
		    --title="Zone Console: $zonename" \
		    --command "zlogin -C $zonename" &
	fi
	zoneadm -z $zonename boot &
}

createZones() {
	if [[ $(zoneadm list -c) == global ]] ; then
		clonefirst=1;
	else
		snapzone=$(zoneadm list -cp|grep ${SNAPSHOT}:)
		if [ $? -ne 0 ]; then
			gettext "A $SNAPSHOT zone is required\n"
			return 1
		else
			clonefirst=0;
		fi
	fi
	cat $ZONELIST|while read zonename curlabel; do
		hexlabel=$(atohexlabel "$curlabel")
		if [ $clonefirst == 1 ] ; then
			createDefaultPublic $zonename $hexlabel
			if [ $? -ne 0 ] ; then
				return 1
			fi
			createSnapshot $zonename $SNAPSHOT
			clonefirst=0;
		else
			newZone	
			zone_cnt+=1 
			zonecfg -z $zonename "set autoboot=false;
			    set limitpriv=default,!net_mac_aware"
			unlab_template="${zonename}_unlab"
			cipso_template="${zonename}_cipso"
			setTNdata
			if [ $? -ne 0 ] ; then
				return 1
			fi
			clone $SNAPSHOT
		fi
	done
}

selectZone() {
	set -A zonelist "global running ADMIN_HIGH"
	integer zone_cnt=1

	for p in $(zoneadm list -cp|grep -v global:) ; do
		zone_cnt+=1
	done
	if [ $zone_cnt == 1 ] ; then
		createDefaultZones
	fi
	if [ $zone_cnt == 1 ] ; then
		zonename=global
		singleZone
		return
	fi

	zone_cnt=1
	for p in $(zoneadm list -cp|grep -v global:) ; do
		zonename=$(echo "$p"|cut -d : -f2)
		state=$(echo "$p"|cut -d : -f3)
		curlabel=$(tncfg -z $zonename info label 2>/dev/null)
		if [[ $curlabel ]] ; then
			curlabel="$(echo $curlabel|cut -f2 -d=|tr " " _)"
		else
			curlabel=...
		fi
		zonelist[zone_cnt]="$zonename $state $curlabel"
		zone_cnt+=1
	done
	zonename=$(zenity --list \
	    --title="$title" \
	    --text="$msg_getzone" \
	    --height=300 \
	    --width=500 \
	    --column="Zone Name" \
	    --column="Status" \
	    --column="Sensitivity Label" \
	    ${zonelist[*]}
	)

	# if the menu choice was a zonename, pop up zone menu
	if [[ -n $zonename ]] ; then
		singleZone
	else
		exit
	fi
}

# Loop for single-zone menu
singleZone() {

	while (( 1 )) do
		# Clear list of commands

		console=
		label=
		start=
		reboot=
		unmount=
		stop=
		clone=
		install=
		ready=
		uninstall=
		autoboot=
		delete=
		deletenet=
		permitrelabel=

		if [ $zone_cnt -gt 1 ] ; then
			killZones="Destroy all zones...\n"
			xit="Select another zone..."
		else
			killZones=
			xit="Exit"
		fi
		if [ $zonename = global ] ; then
			ldapClient="Create LDAP Client...\n"
			nscdOpt="$NSCD_OPT\n"
			createZone="Create a new zone...\n"
			addnet="Configure Network Interfaces...\n"
			unlab_template="admin_low"
			cipso_template="cipso"
		else
			ldapClient=
			nscdOpt=
			createZone=
			addnet=
			killZones=
			unlab_template="${zonename}_unlab"
			cipso_template="${zonename}_cipso"
		fi

		zonestate=$(zoneadm -z $zonename list -p | cut -d : -f 3)

		consoleCheck;
		labelCheck;
		delay=0

		if [ $zonename != global ] ; then
			case $zonestate in
				running)
					ready="Ready\n"
					reboot="Reboot\n"
					stop="Halt\n"
					;;
				mounted)
					unmount="Unmount\n"
					;;
				ready)
					start="Boot\n"
					stop="Halt\n"
					;;
				installed)
					if [[ -z $label ]] ; then
						ready="Ready\n"
						start="Boot\n"
					fi
					uninstall="Uninstall\n"
					relabelCheck
					autobootCheck
					;;
				configured) 
					install="Install...\n"
					cloneCheck
					delete="Delete\n"
					console=
					;;
				incomplete)
					uninstall="Uninstall\n"
					;;
				*)
				;;
			esac
		fi

		command=$(echo ""\
		    $createZone \
		    $console \
		    $label \
		    $start \
		    $reboot \
		    $unmount \
		    $stop \
		    $clone \
		    $install \
		    $ready \
		    $uninstall \
		    $delete \
		    $addnet \
		    $deletenet \
		    $addremotehost \
		    $addcipsohost \
		    $removeremotehost \
		    $removecipsohost \
		    $setmlps \
		    $permitrelabel \
		    $autoboot \
		    $ldapClient \
		    $nscdOpt \
		    $killZones \
		    $xit \
		    | zenity --list \
		    --title="$title" \
		    --text="$msg_getcmd" \
		    --height=400 \
		    --width=330 \
		    --column="Zone: $zonename   Status: $zonestate" )

		case $command in
		    " Create a new zone...")
			zonename=
			newZone ;;

		    " Zone Console...")
			delay=2
			gnome-terminal \
			    --title="Zone Console: $zonename" \
			    --command "zlogin -C $zonename" & ;;

		    " Select Label...")
			selectLabel;;

		    " Ready")
			zoneadm -z $zonename ready ;;

		    " Boot")
			zoneadm -z $zonename boot ;;

		    " Unmount")
			zoneadm -z $zonename unmount ;;

		    " Halt")
			zoneadm -z $zonename halt ;;

		    " Reboot")
			zoneadm -z $zonename reboot ;;

		    " Install...")
			install;;

		    " Clone...")
			clone ;;

		    " Uninstall")
			zoneadm -z $zonename uninstall -F;;

		    " Delete")
			delete
			return ;;

		    " Configure Network Interfaces...")
			if [ $zonename = global ] ; then
				manageNets
			else
				manageZoneNets
			fi;;	

		    " Add Single-level Access to Remote Host...")
			addTnrhdb $unlab_template ;;

		    " Add Multilevel Access to Remote Host...")
			addTnrhdb $cipso_template ;;

		    " Remove Single-level Access to Remote Host...")
			removeTnrhdb $unlab_template ;;

		    " Remove Multilevel Access to Remote Host...")
			removeTnrhdb $cipso_template ;;

		    " Configure Multilevel Ports...")
			manageMLPs $mlpType;;

		    " Permit Relabeling")
			zonecfg -z $zonename set limitpriv=default,\
win_mac_read,win_mac_write,win_selection,win_dac_read,win_dac_write,\
file_downgrade_sl,file_upgrade_sl,sys_trans_label ;;

		    " Deny Relabeling")
			zonecfg -z $zonename set limitpriv=default ;;

		    " Set Automatic Booting")
			zonecfg -z $zonename set autoboot=true ;;

		    " Set Manual Booting")
			zonecfg -z $zonename set autoboot=false ;;

		    " Create LDAP Client...")
			createLDAPclient ;;

		    " Configure per-zone name service")
			manageNscd ;;

		    " Unconfigure per-zone name service")
			manageNscd ;;

		    " Destroy all zones...")
			tearDownZones
			return ;;

		    *)
			if [ $zone_cnt == 1 ] ; then
				exit
			else
				return
			fi;;
		esac
		sleep $delay;
	done
}

# Main loop for top-level window
#

/usr/bin/plabel $$ 1>/dev/null 2>&1
if [ $? != 0 ] ; then
	gettext "$0 : Trusted Extensions must be enabled.\n"
	exit 1
fi

myzone=$(/sbin/zonename)
if [ $myzone != "global" ] ; then
	gettext "$0 : must be in global zone to run.\n"
	exit 1
fi


process_options "$@" || exit

mkdir $TXTMP 2>/dev/null
deflabel=$(chk_encodings -a|grep "Default User Sensitivity"|\
   sed 's/= /=/'|sed 's/"/'''/g|cut -d"=" -f2)
DEFAULTLABEL=$(atohexlabel ${deflabel})
intlabel=$(chk_encodings -a|grep "Default User Clearance"|\
   sed 's/= /=/'|sed 's/"/'''/g|cut -d"=" -f2)
INTLABEL=$(atohexlabel -c "${intlabel}")

AUTOMOUNTDIR=/var/tsol/doors/automount
if [ ! -d $AUTOMOUNTDIR ] ; then
	mkdir $AUTOMOUNTDIR
fi

# are there any zfs pools?
ZDSET=none
zpool iostat 1>/dev/null 2>&1
if [ $? = 0 ] ; then
	# is there a zfs pool named "zone"?
	zpool list -H zone 1>/dev/null 2>&1
	if [ $? = 0 ] ; then
		# yes
		ZDSET=zone
	else
		# no, but is there a root pool?
		rootfs=$(df -n / | awk '{print $3}')
		if [ $rootfs = "zfs" ] ; then
			# yes, use it
			ZDSET=$(zfs list -Ho name / | cut -d/ -f 1)/zones
			zfs list -H $ZDSET 1>/dev/null 2>&1
			if [ $? = 1 ] ; then
				createZDSET "-o mountpoint=/zone" $ZDSET
			fi
		fi
	fi
fi

if [ $DISP -eq 0 ] ; then
	gettext "non-interactive mode ...\n"

	if [[ $ZONELIST ]]; then
		createZones
		exit
	fi

	if [ $DESTROYZONES -eq 1 ] ; then
		tearDownZones
	fi

	if [ $CREATEDEF -eq 1 ] ; then
		if [[ $(zoneadm list -c) == global ]] ; then
			createDefaultZones
		else
			gettext "cannot create default zones because there are existing zones.\n"
		fi
	fi

	exit
fi

if [ $NSCD_PER_LABEL -eq 0 ] ; then
	NSCD_OPT="Configure per-zone name service"
else
	NSCD_OPT="Unconfigure per-zone name service"
fi


while (( 1 )) do
	selectZone
done
