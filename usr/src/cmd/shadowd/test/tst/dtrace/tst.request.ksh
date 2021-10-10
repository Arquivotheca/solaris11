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
# Tests the request-start and request-done dtrace probes.
#

. $ST_TOOLS/utility.ksh

tst_create_root

mkdir $TST_ROOT/dir || fail "failed to create $TST_ROOT/dir"
echo foo > $TST_ROOT/dir/foo || fail "failed to create $TST_ROOT/dir/foo"

tst_create_dataset

ls $TST_SROOT > /dev/null

OUTPUT=/var/tmp/shadowtest.request.$$

dtrace -s /dev/stdin <<EOF > $OUTPUT.1 &
#pragma D option quiet

BEGIN
{
	printf("%-24dBEGIN\n", timestamp);
	self->depth = 0;
}

shadowfs:::request-start
/self->depth == 0/
{
	printf("%-24dSTART %s\n", timestamp, args[0]->fi_pathname);
}

shadowfs:::request-start
{
	self->depth++;
}

shadowfs:::request-done
{
	self->depth--;
}

shadowfs:::request-done
/self->depth == 0/
{
	printf("%-24dDONE %s %s\n", timestamp, args[0]->fi_pathname, args[1]);
}

shadowfs:::complete
/self->depth == 1/
{
	printf("%-24dCOMPLETE %s\n", timestamp, args[0]->fi_pathname);
}

shadowfs:::request-done
/self->depth == 0 && strstr(args[0]->fi_pathname, "foo") != NULL/
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
cat $OUTPUT.1 | sort -n | cut -c 25- > $OUTPUT.3 || fail "failed to trim output"

# compare the output
cat > $OUTPUT.2 <<EOF

BEGIN
START $TST_SROOT/dir
COMPLETE $TST_SROOT/dir
DONE $TST_SROOT/dir dir
START $TST_SROOT/dir/foo
COMPLETE $TST_SROOT/dir/foo
DONE $TST_SROOT/dir/foo dir/foo
EOF

diff $OUTPUT.2 $OUTPUT.3 || fail "mismatched output"

rm -f $OUTPUT.*
tst_destroy_dataset
exit 0
