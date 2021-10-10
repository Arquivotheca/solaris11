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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

provider lldp {
	probe	pkt__recv(char *, uint32_t);
	probe	tlv__recv(uint8_t, uint16_t, uint32_t, uint8_t);
	probe	tlv__recv__discard(uint8_t, uint32_t, uint8_t);
	probe	pkt__recv__discard(char *);
	probe	pkt__recv__toomany__neighbors(char *);
	probe	rx__state__change(char *, char *);
	probe	tx__state__change(char *, char *);
	probe	pkt__send(char *, uint32_t);
	probe	tlv__send(uint8_t, uint16_t, uint32_t, uint8_t);
};

#pragma D attributes Private/Private/Common provider lldp provider
#pragma D attributes Private/Private/Common provider lldp module
#pragma D attributes Private/Private/Common provider lldp function
#pragma D attributes Private/Private/Common provider lldp name
#pragma D attributes Private/Private/Common provider lldp args
