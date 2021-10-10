#!/usr/perl5/bin/perl
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

require 5.8.4;
use strict;
use warnings;

my $file;
my $string;
my @matches;
my $match;

# This file generates po files based on gettext calls in shells scripts.
# Escaped quotes within strings are properly handed.  So are instances of
# "gettext " found within strings.
#
# It expects gettext calls of the forms:
#
# 	gettext "...text..."
# or
#	gettext "\
#	...text..."
# or
#	gettext "part1\
#	part2"
# or
#	gettext "\
#	line1\n\
#	line2\n\
#	line3\n"
#

# Open file passed as first arg
open $file, "<", $ARGV[0] or die "Cannot read file\n";

# Read file in as single string
$string = do { local $/; <$file> };

# Match all words followed by quoted strings.  Deal with escaped "'s
@matches = $string =~ /\w*\s*\"(?:[^\\\"]|\\.)*\"/xgs;

# For each match, print msgid and msgstr for po file
foreach $match (@matches) {

	my @lines;
	my $line;
	my $count=0;

	#
	# If a gettext string, remove leading "gettext" and surrounding quotes,
	# else next.
	#
	$match =~ s/^gettext\s*\"(.*)\"$/$1/xs or next;
	
	# join all lines continued with "\"
	$match =~ s/\\\s*\n/\n/gxs;

	# split into lines
	@lines = split(/\n/, $match);
	foreach $line (@lines) {
		# Remove any empty strings
		next if $line =~ /^$/;

		# remove trailing "\n"
		chomp($line);

		# print po file token
		if ($count == 0) {
			print "msgid ";
		} else {
			print "      ";
		}
		print "\"$line\"\n";
		$count = $count + 1;
	}
	print "msgstr\n"
}
exit 0;
