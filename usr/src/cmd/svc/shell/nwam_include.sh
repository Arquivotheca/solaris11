#!/usr/sbin/sh
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

. /lib/svc/share/smf_include.sh
. /lib/svc/share/net_include.sh

# FMRI constants
IPSEC_IKE_FMRI="svc:/network/ipsec/ike"
IPSEC_POLICY_FMRI="svc:/network/ipsec/policy"
IPFILTER_FMRI="svc:/network/ipfilter:default"
DNS_CLIENT_FMRI="svc:/network/dns/client:default"
LDAP_CLIENT_FMRI="svc:/network/ldap/client:default"
NIS_DOMAIN_FMRI="svc:/network/nis/domain:default"
NIS_CLIENT_FMRI="svc:/network/nis/client:default"
NP_DEFAULT_FMRI="svc:/network/physical:default"
NP_NWAM_FMRI="svc:/network/physical:nwam"
NET_LOC_FMRI="svc:/network/location:default"
NFS_MAPID_FMRI="svc:/network/nfs/mapid:default"
NS_SWITCH_FMRI="svc:/system/name-service/switch:default"

# commands
BASENAME=/usr/bin/basename
CP=/usr/bin/cp
GREP=/usr/bin/grep
LDAPCLIENT=/usr/sbin/ldapclient
MKDIR=/usr/bin/mkdir
NAWK=/usr/bin/nawk
NETCFG=/usr/sbin/netcfg
PFEXEC=/usr/bin/pfexec
SED=/usr/bin/sed
SU=/usr/bin/su
SVCADM=/usr/sbin/svcadm
SVCCFG=/usr/sbin/svccfg
SVCPROP=/usr/bin/svcprop
TAIL=/usr/bin/tail

# Path to directories
# We don't have a writable file system so we write to $SMF_SYSVOL_FS and
# then later copy anything interesting to /etc/nwam.
VOL_NETCFG_PATH=$SMF_SYSVOL_FS/netcfg
VOL_LEGACY_PATH=$VOL_NETCFG_PATH/Legacy
LEGACY_LOC_PATH=/etc/nwam/loc/Legacy

#
# copy_to_legacy_loc <file>
#
# Copies the file to the Legacy location directory
# (in $SMF_SYSVOL_FS/nwam/Legacy)
#
copy_to_legacy_loc() {
	$MKDIR -p $VOL_LEGACY_PATH
	if [ -f "$1" ]; then
		$CP -p $1 $VOL_LEGACY_PATH
	fi
}

#
# copy_from_legacy_loc <destination file>
#
# Copies file with the same name from Legacy location
# (in /etc/nwam/loc/Legacy) to the given destination file
#
copy_from_legacy_loc () {
	DEST_DIR=`/usr/bin/dirname $1`
	SRC_FILE="$LEGACY_LOC_PATH/`$BASENAME $1`"

	# Make destination directory if needed
	if [ ! -d "$DEST_DIR" ]; then
		$MKDIR -p $DEST_DIR
	fi

	if [ -f "$SRC_FILE" ]; then
		$CP -p $SRC_FILE $DEST_DIR
	fi
}

#
# write_loc_prop <property> <value> <file>
#
# Appends to <file> a netcfg command to set <property> to <value> if non-empty
#
write_loc_prop () {
	prop=$1
	val=$2
	file=$3

	if [ -n "$val" -a -n "$file" ]; then
		echo "set $prop=$val" >> $file
	fi
}

#
# set_smf_prop <fmri> <property name> <property value>
#
set_smf_prop () {
	$SVCCFG -s $1 setprop $2 = astring: \"$3\" && return
}

#
# get_smf_prop <fmri> <property name>
#
get_smf_prop () {
	$SVCPROP -p $2 $1 2>/dev/null
}

#
# loc_exists <location>
#
# Checks if the specified location exists
#
loc_exists ()
{
	$NETCFG "select loc \"$1\"" >/dev/null 2>&1 && return 0
	return 1
}

#
# get_smf_comma_prop <fmri> <property name>
#
# Returns the SMF value separated by commas
#
get_smf_comma_prop () {
	$SVCPROP -p $2 $1 2>/dev/null | $SED s/" "/","/g
}

#
# Creates Legacy location from the current configuration
#
create_legacy_loc () {
	LEGACY_LOC_SCRIPT=$VOL_NETCFG_PATH/create_loc_legacy

	if loc_exists Legacy ; then
		echo "destroying existing 'Legacy' location"
		# Only the "netadm" user can destroy the Legacy location
		$SU netadm -c "$NETCFG destroy loc Legacy"
	fi

	#
	# Write netcfg commands to create Legacy location to
	# $LEGACY_LOC_SCRIPT as values for properties are determined
	# Note that some of the *_CONFIG_FILE variables point at copies of
	# files we've made and others indicate where those copies should be
	# if we are enabling the location.
	#
	echo "create loc Legacy" > $LEGACY_LOC_SCRIPT
	write_loc_prop "activation-mode" "system" $LEGACY_LOC_SCRIPT

	NAMESERVICES=""
	NAMESERVICES_CONFIG_FILE=""
	DNS_CONFIGSRC=""
	DNS_DOMAIN=""
	DNS_SERVERS=""
	DNS_SEARCH=""
	DNS_SORTLIST=""
	DNS_OPTIONS=""
	NIS_CONFIGSRC=""
	NIS_SERVERS=""
	LDAP_CONFIGSRC=""
	LDAP_SERVERS=""
	DEFAULT_DOMAIN=""

	# Copy the /etc/nsswitch.conf file
	copy_to_legacy_loc /etc/nsswitch.conf
	NAMESERVICES_CONFIG_FILE="$VOL_LEGACY_PATH/nsswitch.conf"
	
	#
	# If the "host" entry of svc:/system/name-service/switch:default has
	# "dns", then DNS is being used.  Gather DNS information from
	# svc:/network/dns/client.
	#
	$SVCPROP -p config/host $NS_SWITCH_FMRI | $GREP "dns" >/dev/null
	if [ $? -eq 0 ] &&  service_is_enabled $DNS_CLIENT_FMRI ; then
		DNS_DOMAIN=`get_smf_comma_prop $DNS_CLIENT_FMRI config/domain`
		DNS_SERVERS=`get_smf_comma_prop $DNS_CLIENT_FMRI config/nameserver`
		DNS_SEARCH=`get_smf_comma_prop $DNS_CLIENT_FMRI config/search`
		DNS_SORTLIST=`get_smf_comma_prop $DNS_CLIENT_FMRI config/sortlist`
		DNS_OPTIONS=`get_smf_comma_prop $DNS_CLIENT_FMRI config/options`
		#
		# If both domain and search exists, use the last one as seen
		# in /etc/resolv.conf
		#
		if [ -n "$DNS_DOMAIN" -a -n "$DNS_SEARCH" ]; then
			last=`$TAIL -r /etc/resolv.conf | $NAWK 
			    '$1 == "domain" || $1 == "search" \
			    { print $1 ; exit }'`
			if [ "$last" == "search" ]; then
				DNS_DOMAIN=""
			else
				DNS_SEARCH=""
			fi
		fi
		copy_to_legacy_loc /etc/resolv.conf

		# save DNS only if nameservers exist
		if [ -n "$DNS_SERVERS" ]; then
			NAMESERVICES="dns,"
			DNS_CONFIGSRC="manual"
		else
			DNS_DOMAIN=""
			DNS_SERVERS=""
			DNS_SEARCH=""
			DNS_SORTLIST=""
			DNS_OPTIONS=""
		fi
	fi

	#
	# Gather NIS info from svc:/network/nis/domain if
	# svc:/network/nis/client is enabled.
	#
	if service_is_enabled $NIS_CLIENT_FMRI ; then
		DEFAULT_DOMAIN=`get_smf_prop $NIS_DOMAIN_FMRI config/domainname`
		yp_servers=`get_smf_prop $NIS_DOMAIN_FMRI config/ypservers`
		for serv in $yp_servers; do
			if is_valid_addr $serv; then
				addr="$serv,"
			else
				addr=`$GREP -iw $serv /etc/inet/hosts \
				    | $NAWK '{ printf "%s,", $1 }'`
			fi
			NIS_SERVERS="${NIS_SERVERS}$addr"
		done

		# save NIS only if the default-domain is set
		if [ -n "$DEFAULT_DOMAIN" ]; then
			NAMESERVICES="${NAMESERVICES}nis," 
			NIS_CONFIGSRC="manual"
		fi
	fi

	# Gather LDAP info via ldapclient(1M) if LDAP is enabled.
	if service_is_enabled $LDAP_CLIENT_FMRI:default ; then
		copy_to_legacy /var/ldap/ldap_client_file
		copy_to_legacy /var/ldap/ldap_client_cred

		DEFAULT_DOMAIN=`get_smf_prop $NIS_DOMAIN_FMRI config/domainname`
		LDAP_SERVERS=`$PFEXEC $LDAPCLIENT list 2>/dev/null \
		    | $NAWK '$1 == "NS_LDAP_SERVERS=" { print $2 }'`

		# save LDAP only if both domain and servers exist
		if [ -n "$LDAP_SERVERS" -a -n "$DEFAULT_DOMAIN" ];
		then
			NAMESERVICES="${NAMESERVICES}ldap,"
			LDAP_CONFIGSRC="manual"
		fi
	fi
	# if $NAMESERVICES is empty, set it to "files"
	if [ -z "$NAMESERVICES" ]; then
		NAMESERVICES="files"
	fi

	# Now, write netcfg commands for nameservices
	write_loc_prop "nameservices" $NAMESERVICES $LEGACY_LOC_SCRIPT
 	write_loc_prop "nameservices-config-file" $NAMESERVICES_CONFIG_FILE \
 	    $LEGACY_LOC_SCRIPT
	write_loc_prop "dns-nameservice-configsrc" $DNS_CONFIGSRC \
	    $LEGACY_LOC_SCRIPT
	write_loc_prop "dns-nameservice-domain" $DNS_DOMAIN $LEGACY_LOC_SCRIPT
	write_loc_prop "dns-nameservice-servers" $DNS_SERVERS $LEGACY_LOC_SCRIPT
	write_loc_prop "dns-nameservice-search" $DNS_SEARCH $LEGACY_LOC_SCRIPT
	write_loc_prop "dns-nameservice-sortlist" $DNS_SORTLIST \
	    $LEGACY_LOC_SCRIPT
	write_loc_prop "dns-nameservice-options" $DNS_OPTIONS $LEGACY_LOC_SCRIPT
	write_loc_prop "nis-nameservice-configsrc" $NIS_CONFIGSRC \
	    $LEGACY_LOC_SCRIPT
	write_loc_prop "nis-nameservice-servers" $NIS_SERVERS $LEGACY_LOC_SCRIPT
	write_loc_prop "ldap-nameservice-configsrc" $LDAP_CONFIGSRC\
	    $LEGACY_LOC_SCRIPT
	write_loc_prop "ldap-nameservice-servers" $LDAP_SERVERS \
	    $LEGACY_LOC_SCRIPT
	write_loc_prop "default-domain" $DEFAULT_DOMAIN $LEGACY_LOC_SCRIPT
	#
	# Creating a location sets the dns-nameservice-configsrc to dhcp by
	# default.  Remove that property if DNS is not configured.
	#
	echo $NAMESERVICES | $GREP "dns" >/dev/null
	if [ $? -eq 1 ]; then
		echo "clear dns-nameservice-configsrc" >> "$LEGACY_LOC_SCRIPT"
	fi

	# Retrieve NFSv4 domain from SMF.
	if service_is_enabled $NFS_MAPID_FMRI ; then
		NFS_DOMAIN=`get_smf_prop $NFS_MAPID_FMRI \
		    nfs-props/nfsmapid_domain`    
		# empty values are returned as the two-character string ""
		if [ $NFS_DOMAIN != \"\" ]; then
			write_loc_prop "nfsv4-domain" $NFS_DOMAIN \
			    $LEGACY_LOC_SCRIPT
		fi
	fi

	IPF_CONFIG_FILE=""
	IPF6_CONFIG_FILE=""
	IPNAT_CONFIG_FILE=""
	IPPOOL_CONFIG_FILE=""
	IKE_CONFIG_FILE=""
	IPSEC_POLICY_CONFIG_FILE=""

	#
	# IPFilter
	#
	# If the firewall policy is "custom", simply copy the
	# custom_policy_file.  If the firewall policy is "none", "allow" or
	# "deny", save the value as "/<value>".  When reverting back to the
	# Legacy location, these values will have to be treated as special.
	#
	# For all configuration files, copy them to the Legacy directory.
	# Use the respective properties to remember the original locations
	# of the files so that they can be copied back there when NWAM is
	# stopped.
	#
	if service_is_enabled $IPFILTER_FMRI ; then
		FIREWALL_POLICY=`get_smf_prop $IPFILTER_FMRI \
		    firewall_config_default/policy`
		if [ "$FIREWALL_POLICY" = "custom" ]; then
			IPF_CONFIG_FILE=`get_smf_prop $IPFILTER_FMRI \
			    firewall_config_default/custom_policy_file`
			copy_to_legacy_loc $IPF_CONFIG_FILE
		else
			# save value as /none, /allow, or /deny
			IPF_CONFIG_FILE="/$FIREWALL_POLICY"
		fi
		IPF6_CONFIG_FILE=`get_smf_prop $IPFILTER_FMRI \
		    config/ipf6_config_file`
		copy_to_legacy_loc $IPF6_CONFIG_FILE

		IPNAT_CONFIG_FILE=`get_smf_prop $IPFILTER_FMRI \
		    config/ipnat_config_file`
		copy_to_legacy_loc $IPNAT_CONFIG_FILE

		IPPOOL_CONFIG_FILE=`get_smf_prop $IPFILTER_FMRI \
		    config/ippool_config_file`
		copy_to_legacy_loc $IPPOOL_CONFIG_FILE
	fi

	# IKE
	if service_is_enabled $IPSEC_IKE_FMRI:default ; then
		IKE_CONFIG_FILE=`get_smf_prop $IPSEC_IKE_FMRI config/config_file`
		copy_to_legacy_loc $IKE_CONFIG_FILE
	fi

	# IPsec
	if service_is_enabled $IPSEC_POLICY_FMRI:default ; then
		IPSEC_POLICY_CONFIG_FILE=`get_smf_prop $IPSEC_POLICY_FMRI \
		    config/config_file`
		copy_to_legacy_loc $IPSEC_POLICY_CONFIG_FILE
	fi

	if [ -n "$IPF_CONFIG_FILE" -a \( "$IPF_CONFIG_FILE" = "/allow" \
	    -o "$IPF_CONFIG_FILE" = "/deny" -o "$IPF_CONFIG_FILE" = "/none" \
	    -o -f "$IPF_CONFIG_FILE" \) ]; then
		write_loc_prop "ipfilter-config-file" $IPF_CONFIG_FILE \
		    $LEGACY_LOC_SCRIPT
	fi
	if [ -n "$IPF6_CONFIG_FILE" -a -f "$IPF6_CONFIG_FILE" ]; then
		write_loc_prop "ipfilter-v6-config-file" $IPF6_CONFIG_FILE \
		    $LEGACY_LOC_SCRIPT
	fi
	if [ -n "$IPNAT_CONFIG_FILE" -a -f "$IPNAT_CONFIG_FILE" ]; then
		write_loc_prop "ipnat-config-file" $IPNAT_CONFIG_FILE \
		    $LEGACY_LOC_SCRIPT
	fi   
	if [ -n "$IPPOOL_CONFIG_FILE" -a -f "$IPPOOL_CONFIG_FILE" ]; then
		write_loc_prop "ippool-config-file" $IPPOOL_CONFIG_FILE \
		    $LEGACY_LOC_SCRIPT
	fi
	if [ -n "$IKE_CONFIG_FILE" -a -f "$IKE_CONFIG_FILE" ]; then
		write_loc_prop "ike-config-file" $IKE_CONFIG_FILE \
		    $LEGACY_LOC_SCRIPT
	fi
	if [ -n "$IPSEC_POLICY_CONFIG_FILE" -a -f "$IPSEC_POLICY_CONFIG_FILE" ]
	then
		write_loc_prop "ipsecpolicy-config-file" \
		    $IPSEC_POLICY_CONFIG_FILE $LEGACY_LOC_SCRIPT
	fi

	# End
	echo "end" >> $LEGACY_LOC_SCRIPT
	# network/location will create the Legacy location with these commands.
}

#
# nwam_is_running()
#
# Returns 0 if /lib/inet/nwamd is running in the current zone, 1 if not.
#
nwam_is_running()
{
	/usr/bin/pgrep -z `smf_zonename` nwamd >/dev/null
}

#
# reactive_ncp_enabled()
#
# Returns 0 if the currently active ncp is reactive, 1 if not.
#
reactive_ncp_enabled()
{
	ncp=`$SVCPROP -p netcfg/active_ncp $NP_DEFAULT_FMRI`
	[ "$ncp" = "DefaultFixed" ] && return 1
	return 0
}

#
# upgrade_nwamd_props
#
# Transfers the nwamd property group from svc:/network/physical:nwam
# to svc:/network/physical:default, with a few exceptions:
#   - the nwamd/active_ncp property is handled separately, so is skipped here
#   - the nwamd/version property is generalized, and will be set to
#       netcfg/version = 2 by nwamd; it is not transferred here.
#
upgrade_nwamd_props()
{
	$SVCCFG -s $NP_DEFAULT_FMRI addpg nwamd application >/dev/null 2>&1
	$SVCPROP -p nwamd $NP_NWAM_FMRI | $NAWK \
	    '$1 != "nwamd/active_ncp" && $1 != "nwamd/version" {\
	    system("/usr/sbin/svccfg -s svc:/network/physical:default "\
	    "setprop " $1 " = "$2 ": " $3) }'
}

#
# Refreshes nwam, to handle a change in active ncp.  Sends a SIGHUP
# to nwamd, and ensures that svc:/network/location is enabled.
#
nwam_refresh()
{
	/usr/bin/pkill -HUP -z `smf_zonename` nwamd

	# Enable network/location.
	if service_exists $NET_LOC_FMRI ; then
		$SVCADM enable $NET_LOC_FMRI
	fi
}

#
# Performs shutdown tasks for nwam: kills nwamd, restores the Legacy
# location settings, and disables svc:/network/location:default.
#
nwam_stop()
{
	/usr/bin/pkill -z `smf_zonename` nwamd
	# If nwamd was not running, skip the rest of the steps
	if [ $? -eq 1 ]; then
		return
	fi

	#
	# To restore the non-NWAM settings, set the location/selected property
	# of svc:/network/location:default to Legacy and refresh the service.
	#
	$NETCFG list loc Legacy >/dev/null 2>&1
	if [ $? -eq 1 ]; then
		echo "No Legacy location to revert to!"
	else
		$SVCCFG -s $NET_LOC_FMRI setprop location/selected = \
		    astring: "Legacy"
		$SVCADM refresh $NET_LOC_FMRI
	fi
}

#
# Performs start-up tasks for nwam: enables svc:/network/location:default,
# creates Legacy location, and starts nwamd.
#
nwam_start()
{
	#
	# Enable network/location.
	#
	if service_exists $NET_LOC_FMRI ; then
		$SVCADM enable $NET_LOC_FMRI
	fi

	#
	# We also need to create the Legacy location, which is used
	# to restore non-NWAM settings that are overwritten when
	# NWAM is enabled (e.g. resolv.conf, nsswitch.conf, etc.).
	#
	$NETCFG list loc Legacy >/dev/null 2>&1
	if [ $? -eq 1 ]; then
		create_legacy_loc
	fi

	# start nwamd in foreground; it will daemonize itself
	if /lib/inet/nwamd ; then
		exit $SMF_EXIT_OK
	else
		exit $SMF_EXIT_ERR_FATAL
	fi
}
