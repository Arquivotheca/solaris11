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
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>

#define	offsetof(s, m)	(size_t)(&(((s *)0)->m))

int
main(int argc, char **argv)
{
	FILE *ofile;

	if ((ofile = fopen("kernel_fp_use_offs.h", "w+")) == NULL) {
		printf("Failed to open kernel_fp_use_offs.h, errno=%d\n",
		    errno);
		exit(1);
	}

	fprintf(ofile, "#define\tT_LWP\t0x%x\n",
	    offsetof(kthread_t, t_lwp));
	fprintf(ofile, "#define\tT_CPU\t0x%x\n",
	    offsetof(kthread_t, t_cpu));
	fprintf(ofile, "#define\tT_PREEMPT\t0x%x\n",
	    offsetof(kthread_t, t_preempt));
	fprintf(ofile, "#define\tCPU_KPRUNRUN\t0x%x\n",
	    offsetof(cpu_t, cpu_kprunrun));

	fclose(ofile);

	return (0);
}
