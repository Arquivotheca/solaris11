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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2008 Denis Cheng
 */

#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "filebench.h"
#include "procflow.h"
#include "flowop.h"
#include "ipc.h"

/* pid and procflow pointer for this process */
pid_t my_pid;
procflow_t *my_procflow = NULL;

static procflow_t *procflow_define_common(procflow_t **list, char *name,
    procflow_t *inherit, int instance);
static void procflow_sleep(procflow_t *procflow, int wait_cnt);

#ifdef USE_PROCESS_MODEL

static enum create_n_wait {
	CNW_DONE,
	CNW_ERROR
} cnw_wait;

#endif	/* USE_PROCESS_MODEL */


/*
 * Procflows are filebench entities which manage processes. Each
 * worker procflow spawns a separate filebench process, with attributes
 * inherited from a FLOW_MASTER procflow created during f model language
 * parsing. This section contains routines to define, create, control,
 * and delete procflows.
 *
 * Each process defined in the f model creates a FLOW_MASTER
 * procflow which encapsulates the defined attributes, and threads of
 * the f process, including the number of instances to create. At
 * runtime, a worker procflow instance with an associated filebench
 * process is created, which runs until told to quite by the original
 * filebench process or is specifically deleted.
 */


/*
 * Prints a summary of the syntax for setting procflow parameters.
 */
void
procflow_usage(void)
{
	(void) fprintf(stderr,
	    "define process name=<name>[,instances=<count>]\n");
	(void) fprintf(stderr, "{\n");
	(void) fprintf(stderr, "  thread ...\n");
	(void) fprintf(stderr, "  thread ...\n");
	(void) fprintf(stderr, "  thread ...\n");
	(void) fprintf(stderr, "}\n");
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "\n");
}

/*
 * If filebench has been compiled to support multiple processes
 * (USE_PROCESS_MODEL defined), this routine forks a child
 * process and uses either system() or exec() to start up a new
 * instance of filebench, passing it the procflow name, instance
 * number and shared memory region address.
 * If USE_PROCESS_MODEL is NOT defined, then the routine
 * just creates a child thread which begins executing
 * threadflow_init() for the specified procflow.
 */
static int
procflow_createproc(procflow_t *procflow)
{
	char instance[128];
	char shmaddr[128];
	char procname[128];
	pid_t pid;

#ifdef USE_PROCESS_MODEL

	(void) snprintf(instance, sizeof (instance), "%d",
	    procflow->pf_instance);
	(void) snprintf(procname, sizeof (procname), "%s", procflow->pf_name);
#if defined(_LP64) || (__WORDSIZE == 64)
	(void) snprintf(shmaddr, sizeof (shmaddr), "%llx", filebench_shm);
#else
	(void) snprintf(shmaddr, sizeof (shmaddr), "%x", filebench_shm);
#endif
	filebench_log(LOG_DEBUG_IMPL, "creating process %s",
	    procflow->pf_name);

	procflow->pf_running = 0;

#ifdef HAVE_FORK1
	if ((pid = fork1()) < 0) {
		filebench_log(LOG_ERROR,
		    "procflow_createproc fork failed: %s",
		    strerror(errno));
		return (-1);
	}
#else
	if ((pid = fork()) < 0) {
		filebench_log(LOG_ERROR,
		    "procflow_createproc fork failed: %s",
		    strerror(errno));
		return (-1);
	}
#endif /* HAVE_FORK1 */

	/* if child, start up new copy of filebench */
	if (pid == 0) {
#ifdef USE_SYSTEM
		char syscmd[1024];
#endif

		(void) sigignore(SIGINT);
		filebench_log(LOG_DEBUG_SCRIPT,
		    "Starting %s-%d", procflow->pf_name,
		    procflow->pf_instance);
		/* Child */

#ifdef USE_SYSTEM
		(void) snprintf(syscmd, sizeof (syscmd), "%s -a %s -i %s -s %s",
		    execname,
		    procname,
		    instance,
		    shmaddr);
		if (system(syscmd) < 0) {
			filebench_log(LOG_ERROR,
			    "procflow exec proc failed: %s",
			    strerror(errno));
			filebench_shutdown(1);
		}

#else
		if (execlp(execname, procname, "-a", procname, "-i",
		    instance, "-s", shmaddr, "-m", shmpath, NULL) < 0) {
			filebench_log(LOG_ERROR,
			    "procflow exec proc failed: %s",
			    strerror(errno));
			filebench_shutdown(1);
		}
#endif
		exit(1);
	} else {
		/* if parent, save pid and return */
		procflow->pf_pid = pid;
	}
#else
	procflow->pf_running = 1;
	if (pthread_create(&procflow->pf_tid, NULL,
	    (void *(*)(void*))threadflow_init, procflow) != 0) {
		filebench_log(LOG_ERROR, "proc-thread create failed");
		procflow->pf_running = 0;
	}
#endif
	filebench_log(LOG_DEBUG_IMPL, "procflow_createproc created pid %d",
	    pid);

	return (0);
}

/*
 * Find a procflow of name "name" and instance "instance" on the
 * master procflow list, filebench_shm->shm_proclist. Locks the list
 * and scans through it searching for a procflow with matching
 * name and instance number. If found returns a pointer to the
 * procflow, otherwise returns NULL.
 */
static procflow_t *
procflow_find(char *name, int instance)
{
	procflow_t *procflow = filebench_shm->shm_proclist;

	filebench_log(LOG_DEBUG_IMPL, "Find: (%s-%d) proclist = %zx",
	    name, instance, procflow);

	(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);

	while (procflow) {
		filebench_log(LOG_DEBUG_IMPL, "Find: (%s-%d) == (%s-%d)",
		    name, instance,
		    procflow->pf_name,
		    procflow->pf_instance);
		if ((strcmp(name, procflow->pf_name) == 0) &&
		    (instance == procflow->pf_instance)) {

			(void) ipc_mutex_unlock(
			    &filebench_shm->shm_procflow_lock);

			return (procflow);
		}
		procflow = procflow->pf_next;
	}

	(void) ipc_mutex_unlock(&filebench_shm->shm_procflow_lock);

	return (NULL);
}

static int
procflow_create_all_procs(void)
{
	procflow_t *procflow = filebench_shm->shm_proclist;
	int	ret = 0;

	while (procflow) {
		int i, instances;

		instances = (int)avd_get_int(procflow->pf_instances);
		filebench_log(LOG_INFO, "Starting %d %s instances",
		    instances, procflow->pf_name);

		/* Create instances of procflow */
		for (i = 0; (i < instances) && (ret == 0); i++) {
			procflow_t *newproc;

			/* Create processes */
			newproc =
			    procflow_define_common(&filebench_shm->shm_proclist,
			    procflow->pf_name, procflow, i + 1);
			if (newproc == NULL)
				ret = -1;
			else
				ret = procflow_createproc(newproc);
		}

		if (ret != 0)
			break;

		procflow = procflow->pf_next;
	}

	return (ret);
}

#ifdef USE_PROCESS_MODEL
/*
 * Used to start up threads on a child process, when filebench is
 * compiled to support multiple processes. Uses the name string
 * and instance number passed to the child to find the previously
 * created procflow entity. Then uses nice() to reduce the
 * process' priority by at least 10. A call is then made to
 * threadflow_init() which creates and runs the process' threads
 * and flowops to completion. When threadflow_init() returns,
 * a call to exit() terminates the child process.
 */
int
procflow_exec(char *name, int instance)
{
	procflow_t *procflow;
	int proc_nice;
#ifdef HAVE_SETRLIMIT
	struct rlimit rlp;
#endif
	int ret;

	filebench_log(LOG_DEBUG_IMPL,
	    "procflow_execproc %s-%d",
	    name, instance);

	if ((procflow = procflow_find(name, instance)) == NULL) {
		filebench_log(LOG_ERROR,
		    "procflow_exec could not find %s-%d",
		    name, instance);
		return (-1);
	}

	/* set the slave process' procflow pointer */
	my_procflow = procflow;

	/* set its pid from value stored by main() */
	procflow->pf_pid = my_pid;

	filebench_log(LOG_DEBUG_IMPL,
	    "Started up %s pid %d", procflow->pf_name, my_pid);

	filebench_log(LOG_DEBUG_IMPL,
	    "nice = %llx", procflow->pf_nice);

	proc_nice = avd_get_int(procflow->pf_nice);
	filebench_log(LOG_DEBUG_IMPL, "Setting pri of %s-%d to %d",
	    name, instance, nice(proc_nice + 10));

	procflow->pf_running = 1;

#ifdef HAVE_SETRLIMIT
	/* Get resource limits */
	(void) getrlimit(RLIMIT_NOFILE, &rlp);
	filebench_log(LOG_DEBUG_SCRIPT, "%d file descriptors", rlp.rlim_cur);
#endif

	if ((ret = threadflow_init(procflow)) != FILEBENCH_OK) {
		if (ret < 0) {
			filebench_log(LOG_ERROR,
			    "Failed to start threads for %s pid %d",
			    procflow->pf_name, my_pid);
		}
	} else {
		filebench_log(LOG_DEBUG_IMPL,
		    "procflow_createproc exiting...");
	}

	(void) ipc_mutex_lock(&filebench_shm->shm_procs_running_lock);
	filebench_shm->shm_procs_running --;
	(void) ipc_mutex_unlock(&filebench_shm->shm_procs_running_lock);
	procflow->pf_running = 0;

	return (ret);
}


/*
 * A special thread from which worker (child) processes are created, and
 * which then waits for worker processes to die. If they die unexpectedly,
 * that is not a simple exit(0), then report an error and terminate the
 * run.
 */
/* ARGSUSED */
static void *
procflow_createnwait(void *nothing)
{
	(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);

	if (procflow_create_all_procs() == 0)
		cnw_wait = CNW_DONE;
	else
		cnw_wait = CNW_ERROR;

	if (pthread_cond_signal(&filebench_shm->shm_procflow_procs_cv) != 0)
		exit(1);

	(void) ipc_mutex_unlock(&filebench_shm->shm_procflow_lock);

	/* CONSTCOND */
	while (1) {
		siginfo_t status;

		/* wait for any child process to exit */
		if (waitid(P_ALL, 0, &status, WEXITED) != 0)
			pthread_exit(0);

		(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);
		/* if normal shutdown in progress, just quit */
		if (filebench_shm->shm_f_abort) {
			(void) ipc_mutex_unlock(
			    &filebench_shm->shm_procflow_lock);
			pthread_exit(0);
		}

		/* if nothing running, exit */
		if (filebench_shm->shm_procs_running == 0) {
			filebench_shm->shm_f_abort = FILEBENCH_ABORT_RSRC;
			(void) ipc_mutex_unlock(
			    &filebench_shm->shm_procflow_lock);
			pthread_exit(0);
		}
		(void) ipc_mutex_unlock(&filebench_shm->shm_procflow_lock);

		if (status.si_code == CLD_EXITED) {
			/* A process called exit(); check returned status */
			if (status.si_status != 0) {
				filebench_log(LOG_ERROR,
				    "Unexpected Process termination; exiting",
				    status.si_status);
				filebench_shutdown(1);
			}
		} else {
			/* A process quit because of some fatal error */
			filebench_log(LOG_ERROR,
			    "Unexpected Process termination Code %d, Errno %d",
			    status.si_code, status.si_errno);
			filebench_shutdown(1);
		}

	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Cancel all threads within a processes, as well as the process itself.
 * Called by ^c or by sig_kill
 */
/* ARGSUSED */
static void
procflow_cancel(int arg1)
{
	filebench_log(LOG_DEBUG_IMPL, "Process signal handler on pid %",
	    my_procflow->pf_pid);

	procflow_sleep(my_procflow, SHUTDOWN_WAIT_SECONDS);

	threadflow_delete_all(&my_procflow->pf_threads);

	/* quit the main procflow thread and hence the process */
	exit(0);
}

#endif	/* USE_PROCESS_MODEL */

/*
 * Iterates through proclist, the master list of procflows,
 * creating the number of instances of each procflow specified
 * by its pf_instance attribute. Returns 0 on success, or -1
 * times the number of procflow instances that were not
 * successfully created.
 */
int
procflow_init(void)
{
	procflow_t *procflow = filebench_shm->shm_proclist;
	pthread_t tid;
	int ret = 0;

	if (procflow == NULL) {
		filebench_log(LOG_ERROR, "Workload has no processes");
		return (FILEBENCH_ERROR);
	}

	filebench_log(LOG_DEBUG_IMPL,
	    "procflow_init %s, %llu",
	    procflow->pf_name,
	    (u_longlong_t)avd_get_int(procflow->pf_instances));

#ifdef USE_PROCESS_MODEL
	if ((pthread_create(&tid, NULL, procflow_createnwait, NULL)) != 0)
		return (ret);

	(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);

	(void) signal(SIGUSR1, procflow_cancel);

	if ((ret = pthread_cond_wait(&filebench_shm->shm_procflow_procs_cv,
	    &filebench_shm->shm_procflow_lock)) != 0)
		return (ret);

	if (cnw_wait == CNW_ERROR)
		ret = -1;

#else /* USE_PROCESS_MODEL */
	(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);

	ret = procflow_create_all_procs();
#endif /* USE_PROCESS_MODEL */

	(void) ipc_mutex_unlock(&filebench_shm->shm_procflow_lock);

	return (ret);
}

#ifdef USE_PROCESS_MODEL
/*
 * Waits for child processes to finish and returns their exit
 * status. Used by procflow_delete() when the process model is
 * enabled to wait for a deleted process to exit.
 */
static void
procflow_wait(pid_t pid)
{
	pid_t wpid;
	int stat;

	(void) waitpid(pid, &stat, 0);
	while ((wpid = waitpid(getpid() * -1, &stat, WNOHANG)) > 0)
		filebench_log(LOG_DEBUG_IMPL, "Waited for pid %d", (int)wpid);
}
#endif

/*
 * Common routine to sleep for wait_cnt seconds or for pf_running to
 * go false. Checks once a second to see if pf_running has gone false.
 */
static void
procflow_sleep(procflow_t *procflow, int wait_cnt)
{
	while (procflow->pf_running & wait_cnt) {
		(void) sleep(1);
		wait_cnt--;
	}
}

/*
 * Deletes the designated procflow. Finally it frees the
 * procflow entity. filebench_shm->shm_procflow_lock must be held on entry.
 *
 * If the designated procflow is not found on the list it returns -1 and
 * the procflow is not deleted. Otherwise it returns 0.
 */
static int
procflow_cleanup(procflow_t *procflow)
{
	procflow_t *entry;

	filebench_log(LOG_DEBUG_SCRIPT,
	    "Deleted proc: (%s-%d) pid %d",
	    procflow->pf_name,
	    procflow->pf_instance,
	    procflow->pf_pid);

	procflow->pf_running = 0;

	/* remove entry from proclist */
	entry = filebench_shm->shm_proclist;

	/* unlink procflow entity from proclist */
	if (entry == procflow) {
		/* at head of list */
		filebench_shm->shm_proclist = procflow->pf_next;
	} else {
		/* search list for procflow */
		while (entry && entry->pf_next != procflow)
			entry = entry->pf_next;

		/* if entity found, unlink it */
		if (entry == NULL)
			return (-1);
		else
			entry->pf_next = procflow->pf_next;
	}

	/* free up the procflow entity */
	ipc_free(FILEBENCH_PROCFLOW, (char *)procflow);
	return (0);
}


/*
 * Waits till all threadflows are started, or a timeout occurs.
 * Checks through the list of procflows, waiting up to 30
 * seconds for each one to set its pf_running flag to 1. If not
 * set after 30 seconds, continues on to the next procflow
 * anyway after logging the fact. Once pf_running is set
 * to 1 for a given procflow or the timeout is reached,
 * threadflow_allstarted() is called to start the threads.
 * Returns 0 (OK), unless filebench_shm->shm_f_abort is signaled,
 * in which case it returns -1.
 */
int
procflow_allstarted()
{
	procflow_t *procflow = filebench_shm->shm_proclist;
	int running_procs = 0;
	int ret = 0;

	(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);

	(void) sleep(1);

	while (procflow) {
		int waits;

		if (procflow->pf_instance &&
		    (procflow->pf_instance == FLOW_MASTER)) {
			procflow = procflow->pf_next;
			continue;
		}

		waits = 10;
		while (waits && procflow->pf_running == 0) {
			(void) ipc_mutex_unlock(
			    &filebench_shm->shm_procflow_lock);
			if (filebench_shm->shm_f_abort == 1)
				return (-1);

			if (waits < 3)
				filebench_log(LOG_INFO,
				    "Waiting for process %s-%d %d",
				    procflow->pf_name,
				    procflow->pf_instance,
				    procflow->pf_pid);

			(void) sleep(3);
			waits--;
			(void) ipc_mutex_lock(
			    &filebench_shm->shm_procflow_lock);
		}

		if (waits == 0)
			filebench_log(LOG_INFO,
			    "Failed to start process %s-%d",
			    procflow->pf_name,
			    procflow->pf_instance);

		running_procs++;
		threadflow_allstarted(procflow->pf_pid, procflow->pf_threads);

		procflow = procflow->pf_next;
	}
	(void) ipc_mutex_lock(&filebench_shm->shm_procs_running_lock);
	filebench_shm->shm_procs_running = running_procs;
	(void) ipc_mutex_unlock(&filebench_shm->shm_procs_running_lock);

	(void) ipc_mutex_unlock(&filebench_shm->shm_procflow_lock);


	return (ret);
}


/*
 * Sets the f_abort flag and clears the running count to stop
 * all the flowop execution threads from running. Iterates
 * through the procflow list and deletes all procflows except
 * for the FLOW_MASTER procflow. Resets the f_abort flag when
 * finished.
 *
 */
void
procflow_shutdown(void)
{
	procflow_t *procflow, *next_procflow;
	int wait_cnt = SHUTDOWN_WAIT_SECONDS;

	(void) ipc_mutex_lock(&filebench_shm->shm_procs_running_lock);
	if (filebench_shm->shm_procs_running <= 0) {
		/* No processes running, so no need to do anything */
		(void) ipc_mutex_unlock(&filebench_shm->shm_procs_running_lock);
		return;
	}
	(void) ipc_mutex_unlock(&filebench_shm->shm_procs_running_lock);

	(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);
	if (filebench_shm->shm_f_abort == FILEBENCH_ABORT_FINI) {
		(void) ipc_mutex_unlock(
		    &filebench_shm->shm_procflow_lock);
		return;
	}

	procflow = filebench_shm->shm_proclist;
	if (filebench_shm->shm_f_abort == FILEBENCH_OK)
		filebench_shm->shm_f_abort = FILEBENCH_ABORT_DONE;

	while (procflow) {
		if (procflow->pf_instance &&
		    (procflow->pf_instance == FLOW_MASTER)) {
			procflow = procflow->pf_next;
			continue;
		}
		filebench_log(LOG_DEBUG_IMPL, "Deleting process %s-%d %d",
		    procflow->pf_name,
		    procflow->pf_instance,
		    procflow->pf_pid);

		next_procflow = procflow->pf_next;

		/*
		 * Signalling the process with SIGUSR1 will result in it
		 * gracefully shutting down and exiting
		 */
		procflow_sleep(procflow, wait_cnt);
		if (procflow->pf_running) {
#ifdef USE_PROCESS_MODEL
			pid_t pid;

			pid = procflow->pf_pid;
#ifdef HAVE_SIGSEND
			(void) sigsend(P_PID, pid, SIGUSR1);
#else
			(void) kill(pid, SIGUSR1);
#endif
			procflow_wait(pid);

#else /* USE_PROCESS_MODEL */
			threadflow_delete_all(&procflow->pf_threads);
#endif /* USE_PROCESS_MODEL */
		}
		(void) procflow_cleanup(procflow);
		procflow = next_procflow;
		if (wait_cnt > 0)
			wait_cnt--;
	}

	filebench_shm->shm_f_abort = FILEBENCH_ABORT_FINI;
	(void) ipc_mutex_unlock(&filebench_shm->shm_procflow_lock);

	/* indicate all processes are stopped, even if some are "stuck" */
	(void) ipc_mutex_lock(&filebench_shm->shm_procs_running_lock);
	filebench_shm->shm_procs_running = 0;
	(void) ipc_mutex_unlock(&filebench_shm->shm_procs_running_lock);
}


/*
 * Create an in-memory process object. Allocates a procflow
 * entity, initialized from the "inherit" procflow if supplied.
 * The name and instance number are set from the supplied name
 * and instance number and the procflow is added to the head of
 * the master procflow list. Returns pointer to the allocated
 * procflow, or NULL if a name isn't supplied or the procflow
 * entity cannot be allocated.
 *
 * The calling routine must hold the filebench_shm->shm_procflow_lock.
 */
static procflow_t *
procflow_define_common(procflow_t **list, char *name,
    procflow_t *inherit, int instance)
{
	procflow_t *procflow;

	if (name == NULL)
		return (NULL);

	procflow = (procflow_t *)ipc_malloc(FILEBENCH_PROCFLOW);

	if (procflow == NULL)
		return (NULL);

	if (inherit)
		(void) memcpy(procflow, inherit, sizeof (procflow_t));
	else
		(void) memset(procflow, 0, sizeof (procflow_t));

	procflow->pf_instance = instance;
	(void) strcpy(procflow->pf_name, name);

	filebench_log(LOG_DEBUG_IMPL, "defining process %s-%d", name, instance);

	/* Add procflow to list, lock is being held already */
	if (*list == NULL) {
		*list = procflow;
		procflow->pf_next = NULL;
	} else {
		procflow->pf_next = *list;
		*list = procflow;
	}
	filebench_log(LOG_DEBUG_IMPL, "process %s-%d proclist %zx",
	    name, instance, filebench_shm->shm_proclist);

	return (procflow);
}

/*
 * Create an in-memory process object as described by the syntax.
 * Acquires the filebench_shm->shm_procflow_lock and calls
 * procflow_define_common() to create and initialize a
 * FLOW_MASTER procflow entity from the optional "inherit"
 * procflow with the given name and configured for "instances"
 * number of worker procflows. Currently only called from
 * parser_proc_define().
 */
procflow_t *
procflow_define(char *name, procflow_t *inherit, avd_t instances)
{
	procflow_t *procflow;

	(void) ipc_mutex_lock(&filebench_shm->shm_procflow_lock);

	procflow = procflow_define_common(&filebench_shm->shm_proclist,
	    name, inherit, FLOW_MASTER);
	procflow->pf_instances = instances;

	(void) ipc_mutex_unlock(&filebench_shm->shm_procflow_lock);

	return (procflow);
}
