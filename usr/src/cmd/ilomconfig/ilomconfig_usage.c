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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "ilomconfig.h"


void
usage_list()
{
	(void) printf("Usage: ilomconfig list <type> [options]\n");
	(void) printf("\n");
	(void) printf("  Types:\n");
	(void) printf("    system-summary           :  \
List product summary information and ILOM version\n");
	(void) printf("    interconnect             :  \
List Host-to-ILOM interconnect settings\n");
	(void) printf("\n");
}

void
usage_list_systemsummary()
{
	(void) printf("Usage: ilomconfig list system-summary\n");
	(void) printf("\n");
	(void) printf("  Options:\n");
	(void) printf("     None\n");
	(void) printf("\n");
}

void
usage_list_interconnect()
{
	(void) printf("Usage: ilomconfig list interconnect\n");
	(void) printf("\n");
	(void) printf("  Options:\n");
	(void) printf("     None\n");
	(void) printf("\n");
}

void
usage_modify()
{
	(void) printf("Usage: ilomconfig modify <type> [options]\n");
	(void) printf("\n");
	(void) printf("  Types:\n");
	(void) printf("    interconnect   :  \
Modifies Host-to-ILOM interconnect settings\n");
	(void) printf("\n");
}

void
usage_modify_interconnect()
{
	(void) printf("Usage: ilomconfig modify interconnect [options]\n");
	(void) printf("\n");
	(void) printf("  Options:\n");
	(void) printf("     --ipaddress=ipaddress     : \
ILOM interconnect IP address\n");
	(void) printf("     --hostipaddress=ipaddress : \
Host interconnect IP address\n");
	(void) printf("     --netmask=netmask         : \
Host-to-ILOM interconnect netmask\n");
	(void) printf("\n");
}

void
usage_enable()
{
	(void) printf("Usage: ilomconfig enable <type>\n");
	(void) printf("\n");
	(void) printf("  Types:\n");
	(void) printf(
"    interconnect   :  Enables the Host-to-ILOM interconnect\n");
	(void) printf("\n");
}

void
usage_disable()
{
	(void) printf("Usage: ilomconfig disable <type>\n");
	(void) printf("\n");
	(void) printf("  Types:\n");
	(void) printf("    interconnect   :  \
Disables the Host-to-ILOM interconnect\n");
	(void) printf("\n");
}

void
usage_enable_interconnect()
{
	(void) printf("Usage: ilomconfig enable interconnect [options]\n");
	(void) printf("\n");
	(void) printf("  Options:\n");
	(void) printf("     --ipaddress=ipaddress     : \
ILOM interconnect IP address\n");
	(void) printf("     --hostipaddress=ipaddress : \
Host interconnect IP address\n");
	(void) printf("     --netmask=netmask         : \
Host-to-ILOM interconnect netmask\n");
	(void) printf("\n");
}

void
usage_disable_interconnect()
{
	(void) printf("Usage: ilomconfig disable interconnect\n");
	(void) printf("\n");
	(void) printf("  Options:\n");
	(void) printf("     None\n");
	(void) printf("\n");
}

void
usage_top()
{
	(void) printf("Usage: ilomconfig <subcommand> <type> [options] \n");
	(void) printf("Enter 'ilomconfig <subcommand> -?' for \
help on a specific subcommand.\n");
	(void) printf("\n");
	(void) printf("Available subcommands:\n");
	(void) printf("    list          : \
Show ILOM interconnect and system summary\n");
	(void) printf("    modify        : Modify ILOM settings\n");
	(void) printf(
"    enable        : Enable Host-to-ILOM interconnect\n");
	(void) printf(
"    disable       : Disable Host-to-ILOM interconnect\n");
	(void) printf("\n");

}

void
usage_general_opt()
{
	(void) printf("  General Options:\n");
	(void) printf("     -?, --help     : help\n");
	(void) printf(
"     -V, --version  : Show the version of the command.\n");
	(void) printf("     -q, --quiet    : \
Suppress informational message output and only return error codes.\n");
	(void) printf("\n");
}


void
usage(int command, int subcommand) {

	switch (command) {
		case OPT_ENABLE:
			if (subcommand == OPT_INTERCONNECT) {
				usage_enable_interconnect();
			} else {
				usage_enable();
			}
			break;
		case OPT_DISABLE:
			if (subcommand == OPT_INTERCONNECT) {
				usage_disable_interconnect();
			} else {
				usage_disable();
			}
			break;
		case OPT_LIST:
			if (subcommand == OPT_SYSTEMSUMMARY) {
				usage_list_systemsummary();
			} else if (subcommand == OPT_INTERCONNECT) {
				usage_list_interconnect();
			} else {
				usage_list();
			}
			break;
		case OPT_MODIFY:
			if (subcommand == OPT_INTERCONNECT) {
				usage_modify_interconnect();
			} else {
				usage_modify();
			}
			break;
		default:
			usage_top();
			break;
	}
	if (command != OPT_ENABLE) usage_general_opt();
}
