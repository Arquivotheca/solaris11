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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * lldpd - LLDP daemon
 */
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <libdladm.h>
#include <libdllink.h>
#include <priv_utils.h>
#include <signal.h>
#include <stdio.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include "lldp.h"
#include "lldp_impl.h"
#include "dcbx_impl.h"
#include "lldpsnmp_impl.h"

/* used to communicate failure to parent process, which spawned the daemon */
static int	pfds[2];

static int	lldpd_door_fd = -1;

dladm_handle_t	dld_handle = NULL;

/*
 * linked list to capture all the LLDP agents running on this system and a
 * rwlock to synchronize access to lldp agents list.
 */
list_t			lldp_agents;
pthread_rwlock_t	lldp_agents_list_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * event channel handle used by the lldpd daemon to publish events and the
 * same handle is used by the DCB features, PFC and Application, to receive
 * sysevents.
 */
evchan_t	*lldpd_evchp = NULL;

/*
 * lldp system information nvlist and a rwlock to synchronize access to
 * lldp_sysinfo nvlist.
 */
nvlist_t		*lldp_sysinfo;
pthread_rwlock_t	lldp_sysinfo_rwlock = PTHREAD_RWLOCK_INITIALIZER;

boolean_t snmp_enabled = B_FALSE;
boolean_t done_subagent_init = B_FALSE;
boolean_t snmp_shutting_down = B_FALSE;

static void	lldpd_fini(void);
static void	*lldpd_snmp(void *);

static void
lldpd_door_fini(void)
{
	if (lldpd_door_fd == -1)
		return;

	(void) fdetach(LLDPD_DOOR);
	if (door_revoke(lldpd_door_fd) == -1) {
		syslog(LOG_ERR, "failed to revoke access to door %s: %s",
		    LLDPD_DOOR, strerror(errno));
	}
}

static void
lldpd_fini(void)
{
	(void) sysevent_evc_unbind(lldpd_evchp);
	lldpd_door_fini();
	if (dld_handle != NULL) {
		dladm_close(dld_handle);
		dld_handle = NULL;
	}
}

void
i_lldpd_handle_snmp_prop(boolean_t enable_snmp)
{
	pthread_attr_t	attr;

	/* enable/disable SNMP support */
	if (enable_snmp && !snmp_enabled) {
		snmp_shutting_down = B_FALSE;
		(void) pthread_attr_init(&attr);
		(void) pthread_attr_setdetachstate(&attr,
		    PTHREAD_CREATE_DETACHED);
		(void) pthread_create(NULL, &attr, lldpd_snmp, NULL);
		(void) pthread_attr_destroy(&attr);
	} else if (!enable_snmp && snmp_enabled) {
		snmp_shutting_down = B_TRUE;
	}
}

int
i_lldpd_handle_mgmtaddr_prop(char *maddrstr)
{
	lldp_mgmtaddr_t		maddr;
	nvlist_t		*nvl = NULL;
	struct ifaddrs		*ifa = NULL, *ifap;
	struct sockaddr		*addr, *taddr;
	struct sockaddr_in6	sin6, *sin6p;
	struct sockaddr_in	sin, *sinp;
	char			*addrstr, *cp, *lasts;
	int			err;

	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0)
		goto ret;

	/*
	 * Retrieve all the addresses configured on this system. We need this
	 * to determine the datalink_id_t on which the address is configured.
	 * This information is optional and we don't bother if getifaddrs()
	 * fails.
	 */
	(void) getifaddrs(&ifa);
	/*
	 * multi-valued properties are represented as comma separated
	 * values. Use string tokenizer functions to split them.
	 */
	while ((addrstr = strtok_r(maddrstr, ",", &lasts)) != NULL) {
		maddrstr = NULL;
		/* if there is any prefix length provided, ignore it */
		if ((cp = strchr(addrstr, '/')) != NULL)
			*cp = '\0';

		/* check if it's a IPv4 address */
		bzero(&maddr, sizeof (maddr));
		maddr.lm_iftype = LLDP_MGMTADDR_IFTYPE_UNKNOWN;
		if (inet_pton(AF_INET, addrstr, &sin.sin_addr) == 1) {
			addr = (struct sockaddr *)&sin;
			addr->sa_family = AF_INET;
			maddr.lm_subtype = LLDP_MGMTADDR_TYPE_IPV4;
			maddr.lm_addrlen = 4;
			bcopy(&sin.sin_addr.s_addr, maddr.lm_addr,
			    maddr.lm_addrlen);
		} else if (inet_pton(AF_INET6, addrstr, &sin6.sin6_addr) == 1) {
			addr = (struct sockaddr *)&sin6;
			addr->sa_family = AF_INET6;
			maddr.lm_subtype = LLDP_MGMTADDR_TYPE_IPV6;
			maddr.lm_addrlen = 16;
			bcopy(&sin6.sin6_addr.s6_addr, maddr.lm_addr,
			    maddr.lm_addrlen);
		} else {
			err = EINVAL;
			goto ret;
		}

		for (ifap = ifa; ifap != NULL; ifap = ifap->ifa_next) {
			taddr = (struct sockaddr *)(ifap->ifa_addr);
			if (taddr->sa_family != addr->sa_family)
				continue;
			if (addr->sa_family == AF_INET) {
				sinp = (struct sockaddr_in *)(void *)taddr;
				if (sin.sin_addr.s_addr ==
				    sinp->sin_addr.s_addr)
					break;
			} else {
				sin6p = (struct sockaddr_in6 *)(void *)taddr;
				if (IN6_ARE_ADDR_EQUAL(&sin6.sin6_addr,
				    &sin6p->sin6_addr))
					break;
			}
		}
		if (ifap != NULL) {
			datalink_id_t linkid;

			if (dladm_name2info(dld_handle, ifap->ifa_name,
			    &linkid, NULL, NULL, NULL) == DLADM_STATUS_OK) {
				maddr.lm_iftype = LLDP_MGMTADDR_IFTYPE_IFINDEX;
				maddr.lm_ifnumber = linkid;
			}
		}
		err = lldp_add_mgmtaddr2nvlist(&maddr, nvl);
		if (err != 0)
			goto ret;
	}
	err = nvlist_merge(lldp_sysinfo, nvl, 0);
ret:
	nvlist_free(nvl);
	freeifaddrs(ifa);
	return (err);
}

static void
i_lldpd_apply_lldp_config(nvlist_t *lcfg)
{
	nvpair_t	*nvp;
	char		*pname;
	data_type_t	ptype;
	uint64_t	u64;
	boolean_t	bval;
	int		err;

	/* walk through the `lcfg' and apply the properties */
	for (nvp = nvlist_next_nvpair(lcfg, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(lcfg, nvp)) {
		pname = nvpair_name(nvp);
		ptype = nvpair_type(nvp);
		switch (ptype) {
		case DATA_TYPE_UINT64:
			err = nvpair_value_uint64(nvp, &u64);
			break;
		case DATA_TYPE_BOOLEAN_VALUE:
			err = nvpair_value_boolean_value(nvp, &bval);
			break;
		default:
			continue;
		}
		if (err != 0) {
			syslog(LOG_WARNING, "failed reading property '%s'",
			    pname);
			continue;
		}
		if (strcmp(pname, "msgFastTx") == 0 &&
		    u64 >= 1 && u64 <= 3600) {
			assert(ptype == DATA_TYPE_UINT64);
			lldp_msgFastTx = u64;
		} else if (strcmp(pname, "msgTxInterval") == 0 &&
		    u64 >= 1 && u64 <= 3600) {
			assert(ptype == DATA_TYPE_UINT64);
			lldp_msgTxInterval = u64;
		} else if (strcmp(pname, "reinitDelay") == 0 &&
		    u64 >= 1 && u64 <= 100) {
			assert(ptype == DATA_TYPE_UINT64);
			lldp_reinitDelay = u64;
		} else if (strcmp(pname, "msgTxHold") == 0 &&
		    u64 >= 1 && u64 <= 100) {
			assert(ptype == DATA_TYPE_UINT64);
			lldp_msgTxHold = u64;
		} else if (strcmp(pname, "txFastInit") == 0 &&
		    u64 >= 1 && u64 <= 8) {
			assert(ptype == DATA_TYPE_UINT64);
			lldp_txFastInit = u64;
		} else if (strcmp(pname, "txCreditMax") == 0 &&
		    u64 >= 1 && u64 <= 10) {
			assert(ptype == DATA_TYPE_UINT64);
			lldp_txCreditMax = u64;
		} else if (strcmp(pname, "snmp") == 0) {
			assert(ptype == DATA_TYPE_BOOLEAN_VALUE);
			i_lldpd_handle_snmp_prop(bval);
		} else {
			syslog(LOG_WARNING, "Error applying LLDP property %s",
			    pname);
		}
	}
}

static void
i_lldpd_apply_globaltlv_config(nvlist_t *cfg)
{
	lldp_syscapab_t	sc;
	nvlist_t	*nvl;
	nvpair_t	*nvp;
	char		*tlvname, *strval = NULL;
	int64_t		i64;
	int		err;

	/* walk through the `cfg' and apply the global TLV properties */
	for (nvp = nvlist_next_nvpair(cfg, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(cfg, nvp)) {
		err = 0;
		tlvname = nvpair_name(nvp);
		if (nvpair_value_nvlist(nvp, &nvl) != 0)
			continue;
		if (strcmp(tlvname, "mgmtaddr") == 0) {
			if (nvlist_lookup_string(nvl, "ipaddr", &strval) == 0)
				err = i_lldpd_handle_mgmtaddr_prop(strval);
		} else if (strcmp(tlvname, "syscapab") == 0) {
			sc.ls_sup_syscapab = (LLDP_SYSCAPAB_ROUTER |
			    LLDP_SYSCAPAB_MACBRIDGE |
			    LLDP_SYSCAPAB_STATION_ONLY);
			sc.ls_enab_syscapab = 0;
			if (nvlist_lookup_int64(nvl, "supported", &i64) == 0)
				sc.ls_sup_syscapab = (uint16_t)i64;
			if (nvlist_lookup_int64(nvl, "enabled", &i64) == 0)
				sc.ls_enab_syscapab = (uint16_t)i64;
			err = lldp_add_syscapab2nvlist(&sc, lldp_sysinfo);
		}
		if (err != 0) {
			syslog(LOG_WARNING, "failed reapplying tlv '%s'",
			    tlvname);
		}
	}
}

static void
i_lldpd_apply_agent_config(datalink_id_t linkid, const char *laname,
    nvlist_t *acfg)
{
	lldp_agent_t	*lap;
	int64_t		val;
	int		err;

	if (nvlist_lookup_int64(acfg, "mode", &val) != 0)
		return;
	if ((lap = lldp_agent_create(linkid, &err)) == NULL) {
		syslog(LOG_ERR, "Failed to create lldp agent on port '%s': %s",
		    laname, strerror(err));
		return;
	}
	i_lldpd_set_mode(lap, (lldp_admin_status_t)val);
	if (nvlist_lookup_int64(acfg, LLDP_BASICTLV_GRPNAME, &val) == 0) {
		(void) i_lldpd_set_tlv(lap, LLDP_PROPTYPE_BASICTLV,
		    (uint32_t)val, NULL, LLDP_OPT_ACTIVE);
	}
	if (nvlist_lookup_int64(acfg, LLDP_8021TLV_GRPNAME, &val) == 0) {
		(void) i_lldpd_set_tlv(lap, LLDP_PROPTYPE_8021TLV,
		    (uint32_t)val, NULL, LLDP_OPT_ACTIVE);
	}
	if (nvlist_lookup_int64(acfg, LLDP_8023TLV_GRPNAME, &val) == 0) {
		(void) i_lldpd_set_tlv(lap, LLDP_PROPTYPE_8023TLV,
		    (uint32_t)val, NULL, LLDP_OPT_ACTIVE);
	}
	if (nvlist_lookup_int64(acfg, LLDP_VIRTTLV_GRPNAME, &val) == 0) {
		(void) i_lldpd_set_tlv(lap, LLDP_PROPTYPE_VIRTTLV,
		    (uint32_t)val, NULL, LLDP_OPT_ACTIVE);
	}
}

static int
lldpd_init_agents()
{
	datalink_id_t	linkid;
	dladm_status_t	dlstatus;
	nvlist_t	*allcfg = NULL, *cfg = NULL, *acfg = NULL;
	nvpair_t	*nvp;
	char		dlerr[DLADM_STRSIZE], *laname;
	int		err;

	if ((err = nvlist_alloc(&allcfg,  NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	if ((err = lldpd_walk_db(allcfg, NULL)) != 0)
		goto ret;

	/* first apply LLDP protocol properties */
	if (nvlist_lookup_nvlist(allcfg, "lldp", &cfg) == 0) {
		i_lldpd_apply_lldp_config(cfg);
		(void) nvlist_remove(allcfg, "lldp", DATA_TYPE_NVLIST);
	}

	/* then apply any global TLV properties, if any */
	cfg = NULL;
	if (nvlist_lookup_nvlist(allcfg, "globaltlv", &cfg) == 0) {
		i_lldpd_apply_globaltlv_config(cfg);
		(void) nvlist_remove(allcfg, "globaltlv", DATA_TYPE_NVLIST);
	}

	/*
	 * `cfg' also contains all the information regarding
	 * per-agent properties.
	 */
	if (nvlist_lookup_nvlist(allcfg, "agent", &cfg) != 0)
		goto ret;
	for (nvp = nvlist_next_nvpair(cfg, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(cfg, nvp)) {
		laname = nvpair_name(nvp);
		(void) nvpair_value_nvlist(nvp, &acfg);
		if ((dlstatus = dladm_name2info(dld_handle, laname,
		    &linkid, NULL, NULL, NULL)) == DLADM_STATUS_OK) {
			i_lldpd_apply_agent_config(linkid, laname, acfg);
		} else {
			syslog(LOG_ERR, "Unable to reinstantiate properties "
			    "on '%s' : %s", laname,
			    dladm_status2str(dlstatus, dlerr));
		}
	}
ret:
	nvlist_free(allcfg);
	return (0);
}

static void
lldpd_refresh(void)
{
	nvlist_t	*cfg = NULL, *lldp_cfg = NULL;
	int		err;

	if ((err = nvlist_alloc(&cfg, NV_UNIQUE_NAME, 0)) == 0 &&
	    (err = lldpd_walk_db(cfg, "lldp")) == 0 &&
	    (err = nvlist_lookup_nvlist(cfg, "lldp", &lldp_cfg)) == 0) {
		i_lldpd_apply_lldp_config(lldp_cfg);
	} else {
		syslog(LOG_WARNING, "failed refreshing lldpd %s'",
		    strerror(err));
	}
	nvlist_free(cfg);
}

/* ARGSUSED */
static void *
sighandler(void *arg)
{
	sigset_t	sigset;
	int		sig;

	(void) sigfillset(&sigset);
	for (;;) {
		sig = sigwait(&sigset);
		switch (sig) {
		case SIGHUP:
			lldpd_refresh();
			break;
		default:
			/* clean up before exiting */
			(void) close(pfds[1]);
			lldpd_fini();
			exit(EXIT_FAILURE);
		}
	}
	/* NOTREACHED */
	return (NULL);
}

static int
lldpd_init_signal_handling(void)
{
	pthread_attr_t	attr;
	int		err;
	sigset_t	new;

	(void) sigfillset(&new);
	(void) pthread_sigmask(SIG_BLOCK, &new, NULL);
	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	err = pthread_create(NULL, &attr, sighandler, NULL);
	(void) pthread_attr_destroy(&attr);
	return (err);
}

static int
lldpd_door_init(void)
{
	int fd;
	int err = 0;

	/* create the door file for lldpd */
	if ((fd = open(LLDPD_DOOR, O_CREAT|O_RDONLY, 0644)) == -1) {
		err = errno;
		syslog(LOG_ERR, "could not open %s: %s",
		    LLDPD_DOOR, strerror(err));
		return (err);
	}
	(void) close(fd);

	if ((lldpd_door_fd = door_create(lldpd_handler, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL)) == -1) {
		err = errno;
		syslog(LOG_ERR, "failed to create door: %s", strerror(err));
		return (err);
	}
	/*
	 * fdetach first in case a previous daemon instance exited
	 * ungracefully.
	 */
	(void) fdetach(LLDPD_DOOR);
	if (fattach(lldpd_door_fd, LLDPD_DOOR) != 0) {
		err = errno;
		syslog(LOG_ERR, "failed to attach door to %s: %s",
		    LLDPD_DOOR, strerror(err));
		(void) door_revoke(lldpd_door_fd);
		lldpd_door_fd = -1;
	}
	return (err);
}

/* ARGSUSED */
static void *
lldpd_snmp(void *arg)
{
	if (!done_subagent_init) {
		syslog(LOG_ERR, "There was failure initializing SNMP subagent."
		    " Cannot enable SNMP");
		return (NULL);
	}

	snmp_enabled = B_TRUE;
	/* initialize and register, all the MIB OID handlers */
	init_oracleLLDPV2();

	while (!snmp_shutting_down) {
		/*
		 * if snmp subagent is enabled, check for packets arriving
		 * from the master agent and process them, using the
		 * select(). `1', a non-zero value, makes select() to block.
		 */
		(void) agent_check_and_process(1);
	}
	/* SNMP is shutting down, unregister all the MIB OID handlers. */
	uninit_oracleLLDPV2();
	snmp_enabled = B_FALSE;
	return (NULL);
}

static void
lldpd_init_subagent(void)
{
	/* Specify the role of the subagent (We are agentx client) */
	if (netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID,
	    NETSNMP_DS_AGENT_ROLE, 1) != SNMPERR_SUCCESS) {
		syslog(LOG_ERR, "Error setting the subagent role.");
		return;
	}

	/*
	 * Initialize the embedded agent. The string name specifies which
	 * .conf file to read when init_snmp() is called later.
	 */
	if (init_agent(LLDPV2_AGENTNAME) != 0) {
		syslog(LOG_ERR, "Error initializing the subagent");
		return;
	}

	/*
	 * Initialize the SNMP library, which causes the agent to read the
	 * application's configuration files. The agent first tries to read
	 * the configuration files named by the string passed as an argument.
	 * This might be required to configure access  control, for example.
	 */
	init_snmp(LLDPV2_AGENTNAME);
	(void) snmp_log_syslogname(LLDPV2_AGENTNAME);
	snmp_enable_syslog();
	snmp_disable_stderrlog();
	done_subagent_init = B_TRUE;
}

static int
lldp_init_sysinfo()
{
	lldp_syscapab_t sc;
	int		err;

	/* default supported system capabilities */
	sc.ls_sup_syscapab = (LLDP_SYSCAPAB_ROUTER | LLDP_SYSCAPAB_MACBRIDGE |
	    LLDP_SYSCAPAB_STATION_ONLY);
	/* default enabled system capabilities, i.e. Nothing */
	sc.ls_enab_syscapab = 0;

	/* allocate `lldp_sysinfo' nvlist and default system capab info */
	if ((err = nvlist_alloc(&lldp_sysinfo, NV_UNIQUE_NAME, 0)) == 0) {
		if ((err = lldp_add_syscapab2nvlist(&sc, lldp_sysinfo)) != 0)
			nvlist_free(lldp_sysinfo);
	}
	return (err);
}

static int
lldpd_init(void)
{
	int	err;

	/* open dladm handle, before we drop privileges */
	if (dladm_open(&dld_handle) != DLADM_STATUS_OK) {
		syslog(LOG_ERR, "dladm_open() failed");
		goto fail;
	}

	/*
	 * Unfortunately, until 4791900 is fixed, only privileged processes
	 * can bind and thus receive sysevents. So, we create all the event
	 * channels now before we drop the privileges.
	 */
	if (sysevent_evc_bind(LLDP_EVENT_CHAN, &lldpd_evchp, EVCH_CREAT) != 0) {
		syslog(LOG_ERR, "Failed to create event channel");
		goto fail;
	}

	/*
	 * Drop unneeded privileges. We need PRIV_NET_RAWACCESS to open
	 * raw sockets and we need PRIV_SYS_DL_CONFIG to modify link
	 * properties.
	 */
	if (getzoneid() == GLOBAL_ZONEID) {
		err = __init_daemon_priv(PU_RESETGROUPS|PU_CLEARLIMITSET,
		    UID_DLADM, GID_NETADM, PRIV_NET_RAWACCESS,
		    PRIV_SYS_DL_CONFIG, PRIV_FILE_DAC_READ,
		    PRIV_SYS_DEVICES, NULL);
		if (err == 0) {
			err = priv_set(PRIV_OFF, PRIV_EFFECTIVE,
			    PRIV_FILE_DAC_READ, PRIV_SYS_DEVICES, NULL);
		}
	} else {
		err = __init_daemon_priv(PU_RESETGROUPS|PU_CLEARLIMITSET,
		    UID_DLADM, GID_NETADM, PRIV_NET_RAWACCESS, NULL);
	}

	if (err == -1) {
		syslog(LOG_ERR, "Could not set priviliges for the daemon");
		goto fail;
	}

	/* initialize the timer thread */
	if (lldp_timeout_init() != 0) {
		syslog(LOG_ERR, "Could not start the timer thread");
		goto fail;
	}

	/*
	 * initialize SNMP subagent. We later register handlers for the OID
	 * based on if SNMP needs to be enabled or not.
	 */
	lldpd_init_subagent();

	/* initialize `lldp_sysinfo' nvlist */
	if (lldp_init_sysinfo() != 0) {
		syslog(LOG_ERR, "Could not initialize lldp_sysinfo nvlist");
		goto fail;
	}

	list_create(&lldp_agents, sizeof (lldp_agent_t),
	    offsetof(lldp_agent_t, la_node));

	/*
	 * initialize LLDP protocol properties, LLDP agent properties and
	 * finally global TLV and per-agent TLV properties.
	 */
	if (lldpd_init_agents() != 0) {
		syslog(LOG_ERR, "Could not initialize LLDP agents");
		list_destroy(&lldp_agents);
		goto fail;
	}

	/* initialize door */
	if (lldpd_door_init() != 0) {
		list_destroy(&lldp_agents);
		goto fail;
	}
	return (0);
fail:
	lldpd_fini();
	return (-1);
}

/*
 * This is called by the child process to inform the parent process to
 * exit with the given return value.
 */
static void
lldpd_inform_parent_exit(int rv)
{
	if (write(pfds[1], &rv, sizeof (int)) != sizeof (int)) {
		syslog(LOG_ERR,
		    "failed to inform parent process of status: %s",
		    strerror(errno));
		(void) close(pfds[1]);
		exit(EXIT_FAILURE);
	}
	(void) close(pfds[1]);
}

/*
 * Keep the pfds fd open, close other fds.
 */
/*ARGSUSED*/
static int
closefunc(void *arg, int fd)
{
	if (fd != pfds[1])
		(void) close(fd);
	return (0);
}

/*
 * We cannot use libc's daemon() because the door we create is associated with
 * the process ID. If we create the door before the call to daemon(), it will
 * be associated with the parent and it's incorrect. On the other hand if we
 * create the door later, after the call to daemon(), parent process exits
 * early and gives a false notion to SMF that 'lldpd' is up and running,
 * which is incorrect. So, we have our own daemon() equivalent.
 */
static boolean_t
lldpd_daemonize(void)
{
	pid_t	 pid;
	int	 rv;

	if (pipe(pfds) < 0) {
		(void) fprintf(stderr, "%s: pipe() failed: %s\n",
		    getprogname(), strerror(errno));
		exit(EXIT_FAILURE);
	}

	if ((pid = fork()) == -1) {
		(void) fprintf(stderr, "%s: fork() failed: %s\n",
		    getprogname(), strerror(errno));
		exit(EXIT_FAILURE);
	} else if (pid > 0) { /* Parent */
		(void) close(pfds[1]);

		/*
		 * Parent should not exit early, it should wait for the child
		 * to return Success/Failure. If the parent exits early, then
		 * SMF will think 'lldpd' is up and would start all the
		 * depended services.
		 *
		 * If the child process exits unexpectedly, read() returns -1.
		 */
		if (read(pfds[0], &rv, sizeof (int)) != sizeof (int)) {
			(void) kill(pid, SIGKILL);
			rv = EXIT_FAILURE;
		}

		(void) close(pfds[0]);
		exit(rv);
	}

	/* Child */
	(void) setsid();

	/* close all files except pfds[1] */
	(void) fdwalk(closefunc, NULL);
	(void) chdir("/");
	openlog(getprogname(), LOG_PID | LOG_NDELAY, LOG_DAEMON);
	return (B_TRUE);
}

int
main(int argc, char *argv[])
{
	int opt;
	boolean_t fg = B_FALSE;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	/* Process options */
	while ((opt = getopt(argc, argv, "f")) != EOF) {
		switch (opt) {
		case 'f':
			fg = B_TRUE;
			break;
		default:
			(void) fprintf(stderr, "Usage: %s [-f]\n",
			    getprogname());
			exit(EXIT_FAILURE);
		}
	}

	if (!fg && getenv("SMF_FMRI") == NULL) {
		(void) fprintf(stderr,
		    "lldpd is a smf(5) managed service and cannot be run "
		    "from the command line.\n");
		exit(EXIT_FAILURE);
	}

	if (!fg && !lldpd_daemonize()) {
		(void) fprintf(stderr, "could not daemonize lldpd: %s",
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* initialize signal handling thread */
	if (lldpd_init_signal_handling() != 0) {
		syslog(LOG_ERR, "Could not start signal handling thread");
		goto child_out;
	}

	if (lldpd_init() != 0) {
		syslog(LOG_ERR, "Could not initialize the lldp daemon");
		goto child_out;
	}

	/* Inform the parent process that it can successfully exit */
	if (!fg)
		lldpd_inform_parent_exit(EXIT_SUCCESS);

	for (;;)
		(void) pause();

child_out:
	/* return from main() forcibly exits an MT process */
	if (!fg)
		lldpd_inform_parent_exit(EXIT_FAILURE);
	return (EXIT_FAILURE);
}
