#!/usr/perl5/bin/perl -w
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
# Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
#

# local script to process audit_record_attr.txt -> audit_record_attr
#
# comments in the source file may start with "#" or "##" in any
# column.  Those with double hash are retained as comments (but with a
# single "#") in the destination and the others are removed.  Because
# of the comment removal, any sequence of more than one line of blank
# lines is also removed.

use strict;
use auditxml;
use Getopt::Std;
use vars qw($opt_d);
require 5.005;

my $blankCount = 1;	# not zero is a kludge to avoid making the first
			# line of the output a blank line.

my $app_debug = 0;	# used after return from "new auditxml"

getopts('d');
$app_debug = $opt_d;

my $prog = $0; $prog =~ s|.*/||g;
my $usage = "usage: $prog adt.xml audit_record_attr.txt\n" ;
die $usage if ($#ARGV < 0);

our $debug = 0;
my $doc = new auditxml ($ARGV[0]);  # input XML file

open(ATTR_FILE, $ARGV[1]) or die "Cannot open file: $ARGV[1]";

my %tokens;		# tokens defined in audit_record_attr
my $last_pos = 0;	# position of the latest token

#
# Process audit_record_attr.txt up to the latest token defintion.
while (<ATTR_FILE>) {
        s/(?<!#)#(?!#).*//;
	if (/^\s*$/) {
		$blankCount++ ;
		next if ($blankCount > 1);
	} else {
		$blankCount = 0;
	}
	s/##/#/;
	if (/^token=([\w]+):([\w]+)/) {
		$tokens{$1} = "$2";
		$last_pos = tell(ATTR_FILE);
	}
	if (/^[\w]+=/ && !/^token=/ && $last_pos) {
		seek(ATTR_FILE, $last_pos, 0);
		last;
	}
	print;
}

# Read, validate and generate token defitions from adt.xml.
foreach my $token_idref_xml ($doc->getTokenIds()) {
	my $token_xml = $doc->getToken($token_idref_xml);
	my $token_id_xml = $token_xml->getId();
	my $token_usage_xml = $token_xml->getUsage();

	if ($token_usage_xml eq "") {
		$token_usage_xml = $token_idref_xml;
	}

	if (!$tokens{$token_id_xml}) {
		print "token=$token_id_xml:$token_usage_xml\n"
	} elsif ($app_debug) {
		print "# Debug: Redundant token defition for $token_id_xml.\n"
	}
}

# Process the remainder of audit_record_attr.txt.
while (<ATTR_FILE>) {
        s/(?<!#)#(?!#).*//;
	if (/^\s*$/) {
		$blankCount++ ;
		next if ($blankCount > 1);
	} else {
		$blankCount = 0;
	}
	s/##/#/;
	print;
}
close ATTR_FILE;
