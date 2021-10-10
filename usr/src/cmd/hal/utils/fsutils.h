/*
 *
 * fsutils.h : definitions for filesystem utilities
 *
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file is licensed under either the Academic Free License
 * version 2.1 or The GNU General Public License version 2.
 *
 */

#ifndef FSUTILS_H
#define	FSUTILS_H

#include <sys/types.h>
#include <sys/vtoc.h>

boolean_t dos_to_dev(char *path, char **devpath, int *num);
char *get_slice_name(char *devlink);
boolean_t is_dos_drive(uchar_t id);
boolean_t is_dos_extended(uchar_t id);
boolean_t find_dos_drive(int fd, int num, uint_t secsz, off_t *offset);
int get_num_dos_drives(int fd, uint_t);
boolean_t vtoc_one_slice_entire_disk(struct extvtoc *vtoc);

#endif /* FSUTILS_H */
