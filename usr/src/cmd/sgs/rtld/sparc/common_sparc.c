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
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include	<stdio.h>
#include	<strings.h>
#include	<sys/elf.h>
#include	<sys/elf_SPARC.h>
#include	<alloca.h>
#include	"_rtld.h"
#include	"_elf.h"
#include	"msg.h"
#include	"conv.h"

/*
 *
 *  Matrix of legal combinations of usage of a given register:
 *
 *	Obj1\Obj2       Scratch Named
 *	Scratch          OK      NO
 *	Named            NO      *
 *
 *  * OK if the symbols are identical, NO if they are not.  Two symbols
 *  are identical if and only if one of the following is true:
 *        A. They are both global and have the same name.
 *        B. They are both local, have the same name, and are defined in
 *        the same object.  (Note that a local symbol in one object is
 *        never identical to a local symbol in another object, even if the
 *        name is the same.)
 *
 *  Matrix of legal combinations of st_shndx for the same register symbol:
 *
 *	Obj1\Obj2       UNDEF   ABS
 *	UNDEF            OK      OK
 *	ABS              OK      NO
 */

/*
 * Test the compatiblity of two register symbols, 0 pass, >0 fail
 */
static uintptr_t
check_regsyms(Sym *sym1, const char *name1, Sym *sym2, const char *name2)
{
	if ((sym1->st_name == 0) && (sym2->st_name == 0))
		return (0);	/* scratches are always compatible */

	if ((ELF_ST_BIND(sym1->st_info) == STB_LOCAL) ||
	    (ELF_ST_BIND(sym2->st_info) == STB_LOCAL)) {
		if (sym1->st_value == sym2->st_value)
			return (1);	/* local symbol incompat */
		return (0);		/* no other prob from locals */
	}

	if (sym1->st_value == sym2->st_value) {
		/* NOTE this just avoids strcmp */
		if ((sym1->st_name == 0) || (sym2->st_name == 0))
			return (2);	/* can't match scratch to named */

		if (strcmp(name1, name2) != 0)
			return (4);	/* diff name, same register value */

		if ((sym1->st_shndx == SHN_ABS) && (sym2->st_shndx == SHN_ABS))
			return (3);	/* multiply defined */
	} else if (strcmp(name1, name2) == 0)
		return (5);	/* same name, diff register value */

	return (0);
}

int
elf_regsyms(Rt_map *lmp)
{
	Dyn	*dyn;
	Dyninfo	*dip;
	Sym	*symdef;
	ulong_t	rsymndx;

	/*
	 * Scan through the .dynamic section of this object looking for all
	 * DT_REGISTER entries.  For each DT_REGISTER entry found identify the
	 * register symbol it identifies and confirm that it doesn't conflict
	 * with any other register symbols.
	 */
	for (dyn = DYN(lmp), dip = DYNINFO(lmp);
	    !(dip->di_flags & FLG_DI_IGNORE); dyn++, dip++) {
		Reglist	*rp;

		if ((dip->di_flags & FLG_DI_REGISTER) == 0)
			continue;

		/*
		 * Get the local symbol table entry.
		 */
		rsymndx = dyn->d_un.d_val;
		symdef = (Sym *)((unsigned long)SYMTAB(lmp) +
		    (rsymndx * SYMENT(lmp)));

		for (rp = reglist; rp; rp = rp->rl_next) {
			Conv_inv_buf_t	inv_buf;
			const char	*str, *sym1, *sym2;

			if (rp->rl_sym == symdef) {
				/*
				 * Same symbol definition - everything is a-ok.
				 */
				return (1);
			}

			sym1 = (STRTAB(rp->rl_lmp) + rp->rl_sym->st_name);
			sym2 = (STRTAB(lmp) + symdef->st_name);

			if (check_regsyms(rp->rl_sym, sym1, symdef, sym2) == 0)
				continue;

			if ((str = demangle(sym1)) != sym1) {
				char	*_str = alloca(strlen(str) + 1);
				(void) strcpy(_str, str);
				sym1 = (const char *)_str;
			}
			sym2 = demangle(sym2);

			if (LIST(lmp)->lm_flags & LML_FLG_TRC_WARN) {
				(void) printf(MSG_INTL(MSG_LDD_REG_SYMCONF),
				    conv_sym_SPARC_value(symdef->st_value,
				    0, &inv_buf), NAME(rp->rl_lmp),
				    sym1, NAME(lmp), sym2);
			} else {
				eprintf(LIST(lmp), ERR_FATAL,
				    MSG_INTL(MSG_REG_SYMCONF),
				    conv_sym_SPARC_value(symdef->st_value,
				    0, &inv_buf), NAME(rp->rl_lmp),
				    sym1, NAME(lmp), sym2);
				return (0);
			}
		}
		if ((rp = calloc(sizeof (Reglist), 1)) == NULL)
			return (0);
		rp->rl_lmp = lmp;
		rp->rl_sym = symdef;
		rp->rl_next = reglist;
		reglist = rp;
	}
	return (1);
}
