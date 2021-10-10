#!/bin/ksh -p
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

#
# Tests the 'discover' start/done probes.  We don't know the order that entries
# will be enumerated, so we have to sort the output and compare that way.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to create $TST_ROOT/dir"
echo foo > $TST_ROOT/dir/foo || fail "failed to create $TST_ROOT/dir/foo"
echo bar > $TST_ROOT/dir/bar || fail "failed to create $TST_ROOT/dir/bar"

tst_create_dataset

ls $TST_SROOT > /dev/null

OUTPUT=/var/tmp/shadowtest.discover.$$

dtrace -s /dev/stdin <<EOF > $OUTPUT.1 &
#pragma D option quiet

int total;

BEGIN
{
	printf("BEGIN\n");
}

shadowfs:::discover-start
{
	printf("START %s %s %s\n", args[0]->fi_pathname, args[1], args[2]);
}

shadowfs:::discover-done
{
	printf("DONE %s %s %s\n", args[0]->fi_pathname, args[1], args[2]);
	total++;
}

shadowfs:::discover-done
/total == 2/
{
	exit(0);
}
EOF

DPID=$!

# wait for dtrace to start
until grep BEGIN $OUTPUT.1; do
	sleep 0.1
done

# perform migration
ls $TST_SROOT/dir > /dev/null || fail "failed to ls $TST_SROOT/dir"
cat $TST_SROOT/dir/foo > /dev/null || fail "failed to read $TST_SROOT/dir/foo"

wait $DPID

# sort the output by time and trim timestamps
cat $OUTPUT.1 | sort > $OUTPUT.3 || fail "failed to trim output"

# compare the output
cat > $OUTPUT.2 <<EOF

BEGIN
DONE $TST_SROOT/dir dir bar
DONE $TST_SROOT/dir dir foo
START $TST_SROOT/dir dir bar
START $TST_SROOT/dir dir foo
EOF

diff $OUTPUT.2 $OUTPUT.3 || fail "mismatched output"

rm -f $OUTPUT.*
tst_destroy_dataset
exit 0
