#!/bin/perl

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

require 5.6.1;

use File::Find;
use File::Basename;
use File::Path;
use Getopt::Std;
use Cwd qw/getcwd realpath/;
use FindBin;
use Sys::Hostname;

$PNAME = $0;
$PNAME =~ s:.*/::;
($MACH = `uname -p`) =~ s/\W*\n//;
$USAGE = "Usage: $PNAME [-lp] [-d dir] [-n nfsdir] [-z pool]\n";

$SKIP_RETVAL = 87;

$opt_p;
$opt_z = "shadowtest.$$";
$datafile = "/var/tmp/shadowtest.$$";
@files = ();

$ksh_path = '/usr/bin/ksh';
$perl_path = '/usr/bin/perl';
$ro_root = 0;

sub errmsg
{
	my($msg) = @_;

	print STDERR $msg;
	print LOG $msg if ($opt_l);
	$errs++;
}

sub logmsg
{
	my($msg) = @_;

	print STDOUT $msg unless ($opt_q);
	print LOG $msg if ($opt_l);
}

sub fail
{
	my(@parms) = @_;
	my($msg) = $parms[0];
	my($errfile) = $parms[1];
	my($n) = 0;
	my($dest) = basename($file);

	while (-d "$opt_d/failure.$n") {
		$n++;
	}

	unless (mkdir "$opt_d/failure.$n") {
		warn "ERROR: failed to make directory $opt_d/failure.$n: $!\n";
		exit(125);
	}

	open(README, ">$opt_d/failure.$n/README");
	print README "Failure During Pass $pass\n";
	print README "ERROR: " . $file . " " . $msg;
	
	if (scalar @parms > 1) {
		print README "; see $errfile\n";
	} else {
		if (-f "$opt_d/$pid.core") {
			print README "; see $pid.core\n";
		} else {
			print README "\n";
		}
	}

	close(README);

	if (-f "$opt_d/$pid.out") {
		rename("$opt_d/$pid.out", "$opt_d/failure.$n/$pid.out");
	}

	if (-f "$file.out") {
		symlink(realpath("$file.out"), "$opt_d/failure.$n/$dest.out");
	}

	if (-f "$opt_d/$pid.err") {
		rename("$opt_d/$pid.err", "$opt_d/failure.$n/$pid.err");
	}

	if (-f "$file.err") {
		symlink(realpath("$file.err"), "$opt_d/failure.$n/$dest.err");
	}

	if (-f "$opt_d/$pid.core") {
		rename("$opt_d/$pid.core", "$opt_d/failure.$n/$pid.core");
	}

	symlink(realpath("$file"), "$opt_d/failure.$n/$dest");

	$msg = "ERROR: " . $dest . " " . $msg;

	if (scalar @parms > 1) {
		$msg = $msg . "; see $errfile in failure.$n\n";
	} else {
		$msg = $msg . "; details in failure.$n\n";
	}

	errmsg($msg);
}

sub run_tests {
	$ENV{'ST_TOOLS'} = dirname($0);
	$ENV{'ST_SKIP'} = $SKIP_RETVAL;
	if ($opt_n) {
		if (!($opt_n =~ /^(.*):(.*)/)) {
			die "$PNAME: invalid NFS scratch (must be  " .
			    "host:path)";
		}
		$ENV{'ST_NFS_HOST'} = $1;
		$ENV{'ST_NFS_PATH'} = $2;
	}

	$total = $errs = $skipped = 0;

	my @filelist = sort @files;

	foreach $file (@filelist) {
		($base, $dir, $ext) =
		    fileparse($file, '\.ksh', '\.pl');

		$name = "$base$ext";
		$droptag = 0;
		$status = 0;

		$isksh = ($ext eq '.ksh');
		$isperl = ($ext eq '.pl');

		if ($dir =~ /failure\.[0-9]+/) {
			next;
		}

		$total++;

		if (!($name =~ /^tst\./)) {
			errmsg("ERROR: $file is not a valid test file name\n");
			next;
		}

		$fullname = "$dir/$name";
		logmsg("testing $file ... ");

		if (($pid = fork()) == -1) {
			errmsg("ERROR: failed to fork to run test $file: $!\n");
			next;
		}

		if ($pid == 0) {
			$SIG{INT} = 'DEFAULT';

			open(STDIN, '</dev/null');
			exit(125) unless open(STDOUT, ">$opt_d/$$.out");
			exit(125) unless open(STDERR, ">$opt_d/$$.err");

			unless (chdir($dir)) {
				warn "ERROR: failed to chdir for $file: $!\n";
				exit(126);
			}

			if ($isperl) {
				exec($perl_path, $name, $uri, $httpsuri);
			} elsif ($isksh) {
				exit(123) unless open(STDIN, "<$name");
				exec($ksh_path, $name, $uri, $httpsuri);
			} else {
				warn "ERROR: $file is of unrecognized type\n";
				exit(124);
			}

			warn "ERROR: failed to exec for $file: $!\n";
			exit(127);
		}

		if (waitpid($pid, 0) == -1) {
			errmsg("ERROR: timed out waiting for $file\n");
			kill(9, $pid);
			next;
		}

		$wstat = $?;
		$wifexited = ($wstat & 0xFF) == 0;
		$wexitstat = ($wstat >> 8) & 0xFF;
		$wtermsig = ($wstat & 0x7F);

		if ($wifexited && $wexitstat == $SKIP_RETVAL) {
			logmsg("[skipped]\n");
			unlink($pid . '.out');
			unlink($pid . '.err');
			$skipped++;
			next;
		}

		logmsg("[$pid]\n");

		if (!$wifexited) {
			fail("died from signal $wtermsig");
			next;
		}

		if ($wexitstat == 125) {
			die "$PNAME: failed to create output file in $opt_d " .
			    "(cd elsewhere or use -d)\n";
		}

		if ($wexitstat != $status) {
			fail("returned $wexitstat instead of $status");
			next;
		}

		if (-f "$file.out" &&
		    system("cmp -s $file.out $opt_d/$pid.out") != 0) {
			fail("stdout mismatch", "$pid.out");
			next;
		}

		if (-f "$file.err" &&
		    system("cmp -s $file.err $opt_d/$pid.err") != 0) {
			fail("stderr mismatch: see $pid.err");
			next;
		}

		unlink($pid . '.out');
		unlink($pid . '.err');
	}
}

sub wanted
{
	if ($_ =~ /^(tst)\..+\.(ksh|pl)$/ &&
	    -f "$_") {
		push(@files, $File::Find::name)
	}
}

sub disable_shadowd {
	system("svcadm disable shadowd > /dev/null 2>&1");
}

sub find_files {
	printf("Searching for files ... ");
	foreach $arg (@ARGV) {
		if (-f $arg) {
			push(@files, $arg);
		} elsif (-d $arg) {
			find(\&wanted, $arg);
		} else {
			die "$PNAME: $arg is not a valid file or directory\n";
		}
	}
	printf("done.\n");
}

sub install_drv {
	my $kerneldir = "/kernel";
	my $karch = `isainfo -k`;

	printf("Installing kernel driver ... ");
	chomp($karch);

	(-f "$kerneldir/drv/shadowtest") ||
	(-f "$kerneldir/drv/$karch/shadowtest") ||
		die "missing kernel module at $kerneldir/drv/shadowtest";

	if (system("touch /shadowtest.$$ > /dev/null 2>&1") != 0) {
		$ro_root = 1;
		system("mount -o remount,rw /") &&
		    die "failed to make root writable";
	} else {
		system("rm -f /shadowtest.$$");
	}

	#
	# In case we had an interrupted aktest.
	#
	system("rem_drv shadowtest > /dev/null 2>&1");

	system("add_drv -m '* 0666 root root' shadowtest") &&
	    die "failed to add kernel driver";

	if ($ro_root) {
		system("mount -o remount,ro /") &&
		    die "failed to make root read-only";
	}

	printf("done.\n");
}

sub remove_drv {
	printf("Removing kernel driver ... ");

	if ($ro_root) {
		system("mount -o remount,rw /") &&
		    die "failed to make root writable";
	}

	system("rem_drv shadowtest") &&
	    die "failed to remove driver";

	if ($ro_root) {
		system("mount -o remount,ro /") &&
		    die "failed to make root read-only";
	}

	printf("done.\n");
}

sub create_pool {
	if (system("zpool list $opt_z >/dev/null 2>&1") != 0) {
		logmsg("Creating pool '$opt_z' ... ");

		system("mkfile -n 1G $datafile") &&
		    die "failed to create backing store";
		system("zpool create -O mountpoint=none $opt_z $datafile") &&
		    die "failed to create pool";

		logmsg("done.\n");
	}

	$scratchds = "$opt_z/shadowtest.$$";
	$scratchroot = "/tmp/shadowtest.$$";
	logmsg("Creating dataset '$scratchds' ... ");
	system("zfs create -o mountpoint=$scratchroot $scratchds") &&
	    die "failed to create dataset";
	logmsg("done.\n");

	$ENV{'ST_ROOT'} = $scratchroot;
	$ENV{'ST_DATASET'} = $scratchds;
}

sub destroy_pool {
	if ($opt_z eq "shadowtest.$$") {
		if ($opt_p) {
			logmsg("Preserving pool '$opt_z'\n");
		} else {
			logmsg("Destroying pool '$opt_z' ... ");
			system("zpool destroy $opt_z");
			system("rm -f $datafile");
			logmsg("done.\n");
		}
	} else {
		if ($opt_p) {
			logmsg("Preserving dataset '$scratchds'\n");
		} else {
			logmsg("Destroying dataset '$scratchds' ... ");
			system("zfs destroy -R $scratchds");
			logmsg("done.\n");
		}
	}
}

die $USAGE unless (getopts('d:hlpz:n:'));
usage() if ($opt_h);

if ($#ARGV == -1) {
	push (@ARGV, realpath(dirname($0) . "/../tst"));
}
$opt_d = "." if (!$opt_d);

die "$PNAME: failed to open $PNAME.$$.log: $!\n"
    unless (!$opt_l || open(LOG, ">$opt_d/$PNAME.$$.log"));

logmsg("==== Disable shadowd ====\n\n");
disable_shadowd();

logmsg("==== Configuring tests ===\n\n");
find_files();
install_drv();
create_pool();

logmsg("\n==== Executing tests ====\n\n");
run_tests();
logmsg("\n==== Tests complete ====\n\n");

logmsg("    passed: " . ($total - $errs - $skipped) . "\n");
logmsg("    failed: " . $errs . "\n");
logmsg("   skipped: " . $skipped . "\n");
logmsg("     total: " . ($total) . "\n");

logmsg("\n==== Cleaning up ====\n\n");

destroy_pool();
remove_drv();
