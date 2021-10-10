/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


!
! uint64_t get_ulcwq_status(void);
!
        .inline get_ulcwq_status, 0
	mov	0x10, %o0
	ldxa	[%o0]0x90, %o0
	and	%o0, 7, %o0	
        .end

        .inline set_ulcwq_status, 0
	mov	0x10, %o0
	mov	4, %o1
	stxa	%o1, [%o0]0x90
        .end

        .inline get_ulcwq_tail, 0
	mov	0x08, %o0
	ldxa	[%o0]0x90, %o0	
        .end

        .inline set_ulcwq_tail, 8
	mov	0x08, %o1
	stxa	%o0, [%o1]0x90	
        .end
