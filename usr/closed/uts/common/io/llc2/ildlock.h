/*
 * Copyright (c) 1998-1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * Copyright (c) 1992-1998 NCR Corporation, Dayton, Ohio USA
 */

#ifndef _LLC2_ILDLOCK_H
#define	_LLC2_ILDLOCK_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif


#include <sys/mutex.h>
#include <sys/condvar.h>
#define	MP_ASSERT(x) ASSERT(x)
/*
 *	ildlock.h
 *	Contains the locking primitives used by the ild (aka ncrllc2) driver.
 */


/*
 * ILD lock structure
 *
 * This structure contains a pointer to the lock structure and the value
 * of the interrupt priority level returned when a lock is set. The ipl
 * is saved so that it can be restored when the lock is reset.
 *
 */

typedef struct ild_lock_st {
	kmutex_t lock;
	int ipl;
	unsigned int locker;
	unsigned int cpuid;
	unsigned int lock_init;
} ild_lock_t;

/*
 * ILD locking macros
 *
 */

/* State/Condition Variables */
#define	ILD_DECL_SV(name)	\
	kcondvar_t name
#define	ILD_SLEEP_ALLOC(svp)	\
{\
	cv_init(&svp, NULL, CV_DRIVER, NULL); \
}

#define	ILD_SLEEP_DEALLOC(svp)	\
{\
	cv_destroy((&svp)); \
}

#define	ILD_SLEEP(svp, lockp, ret) 	\
	ret = cv_wait_sig((&svp), (&(((ild_lock_t *)(&lockp))->lock)))

#define	ILD_WAKEUP(svp)		\
	cv_broadcast((&svp))



/* Lock macro declarations */

#define	ILD_DECL_LOCK(name)	\
	ild_lock_t name
#define	ILD_EDECL_LOCK(name)	\
	extern ild_lock_t name

#define	ILD_RWALLOC_LOCK(lockp, hierarchy, lckinfo) {		\
	mutex_init(&(((ild_lock_t *)(lockp))->lock), NULL,	\
			MUTEX_DRIVER, NULL);			\
	(lockp)->lock_init = 1;					\
}

#define	ILD_RWDEALLOC_LOCK(lockp) {				\
	(lockp)->lock_init = 0;					\
	mutex_destroy(&(((ild_lock_t *)(lockp))->lock));	\
}


#define	ILD_LOCKMINE(lockp)					\
	mutex_owned(&(((ild_lock_t *)(&lockp))->lock))

#define	ILD_TRYRDLOCK(lockp, got_it)				\
{ 								\
if ((got_it = mutex_tryenter(&(((ild_lock_t *)(&lockp))->lock))) != 0)  \
	((ild_lock_t *)(&lockp))->ipl = got_it; 			\
}

/*
 * ild_glnk_lck[ppa] and lnk->lk_lock should be held before
 * calling ILD_SWAPIPL
 */
#define	ILD_SWAPIPL(ppa, lnk)  					\
{								\
	RMV_DLNKLST(&(lnk)->chain); 				\
	ADD_DLNKLST(&(lnk)->chain, &mac_lnk[(ppa)]); 		\
};


#ifndef	LOCK_DEBUGGING

#define	ILD_WRLOCK(lockp)					\
{ 								\
	mutex_enter(&(((ild_lock_t *)(&lockp))->lock));		\
}
#define	ILD_RDLOCK(lockp)					\
{ 								\
	mutex_enter(&(((ild_lock_t *)(&lockp))->lock));		\
}
#define	ILD_RWUNLOCK(lockp) 					\
{ 								\
	mutex_exit(&(((ild_lock_t *)(&lockp))->lock));		\
}
#define	ILD_TRYWRLOCK(lockp, got_it) {				\
if ((got_it = mutex_tryenter(&(((ild_lock_t *)(&lockp))->lock))) != 0) \
	((ild_lock_t *)(&lockp))->ipl = got_it; 			\
}

#else /* LOCK_DEBUGGING */


#define	ILD_WRLOCK(lockp) 						\
{ 									\
	mutex_enter(&(((ild_lock_t *)(&lockp))->lock));			\
	((ild_lock_t *)(&lockp))->locker = (((THIS_MOD << 16)		\
						& 0x01ff0000) +		\
						(__LINE__ & 0xffff)); 	\
}
#define	ILD_RDLOCK(lockp)						\
{ 									\
	mutex_enter(&(((ild_lock_t *)(&lockp))->lock));			\
	((ild_lock_t *)(&lockp))->locker = (((THIS_MOD << 16)		\
						& 0x02ff0000) + 	\
						(__LINE__ & 0xffff)); 	\
}
#define	ILD_RWUNLOCK(lockp)						\
{ 									\
	mutex_exit(&(((ild_lock_t *)(&lockp))->lock));			\
	((ild_lock_t *)(&lockp))->locker = (((THIS_MOD << 16)		\
						& 0x00ff0000) + 	\
						(__LINE__ & 0xffff)); 	\
}

#define	ILD_TRYWRLOCK(lockp, got_it) 					\
{									\
	if ((got_it = mutex_tryenter(&(((ild_lock_t *)			\
					(&lockp))->lock))) != 0) {	\
		((ild_lock_t *)(&lockp))->ipl = got_it; 		\
		((ild_lock_t *)(&lockp))->locker = (((THIS_MOD << 16)	\
						& 0x01ff0000) + 	\
						(__LINE__ & 0xffff)); 	\
	}								\
}

#endif /* LOCK_DEBUGGING */

/*
 * ILD lock hierarchy values
 */
#define	ILD_LCK1		1
#define	ILD_LCK2		2
#define	ILD_LCK3		3
#define	ILD_LCK4		4
#define	ILD_LCK5		5
#define	ILD_LCK6		6
#define	ILD_LCK7		7
#define	ILD_LCK8		8
#define	ILD_LCK9		9

#ifndef D_MP
#define	D_MP D_NEW
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _LLC2_ILDLOCK_H */
