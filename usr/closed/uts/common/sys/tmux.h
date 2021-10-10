/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


#ifndef	_SYS_TMUX_H
#define	_SYS_TMUX_H

/*	SVr4/SVVS: BA_DEV:BA/drivers/include/tmux.h	1.5 - 89/10/10 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Header file for streams test multiplexor
 */

struct tmx {
	uint32_t tmx_state;
	queue_t *tmx_rdq;
};

struct tmxl {
	int32_t muxid;	/* id of link */
	uint32_t ltype;	/* persistent or non-persistent link */
	queue_t *muxq;	/* linked write queue on lower side of mux */
	queue_t *ctlq;	/* controlling write queue on upper side of mux */
};

/*
 * Driver state values.
 */
#define	TMXOPEN 01
#define	TMXPLINK 02

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TMUX_H */
