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
#include "montmul_vt.h"
#include "mpmul_vt.h"

int
main(int argc, char **argv)
{
	FILE *ofile;

	if ((ofile = fopen("montmul_offsets.h", "w+")) == NULL) {
		printf("Failed to open montmul_offsets.h, errno=%d\n", errno);
		exit(1);
	}

	fprintf(ofile, "#define	PTR_SIZE\t%d\n",
	    MM_YF_PARS_STORE_A_OFFS - MM_YF_PARS_LOAD_A_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_A_OFFS\t%d\n", MM_YF_PARS_A_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_B_OFFS\t%d\n", MM_YF_PARS_B_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_N_OFFS\t%d\n", MM_YF_PARS_N_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_RET_OFFS\t%d\n",
	    MM_YF_PARS_RET_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_NPRIME_OFFS\t%d\n",
	    MM_YF_PARS_NPRIME_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_LOAD_A_OFFS\t%d\n",
	    MM_YF_PARS_LOAD_A_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_STORE_A_OFFS\t%d\n",
	    MM_YF_PARS_STORE_A_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_LOAD_N_OFFS\t%d\n",
	    MM_YF_PARS_LOAD_N_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_LOAD_B_OFFS\t%d\n",
	    MM_YF_PARS_LOAD_B_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_MONTMUL_OFFS\t%d\n",
	    MM_YF_PARS_MONTMUL_OFFS);
	fprintf(ofile, "#define	MM_YF_PARS_MONTSQR_OFFS\t%d\n",
	    MM_YF_PARS_MONTSQR_OFFS);

	fprintf(ofile, "#define	MPM_YF_PARS_M1_OFFS\t%d\n",
	    MPM_YF_PARS_M1_OFFS);
	fprintf(ofile, "#define	MPM_YF_PARS_M2_OFFS\t%d\n",
	    MPM_YF_PARS_M2_OFFS);
	fprintf(ofile, "#define	MPM_YF_PARS_RES_OFFS\t%d\n",
	    MPM_YF_PARS_RES_OFFS);
	fprintf(ofile, "#define	MPM_YF_PARS_LOAD_M1_OFFS\t%d\n",
	    MPM_YF_PARS_LOAD_M1_OFFS);
	fprintf(ofile, "#define	MPM_YF_PARS_LOAD_M2_OFFS\t%d\n",
	    MPM_YF_PARS_LOAD_M2_OFFS);
	fprintf(ofile, "#define	MPM_YF_PARS_STORE_RES_OFFS\t%d\n",
	    MPM_YF_PARS_STORE_RES_OFFS);
	fprintf(ofile, "#define	MPM_YF_PARS_MPMUL_OFFS\t%d\n",
	    MPM_YF_PARS_MPMUL_OFFS);

	fclose(ofile);

	return (0);
}
