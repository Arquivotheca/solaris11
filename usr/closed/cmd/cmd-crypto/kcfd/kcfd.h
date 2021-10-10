/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _KCFD_H
#define	_KCFD_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Local header for kcfd components
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hardcode the daemon uid/gid to avoid nameservice lookup.  It is unlikely
 * that these could ever change anyway it certainly isn't supported for
 * customers to change them in the /etc/passwd and /etc/group files.
 */
#define	DAEMON_UID	1
#define	DAEMON_GID	12

enum ret_e {
	KCFD_EXIT_OKAY,
	KCFD_EXIT_INVALID_ARG,
	KCFD_EXIT_SYS_ERR,
	KCFD_EXIT_SERVICE_CREATE,
	KCFD_EXIT_SERVICE_DESTROY,
	KCFD_EXIT_ALREADY_RUNNING
};

extern void	kcf_svcinit(void);
extern int	kcfd_modverify(int cafd, int kernel_server);
extern int	kcfd_modverify_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* _KCFD_H */
