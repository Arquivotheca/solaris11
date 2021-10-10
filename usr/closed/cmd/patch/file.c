/*
 * Copyright (c) 1995, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * File: file.c
 * Date: Sun Feb 12 19:12:01 PST 1995
 *
 * Description:
 *
 *	This file contain routines for file management / manipulation.
 *	Each opened file is converted to wide character format and
 *	stored in P_tmpdir as an intermediate file until the program exits.
 *	Files previously opened will use the intermediate version
 *	of the file and the original version is ignored.
 *
 *	All data manipulation performed by insert_line, and delete_line
 *	is done to the intermediate version of the file.
 *
 *	Fetch_line maps the appropriate page of the temp files into
 *	memory and returns the pointer to the requested line.
 *
 *	Data will not be written out to a file until sync_file is called.
 *	sync_file will not alter the state of the intermediate file.
 *
 *	These algorithms enable patch to process any arbitrary number of files
 *	and allow each and every file to be modified endlessly in any order
 *	specified by the patch file "Index:" lines and/or contest diff file
 *	lines.  Additional hooks have been placed in the sync_file routine
 *	to force writing to a specific file for the case were the output
 *	file is specifically specified with the -o option.
 *
 *	A visual view the data structures in action:
 *
 *	opened_file_descriptors = 3
 *
 *	file_info **opened_file
 *	+------------+
 *	|            |--------->+---------------------------------+
 *	+------------+          | char        *name = "patch_file"|
 *	|            |------+   +---------------------------------+
 *	+------------+      |   | ...                             |
 *	|            |----+ |   +---------------------------------+
 *	+------------+    | |   | ulong        mapped_page = 0    |
 *	                  | |   +---------------------------------+
 *	                  | |   | caddr_t      mapped_address     |
 *	                  | |   +---------------------------------+
 *	                  | |   | line_address *lines[]           |
 *	                  | |   +---------------------------------+
 *	                  | |
 *	                  | +-->+---------------------------------+
 *	                  |     | char        *name = "file1"	  |
 *	                  |     +---------------------------------+
 *	                  |     | ...                             |
 *	                  |     +---------------------------------+
 *	                  |     | ulong        mapped_page = 99   |
 *	                  |     +---------------------------------+
 *	                  |     | caddr_t      mapped_address     |
 *	                  |     +---------------------------------+
 *	                  |     | line_address *lines[]           |
 *	                  |     +---------------------------------+
 *	                  |
 *	                  +---->+---------------------------------+
 *	                        | char        *name = "file2"	  |
 *	                        +---------------------------------+
 *	                        | ...                             |
 *	                        +---------------------------------+
 *	                        | ulong        mapped_page = 10   |
 *	                        +---------------------------------+
 *	                        | caddr_t      mapped_address     |
 *	                        +---------------------------------+
 *	                        | line_address *lines[]           |
 *	                        +---------------------------------+
 */

/*
 * Include files:
 */
#include	"config.h"
#include 	"common.h"


/*
 * local variables.
 */

file_info	**opened_files = NULL;
unsigned long	opened_file_descriptors = 0;
static long	page_size;


/*
 * Function: void insert_line(file_info, wchar_t *, LINENUM)
 *
 * Description:
 *
 *	Insert the wide character string into the file at a specific
 *	line.
 *
 * Inputs:
 *	info	-> A pointer to the file descriptor.
 *	data	-> A pointer to the wide character string to insert.
 *	line	-> Line number in file to insert.
 */

void
insert_line(file_info *info, wchar_t *data, LINENUM line)
{
	line_address	*out, *in;
	unsigned long	i;
	off_t		eof;

	/*
	 * Handle inserts beyond end of file by putting empty lines
	 * at the end of the file until just before the requested position.
	 */
	while (line > info->line_count) {
		insert_line(info, L"\n", info->line_count);
	}

	/* Expand internal data structures if necessary */
	if (info->line_count >= info->max_lines) {
		info->max_lines += LINE_REALLOC_INCR;
		info->lines = (line_address *)reallocate(info->lines,
		    sizeof (line_address) * info->max_lines);
	}

	/* shift up all lines above the line to insert */
	if (line < info->line_count) {
		out = &info->lines[info->line_count];
		in = out - 1;
		for (i = line; i < info->line_count; i++) {
			*(out--) = *(in--);
		}
	}

	/*
	 * Find the end of file this is where the new text will go.
	 */
	if ((eof = lseek(info->tempfd, 0, SEEK_END)) == -1) {
		pfatal(gettext("Temp file seek error"));
		/* NOTREACHED */
	}

	/*
	 * Point to it...
	 */
	info->lines[line].offset = eof;
	info->lines[line].length = sizeof (wchar_t) * (wcslen(data) + 1);

	/*
	 * and write out the actual data.
	 */
	if (write(info->tempfd, data, info->lines[line].length) == -1) {
		pfatal(gettext("Temp file write error"));
		/* NOTREACHED */
	}

	/* count new line */
	info->line_count++;
}


/*
 * Function: void delete_line(info, line)
 *
 * Description:
 *
 *	Delete the specified line from the file.
 *
 * Inputs:
 *	info	-> A pointer to the file descriptor.
 *	line	-> Line number in file to delete.
 */

void
delete_line(file_info *info, LINENUM line)
{
	line_address	*in, *out;

	if (info->line_count > 0) {
		out = &info->lines[line];
		in = out + 1;
		while (line++ < info->line_count) {
			*(out++) = *(in++);
		}
		info->line_count--;
	}
}


/*
 * Function: wchar_t *fetch_line(info, line)
 *
 * Description:
 *
 *	Map the specified line of the file into system memory and
 *	return the line's address.
 *
 * Inputs:
 *	info	-> A pointer to the file descriptor.
 *	line	-> Line number in file to insert.
 *
 * Returns:
 *	wchar_t * -> A pointer to the files line, NULL if line does not exist.
 */

wchar_t *
fetch_line(file_info *info, LINENUM line)
{
	unsigned long	s_page, e_page, n_pages;
	unsigned long	mapped_page, mapped_npages;

	if (line < 0 || line >= info->line_count)
		return (NULL);

	/* Beginning of this line belongs to 's_page' */
	s_page = info->lines[line].offset / page_size;

	/* End of this line belongs to 'e_page' */
	e_page = (info->lines[line].offset + info->lines[line].length - 1) /
	    page_size;
	/* This line consumes 'n_pages' */
	n_pages = e_page - s_page + 1;

	/* beginning of the currently mapped page */
	mapped_page = info->mapped_page;

	/* number of the currently mapped pages */
	mapped_npages = info->mapped_npages;

	if ((info->mapped_address == NULL) ||
	    (s_page < mapped_page) ||
	    (e_page > mapped_page + mapped_npages - 1)) {
		/*
		 * not yet mapped or
		 * this line is not entirely contained in the pages currently
		 * mapped.
		 */
		if (info->mapped_address) {
			/* if currently mapped, unmap it */
			(void) munmap(info->mapped_address,
			    mapped_npages * page_size);
		}
		/*
		 * map from 's_page * page_size' for 'n_pages * page_size'
		 */
		info->mapped_address = mmap(0, n_pages * page_size,
		    PROT_READ, MAP_PRIVATE, info->tempfd,
		    s_page * page_size);

		if (info->mapped_address == (caddr_t)-1) {
			pfatal(gettext("Memory mapping error"));
			/* NOTREACHED */
		}
		info->mapped_page = s_page;
		info->mapped_npages = n_pages;
		return ((wchar_t *)((unsigned long)info->mapped_address +
		    info->lines[line].offset % page_size));
	} else {
		return ((wchar_t *)((unsigned long)info->mapped_address +
		    (s_page - mapped_page) * page_size +
		    info->lines[line].offset % page_size));
	}
}


/*
 * Function: void update_with_file_contents(info, file)
 *
 * Description:
 *
 *	Remove all the lines in file and replace with the data contained in
 *	the file specified by file.
 *
 *	This is used after an ed patch has been applied to get the changes
 *	made by ed.
 *
 * Inputs:
 *	info	-> A pointer to the file descriptor.
 *	file	-> Name of file that contains new data.
 */

void
update_with_file_contents(file_info *info, const char *file)
{
	FILE		*infd;

	if ((infd = fopen(file, "r")) == NULL) {
		pfatal(gettext("'%s'"), file);
		/* NOTREACHED */
	}

	/* Blow off any previous data */
	(void) ftruncate(info->tempfd, 0);
	info->line_count = 0;

	/* We don't think any pages are mapped already */
	info->mapped_address = NULL;
	info->line_count = 0;

	/*
	 * We don't need to expand lines here since the line length
	 * is the maximum line length of all files including the patch file.
	 * Any inserted lines must already exist in the patch file.
	 */
	while (fgetws(wbuf, max_input - 1, infd) != NULL) {
		insert_line(info, wbuf, info->line_count);
	}
	if (ferror(infd)) {
		pfatal(gettext("'%s'"), file);
		/* NOTREACHED */
	}

	(void) fclose(infd);
}


/*
 * Function: void cache_file(file, info)
 *
 * Description:
 *
 *	Create temp file, convert all the lines in file into wide character
 *	format, write lines to file, and update data structures.
 *
 * Inputs:
 *	info	-> A pointer to the file descriptor.
 *	file	-> Name of file that contains new data.
 */

static void
cache_file(const char *file, file_info *info)
{
	FILE	*infd;
	int	characters;

	if (*file != NULL) {
		if ((infd = fopen(file, "r")) == NULL) {
			pfatal(gettext("'%s'"), file);
			/* NOTREACHED */
		}
	} else {
		infd = stdin;
	}

	info->temp_file = savestr(tmpnam(NULL));
	if ((info->tempfd = open(info->temp_file,
	    (O_RDWR|O_CREAT|O_EXCL|O_TRUNC), 0600)) == -1) {
		pfatal(gettext("Can not open temporary file"));
		/* NOTREACHED */
	}

	info->mapped_address = NULL;
	info->line_count = 0;
	while (fgetws(wbuf, max_input - 1, infd) != NULL) {
		/*
		 * Every line should end in a newline, if this line does
		 * not then we assume our buffers are too small and we
		 * expand them to fit the new linesize.
		 */
		characters = wcslen(wbuf);
		while (wbuf[characters - 1] != L'\n') {
			max_input += BUFFER_REALLOC_SIZE;
			buf = reallocate(buf, (max_input + 1) * sizeof (char));
			wbuf = reallocate(wbuf,
			    (max_input + 1) * sizeof (wchar_t));
			if (fgetws(&wbuf[characters], BUFFER_REALLOC_SIZE,
			    infd) == NULL)
				break;
			characters = wcslen(wbuf);
		}
		insert_line(info, wbuf, info->line_count);
	}
	if (ferror(infd)) {
		pfatal(gettext("'%s'"), file);
		/* NOTREACHED */
	}

	(void) fclose(infd);
}


/*
 * Function: int open_file(file, empty)
 *
 * Description:
 *
 *	Open a file and initialize data structures.
 *	make an empty file if required.
 *
 * Inputs:
 *	file	-> Name of file to open.
 *	empty	-> Make the file an empty file.
 *
 * Returns:
 *	file table entry.
 */

int
open_file(char *file, int empty)
{
	int	i;

	for (i = 0; i < opened_file_descriptors; i++) {
		if (strcmp(file, opened_files[i]->name) == 0)
			break;
	}
	if (i == opened_file_descriptors) {
		opened_file_descriptors++;
		if (opened_file_descriptors == 1) {
			opened_files = allocate(sizeof (file_info *));
			page_size = sysconf(_SC_PAGESIZE);
		} else {
			opened_files = reallocate(opened_files,
			    sizeof (file_info *) *
			    opened_file_descriptors);
		}

		opened_files[i] = allocate(sizeof (file_info));
		opened_files[i]->name = savestr(file);
		opened_files[i]->tempfd = -1;
		opened_files[i]->mapped_address = NULL;

		/* Allocate some initial data space too */
		opened_files[i]->max_lines = LINE_REALLOC_INCR;
		opened_files[i]->lines = (line_address *)
		    allocate(sizeof (line_address) * LINE_REALLOC_INCR);

		if (empty)
			cache_file("/dev/null", opened_files[i]);
		else
			cache_file(file, opened_files[i]);
	} else {
		if ((opened_files[i]->tempfd = open(opened_files[i]->temp_file,
		    O_RDWR|O_EXCL)) == -1) {
			pfatal(gettext("Can not reopen temporary file"));
			/* NOTREACHED */
		}
	}
	return (i);
}

void
close_file(file_info *info)
{
	if (info->mapped_address) {
		(void) munmap(info->mapped_address,
		    info->mapped_npages * page_size);
		info->mapped_address = NULL;
	}
	(void) close(info->tempfd);
	info->tempfd = -1;
}

/*
 * Function: void sync_file(info, outname, create_outputfile)
 *
 * Description:
 *
 *	Write all the lines of a file to output file.  If the output
 *	file doesn't already exist (i.e., the create_outputfile flag is
 *	set), change the permissions of the output file to be those of
 *	the target file.  Write to original file name if outfile is
 *	not specified.
 *
 * Inputs:
 *	info	-> A pointer to the file descriptor.
 *	outname	-> Name of output file to write to.
 *	create_outputfile -> flag set if we are creating the output file.
 */

void
sync_file(file_info *info, char *outname, int create_outputfile)
{
	static int	outname_opened = 0;
	static FILE	*out;
	wchar_t		*ret;
	struct stat	statbuf;
	unsigned	i;

	if (lstat(info->name, &statbuf) == -1) {
		statbuf.st_mode = 0666;
	}

	if (info->flags & SAVE_ORIGINAL) {
		(void) snprintf(buf, max_input, "%s.orig", info->name);
		if (rename(info->name, buf) == -1) {
			pfatal(gettext("Failed to rename %s to %s"),
			    info->name, buf);
			/* NOTREACHED */
		}
		info->flags &= ~SAVE_ORIGINAL;
	}

	if (outname == NULL) {
		if ((out = fopen(info->name, "w")) == NULL) {
			(void) fprintf(stderr, "patch: ");
			(void) snprintf(buf, max_input,
			    gettext("Failed to open output file %s"),
			    info->name);
			perror(buf);
			return;
		}
		(void) fchmod(fileno(out), statbuf.st_mode);
	} else if (*outname == NULL) {
		if ((out = fopen(info->name, "w")) == NULL) {
			pfatal(gettext("Failed to open output file %s"),
			    info->name);
			/* NOTREACHED */
		}
		(void) fchmod(fileno(out), statbuf.st_mode);
	} else if (strcmp(outname, "-") == 0) {
		/* output goes to stdout */
		out = stdout;
	} else if (outname_opened == 0) {
		if ((out = fopen(outname, "w")) == NULL) {
			pfatal(gettext("Failed to open output file %s"),
			    outname);
			/* NOTREACHED */
		}
		if (create_outputfile) {
			(void) fchmod(fileno(out), statbuf.st_mode);
		}
		outname_opened = 1;
	}

	for (i = 0; i < info->line_count; i++) {
		ret = fetch_line(info, i);
		(void) fputws(ret, out);
	}
	(void) fflush(out);
	if (outname == NULL || *outname == NULL) {
		(void) fclose(out);
	}
}
