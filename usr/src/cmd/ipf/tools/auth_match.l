%{
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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <net/if.h>

#include "auth_types.h"
#include "auth_match.tab.h"

%}

%e6800
%p2000000
%a70000
%n40000

%%

from		return (TOKEN_FROM);

to		return (TOKEN_TO);

on		return (TOKEN_ON);

\/		return (TOKEN_SLASH);

port		return (TOKEN_PORT);

=		return (TOKEN_ASSIGN);

pass 	{
	yylval.action = AUTH_PASS;
	return (TOKEN_RACTION);
}

block	{
	yylval.action = AUTH_BLOCK;
	return (TOKEN_RACTION);
}

using		return (TOKEN_USING);

src-to-pool	return (TOKEN_SRC_TO_POOL);

dst-to-pool	return (TOKEN_DST_TO_POOL);

keep-state	return (TOKEN_KEEP_STATE);

return-rst	return (TOKEN_RETURN_RST);

\n		return (TOKEN_EOL);

^#.*$		return (TOKEN_COMMENT);

in	{
	yylval.direction = PKT_IN;
	return (TOKEN_DIR);
}

out	{
	yylval.direction = PKT_OUT;
	return (TOKEN_DIR);
}

tcp	{
	return (TOKEN_TCP);
}

udp	{
	return (TOKEN_UDP);
}

any	return (TOKEN_ANY);

[0-9]+	{
	yylval.number = atoi(yytext);
	return (TOKEN_NUMBER);
}

[0-9][0-9]?[0-9]?\.[0-9][0-9]?[0-9]?\.[0-9][0-9]?[0-9]?\.[0-9][0-9]?[0-9]? {
	bzero(&yylval.ip_addr, sizeof (yylval.ip_addr));
	if (inet_pton(AF_INET, yytext, &yylval.ip_addr.in4) != 1) {
		printf("Invalid IPv4 Address %s", yytext);
		exit(-1);
	}

	return (TOKEN_IPV4);
}

[A-Fa-f0-9]{0,4}:(:[A-Fa-f0-9]{1,4}){1,8}|[A-Fa-f0-9:]+ {
	bzero(&yylval.ip_addr, sizeof (yylval.ip_addr));
	if (inet_pton(AF_INET6, yytext, &yylval.ip_addr.in6) != 1) {
		printf("Invalid IPv6 Address %s", yytext);
		exit(-1);
	}

	return (TOKEN_IPV6);
}

[a-zA-Z0-9_-]+ {
	yylval.string = strdup(yytext);
	return (TOKEN_STRING);
}

%%
