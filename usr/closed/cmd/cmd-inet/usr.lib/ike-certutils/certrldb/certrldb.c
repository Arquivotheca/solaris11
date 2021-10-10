/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/param.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <libintl.h>
#include <locale.h>

#include <ike/sshincludes.h>
#include <ike/sshfileio.h>
#include <ike/sshbase64.h>
#include <ike/certlib.h>
#include "dumputils.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#undef	snprintf

extern char *optarg;
extern int optind;

/* In ssh-certview.c */
extern Boolean dump_crl(SshX509Crl);

/* verbose list option */
int verbose = 0;

/* Variable needed for proper linking with dumputils.c */
char *pkcs11_token_id;

/* Certificate Library Mode */
int certlib_mode;

#define	debug (certlib_mode & CERTLIB_DEBUG)

/* For libike's X.509 routines. */
static struct SshX509ConfigRec x509config_storage;
SshX509Config x509config;

static void
usage(void)
{
	(void) fprintf(stderr, "%s\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n"
	    "\t%s\n\t%s\n\n\t%s\n\t%s\n\n\t%s\n\t%s\n\n",
	    gettext("Usage:"),
	    gettext("certrldb [-v] -l [pattern]"),
	    gettext(" List CRLs"),
	    gettext("certrldb -a"),
	    gettext(" Add CRL to database via stdin"),
	    gettext("certrldb -e [-f output_format] pattern"),
	    gettext(" Extract CRL from database via stdout."),
	    gettext("certrldb -r pattern"),
	    gettext(" Remove CRL from database."),
	    gettext("certrldb -h"),
	    gettext(" This help page"));
	exit(1);
}

/*
 * This extracts the data from a BER formated buffer.	Required are the
 * buffer & size of the buffer.
 *
 * If extraction fails, due to incorrect buffer format (ie PEM) or not
 * IKE data, NULL is returned.
 * If successful, the crl structure is returned.
 */
static SshX509Crl
extract_from_ber(unsigned char *buf, size_t len)
{
	SshX509Crl crl;

	crl = ssh_x509_crl_allocate();
	if (ssh_x509_crl_decode(buf, len, crl) == SSH_X509_OK) {
		if (debug)
			(void) fprintf(stderr, "%s\n",
			    gettext("This is a CRL."));
	} else {
		ssh_x509_crl_free(crl);
		crl = NULL;
	}

	if (crl == NULL) {
		if (debug)
			(void) fprintf(stderr, "%s\n",
			    gettext("Not BER format."));
	}

	return (crl);
}

/*
 * This prepares the certspec pattern for a search through the certificates
 * database for a CRL.  The below are CRL certspec patterns are not valid for
 * certificate certspec patterns
 */
void
search_certs(struct certlib_certspec *pattern)
{
	int i = -1;
	while (pattern->num_excludes > (++i)) {
		if (strcmp(pattern->excludes[i], "SLOT") != 0) {
			pattern->excludes[i] = "";
			break;
		}
	}
	i = -1;
	while (pattern->num_includes > (++i)) {
		if (strcmp(pattern->includes[i], "SLOT") != 0) {
			pattern->includes[i] = "";
			break;
		}
	}
}

int matches = 0;

/* Print out CRL */
int
print_crl(struct certlib_crl *p)
{
	matches = 1;

	(void) printf("%-2s) CRL: %s\n", p->slotname, p->issuer_name);

	if (verbose) {
		(void) dump_crl(p->crl);
		(void) printf("\n");
	}
	return (0);
}

int
print_crl_from_cert(struct certlib_cert *p)
{
	if (p->crl != NULL) {
		(void) print_crl(p->crl);
		return (1);
	}
	if (debug)
		(void) fprintf(stderr, "%s\n",
		    gettext("Cert found, but no CRL attached."));
	return (0);
}

int
print_crls(struct certlib_certspec *pattern)
{
	if (pattern) {
		certlib_find_crl_spec(pattern, print_crl);
		if (!matches) {
			search_certs(pattern);
			certlib_find_cert_spec(pattern, print_crl_from_cert);

			if (matches == 0) {
				if (debug)
					(void) fprintf(stderr, "%s\n",
					    gettext("Not certificate match."));
				return (1);
			}
		}
	} else {
		certlib_iterate_crls(print_crl);
	}
	return (0);
}

void
add_crl(const char *infile)
{
	FILE *f;
	char outfile[MAXPATHLEN];
	unsigned char *b = NULL, *p = NULL;
	size_t size = 0, total_size = 0;
	int i = 0, fout;
	SshX509Crl crl;

	/* Assign or open input source */
	if (infile == NULL)
		f = stdin;
	else {
		if ((f = fopen(infile, "r")) == NULL) {
			perror(infile);
			exit(1);
		}
	}

	/* Load data and write to file */
	while (feof(f) == 0) {
		b = ssh_realloc(b, total_size, 1024 + total_size);
		if (b == NULL)
			memory_bail();

		size = fread(b+total_size, sizeof (unsigned char), 1023, f);
		total_size += size;
	}
	b[total_size] = NULL;
	if (f != stdin)
		(void) fclose(f);

	/* Extract certificate if input was BER */
	crl = extract_from_ber(b, total_size);


	/*
	 * Check if extraction was successful, if not, try converting from
	 * PEM and try extractioning again.
	 */
	if (crl == NULL) {
		p = (unsigned char *)pem_to_ber(b, &total_size);
		if (p == NULL) {
			(void) fprintf(stderr, "%s\n",
			    gettext("Input is neither PEM-encoded "
			    "nor BER-encoded; unable to continue."));
			exit(1);
		}
		crl = extract_from_ber(p, total_size);
		if (crl == NULL) {
			(void) fprintf(stderr, "%s\n",
			    gettext("Unknown format given."));
			exit(1);
		} else
			if (debug)
				(void) fprintf(stderr, "%s\n",
				    gettext("PEM format detected."));
	} else {
		if (debug)
			(void) fprintf(stderr, "%s\n",
			    gettext("BER format detected."));
		p = b;
	}

	/* If extraction successful, import data to database */
	if (crl != NULL) {
		/* Find available slot number and open */
		(void) snprintf(outfile, MAXPATHLEN, "%s%d",
		    certlib_crls_dir(), i);
		while ((fout = open(outfile, O_CREAT|O_EXCL|O_WRONLY,
		    S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) < 0) {
			if (errno == EEXIST) {
				(void) snprintf(outfile, MAXPATHLEN, "%s%d",
				    certlib_crls_dir(), ++i);
			} else {
				perror(gettext("Unable to open CRL database."));
				exit(1);
			}
		}
		if (write(fout, p, total_size) != total_size)
			perror(outfile);
		(void) close(fout);

	} else {
		(void) fprintf(stderr, "%s\n", gettext("No CRL detected."));
		exit(1);
	}

	ssh_free(p);
	ssh_free(b);
	/* ssh_x509_crl_free(crl); */
}

int errors = 0;

int
remove_crl(struct certlib_crl *p)
{
	char filename[MAXPATHLEN];

	matches = 1;

	(void) snprintf(filename, MAXPATHLEN, "%s%s",
	    certlib_crls_dir(), p->slotname);
	if (unlink(filename) != 0) {
		(void) fprintf(stderr, "certrldb: unlink %s: %s", p->slotname,
		    strerror(errno));
		errors = 1;

	}
	return (0);
}

int
remove_crl_from_cert(struct certlib_cert *p)
{
	if (p->crl != NULL) {
		(void) remove_crl(p->crl);
		return (1);
	}
	if (debug)
		(void) fprintf(stderr, "%s\n",
		    gettext("Cert found, but no CRL attached."));
	return (0);
}

int
remove_crls(struct certlib_certspec *pattern)
{

	certlib_find_crl_spec(pattern, remove_crl);

	if (!matches) {
		search_certs(pattern);
		certlib_find_cert_spec(pattern, remove_crl_from_cert);

		if (debug && (matches == 0))
			(void) fprintf(stderr, "%s\n",
			    gettext("Not certificate match."));
	}

	if (!matches) {
		(void) fprintf(stderr, "%s\n",
		    gettext("certrldb: no CRL found"));
		return (1);
	}

	return (errors);
}

const char *extract_outfile;
const char *extract_format;

int
extract_crl(struct certlib_crl *p)
{
	matches = 1;
	export_data(p->data, p->datalen, extract_outfile, extract_format,
	    SSH_PEM_X509_CRL_BEGIN, SSH_PEM_X509_CRL_END);
	return (1);
}

int
extract_crl_from_cert(struct certlib_cert *p)
{
	if (p->crl != NULL) {
		(void) extract_crl(p->crl);
		return (1);
	}
	if (debug)
		(void) fprintf(stderr, "%s\n",
		    gettext("Cert found, but no CRL attached."));
	return (0);
}

void
extract_crls(struct certlib_certspec *pattern, char *outfile, char *out_fmt)
{
	extract_outfile = outfile;
	extract_format = out_fmt;

	certlib_find_crl_spec(pattern, extract_crl);

	if (!matches) {
		search_certs(pattern);
		certlib_find_cert_spec(pattern, extract_crl_from_cert);

		if (debug && (matches == 0))
			(void) fprintf(stderr, "%s\n",
			    gettext("Not certificate match."));
	}

	if (matches == 0) {
		(void) fprintf(stderr, "%s\n", gettext("Not match."));
		exit(1);
	}
}

int
main(int argc, char *argv[])
{
	struct certlib_certspec *pattern;
	int help = 0;
	int lflag = 0, aflag = 0, rflag = 0, eflag = 0;
	char c, *infile = NULL, *out_fmt = NULL, *outfile = NULL;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	certlib_mode = CERTLIB_NORMAL;
	if (argc == 1)
		help = 1;

	while ((c = getopt(argc, argv, "h?dvlaref:i:o:")) != EOF) {
		switch (c) {
		case 'h':
		case '?':
			help = 1;
			break;
		case 'd':
			certlib_mode |= CERTLIB_DEBUG;
			break;
		case 'a': /* Add cert */
			aflag = 1;
			break;
		case 'i': /* input file */
			infile = optarg;
			break;
		case 'r': /* Remove cert */
			rflag = 1;
			break;
		case 'e': /* Extract certificate */
			eflag = 1;
			break;
		case 'f': /* certificate out_fmt */
			out_fmt = optarg;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'o':
			outfile = optarg;
			break;
		default:
			help = 1;
		}
	}

	if (help ||
	    (!aflag && !rflag && !eflag && !lflag))
		usage();

	/* Initialize the libike X.509 library. */
	x509config = &x509config_storage;
	ssh_x509_library_set_default_config(x509config);
	if (!ssh_x509_library_initialize(x509config))
		ssh_fatal("x509_library_initialize failed.");

	if (!certlib_init(certlib_mode, CERTLIB_CRL|CERTLIB_CERT))
		exit(1);

	if (aflag) {
		add_crl(infile);
		/* add_crl() calls exit(). */
	} else if (rflag) {
		pattern = gather_certspec(argv + optind, argc - optind);
		if (pattern == NULL) {
			(void) fprintf(stderr,
			    gettext("certrldb: missing pattern for remove\n"));
			exit(1);
		}
		exit(remove_crls(pattern));
	} else if (eflag) {
		pattern = gather_certspec(argv + optind, argc - optind);
		if (pattern == NULL) {
			(void) fprintf(stderr,
			    gettext("certrldb: missing pattern for extract\n"));
			exit(1);
		}
		extract_crls(pattern, outfile, out_fmt);
		/* extract_crls() will exit when appropriate. */
	} else if (lflag) {
		pattern = gather_certspec(argv+optind, argc-optind);
		exit(print_crls(pattern));
#if 0
	} else if (gflag) {
		generate_crl(dname, serialnum, keytype, outfile, out_fmt);
#endif
	}

	return (EXIT_SUCCESS);
}
