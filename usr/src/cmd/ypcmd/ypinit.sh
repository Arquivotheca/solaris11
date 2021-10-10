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
# Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.

#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

# Portions of this source code were derived from Berkeley 4.3 BSD
# under license from the Regents of the University of California.

. /lib/svc/share/smf_include.sh

# set -xv
YPXFR=/usr/lib/netsvc/yp/ypxfr
MAKEPATH=/usr/bin
SVCS=/usr/bin/svcs
maps="publickey publickey.byname"
yproot_dir=/var/yp
yproot_exe=/usr/sbin/yp
hf=$SMF_SYSVOL_FS/ypservers.$$
XFR=${YPXFR}

hosts_file=/etc/hosts
hosts6_file=/etc/inet/ipnodes
clientp=F
masterp=F
slavep=F
host=""
def_dom=""
master=""
got_host_list=F
first_time=T
exit_on_error=F
errors_in_setup=F

enable_next_boot () {
	ONLINE=`$SVCS -o STATE $1 2>>/dev/null | grep "^online"`
	if [ -z "$ONLINE" ]
	then
		# Make sure it will be up the next boot
		/usr/sbin/svccfg -s $1 \
		    setprop general/enabled = true
		[ $? = 0 ] || echo "ypinit: unable to enable $1 for next boot"
	else
		# Service was enabled already, only restart is needed
		/usr/sbin/svcadm restart $1
		[ $? = 0 ] || echo "ypinit: unable to restart $1"
	fi
}

enable_this_boot () {
	/usr/sbin/svcadm enable $1
	[ $? = 0 ] || echo "ypinit: unable to enable $1"
}

is_valid_ipaddr () {
	test -n "`echo $1 | awk 'NF != 1 {exit} \
	    $1 !~ /[0-9]/ || /[;-~]/ || /!--/ || /\// {exit} \
	    $1 !~ /\./ {exit} {print}'`" || \
	test -n "`echo $1 | awk 'NF != 1 {exit} \
	    ($1 !~ /[0-9]/ && $1 !~ /[A-F]/ && \
	    $1 !~ /[a-f]/) || \
	    /[;-@]/ || /[G-\`]/ || /[g-~]/ || /!--/ || \
	    /\// {exit} \
	    $1 !~ /:/ {exit} {print}'`"
}

is_service_avail () {
	$SVCS $1 2>/dev/null >/dev/null
	if [ $? -ne 0 ]
	then
		echo "ypinit: missing $1, install NIS service package"
		exit 1
	fi
}

PATH=/bin:/usr/bin:/usr/sbin:$yproot_exe:$MAKEPATH:$PATH
export PATH

# To do cleanup
trap '/usr/bin/rm -f $hf' 0 1 2 3 15

case $# in
1)	case $1 in
	-c)	clientp=T;;
	-m)	masterp=T;;
	*)	echo 'usage:'
		echo '	ypinit -c'
		echo '	ypinit -m'
		echo '	ypinit -s master_server'
		echo ""
		echo "\
where -c is used to set up a yp client, -m is used to build a master "
                echo "\
yp server data base, and -s is used for a slave data base."
		echo "\
master_server must be an existing reachable yp server."
		exit 1;;
	esac;;

2)	case $1 in
	-s)	slavep=T; master=$2;
		if ( grep $master $hosts_file $hosts6_file > /dev/null )
		then
			echo ""
		else
			echo "server not found in $hosts_file or $hosts6_file"
			exit 1
		fi;;
		
	*)	echo 'usage:'
		echo '	ypinit -c'
		echo '	ypinit -m'
		echo '	ypinit -s master_server'
		echo ""
		echo "\
where -c is used to set up a yp client, -m is used to build a master "
                echo "\
yp server data base, and -s is used for a slave data base."
		echo "\
master_server must be an existing reachable yp server."
		exit 1;;
	esac;;
3)	case $1 in
	-c)	clientp=T;;
	*)	echo 'usage:'
		echo '	ypinit -c'
		echo '	ypinit -m'
		echo '	ypinit -s master_server'
		echo ""
		echo "\
where -c is used to set up a yp client, -m is used to build a master "
                echo "\
yp server data base, and -s is used for a slave data base."
		echo "\
master_server must be an existing reachable yp server."
		exit 1;;
	esac;;

*)	echo 'usage:'
	echo '	ypinit -c'
	echo '	ypinit -m'
	echo '	ypinit -s master_server'
	echo ""
	echo "\
where -c is used to set up a yp client, -m is used to build a master "
	echo "\
yp server data base, and -s is used for a slave data base."
	echo "\
master_server must be an existing reachable yp server."
	exit 1;;
esac

if [ $? -ne 0 ]
then
	echo "\
You have to be the superuser to run this.  Please log in as root."
	exit 1
fi

host=`uname -n`

if [ $? -ne 0 ]
then
	echo "Can't get local host's name.  Please check your path."
	exit 1
fi

if [ -z "$host" ]
then
	echo "The local host's name hasn't been set.  Please set it."
	exit 1
fi

def_dom=`domainname`

if [ $? -ne 0 ]
then
	echo "Can't get local host's domain name.  Please check your path."
	exit 1
fi

if [ -z "$def_dom" ]
then
	echo "The local host's domain name hasn't been set.  Please set it."
	exit 1
fi

domainname $def_dom
real_def_dom=$def_dom
#def_dom=`ypalias -d $def_dom`
ypservers_map=`ypalias ypservers`
domain_dir="$yproot_dir""/""$def_dom"
binding_dir="$yproot_dir""/binding/""$def_dom"
binding_file="$yproot_dir""/binding/""$def_dom""/ypservers"

if [ ! -d $yproot_dir -o -f $yproot_dir ]
then
    echo "\
The directory $yproot_dir doesn't exist.  Restore it from the distribution."
	exit 1
fi

# add domainname and ypservers aliases to aliases file
echo ypservers $ypservers_map >> $yproot_dir/aliases
echo $real_def_dom $def_dom >> $yproot_dir/aliases
sort $yproot_dir/aliases | uniq > $SMF_SYSVOL_FS/.ypaliases; mv $SMF_SYSVOL_FS/.ypaliases $yproot_dir/aliases

if [ ! -d "$yproot_dir"/binding ]
then
	mkdir "$yproot_dir"/binding
fi

if [ ! -d  $binding_dir ]
then
	mkdir  "$binding_dir"
fi

if [ $slavep = F ]
then
	while [ $got_host_list = F ]; do
		touch $hf    # make sure file exists
		echo ""
		echo "\
In order for NIS to operate successfully, we have to construct a list of the "
		echo "\
NIS servers.  Please continue to add the names for YP servers in order of"
		echo "\
preference, one per line.  When you are done with the list, type a <control D>"
		echo "\
or a return on a line by itself."
		if [ $masterp = T ]
		then
			echo $host > $hf
			echo "\tnext host to add:  $host"
		elif [ -f $binding_file ]
		then
			if [ $first_time = T ]
			then
				grep -v '^#' $binding_file | \
				    while read h
				do
					echo $h >> $hf
					echo "\tnext host to add:  $h"
				done
			fi
		fi

		echo  "\tnext host to add:  \c"

		while read h ; test -n "$h"
		do
			#
			# Host should be in the v4 or v6 hosts file or
			# reasonably resemble an IP address.  We'll do a
			# sanity check that a v4 addr is one word consisting
			# of only numbers and the "." character,
			# which should guard against fully qualified
			# hostnames and most malformed entries.  IPv6
			# addresses can be numbers, hex letters, and have
			# at least one ":" character and possibly one or
			# more "." characters for embedded v4 addresses
			#
			if ( grep $h $hosts_file $hosts6_file > /dev/null ) || \
			    ( test $clientp = T && `is_valid_ipaddr $h` )
			then
				echo $h >> $hf
				echo  "\tnext host to add:  \c"
			else
				echo "host $h not found in $hosts_file or" \
				    "$hosts6_file.\nNot added to the list."
				echo ""
				echo  "Do you wish to abort [y/n: y]  \c"
				read cont_ok

				case $cont_ok in
				n*)	echo "\tnext host to add:  \c";;	
				N*)	echo "\tnext host to add:  \c";;	
				*)	exit 1;;
				esac
			fi

		done

		echo ""
		if [ -s $hf ]
		then
			echo "The current list of yp servers looks like this:"
			echo ""
			cat $hf
			echo ""
			echo "Is this correct?  [y/n: y]  \c"
		else
			echo "You have not added any server information."
			echo ""
			echo "Do you still wish to exit? [y/n: y]  \c"
		fi

		read hlist_ok

		case $hlist_ok in
		n*)	got_host_list=F
			first_time=F
			rm $hf
			echo "Let's try the whole thing again...";;
		N*)	got_host_list=F
			first_time=F
			rm $hf
			echo "Let's try the whole thing again...";;
		*)	got_host_list=T;;
		esac
	done

	if [ -s $hf ]
	then
		cp  $hf $binding_file
	fi
fi

#
# Start client service on next boot, unless we're establishing a slave
# server, in which case the binding is needed now (or should be
# preserved).
#
if [ $slavep = T ]
then
	enable_this_boot network/nis/client:default
else
	enable_next_boot network/nis/client:default
fi

#
# As a client, our configuration is correct once a binding file is
# established, and so we can exit.
if [ $clientp = T ]
then
	rm $hf
	exit 0
fi

# Check if all needed NIS server services are available
is_service_avail svc:/network/nis/server:default
is_service_avail svc:/network/nis/passwd:default
is_service_avail svc:/network/nis/update:default
is_service_avail svc:/network/nis/xfr:default

if [ $slavep = T ]
then
	if [ $host = $master ]
	then
		echo "\
The host specified should be a running master yp server, not this machine."
		exit 1
	fi

	maps=`ypwhich -m | egrep $master$| awk '{ printf("%s ",$1) }' -`
	if [ -z "$maps" ]
	then
		echo "Can't enumerate maps from $master. Please check that it is running."
		exit 1
	fi
fi

echo ""

echo "Installing the YP database will require that you answer a few questions."
echo "Questions will all be asked at the beginning of the procedure."
echo ""
echo "Do you want this procedure to quit on non-fatal errors? [y/n: n]  \c"
read doexit

case $doexit in
y*)	exit_on_error=T;;
Y*)	exit_on_error=T;;
*)	echo "\
OK, please remember to go back and redo manually whatever fails.  If you"
	echo "\
don't, some part of the system (perhaps the yp itself) won't work.";;
esac

echo "The yp domain directory is $yproot_dir""/""$def_dom"

for dir in $yproot_dir/$def_dom
do

	if [ -d $dir ]; then
		echo  "Can we destroy the existing $dir and its contents? [y/n: n]  \c"
		read kill_old_dir

		case $kill_old_dir in
		y*)	rm -r -f $dir

			if [ $?  -ne 0 ]
			then
			echo "Can't clean up old directory $dir.  Fatal error."
				exit 1
			fi;;

		Y*)	rm -r -f $dir

			if [ $?  -ne 0 ]
			then
			echo "Can't clean up old directory $dir.  Fatal error."
				exit 1
			fi;;

		*)    echo "OK, please clean it up by hand and start again.  Bye"
			exit 0;;
		esac
	fi

	mkdir $dir

	if [ $?  -ne 0 ]
	then
		echo "Can't make new directory $dir.  Fatal error."
		exit 1
	fi

done

if [ $slavep = T ]
then
	echo "\
There will be no further questions. The remainder of the procedure should take"
	echo "a few minutes, to copy the data bases from $master."

	for dom in  $real_def_dom
	do
		for map in $maps
		do
			echo "Transferring $map..."
			$XFR -h $master -c -d $dom $map

			if [ $?  -ne 0 ]
			then
				errors_in_setup=T

				if [ $exit_on_error = T ]
				then
					exit 1
				fi
			fi
		done
	done

	echo ""
	echo  "${host}'s nis data base has been set up\n"

	if [ $errors_in_setup = T ]
	then
		echo " with errors.  Please remember"
		echo "to figure out what went wrong, and fix it."
	else
		echo " without any errors."
	fi

	# enable slave services
	enable_this_boot network/nis/server:default

	enable_this_boot network/nis/client:default

	exit 0
else

	rm -f $yproot_dir/*.time

	echo "\
There will be no further questions. The remainder of the procedure should take"
	echo "5 to 10 minutes."

	echo "Building $yproot_dir/$def_dom/ypservers..."
	makedbm $hf $yproot_dir/$def_dom/$ypservers_map

	if [ $?  -ne 0 ]
	then
		echo "\
Couldn't build yp data base $yproot_dir/$def_dom/$ypservers_map."
		errors_in_setup=T

		if [ $exit_on_error = T ]
		then
			exit 1
		fi
	fi

	rm $hf

	in_pwd=`pwd`
	cd $yproot_dir
	echo  "Running \c"
	echo  $yproot_dir "\c"
	echo "/Makefile..."
	make NOPUSH=1

	if [ $?  -ne 0 ]
	then
		echo "\
Error running Makefile."
		errors_in_setup=T
		
		if [ $exit_on_error = T ]
		then
			exit 1
		fi
	fi

	cd $in_pwd
	echo ""
	echo  "\
$host has been set up as a yp master server\c"

	if [ $errors_in_setup = T ]
	then
		echo " with errors.  Please remember"
		echo "to figure out what went wrong, and fix it."
	else
		echo " without any errors."
	fi

	echo ""
	echo "\
If there are running slave yp servers, run yppush now for any data bases"
	echo "\
which have been changed.  If there are no running slaves, run ypinit on"
	echo "\
those hosts which are to be slave servers."

	# enable master services
	enable_this_boot network/nis/server:default
	enable_this_boot network/nis/xfr:default
	enable_this_boot network/nis/passwd:default
	enable_this_boot network/nis/update:default

	enable_this_boot network/nis/client:default
fi
