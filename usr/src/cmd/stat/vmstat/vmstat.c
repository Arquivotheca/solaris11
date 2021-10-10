/*
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/* from UCB 5.4 5/17/86 */
/* from SunOS 4.1, SID 1.31 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <values.h>
#include <poll.h>
#include <locale.h>

#include "statcommon.h"

char *cmdname = "vmstat";
int caught_cont = 0;

static uint_t timestamp_fmt = NODATE;

static	int	hz;
static	int	pagesize;
static	double	etime;
static	int	lines = 1;
static	int	swflag = 0, pflag = 0;
static	int	suppress_state;
static	long	iter = 0;
static	hrtime_t period_n = 0;
static  struct	snapshot *ss;

struct iodev_filter df;

#define	pgtok(a) ((a) * (pagesize >> 10))
#define	denom(x) ((x) ? (x) : 1)
#define	REPRINT	19

static	void	dovmstats(struct snapshot *old, struct snapshot *new);
static	void	printhdr(int);
static	void	dosum(struct sys_snapshot *ss);
static	void	dointr(struct snapshot *ss);
static	void	usage(void);

struct snapshot *acquire_snapshot(kstat_ctl_t **, int, struct iodev_filter *);


/*
 * Used to prevent needless strcmps.  If -1 we have
 * to do the strcmps  If not -1 we have the proper index.
 */

static int hat_fault_index = -1;
static int as_fault_index = -1;
static int swapin_index = -1;
static int swapout_index = -1;
static int pgswapin_index = -1;
static int pgswapout_index = -1;
static int pgin_index = -1;
static int pgout_index = -1;
static int pgpgin_index = -1;
static int pgpgout_index = -1;
static int pgrec_index = -1;
static int pgfrec_index = -1;
static int maj_fault_index = -1;
static int cow_fault_index = -1;
static int zfod_index = -1;
static int scan_index = -1;
static int rev_index = -1;
static int dfree_index = -1;
static int sysfork_index = -1;
static int sysvfork_index = -1;
static int sysexec_index = -1;
static int pswitch_index = -1;
static int intr_index = -1;
static int trap_index = -1;
static int syscall_index = -1;
static int cpu_ticks_user_index = -1;
static int cpu_ticks_kernel_index = -1;
static int cpu_ticks_idle_index = -1;
static int cpu_ticks_wait_index = -1;
static int execpgin_index = -1;
static int execpgout_index = -1;
static int execfree_index = -1;
static int anonpgin_index = -1;
static int anonpgout_index = -1;
static int anonfree_index = -1;
static int fspgin_index = -1;
static int fspgout_index = -1;
static int fsfree_index = -1;

int
main(int argc, char **argv)
{
	struct snapshot *old = NULL;
	enum snapshot_types types = SNAP_SYSTEM;
	int summary = 0;
	int intr = 0;
	kstat_ctl_t *kc;
	int forever = 0;
	hrtime_t start_n;
	int c;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"		/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	pagesize = sysconf(_SC_PAGESIZE);
	hz = sysconf(_SC_CLK_TCK);

	while ((c = getopt(argc, argv, "ipqsST:")) != EOF)
		switch (c) {
		case 'S':
			swflag = !swflag;
			break;
		case 's':
			summary = 1;
			break;
		case 'i':
			intr = 1;
			break;
		case 'q':
			suppress_state = 1;
			break;
		case 'p':
			pflag++;	/* detailed paging info */
			break;
		case 'T':
			if (optarg) {
				if (*optarg == 'u')
					timestamp_fmt = UDATE;
				else if (*optarg == 'd')
					timestamp_fmt = DDATE;
				else
					usage();
			} else {
				usage();
			}
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	/* consistency with iostat */
	types |= SNAP_CPUS;

	if (intr)
		types |= SNAP_INTERRUPTS;
	if (!intr)
		types |= SNAP_IODEVS;

	/* max to fit in less than 80 characters */
	df.if_max_iodevs = 4;
	df.if_allowed_types = IODEV_DISK;
	df.if_nr_names = 0;
	df.if_names = safe_alloc(df.if_max_iodevs * sizeof (char *));
	(void) memset(df.if_names, 0, df.if_max_iodevs * sizeof (char *));

	while (argc > 0 && !isdigit(argv[0][0]) &&
	    df.if_nr_names < df.if_max_iodevs) {
		df.if_names[df.if_nr_names] = *argv;
		df.if_nr_names++;
		argc--, argv++;
	}

	kc = open_kstat();

	start_n = gethrtime();

	ss = acquire_snapshot(&kc, types, &df);

	/* time, in seconds, since boot */
	etime = ss->s_sys.ss_ticks / hz;

	if (intr) {
		dointr(ss);
		free_snapshot(ss);
		exit(0);
	}
	if (summary) {
		dosum(&ss->s_sys);
		free_snapshot(ss);
		exit(0);
	}

	if (argc > 0) {
		long interval;
		char *endptr;

		errno = 0;
		interval = strtol(argv[0], &endptr, 10);

		if (errno > 0 || *endptr != '\0' || interval <= 0 ||
		    interval > MAXINT)
			usage();
		period_n = (hrtime_t)interval * NANOSEC;
		if (period_n <= 0)
			usage();
		iter = MAXLONG;
		if (argc > 1) {
			iter = strtol(argv[1], NULL, 10);
			if (errno > 0 || *endptr != '\0' || iter <= 0)
				usage();
		} else
			forever = 1;
		if (argc > 2)
			usage();
	}

	(void) sigset(SIGCONT, printhdr);

	dovmstats(old, ss);
	while (forever || --iter > 0) {
		/* (void) poll(NULL, 0, poll_interval); */

		/* Have a kip */
		sleep_until(&start_n, period_n, forever, &caught_cont);

		free_snapshot(old);
		old = ss;
		ss = acquire_snapshot(&kc, types, &df);

		if (!suppress_state)
			snapshot_report_changes(old, ss);

		/* if config changed, show stats from boot */
		if (old && snapshot_has_changed(old, ss) == 1) {
			old->snap_changed = 1;
			free_snapshot(old);
			old = NULL;
		}
		ss->snap_changed = 0;
		dovmstats(old, ss);
	}

	free_snapshot(old);
	free_snapshot(ss);
	free(df.if_names);
	(void) kstat_close(kc);
	return (0);
}

#define	DELTA(v) (new->v - (old ? old->v : 0))
#define	ADJ(n)	((adj <= 0) ? n : (adj >= n) ? 1 : n - adj)
#define	adjprintf(fmt, n, val)	adj -= (n + 1) - printf(fmt, ADJ(n), val)

static int adj;	/* number of excess columns */

/*ARGSUSED*/
static void
show_disk(void *v1, void *v2, void *d)
{
	struct iodev_snapshot *old = (struct iodev_snapshot *)v1;
	struct iodev_snapshot *new = (struct iodev_snapshot *)v2;
	hrtime_t oldtime = new->is_crtime;
	double hr_etime;
	double reads, writes;

	if (new == NULL)
		return;

	if (old)
		oldtime = old->is_snaptime;
	hr_etime = hrtime_delta(oldtime, new->is_snaptime);
	if (hr_etime == 0.0)
		hr_etime = NANOSEC;
	reads = new->is_stats.reads - (old ? old->is_stats.reads : 0);
	writes = new->is_stats.writes - (old ? old->is_stats.writes : 0);
	adjprintf(" %*.0f", 2, (reads + writes) / hr_etime * NANOSEC);
}

static void
dovmstats(struct snapshot *old, struct snapshot *new)
{
	kstat_t *oldsys = NULL;
	kstat_t *newsys = &new->s_sys.ss_agg_sys;
	kstat_t *oldvm = NULL;
	kstat_t *newvm = &new->s_sys.ss_agg_vm;
	double percent_factor;
	ulong_t sys_updates, vm_updates;
	int count;

	adj = 0;

	if (old) {
		oldsys = &old->s_sys.ss_agg_sys;
		oldvm = &old->s_sys.ss_agg_vm;
	}

	etime = cpu_ticks_delta(oldsys, newsys);

	percent_factor = 100.0 / denom(etime);
	/*
	 * If any time has passed, convert etime to seconds per CPU
	 */
	etime = etime >= 1.0 ? (etime / nr_active_cpus()) / hz : 1.0;
	sys_updates = denom(DELTA(s_sys.ss_sysinfo.updates));
	vm_updates = denom(DELTA(s_sys.ss_vminfo.updates));

	if (timestamp_fmt != NODATE) {
		print_timestamp(timestamp_fmt);
		lines--;
	}

	if (--lines <= 0)
		printhdr(0);

	adj = 0;

	if (pflag)  {
		adjprintf(" %*u", 6,
		    pgtok((int)(DELTA(s_sys.ss_vminfo.swap_avail)
		    / vm_updates)));
		adjprintf(" %*u", 5,
		    pgtok((int)(DELTA(s_sys.ss_vminfo.freemem) / vm_updates)));
		adjprintf(" %*.0f", 3, kstat_delta(oldvm, newvm, "pgrec",
		    &pgrec_index) / etime);
		adjprintf(" %*.0f", 3, (kstat_delta(oldvm, newvm, "hat_fault",
		    &hat_fault_index) + kstat_delta(oldvm, newvm, "as_fault",
		    &as_fault_index)) / etime);
		adjprintf(" %*.0f", 3, pgtok(kstat_delta(oldvm, newvm, "dfree",
		    &dfree_index)) / etime);
		adjprintf(" %*ld", 3, pgtok(new->s_sys.ss_deficit));
		adjprintf(" %*.0f", 3, kstat_delta(oldvm, newvm, "scan",
		    &scan_index) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "execpgin",
		    &execpgin_index)) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "execpgout",
		    &execpgout_index)) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "execfree",
		    &execfree_index)) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "anonpgin",
		    &anonpgin_index)) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "anonpgout",
		    &anonpgout_index)) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "anonfree",
		    &anonfree_index)) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "fspgin",
		    &fspgin_index)) / etime);
		adjprintf(" %*.0f", 4,
		    pgtok(kstat_delta(oldvm, newvm, "fspgout",
		    &fspgout_index)) / etime);
		adjprintf(" %*.0f\n", 4,
		    pgtok(kstat_delta(oldvm, newvm, "fsfree",
		    &fsfree_index)) / etime);
		(void) fflush(stdout);
		return;
	}

	adjprintf(" %*lu", 1, DELTA(s_sys.ss_sysinfo.runque) / sys_updates);
	adjprintf(" %*lu", 1, DELTA(s_sys.ss_sysinfo.waiting) / sys_updates);
	adjprintf(" %*lu", 1, DELTA(s_sys.ss_sysinfo.swpque) / sys_updates);
	adjprintf(" %*u", 6, pgtok((int)(DELTA(s_sys.ss_vminfo.swap_avail)
	    / vm_updates)));
	adjprintf(" %*u", 5, pgtok((int)(DELTA(s_sys.ss_vminfo.freemem)
	    / vm_updates)));
	adjprintf(" %*.0f", 3, swflag?
	    kstat_delta(oldvm, newvm, "swapin", &swapin_index) / etime :
	    kstat_delta(oldvm, newvm, "pgrec", &pgrec_index) / etime);
	adjprintf(" %*.0f", 3, swflag?
	    kstat_delta(oldvm, newvm, "swapout", &swapout_index) / etime :
	    (kstat_delta(oldvm, newvm, "hat_fault", &hat_fault_index)
	    + kstat_delta(oldvm, newvm, "as_fault", &as_fault_index))
	    / etime);
	adjprintf(" %*.0f", 2, pgtok(kstat_delta(oldvm, newvm, "pgpgin",
	    &pgin_index)) / etime);
	adjprintf(" %*.0f", 2, pgtok(kstat_delta(oldvm, newvm, "pgpgout",
	    &pgpgout_index)) / etime);
	adjprintf(" %*.0f", 2, pgtok(kstat_delta(oldvm, newvm, "dfree",
	    &dfree_index)) / etime);
	adjprintf(" %*ld", 2, pgtok(new->s_sys.ss_deficit));
	adjprintf(" %*.0f", 2, kstat_delta(oldvm, newvm, "scan",
	    &scan_index) / etime);

	(void) snapshot_walk(SNAP_IODEVS, old, new, show_disk, NULL);

	count = df.if_max_iodevs - new->s_nr_iodevs;
	while (count-- > 0)
		adjprintf(" %*d", 2, 0);

	adjprintf(" %*.0f", 4, kstat_delta(oldsys, newsys, "intr",
	    &intr_index) / etime);
	adjprintf(" %*.0f", 4, kstat_delta(oldsys, newsys, "syscall",
	    &syscall_index) / etime);
	adjprintf(" %*.0f", 4, kstat_delta(oldsys, newsys, "pswitch",
	    &pswitch_index) / etime);
	adjprintf(" %*.0f", 2,
	    kstat_delta(oldsys, newsys, "cpu_ticks_user",
	    &cpu_ticks_user_index) * percent_factor);
	adjprintf(" %*.0f", 2, kstat_delta(oldsys, newsys, "cpu_ticks_kernel",
	    &cpu_ticks_kernel_index) * percent_factor);
	adjprintf(" %*.0f\n", 2, (kstat_delta(oldsys, newsys, "cpu_ticks_idle",
	    &cpu_ticks_idle_index) + kstat_delta(oldsys, newsys,
	    "cpu_ticks_wait", &cpu_ticks_wait_index)) * percent_factor);
	(void) fflush(stdout);
}

/*ARGSUSED*/
static void
print_disk(void *v, void *v2, void *d)
{
	struct iodev_snapshot *iodev = (struct iodev_snapshot *)v2;

	if (iodev == NULL)
		return;

	(void) printf("%c%c ", iodev->is_name[0], iodev->is_name[2]);
}

/* ARGSUSED */
static void
printhdr(int sig)
{
	int i = df.if_max_iodevs - ss->s_nr_iodevs;

	if (sig == SIGCONT)
		caught_cont = 1;

	if (pflag) {
		(void) printf("     memory           page          ");
		(void) printf("executable      anonymous      filesystem \n");
		(void) printf("   swap  free  re  mf  fr  de  sr  ");
		(void) printf("epi  epo  epf  api  apo  apf  fpi  fpo  fpf\n");
		lines = REPRINT;
		return;
	}

	(void) printf(" kthr      memory            page            ");
	(void) printf("disk          faults      cpu\n");

	if (swflag)
		(void) printf(" r b w   swap  free  si  so pi po fr de sr ");
	else
		(void) printf(" r b w   swap  free  re  mf pi po fr de sr ");

	(void) snapshot_walk(SNAP_IODEVS, NULL, ss, print_disk, NULL);

	while (i-- > 0)
		(void) printf("-- ");

	(void) printf("  in   sy   cs us sy id\n");
	lines = REPRINT;
}

static void
sum_out(char const *pretty, kstat_t *ks, char *name, int *index)
{
	kstat_named_t *ksn = stat_data_lookup(ks, name, index);
	if (ksn == NULL) {
		fail(0, "kstat_data_lookup('%s', '%s') failed",
		    ks->ks_name, name);
	}

	(void) printf("%9llu %s\n", ksn->value.ui64, pretty);
}

static void
dosum(struct sys_snapshot *ss)
{
	uint64_t total_faults;
	kstat_named_t *ksn;
	long double nchtotal;
	uint64_t nchhits;

	sum_out("swap ins", &ss->ss_agg_vm, "swapin", &swapin_index);
	sum_out("swap outs", &ss->ss_agg_vm, "swapout", &swapout_index);
	sum_out("pages swapped in", &ss->ss_agg_vm, "pgswapin",
	    &pgswapin_index);
	sum_out("pages swapped out", &ss->ss_agg_vm, "pgswapout",
	    &pgswapout_index);

	ksn = stat_data_lookup(&ss->ss_agg_vm, "hat_fault", &hat_fault_index);
	if (ksn == NULL) {
		fail(0, "kstat_data_lookup('%s', 'hat_fault') failed",
		    ss->ss_agg_vm.ks_name);
	}
	total_faults = ksn->value.ui64;
	ksn = stat_data_lookup(&ss->ss_agg_vm, "as_fault", &as_fault_index);
	if (ksn == NULL) {
		fail(0, "kstat_data_lookup('%s', 'as_fault') failed",
		    ss->ss_agg_vm.ks_name);
	}
	total_faults += ksn->value.ui64;

	(void) printf("%9llu total address trans. faults taken\n",
	    total_faults);

	sum_out("page ins", &ss->ss_agg_vm, "pgin", &pgin_index);
	sum_out("page outs", &ss->ss_agg_vm, "pgout", &pgout_index);
	sum_out("pages paged in", &ss->ss_agg_vm, "pgpgin", &pgpgin_index);
	sum_out("pages paged out", &ss->ss_agg_vm, "pgpgout", &pgpgout_index);
	sum_out("total reclaims", &ss->ss_agg_vm, "pgrec", &pgrec_index);
	sum_out("reclaims from free list", &ss->ss_agg_vm, "pgfrec",
	    &pgfrec_index);
	sum_out("micro (hat) faults", &ss->ss_agg_vm, "hat_fault",
	    &hat_fault_index);
	sum_out("minor (as) faults", &ss->ss_agg_vm, "as_fault",
	    &as_fault_index);
	sum_out("major faults", &ss->ss_agg_vm, "maj_fault", &maj_fault_index);
	sum_out("copy-on-write faults", &ss->ss_agg_vm, "cow_fault",
	    &cow_fault_index);
	sum_out("zero fill page faults", &ss->ss_agg_vm, "zfod", &zfod_index);
	sum_out("pages examined by the clock daemon", &ss->ss_agg_vm, "scan",
	    &scan_index);
	sum_out("revolutions of the clock hand", &ss->ss_agg_vm, "rev",
	    &rev_index);
	sum_out("pages freed by the clock daemon", &ss->ss_agg_vm, "dfree",
	    &dfree_index);
	sum_out("forks", &ss->ss_agg_sys, "sysfork", &sysfork_index);
	sum_out("vforks", &ss->ss_agg_sys, "sysvfork", &sysvfork_index);
	sum_out("execs", &ss->ss_agg_sys, "sysexec", &sysexec_index);
	sum_out("cpu context switches", &ss->ss_agg_sys, "pswitch",
	    &pswitch_index);
	sum_out("device interrupts", &ss->ss_agg_sys, "intr", &intr_index);
	sum_out("traps", &ss->ss_agg_sys, "trap", &trap_index);
	sum_out("system calls", &ss->ss_agg_sys, "syscall", &syscall_index);

	nchtotal = (long double) ss->ss_nc.ncs_hits.value.ui64 +
	    (long double) ss->ss_nc.ncs_misses.value.ui64;
	nchhits = ss->ss_nc.ncs_hits.value.ui64;
	(void) printf("%9.0Lf total name lookups (cache hits %.0Lf%%)\n",
	    nchtotal, nchhits / denom(nchtotal) * 100);
	sum_out("user   cpu", &ss->ss_agg_sys, "cpu_ticks_user",
	    &cpu_ticks_user_index);
	sum_out("system cpu", &ss->ss_agg_sys, "cpu_ticks_kernel",
	    &cpu_ticks_kernel_index);
	sum_out("idle   cpu", &ss->ss_agg_sys, "cpu_ticks_idle",
	    &cpu_ticks_idle_index);
	sum_out("wait   cpu", &ss->ss_agg_sys, "cpu_ticks_wait",
	    &cpu_ticks_wait_index);
}

static void
dointr(struct snapshot *ss)
{
	size_t i;
	ulong_t total = 0;

	(void) printf("interrupt         total     rate\n");
	(void) printf("--------------------------------\n");

	for (i = 0; i < ss->s_nr_intrs; i++) {
		(void) printf("%-12.8s %10lu %8.0f\n",
		    ss->s_intrs[i].is_name, ss->s_intrs[i].is_total,
		    ss->s_intrs[i].is_total / etime);
		total += ss->s_intrs[i].is_total;
	}

	(void) printf("--------------------------------\n");
	(void) printf("Total        %10lu %8.0f\n", total, total / etime);
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: vmstat [-ipqsS] [-T d|u] [disk ...] "
	    "[interval [count]]\n");
	exit(1);
}
