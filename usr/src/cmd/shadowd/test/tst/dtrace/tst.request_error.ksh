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
# Tests the migrate-start and migrate-done dtrace probes.
#

. $ST_TOOLS/utility.ksh

tst_create_root

echo foo > $TST_ROOT/a || fail "failed to create $TST_ROOT/a"

tst_create_dataset

OUTPUT=/var/tmp/shadowtest.migrate.$$

dtrace -s /dev/stdin <<EOF > $OUTPUT.1 &
#pragma D option quiet

BEGIN
{
	printf("%-24dBEGIN\n", timestamp);
}

shadowfs:::request-error
{
	printf("%-24dERROR %s %s %d\n", timestamp, args[0]->fi_pathname,
	    args[1], args[2]);
	exit (0);
}
EOF

DPID=$!

# wait for dtrace to start
until grep BEGIN $OUTPUT.1; do
	sleep 1
done

# trigger error
ls $TST_SROOT > /dev/null || fail "failed to list root"
mv $TST_ROOT/a $TST_ROOT/b || fail "failed to move a"
cat $TST_SROOT/a && fail "successfully read $TST_SROOT/a"

wait $DPID

# sort the output by time and trim timestamps
cat $OUTPUT.1 | sort -n | cut -c 25- > $OUTPUT.3 || fail "failed to trim output"

# compare the output
cat > $OUTPUT.2 <<EOF

BEGIN
ERROR $TST_SROOT/a a 2
EOF

diff $OUTPUT.2 $OUTPUT.3 || fail "mismatched output"

rm -f $OUTPUT.*
tst_destroy_dataset
exit 0

