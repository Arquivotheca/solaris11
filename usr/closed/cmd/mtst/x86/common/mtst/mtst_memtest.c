/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Controls access to the memtest driver
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/memtest.h>

#include <mtst_debug.h>
#include <mtst_err.h>
#include <mtst.h>

static void
mtst_memtest_open(void)
{
	memtest_inq_t inq;
	uint_t mtflags = 0;
	int fd;

	if ((fd = open(MEMTEST_DEVICE, O_RDONLY)) < 0) {
		mtst_die("failed to open memtest device %s",
		    MEMTEST_DEVICE);
	}

	if (ioctl(fd, MEMTESTIOC_INQUIRE, &inq) < 0) {
		mtst_die("failed to query memtest device %s",
		    MEMTEST_DEVICE);
	}

	if (inq.minq_version != MEMTEST_VERSION) {
		mtst_die("memtest device version %d doesn't match expected "
		    "version %d\n", inq.minq_version, MEMTEST_VERSION);
	}

	if (mtst.mtst_flags & MTST_F_DEBUG)
		mtflags |= MEMTEST_F_DEBUG;

	if (mtst.mtst_flags & MTST_F_DRYRUN)
		mtflags |= MEMTEST_F_DRYRUN;

	if (mtflags != 0 && ioctl(fd, MEMTESTIOC_CONFIG, mtflags) < 0)
		mtst_die("failed to configure memtest driver");

	mtst.mtst_memtest_fd = fd;
}

void
mtst_memtest_close(void)
{
	ASSERT(mtst.mtst_memtest_fd != -1);
	(void) close(mtst.mtst_memtest_fd);
}

int
mtst_memtest_ioctl(int req, void *arg)
{
	if (mtst.mtst_memtest_fd == -1)
		mtst_memtest_open();

	return (ioctl(mtst.mtst_memtest_fd, req, arg));
}
