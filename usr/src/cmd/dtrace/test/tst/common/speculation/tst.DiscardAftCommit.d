/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * ASSERTION:
 * Can call discard() on a buffer after it has been committed.
 *
 * SECTION: Speculative Tracing/Discarding a Speculation;
 *	Options and Tunables/cleanrate
 *
 */
#pragma D option quiet
#pragma D option cleanrate=3000hz

BEGIN
{
	self->i = 0;
	self->commit = 0;
	self->discard = 0;
	var1 = speculation();
	printf("Speculation ID: %d\n", var1);
}

BEGIN
/var1/
{
	speculate(var1);
	printf("This statement and the following are speculative!!\n");
	printf("Speculating on id: %d\n", var1);
	self->i++;
}

BEGIN
/(self->i)/
{
	commit(var1);
	self->commit++;
	discard(var1);
	self->discard++;
}

BEGIN
/self->discard/
{
	printf("Discarded a committed buffer\n");
	exit(0);
}


BEGIN
/!self->discard/
{
	printf("Couldnt discard a committed buffer\n");
	exit(1);
}

ERROR
{
	exit(1);
}
