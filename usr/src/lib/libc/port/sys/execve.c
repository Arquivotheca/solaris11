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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "lint.h"
#include <unistd.h>
#include <sys/execx.h>
#include <sys/syscall.h>

int
execvex(uintptr_t file, char *const argv[], char *const envp[], int flags)
{
	return (syscall(SYS_execve, file, argv, envp, flags));
}

#pragma weak _execve = execve
int
execve(const char *path, char *const argv[], char *const envp[])
{
	return (execvex((uintptr_t)path, argv, envp, 0));
}

int
fexecve(int fd, char *const argv[], char *const envp[])
{
	return (execvex((uintptr_t)fd, argv, envp, EXEC_DESCRIPTOR));
}
