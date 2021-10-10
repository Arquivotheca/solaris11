/*
 *
 * sshdirectory.c
 *
 * Author: Markku Rossi <mtr@ssh.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 *               All rights reserved.
 *
 * Unix implementation of the portable directory access interface.
 *
 */

#include <sshincludes.h>
#include <sshdirectory.h>

/*
 * Types and definitions.
 */

#define SSH_DEBUG_MODULE "SshDirectory"

struct SshDirectoryRec
{
  DIR *dir;
  struct dirent *dirent;
};


/*
 * Global functions.
 */

SshDirectoryHandle
ssh_directory_open(const char *directory)
{
  SshDirectoryHandle dir = ssh_xcalloc(1, sizeof(*dir));

  dir->dir = opendir(directory);
  if (dir->dir == NULL)
    {
      ssh_xfree(dir);
      return NULL;
    }

  return dir;
}


Boolean
ssh_directory_read(SshDirectoryHandle directory)
{
  SSH_ASSERT(directory != NULL);

  directory->dirent = readdir(directory->dir);

  return directory->dirent != NULL;
}


void
ssh_directory_close(SshDirectoryHandle directory)
{
  SSH_ASSERT(directory != NULL);

  closedir(directory->dir);
  ssh_xfree(directory);
}


/* Directory entry access functions. */

const char *
ssh_directory_file_name(SshDirectoryHandle directory)
{
  SSH_ASSERT(directory != NULL);
  SSH_ASSERT(directory->dirent != NULL);

  return directory->dirent->d_name;
}
