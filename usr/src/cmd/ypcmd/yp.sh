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
# Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
#

. /lib/svc/share/smf_include.sh
. /lib/svc/share/ipf_include.sh

YPDIR=/usr/lib/netsvc/yp

create_client_ipf_rules()
{
	FMRI=$1
	file=`fmri_to_file $FMRI $IPF_SUFFIX`
	iana_name=`svcprop -p $FW_CONTEXT_PG/name $FMRI`
	domain=`domainname`

	if [ -z "$domain" ]; then
		return 0
	fi

	# ypbind creates this directory after it has started.
	if [ ! -d $SMF_SYSVOL_FS/daemon/ypbind ]; then
		return
	fi
	echo "# $FMRI" >$file

	ypfile="/var/yp/binding/$domain/ypservers"
	if [ -f $ypfile ]; then
		tports=`$SERVINFO -R -p -t -s $iana_name 2>/dev/null`
		uports=`$SERVINFO -R -p -u -s $iana_name 2>/dev/null`

		server_addrs=""
		for ypsvr in `grep -v '^[ ]*#' $ypfile`; do
			#
			# Get corresponding IPv4 address in /etc/hosts
			#
			servers=`grep -v '^[ ]*#' /etc/hosts | awk ' {
			    if ($1 !~/:/) {
				for (i=2; i<=NF; i++) {
				    if (s == $i) printf("%s ", $1);
				} }
			    }' s="$ypsvr"`

			[ -z "$servers"  ] && continue
			server_addrs="$server_addrs $servers"
		done

		[ -z "$server_addrs"  ] && return 0
		for s in $server_addrs; do
			if [ -n "$tports" ]; then
				for tport in $tports; do
					echo "pass in log quick proto tcp" \
					    "from $s to any port = $tport" >>$file
				done
			fi

			if [ -n "$uports" ]; then
				for uport in $uports; do
					echo "pass in log quick proto udp" \
					    "from $s to any port = $uport" >>$file
				done
			fi
		done
	else
		#
		# How do we handle the client broadcast case? Server replies
		# to the outgoing port that sent the broadcast, but there's
		# no way the client know a packet is the reply.
		#
		# Nis server should be specified and clients shouldn't be
		# doing broadcasts but if it does, no choice but to allow
		# all traffic.
		#
		echo "pass in log quick proto udp from any to any" \
		    "port > 32768" >>$file
	fi
}

#
# Ipfilter method
#
if [ -n "$1" -a "$1" = "ipfilter" ]; then
	create_client_ipf_rules $2
	exit $SMF_EXIT_OK
fi

case $SMF_FMRI in
	'svc:/network/nis/domain:default')
		# Initialize NIS domainame from SMF configuration
		case "$1" in
		'start')
			# Test and import if upgrade
			/usr/sbin/nscfg import -q $SMF_FMRI
			err=$?
			if [ $err -eq 1 ] ; then
				echo "WARNING: $SMF_FMRI configuration" \
				     "import error." >& 2
				exit $SMF_EXIT_ERR_CONFIG
			elif [ $err -eq 3 ] ; then
				echo "WARNING: $SMF_FMRI no configuration." >& 2
				exit $SMF_EXIT_ERR_CONFIG
			fi
			/usr/sbin/nscfg export $SMF_FMRI
			err=$?
			if [ $err -eq 1 ] ; then
				echo "WARNING: $SMF_FMRI configuration" \
				     "export error." >& 2
				exit $SMF_EXIT_ERR_CONFIG
			fi
			DOMAIN=`svcprop -p config/domainname $SMF_FMRI`
			/usr/bin/domainname $DOMAIN
			;;
		'refresh')
			/usr/sbin/nscfg export $SMF_FMRI
			err=$?
			if [ $err -eq 2 ] ; then
				exit $SMF_EXIT_OK
			elif [ $err -ne 0 ] ; then
				echo "Unable to create NIS configuration" >& 2
				exit $SMF_EXIT_ERR_CONFIG
			fi
			DOMAIN=`svcprop -p config/domainname $SMF_FMRI`
			/usr/bin/domainname $DOMAIN
			;;
		'unconfigure')
			domain=`domainname`
			# Permanently shutdown service
			svcadm disable $SMF_FMRI 
			# Unroll any admin customization
			svccfg -s svc:/network/nis/domain delcust
			if [ $? -ne 0 ]; then
				echo "Failed to unroll administrative customizations for $SMF_FMRI"
				exit $SMF_EXIT_ERR_FATAL
			fi
			if [ -n "$domain" ]; then
				rm -rf /var/yp/binding/$domain
			fi
			rm -f /etc/defaultdomain
			/usr/bin/domainname ""
			;;
		esac
		;;

	'svc:/network/nis/client:default')
		case "$1" in
		'start')
			domain=`domainname`

			if [ -z "$domain" ]; then
				echo "$0: domainname not set"
				exit $SMF_EXIT_ERR_CONFIG
			fi

			# Since two ypbinds will cause ypwhich to hang...
			if pgrep -z `/usr/sbin/zonename` ypbind >/dev/null; then
				echo "$0: ypbind is already running."
				exit $SMF_EXIT_ERR_CONFIG
			fi

			bcprop="config/use_broadcast"
			bc=`svcprop -p $bcprop $SMF_FMRI 2>/dev/null`
			if [ "$bc" = "true" ] ; then
				$YPDIR/ypbind -broadcast > /dev/null 2>&1
			elif [ -f /var/yp/binding/$domain/ypservers ]; then
				$YPDIR/ypbind > /dev/null 2>&1
			else
				$YPDIR/ypbind -broadcast > /dev/null 2>&1
			fi

			rc=$?
			if [ $rc != 0 ]; then
				echo "$0: ypbind failed with $rc"
				exit 1
			fi
			;;
		'unconfigure')
			# Permanently shutdown service
			svcadm disable $SMF_FMRI 
			# Unroll any admin customization
			svccfg -s svc:/network/nis/client delcust
			if [ $? -ne 0 ]; then
				echo "Failed to unroll administrative customizations for $SMF_FMRI"
				exit $SMF_EXIT_ERR_FATAL
			fi
			;;
		esac
		;;

	'svc:/network/nis/server:default')
		case "$1" in
		'start')
			domain=`domainname`

			if [ -z "$domain" ]; then
				echo "$0: domainname not set"
				exit $SMF_EXIT_ERR_CONFIG
			fi

			if [ ! -d /var/yp/$domain ]; then
				echo "$0: domain directory missing"
				exit $SMF_EXIT_ERR_CONFIG
			fi

			if [ -f /etc/resolv.conf ]; then
				$YPDIR/ypserv -d
			else
				$YPDIR/ypserv
			fi

			rc=$?
			if [ $rc != 0 ]; then
				echo "$0: ypserv failed with $rc"
				exit 1
			fi
			;;
		'unconfigure')
			domain=`domainname`
			# Permanently shutdown service
			svcadm disable $SMF_FMRI 
			# Unroll any admin customization
			svccfg -s svc:/network/nis/server delcust
			if [ $? -ne 0 ]; then
				echo "Failed to unroll administrative customizations for $SMF_FMRI"
				exit $SMF_EXIT_ERR_FATAL
			fi
			if [ -n "$domain" ]; then
				rm -rf /var/yp/$domain/*.*
			fi
			;;
		esac
		;;

	'svc:/network/nis/passwd:default')
		PWDIR=`grep "^PWDIR" /var/yp/Makefile 2> /dev/null` \
		    && PWDIR=`expr "$PWDIR" : '.*=[ 	]*\([^ 	]*\)'`
		if [ "$PWDIR" ]; then
			if [ "$PWDIR" = "/etc" ]; then
				unset PWDIR
			else
				PWDIR="-D $PWDIR"
			fi
		fi
		$YPDIR/rpc.yppasswdd $PWDIR -m

		rc=$?
		if [ $rc != 0 ]; then
			echo "$0: rpc.yppasswdd failed with $rc"
			exit 1
		fi
		;;

	*)
		echo "$0: Unknown service \"$SMF_FMRI\"."
		exit $SMF_EXIT_ERR_CONFIG
		;;
esac
exit $SMF_EXIT_OK
