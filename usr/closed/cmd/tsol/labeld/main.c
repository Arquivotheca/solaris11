/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

/*
 *	Label translation service main program.
 */

#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/termios.h>

#include <priv.h>
#include <sys/modctl.h>

#include "gfi/std_labels.h"
#include <signal.h>

/* avoid conflicts with std_labels.h */

#ifdef	_STD_LABELS_H
#undef	ACCESS_RELATED
#undef	ALL_ENTRIES
#undef	LONG_WORDS
#undef	NO_CLASSIFICATION
#undef	SHORT_WORDS
#endif	/* STD_LABELS_H */

#include <tsol/label.h>

#include "impl.h"

/* Global definitions */

/* Server extenal definitions */

int		debug = 0;		/* debug level */
mutex_t		gfi_lock = DEFAULTMUTEX;	/* gfi code lock */

/* label prototypes for comparison */

m_label_t	m_low;		/* Admin Low label prototype */
m_label_t	m_high;		/* Admin High label prototype */

/*
 * XXX these two should replace the temporary m_low/high above once
 * life settles down as m_label_t admin_low/high.
 */
bclabel_t	admin_low;	/* the Admin Low label prototype */
bclabel_t	admin_high;	/* the Admin High label prototype */

m_label_t	clear_low;	/* Admin Low Clearance prototype */
m_label_t	clear_high;	/* Admin High Clearance prototype */

/* guaranteed input translation for */

char *Admin_low		= ADMIN_LOW;	/* Admin Low */
char *Admin_high	= ADMIN_HIGH;	/* Admin High */
int  Admin_low_size;			/* size of string */
int  Admin_high_size;			/* size of string */

/* maximum string lengths for return size computation */

static size_t	c_len;			/* Classification */
static size_t	clr_cvt_len;		/* Clearance dimming list */
static size_t	col_len;		/* Color name */
static size_t	sl_cvt_len;		/* SL dimming list */
static size_t	ver_len;		/* Version */
static char	label_file[MAXPATHLEN];	/* path to encodings file */

/* Server Local definitions */

static unsigned int	Max_class = MAX_CLASS;	/* default max class allowed */

extern char *optarg;
extern int optind;

static mutex_t		create_lock = DEFAULTMUTEX;
static int		max_threads = MAX_THREADS;
static int		num_threads = 0;
	/*
	 * used to signal thread exit to  server_d_destroy() as a side
	 * effect of having created the thread specific data element.
	 */
static thread_key_t	server_key;

static int
server_running(void)
{
	/* do a labeld null call, if successful one already running */
	return (0);
}

static void
detach_tty(void)
{
	(void) close(0);
	(void) close(1);
	(void) close(2);
	(void) setsid();
	(void) open("/dev/null", O_RDWR);
	(void) dup(0);
	(void) dup(0);
}

static void
usage(char *p)
{
	(void) fprintf(stderr, "Usage: %s [-c max_class] [-d debug_level] "
	    "[-t max_threads]\n", p);
	exit(1);
}

/* Create server threads */

/*ARGSUSED*/
static void *
new_thread(void *arg)
{
	static void *value = (void *) 1;

	if (debug > 2)
		(void) printf("new_thread called\n");

	/*
	 * Disable cancel because if a client call dies, it cancels the
	 * server thread which may be holding a lock.
	 * This seems to be a bug in the thread library.  The following
	 * code is to work around this.
	 */
	(void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	/*
	 * initialize any thread specific data here
	 */
	/* cause server_key destructor to be called to manage thread count */
	(void) thr_setspecific(server_key, value);
	(void) door_return(NULL, 0, NULL, 0);
/* NOTREACHED */
	return (NULL);		/* dummy */
}


/*ARGSUSED*/
static void
server_t_create(door_info_t *info)
{
	(void) mutex_lock(&create_lock);
	if (debug > 2) {
		(void) printf("server_t_create(cur_cnt = %d, max = %d)\n",
		    num_threads, max_threads);
	}

	if (num_threads >= max_threads) {
		(void) mutex_unlock(&create_lock);
		return;
	}
	num_threads++;
	(void) mutex_unlock(&create_lock);
	(void) thr_create(NULL, 0, new_thread, NULL, THR_BOUND | THR_DETACHED,
	    NULL);
}

/* Manage server threads */

/*ARGSUSED*/
static void
server_t_destroy(void *arg)
{
	/*
	 * dispose of any thread specific data here
	 */
	(void) mutex_lock(&create_lock);
	num_threads--;
	if (debug > 2)
		(void) printf("server_t_destroy(cur_cnt = %d)\n", num_threads);

	(void) mutex_unlock(&create_lock);
}

/* local procedures */

static size_t
cvt_size(struct l_tables *table)
{
	int	i;
	int	prefix;
	int	suffix;
	size_t	l = 0;

	for (i = table->l_first_main_entry; i < table->l_num_entries; i++) {
		struct l_word *word = &(table->l_words[i]);

		/* Long Word */

		/* Prefix ? */
		if ((prefix = word->l_w_prefix) >= 0) {
			l += strlen(table->l_words[prefix].l_w_output_name) + 1;
		}

		/* Word */
		l += strlen(word->l_w_output_name) + 1;

		/* Suffix ? */
		if ((suffix = word->l_w_suffix) >= 0) {
			l += strlen(table->l_words[suffix].l_w_output_name) + 1;
		}

		/* Short Word ? */
		if (word->l_w_soutput_name) {
			/* Prefix ? */
			if ((prefix = word->l_w_prefix) >= 0) {
				if (table->l_words[prefix]. l_w_soutput_name) {
					l += strlen(table->l_words[prefix].
					    l_w_soutput_name) + 1;
				} else {
					l += strlen(table->l_words[prefix].
					    l_w_output_name) + 1;
				}
			}

			/* Short Word */
			l += strlen(word->l_w_soutput_name) + 1;

			/* Suffix ? */
			if ((suffix = word->l_w_suffix) >= 0) {
				if (table->l_words[suffix].l_w_soutput_name) {
					l += strlen(table->l_words[suffix].
					    l_w_soutput_name) + 1;
				} else {
					l += strlen(table->l_words[suffix].
					    l_w_output_name) + 1;
				}
			}
		} else {
			l++;	/* '\0' for No Short Name */
		}  /* if short word */
	}  /* for () */
	return (l);
}

/*
 * l_encodings_initialized - called by std_labels.c routines to ensure
 *	parse tables initialized.
 *
 *	Do server specific initializtion here as well.
 */

int
l_encodings_initialized(void)
{
	static int initialized = 0;	/* have I been here yet */
	int	size;
	int	i;
	size_t	ac_len;			/* Alternate Class string length */
	size_t	lc_len;			/* Long Class string length */
	size_t	sc_len;			/* Short Class string length */

	if (initialized == 0) {
		initialized = 1;
		if (label_file[0] == '\0') {
			(void) snprintf(label_file, MAXPATHLEN, "%s%s",
			    ENCODINGS_PATH, ENCODINGS_NAME);
		}
		if (debug > 1)
			(void) fprintf(stderr, "label file = %s\n",
			    label_file);
		if (l_init(label_file, Max_class, MAX_COMPS, MAX_MARKS) != 0) {
			if (debug)
				(void) fprintf(stderr,
				    "%s initialization failed.\n",
				    label_file);
			else
				syslog(LOG_ALERT, "%s initialization failed.",
				    label_file);
			return (FALSE);
		}

		/* server specific stuff here */

		/* Initialize low and high label prototypes */

		_LOW_LABEL(&m_low, SUN_MAC_ID);
		_HIGH_LABEL(&m_high, SUN_MAC_ID);
		_LOW_LABEL(&clear_low, SUN_UCLR_ID);
		_HIGH_LABEL(&clear_high, SUN_UCLR_ID);

		/*
		 * XXX these two should replace the temporary m_low/high
		 * above once life settles down as m_label_t admin_low/high.
		 */
		BCLLOW(&admin_low);
		BCLHIGH(&admin_high);

		/* update table max lengths */

		size = L_MAX(Admin_low_size, Admin_high_size);

		l_information_label_tables->l_max_length = L_MAX(size,
		    l_information_label_tables->l_max_length);
		l_sensitivity_label_tables->l_max_length = L_MAX(size,
		    l_sensitivity_label_tables->l_max_length);
		l_clearance_tables->l_max_length = L_MAX(size,
		    l_clearance_tables->l_max_length);

		/* set max return lengths */

		/* classification length */
		c_len = 0;
		ac_len = 0;
		lc_len = 0;
		sc_len = 0;
		for (i = 0; i <= l_hi_sensitivity_label->l_classification;
		    i++) {
			if (l_long_classification[i] != NULL) {
				lc_len = L_MAX(lc_len,
				    strlen(l_long_classification[i]));
				c_len = L_MAX(c_len, lc_len);
			}
			if (l_short_classification[i] != NULL) {
				sc_len = L_MAX(sc_len,
				    strlen(l_short_classification[i]));
				c_len = L_MAX(c_len, sc_len);
			}
			/* not presently used */
			if (l_alternate_classification[i] != NULL) {
				ac_len = L_MAX(ac_len,
				    strlen(l_alternate_classification[i]));
				c_len = L_MAX(c_len, ac_len);
			}
		}
		c_len++;	/* terminating '\0' */

		ver_len = strlen(l_version) + 1;

		/* dimming lists */

		/*
		 * max string + dimming list size +
		 * (long classification + short classification) +
		 * (long words + short words)
		 */

		clr_cvt_len = l_clearance_tables->l_max_length +
		    (l_clearance_tables->l_num_entries +
		    l_hi_sensitivity_label->l_classification + 1) +
		    (lc_len * (l_hi_sensitivity_label->l_classification + 1)) +
		    (sc_len * (l_hi_sensitivity_label->l_classification + 1)) +
		    cvt_size(l_clearance_tables);
		sl_cvt_len = l_sensitivity_label_tables->l_max_length +
		    (l_sensitivity_label_tables->l_num_entries +
		    l_hi_sensitivity_label->l_classification + 1) +
		    (lc_len * (l_hi_sensitivity_label->l_classification + 1)) +
		    (sc_len * (l_hi_sensitivity_label->l_classification + 1)) +
		    cvt_size(l_sensitivity_label_tables);

		/* color table */
		if (color_table != NULL) {
			cte_t *entry = *color_table;

			col_len = L_MAX(strlen(high_color), strlen(low_color));
			while (entry != NULL) {
				col_len = L_MAX(col_len, strlen(entry->color));
				entry = entry->next;
			}
		}
		if (color_word != NULL) {
			cwe_t *word = color_word;

			while (word != NULL) {
				col_len = L_MAX(col_len, strlen(word->color));
				word = word->next;
			}
		}
		col_len++;
	}
	return (TRUE);
}

/*ARGSUSED*/
static void
null(labeld_call_t *call, labeld_ret_t *ret, size_t *len, const ucred_t *uc)
{
	*len = RET_SIZE(null_ret_t, 0);
	ret->rvals.null_ret.null = ucred_getpid(uc);
	if (debug > 1) {
		(void) printf("labeld op=null:\n");
		(void) printf("\teuid = %d, egid = %d\n",
		    (int)ucred_geteuid(uc), (int)ucred_getegid(uc));
		(void) printf("\truid = %d, rgid = %d\n",
		    (int)ucred_getruid(uc), (int)ucred_getrgid(uc));
		(void) printf("\tpid = %d\n", (int)ucred_getpid(uc));
	}
}

/*ARGSUSED*/
static void
labeld(void *cookie, char *argp, size_t arg_size, door_desc_t *dp,
    uint_t n_disc)
{
	/*LINTED*/
	labeld_call_t *call = (labeld_call_t *)argp;
	labeld_data_t rval;
	labeld_ret_t *reply = (labeld_ret_t *)&rval;
	size_t	len = sizeof (labeld_data_t);
	size_t	call_len = 0;
	const ucred_t	*uc = NULL;
	uint_t	opcode;

	if (debug > 1) {
		(void) printf("labeld(cookie=%llx, argp=%x, arg_size=%d, "
		    "descp=%x, ndisc=%d)\n", (door_ptr_t)(uintptr_t)cookie,
		    (int)argp, arg_size, (int)dp, n_disc);
	}

	if (call == DOOR_UNREF_DATA) {

		if (debug > 1)
			(void) printf("Bad door call received. Exiting\n");

		syslog(LOG_ERR, "Bad door call received. Exiting");
		exit(0);
	}

	if (call == NULL) {

		/* Just be nice. */
		if (debug > 1)
			(void) printf("Null call received\n");

		(void) door_return(NULL, 0, NULL, 0);
	}

	if (door_ucred((ucred_t **)&uc) < 0) {
		if (debug > 1)
			perror("Can't get client cred");

		syslog(LOG_ERR, "Can't get client cred %m");
		reply->ret = SERVERFAULT;
		goto done;
	}

	/* preset reply */

	opcode = call->op;
	if (debug > 1)
		(void) printf("labeld (%d) opcode = %d\n",
		    (int)ucred_getpid(uc), opcode);

	/* set incoming buffer size and other precall setup */
	switch (opcode) {

	case BLINSET:
		call_len = CALL_SIZE(inset_call_t, 0);
		break;

	case BSLVALID:
		call_len = CALL_SIZE(slvalid_call_t, 0);
		break;

	case BCLEARVALID:
		call_len = CALL_SIZE(clrvalid_call_t, 0);
		break;

	case LABELINFO:
		call_len = CALL_SIZE(info_call_t, 0);
		break;

	case LABELFIELDS:
		call_len = CALL_SIZE(fields_call_t, 0);
		break;

	case UDEFS:
		call_len = CALL_SIZE(udefs_call_t, 0);
		break;

	/* ensure incoming strings are terminated */

	case STOBSL:
		argp[arg_size - 1] = '\0';
		call_len = CALL_SIZE(stobsl_call_t, -BUFSIZE + 1);
		break;

	case STOBCLEAR:
		argp[arg_size - 1] = '\0';
		call_len = CALL_SIZE(stobclear_call_t, -BUFSIZE + 1);
		break;

	case STOL:
		argp[arg_size - 1] = '\0';
		call_len = CALL_SIZE(sl_call_t, -BUFSIZE + 1);
		break;

	case SETFLABEL:
		argp[arg_size - 1] = '\0';
		call_len = CALL_SIZE(setfbcl_call_t, -BUFSIZE + 1);
		break;

	/* ensure enough room for outgoing reply */

	case LABELVERS:
		len = RET_SIZE(char, ver_len);
		call_len = CALL_SIZE(vers_call_t, 0);
		break;

	case BLTOCOLOR:
		len = RET_SIZE(char, col_len);
		call_len = CALL_SIZE(color_call_t, 0);
		break;

	case BSLTOS:
		len = RET_SIZE(char, l_sensitivity_label_tables->l_max_length);
		call_len = CALL_SIZE(bsltos_call_t, 0);
		break;

	case BCLEARTOS:
		len = RET_SIZE(char, l_clearance_tables->l_max_length);
		call_len = CALL_SIZE(bcleartos_call_t, 0);
		break;

	case BSLCVT:
		len = RET_SIZE(bslcvt_ret_t, sl_cvt_len);
		call_len = CALL_SIZE(bslcvt_call_t, 0);
		break;

	case BCLEARCVT:
		len = RET_SIZE(bclearcvt_ret_t, clr_cvt_len);
		call_len = CALL_SIZE(bclearcvt_call_t, 0);
		break;

	/* DIA printer banner labels */
	case PR_TOP:
		len = RET_SIZE(pr_ret_t, c_len);
		call_len = CALL_SIZE(pr_call_t, 0);
		break;

	case PR_LABEL:
		len = RET_SIZE(pr_ret_t,
		    c_len + l_sensitivity_label_tables->l_max_length);
		call_len = CALL_SIZE(pr_call_t, 0);
		break;

	case PR_CAVEATS:
		len = RET_SIZE(pr_ret_t,
		    l_printer_banner_tables->l_max_length);
		call_len = CALL_SIZE(pr_call_t, 0);
		break;

	case PR_CHANNELS:
		len = RET_SIZE(pr_ret_t, l_channel_tables->l_max_length);
		call_len = CALL_SIZE(pr_call_t, 0);
		break;

	/* DIA label to string */
	case LTOS:
		len = RET_SIZE(ls_ret_t,
		    L_MAX(l_sensitivity_label_tables->l_max_length,
		    l_clearance_tables->l_max_length));
		call_len = CALL_SIZE(ls_call_t, 0);
		break;

	default:
		break;
	}

	if (debug > 1)
		(void) printf("arg_size=%d, call_len=%d\n", arg_size, call_len);
	if (arg_size < call_len) {
		/* somebody has made a bad call */

		syslog(LOG_ERR, "Bad %d call length %d < %d", opcode, arg_size,
		    call_len);
		reply->ret = SERVERFAULT;
		goto done;
	}

	if (len > sizeof (labeld_data_t)) {
		/* allocate large enough buffer for potential return */

		if (debug > 1)
			(void) printf("allocating %d for reply buffer\n", len);
		if ((reply = (labeld_ret_t *)alloca(len)) == NULL) {
			reply = (labeld_ret_t *)&rval;
			reply->ret = SERVERFAULT;
			if (debug > 1)
				(void) printf("allocation failed, "
				    "SERVERFAULT\n");
			goto done;
		}
	}

	reply->ret = SUCCESS;
	reply->err = 0;

	switch (opcode) {

	/*	Labeld Commands */

	case LABELDNULL:
		null(call, reply, &len, uc);
		break;

	/*	Miscellaneous */

	case BLINSET:
		inset(call, reply, &len, uc);
		break;

	case BSLVALID:
		slvalid(call, reply, &len, uc);
		break;

	case BCLEARVALID:
		clearvalid(call, reply, &len, uc);
		break;

	case LABELINFO:
		info(call, reply, &len, uc);
		break;

	case LABELVERS:
		vers(call, reply, &len, uc);
		break;

	case BLTOCOLOR:
		color(call, reply, &len, uc);
		break;

	/*	Binary to String Label Translation */

	case BSLTOS:
		sltos(call, reply, &len, uc);
		break;

	case BCLEARTOS:
		cleartos(call, reply, &len, uc);
		break;

	/*	String to Binary Label Translation */

	case STOBSL:
		stosl(call, reply, &len, uc);
		break;

	case STOBCLEAR:
		stoclear(call, reply, &len, uc);
		break;

	/*
	 *	Dimming List Routines
	 *	Contract private for label builders
	 */

	case BSLCVT:
		slcvt(call, reply, &len, uc);
		break;

	case BCLEARCVT:
		clearcvt(call, reply, &len, uc);
		break;

	case LABELFIELDS:
		fields(call, reply, &len, uc);
		break;

	case UDEFS:
		udefs(call, reply, &len, uc);
		break;

	case SETFLABEL:
		setflbl(call, reply, &len, uc);
		break;

	/*
	 *	New TS10 public interfaces
	 */

	/* DIA printer banner page labels */
	case PR_TOP:
	case PR_LABEL:
	case PR_CAVEATS:
	case PR_CHANNELS:
		prtos(call, reply, &len, uc);
		break;

	/* DIA label to string */
	case LTOS:
		ltos(call, reply, &len, uc);
		break;

	/* DIA string to label */
	case STOL:
		stol(call, reply, &len, uc);
		break;

	default:
		reply->ret = NOTFOUND;
		break;

	}  /* switch (opcode) */

done:
	ucred_free((ucred_t *)uc);
	uc = NULL;

	if (debug > 1)
		(void) printf("door_return(reply=%x, len=%d, ret=%d, err=%d)\n",
		    (int)reply, len, reply->ret, reply->err);

	if (door_return((char *)reply, len, NULL, 0) < 0) {
		if (debug > 1) {
			(void) printf("door_return error func = %d", opcode);
			perror("");
		}

		syslog(LOG_ERR, "door_return error func = %d: %m", opcode);
		thr_exit(NULL);
	}
}

int
main(int argc, char *argv[])
{
	int	did;	/* door id */
	int	opt;
	int	fd;	/* door file system descriptor */
	char	door_name[MAXPATHLEN];
	int	omask;

	(void) setlocale(LC_ALL, "");
	openlog(argv[0], LOG_PID | LOG_CONS | LOG_NOWAIT, LOG_DAEMON);
	/* Call NULL proc to see if server already running. */
	if (server_running()) {
		(void) fprintf(stderr, "Label server already running."
		    "%s exiting.\n", argv[0]);
		syslog(LOG_WARNING, "Label server already running. Exiting.");
		exit(1);
	}
	label_file[0] = '\0';

	while ((opt = getopt(argc, argv, "c:d:f:t:")) != EOF) {
		switch (opt) {

		case 'c':	/* set max classification */
			Max_class = atoi(optarg);
			break;

		case 'd':	/* set debug level */
			debug = atoi(optarg);
			break;

		case 'f':
			(void) strncpy(label_file, optarg, MAXPATHLEN);
			break;

		case 't':	/* set max threads */
			max_threads = atoi(optarg);
			break;

		default:
			usage(argv[0]);
		}
	}
	if (debug == 0) {
		switch (fork1()) {

		case 0:		/* child */
			detach_tty();
			break;

		case -1:	/* error */
			perror("fork1");
			syslog(LOG_ERR, "fork1 %m");
			exit(1);
			break;

		default:	/* parent */
			exit(0);
		}
	}
	if (debug > 0) {
		(void) printf("labeld daemon start\n");
		syslog(LOG_INFO, "label daemon start");
	}

	/*
	 * While not ideal, it's in the right order for the door creation
	 * and causes less privilege later on.
	 */
	if (l_encodings_initialized() != TRUE) {
		exit(1);
	}

	/* Create label server thread pool */
	(void) door_server_create(server_t_create);
	if (thr_keycreate(&server_key, server_t_destroy) != 0) {
		if (debug)
			perror("thr_keycreate");
		else
			syslog(LOG_ERR, "thr_keycreate %m");
		exit(1);
	}
	if ((did = door_create(&labeld, COOKIE, DOOR_UNREF)) < 0) {
		if (debug)
			perror("door_create");
		else
			syslog(LOG_ERR, "door_create %m");
		exit(1);
	}

	/* Export the door name */
	(void) snprintf(door_name, MAXPATHLEN, "%s%s", DOOR_PATH, DOOR_NAME);
	omask = umask(0);
	fd = open(door_name, O_EXCL | O_CREAT, 0644);
	(void) umask(omask);
	if (fd == -1 && errno != EEXIST) {
		if (debug) {
			char msg[MAXPATHLEN +
			    sizeof ("open/create label door ")];
				(void) snprintf(msg, sizeof (msg),
			    "open/create label door %s", door_name);
			perror(msg);
		} else {
			syslog(LOG_ERR, "open/create label door %s, %m",
			    door_name);
		}
		exit(1);
	}
	(void) close(fd);
	(void) fdetach(door_name);
	if (fattach(did, door_name) < 0) {
		if (debug) {
			char msg[MAXPATHLEN + sizeof ("door fattach ")];

				(void) snprintf(msg, sizeof (msg),
				    "door fattach %s", door_name);
				perror(msg);
		} else {
			syslog(LOG_ERR, "door fattach %s, %m", door_name);
		}
		exit(1);
	}
	if (debug > 1)
		(void) printf("label server %s started\n", door_name);

	for (;;) {	/* let the server do the work */
		(void) pause();
	}
/*LINTED*/
}
