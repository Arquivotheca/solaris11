/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Hypervisor calls called by the ncp and n2cp drivers.
*/

#include <sys/asm_linkage.h>
#include <sys/hypervisor_api.h>
#include <sys/ncs.h>

#if defined(lint) || defined(__lint)

/*LINTLIBRARY*/

/*
 * NCS HV API v1.0
 */
/*ARGSUSED*/
uint64_t
hv_ncs_request(int cmd, uint64_t realaddr, size_t sz)
{ return (0); }

/*
 * NCS HV API v2.0
 */
/*ARGSUSED*/
uint64_t
hv_ncs_qconf(uint64_t qtype, uint64_t qbase, uint64_t qsize,
	uint64_t *qhandlep)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_ncs_qinfo(uint64_t qhandle, ncs_qinfo_t *qinfop)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_ncs_gethead(uint64_t qhandle, uint64_t *headoffsetp)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_ncs_sethead_marker(uint64_t qhandle, uint64_t headoffset)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_ncs_gettail(uint64_t qhandle, uint64_t *tailoffsetp)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_ncs_settail(uint64_t qhandle, uint64_t tailoffset)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_ncs_qhandle_to_devinop(uint64_t qhandle, uint64_t *devinop)
{ return (0); }

/*
 * NCS HV API v2.1 addition
 */
/*ARGSUSED*/
uint64_t
hv_ncs_ul_cwqconf(uint64_t qbase, uint64_t pagesize, uint64_t qsize,
	uint64_t *qhandlep)
{ return (0); }

#else	/* lint || __lint */

	/*
	 * NCS HV API v1.0
	 */
	/*
	 * hv_ncs_request(int cmd, uint64_t realaddr, size_t sz)
	 */
	ENTRY(hv_ncs_request)
	mov	HV_NCS_REQUEST, %o5
	ta	FAST_TRAP
	retl
	nop
	SET_SIZE(hv_ncs_request)

	/*
	 * NCS HV API v2.0
	 */
	/*
	 * hv_ncs_qconf(uint64_t qtype, uint64_t qbase, uint64_t qsize,
	 *		uint64-t *qhandlep)
	 */
	ENTRY(hv_ncs_qconf)
	mov	%o3, %o4
	mov	HV_NCS_QCONF, %o5
	ta	FAST_TRAP
	brnz,a,pt %o4, 1f
	  stx	%o1, [%o4]
1:
	retl
	nop
	SET_SIZE(hv_ncs_qconf)

	/*
	 * hv_ncs_qinfo(uint64_t qhandle, ncs_qinfo_t *qinfop)
	 */
	ENTRY(hv_ncs_qinfo)
	mov	%o1, %o4
	mov	HV_NCS_QINFO, %o5
	ta	FAST_TRAP
	stx	%o1, [%o4]		! qinfop->qi_qtype
	stx	%o2, [%o4 + 8]		! qinfop->qi_baseaddr
	retl
	stx	%o3, [%o4 + 16]		! qinfop->qi_qsize
	SET_SIZE(hv_ncs_qinfo)

	/*
	 * hv_ncs_gethead(uint64_t qhandle, uint64_t *headoffsetp)
	 */
	ENTRY(hv_ncs_gethead)
	mov	%o1, %o4
	mov	HV_NCS_GETHEAD, %o5
	ta	FAST_TRAP
	retl
	stx	%o1, [%o4]
	SET_SIZE(hv_ncs_gethead)

	/*
	 * hv_ncs_sethead_marker(uint64_t qhandle, uint64_t headoffset)
	 */
	ENTRY(hv_ncs_sethead_marker)
	mov	HV_NCS_SETHEAD_MARKER, %o5
	ta	FAST_TRAP
	retl
	nop
	SET_SIZE(hv_ncs_sethead_marker)

	/*
	 * hv_ncs_gettail(uint64_t qhandle, uint64_t *tailoffsetp)
	 */
	ENTRY(hv_ncs_gettail)
	mov	%o1, %o4
	mov	HV_NCS_GETTAIL, %o5
	ta	FAST_TRAP
	retl
	stx	%o1, [%o4]
	SET_SIZE(hv_ncs_gettail)

	/*
	 * hv_ncs_settail(uint64_t qhandle, uint64_t tailoffset)
	 */
	ENTRY(hv_ncs_settail)
	mov	HV_NCS_SETTAIL, %o5
	ta	FAST_TRAP
	retl
	nop
	SET_SIZE(hv_ncs_settail)

	/*
	 * hv_ncs_qhandle_to_devino(uint64_t qhandle, uint64_t *devinop)
	 */
	ENTRY(hv_ncs_qhandle_to_devino)
	mov	%o1, %o4
	mov	HV_NCS_QHANDLE_TO_DEVINO, %o5
	ta	FAST_TRAP
	retl
	stx	%o1, [%o4]
	SET_SIZE(hv_ncs_qhandle_to_devino)

	
	/*
	 * hv_ncs_ul_cwqconf(uint64_t qbase, uint64_t pagesize, uint64_t qsize,
	 *		uint64_t *qhandlep)
	 */
	ENTRY(hv_ncs_ul_cwqconf)
	mov	%o3, %o4
	mov	HV_NCS_ULQCONF, %o5
	ta	FAST_TRAP
	retl
	stx	%o1, [%o4]
	SET_SIZE(hv_ncs_ul_cwqconf)


#endif	/* lint || __lint */
