/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef __SYS_IPMI_BSD_H__
#define	__SYS_IPMI_BSD_H__



typedef struct ipmi_state  *device_t;

struct selinfo {
	dev_t dev;
	struct cdev *cdev;
	struct thread *td;
};

struct intr_config_hook {
	list_node_t	ich_links;
	void		(*ich_func)(void *arg);
	void		*ich_arg;
};

struct bio {
	int filler;
};

struct knote {
	int filler;
};

struct vm_object {
	int filler;
};

struct resource {
	void *ptr;
};

typedef void *eventhandler_tag;
typedef int driver_intr_t;
typedef int devclass_t;
typedef void * ACPI_HANDLE;
typedef int64_t vm_ooffset_t;
typedef void *vm_offset_t;
typedef uint64_t vm_paddr_t;
typedef uint64_t vm_size_t;
typedef char vm_memattr_t;
typedef uint64_t bus_space_tag_t;
typedef uint64_t bus_space_handle_t;
typedef uint64_t bus_size_t;

typedef struct ipmi_retry {
	int		retries;
	unsigned	int retry_time_ms;
} ipmi_retry_t;

/*
 * The following "clone" stuff is used to un-mangle the minor numbers
 * for this device. In general we use the minor numbers to deliniate
 * instances of this driver. What complicates "instances" is that we
 * need to identify two types of instances within this driver. One is
 * the hardware backend plugin instances. Two is the number of open
 * driver instances. Reason two is a work around for an idiosyncrasy
 * in the way that Unix does closes on device drivers. That is that
 * on EVERY open the system calls the drivers open. But only on the
 * LAST close the system calls the drivers close. This makes it very
 * hard for the driver to allocate resources for every open case since
 * it can't know when to free these resources when an application is
 * finished with it.
 *
 * We have 17 bits of minor number that are available to use to do what
 * we need to do. Four bits are used do identify the plugin interface
 * instances (see ipmi_pi array, This will allow for 16 BMC device
 * instances in one system). The rest of the bits are used to deal with
 * the afore mentioned close problem.
 *
 * There is a way to cause each open to clone its own device. This
 * will tell the driver that an application is closing it. Since
 * each open gets its own private minor device, the close is always
 * the LAST close and the driver will be notified.
 *
 * For open clones we have 13 bits. This would allow about 8 thousand
 * simultanious opens.
 *
 * The following is a breakdown of how we use minor number bits:
 *
 *  Not            |      open
 *  Available      |    instances  | Hardware
 *                 |(device clones)| interfaces
 * ---------------------------------------------
 * 332222222222111 | 1111111000000 | 0000
 * 109876543210987 | 6543210987654 | 3210
 */

typedef struct ipmi_clone {
	list_node_t			ic_link;
	struct ipmi_state 		*ic_statep; /* Parent device state */
	ipmi_retry_t			ic_retrys;
	dev_t				ic_mindev;
	int				ic_instance;
	int				ic_getevents;

	/*
	 * BSD state - to help make BSD drv part of code happy.
	 */
	struct ipmi_device	*ic_bsd_dev; /* BSD open instance */
} ipmi_clone_t;

/* Overall bits to work with */
#define	IPMI_CLONEBITS	(NBITSMINOR32 - 1)
#define	IPMI_CLONESHIFT	(0)
#define	IPMI_CLONEMASK	((1U << IPMI_CLONEBITS) - 1)

/* Hardware interface instances */
#define	IPMI_INTFBITS	(4)	/* Up 16 interface on one system */
#define	IPMI_INTFSHIFT	(IPMI_CLONESHIFT)
#define	IPMI_INTFMASK	(((1U << IPMI_INTFBITS) - 1) << IPMI_INTFSHIFT)

/* Open "cloned" instances */
#define	IPMI_OINSTBITS	(IPMI_CLONEBITS - IPMI_INTFBITS)
#define	IPMI_OINSTSHIFT	(IPMI_INTFBITS)
#define	IPMI_OINSTMASK	(((1U << IPMI_OINSTBITS) - 1) << IPMI_OINSTSHIFT)
/* Max number of clone MINORS minus the base MINOR */
#define	IPMI_OINSTMAX	((((1U << IPMI_OINSTBITS) - 1) - 2))

#define	IPMI_DEV2OINST(dev) (((dev) & IPMI_OINSTMASK) >> IPMI_OINSTSHIFT)
#define	IPMI_DEV2INTF(dev) (((dev) & IPMI_INTFMASK) >> IPMI_INTFSHIFT)
#define	IPMI_MKMINOR(clone, intf) ((((clone) << IPMI_OINSTSHIFT) & \
	IPMI_OINSTMASK) | (((intf) << IPMI_INTFSHIFT) & IPMI_INTFMASK))


typedef int d_open_t(struct ipmi_clone *clonep, int oflags, int devtype,
    struct thread *td);
typedef int d_fdopen_t(struct cdev *dev, int oflags, struct thread *td,
    struct file *fp);
typedef int d_close_t(struct ipmi_clone *clonep, int fflag, int devtype,
    struct thread *td);
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
typedef int d_ioctl_t(struct ipmi_clone *clonep, int cmd, intptr_t data,
    int fflag, cred_t *cred);
typedef int d_poll_t(struct ipmi_clone *clonep, int events, void **td);
typedef int d_mmap_t(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr);
typedef void d_strategy_t(struct bio *bp);
typedef int dumper_t(
	void *_priv, /* Private to the driver. */
	void *_virtual, /* Virtual (mapped) address. */
	vm_offset_t _physical, /* Physical address of virtual. */
	off_t _offset, /* Byte-offset to write at. */
	size_t _length); /* Number of bytes to dump. */
typedef int d_kqfilter_t(struct cdev *dev, struct knote *kn);
typedef void d_purge_t(struct cdev *dev);
typedef int d_mmap_single_t(struct cdev *cdev, vm_ooffset_t *offset,
    vm_size_t size, struct vm_object **object, int nprot);
typedef void *driver_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef unsigned char u_int8_t;

/*
 * Character device switch table
 */
struct cdevsw {
	int		d_version;
	uint_t		d_flags;
	const char	*d_name;
	d_open_t	*d_open;
	d_fdopen_t	*d_fdopen;
	d_close_t	*d_close;
	d_read_t	*d_read;
	d_write_t	*d_write;
	d_ioctl_t	*d_ioctl;
	d_poll_t	*d_poll;
	d_mmap_t	*d_mmap;
	d_strategy_t	*d_strategy;
	dumper_t	*d_dump;
	d_kqfilter_t	*d_kqfilter;
	d_purge_t	*d_purge;
	d_mmap_single_t	*d_mmap_single;

	int32_t		d_spare0[3];
	void		*d_spare1[3];

	/* These fields should not be messed with by drivers */
	list_t		d_devs;
	int		d_spare2;
	union {
		struct cdevsw	*gianttrick;
		list_node_t	postfree_list;
	} __d_giant;
};
#define	d_gianttrick		__d_giant.gianttrick
#define	d_postfree_list		__d_giant.postfree_list

struct cdev {
	void		*si_parent;
	void		(*si_close)(void *);
	void		*si_drv1, *si_drv2;
	struct cdevsw   *si_devsw;
	ulong_t		si_usecount;
	ulong_t		si_threadcount;
};

#ifdef IN_BSD_CODE
#define	SYSCTL_NODE(arg1, arg2, arg3, arg4, arg5, arg6) char *ipmi_desc = arg6
#define	SYSCTL_INT(arg1, arg2, arg3, arg4, arg5, arg6, arg7) \
	char *ipmi_desc2 = arg7
#define	MALLOC_DEFINE(arg1, arg2, arg3) char *ipmi_desc3 = arg3
#define	EVENTHANDLER_REGISTER(list, func, arg, priority)                \
	ipmi_eventhandler_register(func, arg)
#define	EVENTHANDLER_DEREGISTER(list, tag)                              \
	ipmi_eventhandler_deregister(tag)

#define	make_dev(devsw, unit, arg3, arg4, arg5, arg6, arg7)		\
	ipmi_make_dev(devsw, unit)
#define	bus_setup_intr(dev, irq_res, arg3, arg4, arg5, sc, ipmi_irq) (0)
#define	bus_teardown_intr(dev, res, arg)
#define	device_get_softc(dev) ipmi_get_softc(dev)
#define	devfs_set_cdevpriv(clonep, dev, func) \
	ipmi_set_cdevpriv(clonep, dev, func)
#define	devfs_get_cdevpriv(clonep, dev) ipmi_get_cdevpriv(clonep, dev)
#define	selrecord(selector, sip) *selector = sip
#define	mtx_assert(m, what)
#define	selwakeup(sip) pollwakeup(sip, POLLIN|POLLRDNORM)
#define	config_intrhook_establish(hook) ipmi_intr_establish(hook)
#define	config_intrhook_disestablish(hook)
#define	mtx_destroy(lock) ipmi_mtx_destroy(lock)
#define	destroy_dev(cdev) ipmi_destroy_dev(cdev)
#define	bus_release_resource(dev, irq, rid, res)
#define	bus_read_1(res, port)
#define	bus_write_1(res, port, data)
#define	smbus_error(smb_error) ipmi_smbus_error(smb_error)

#define	smbus_bwrite(bus, slave, cmd, count, buf) \
	ipmi_smbus_bwrite(bus, slave, cmd, count, buf)
#define	smbus_bread(bus, slave, cmd, count, buf) \
	ipmi_smbus_bread(bus, slave, cmd, count, buf)
#define	smbus_release_bus(bus, dev) ipmi_smbus_release_bus(bus, dev)
#define	smbus_request_bus(bus, dev, how) ipmi_smbus_request_bus(bus, dev, how)
#define	pause(name, delay) ipmi_pause(name, delay)
#define	cv_init(cv, name) ipmi_cv_init(cv, name)
#define	device_get_nameunit(dev) (0)
#define	device_get_unit(dev) ipmi_device_get_unit(dev)
#define	mtx_lock(mutex) mutex_enter(mutex)
#define	mtx_unlock(mutex) mutex_exit(mutex)
#define	device_printf ipmi_device_printf
#endif

#define	watchdog_list 0

#define	__FreeBSD_version	900021 /* Master, propagated to newvers */

#define	PWAIT		1
#define	ENOIOCTL	EINVAL

#define	MA_OWNED	0

#define	ticks ((int)gethrtime())
#define	D_VERSION_03	0x17122009 /* d_mmap takes memattr,vm_ooffset_t */
#define	D_VERSION	D_VERSION_03
#define	WD_INTERVAL	0x00000ff
#define	MTX_DEF		0x00000000 /* DEFAULT (sleep) lock */


/*
 * Functions external to BSD driver
 */
void mtx_lock(kmutex_t *m);
void mtx_unlock(kmutex_t *m);
int ipmi_device_get_unit(device_t dev);
device_t device_get_parent(device_t dev);
uint32_t AcpiEvaluateObject(ACPI_HANDLE	Handle, char *Pathname,
    void *ExternalParams, void *ReturnBuffer);
void ipmi_cv_init(kcondvar_t *, char *);
eventhandler_tag ipmi_eventhandler_register(void (*func)(void *, unsigned int,
    int *), void *arg);
void ipmi_eventhandler_deregister(eventhandler_tag tag);
struct cdev *ipmi_make_dev(struct cdevsw *devsw, int unit);
struct ipmi_softc *ipmi_get_softc(device_t dev);
int ipmi_set_cdevpriv(struct ipmi_clone *clonep, void *dev,
    void (*func)(void *));
int ipmi_get_cdevpriv(struct ipmi_clone *clonep, void **dev);
int ipmi_msleep(kcondvar_t *sleep_cv, kmutex_t *sleep_lock, int *flag,
    int tick);
void ipmi_wakeup(kcondvar_t *sleep_cv, kmutex_t *sleep_lock, int *flag);
int ipmi_intr_establish(struct intr_config_hook *hook);
void ipmi_mtx_destroy(kmutex_t *lock);
void ipmi_destroy_dev(struct cdev *cdev);
uint16_t ipmi_pci_get_vendor(device_t dev);
uint16_t ipmi_pci_get_device(device_t dev);
uint16_t ipmi_pci_get_class(device_t dev);
uint16_t ipmi_pci_get_subclass(device_t dev);
uint8_t ipmi_pci_get_progif(device_t dev);
uint32_t ipmi_pci_read_config(device_t dev, int reg, int width);
void *ipmi_pmap_mapbios(uint32_t pa,  uint32_t size);
u_int32_t bios_sigsearch(u_int32_t start, char *sig, int siglen,
    int paralen, int sigofs);
void ipmi_pmap_unmapbios(vm_offset_t va, uint32_t size);
int ipmi_smbus_error(int smb_error);
int ipmi_smbus_bwrite(device_t bus, int slave, int cmd, size_t count,
    void *buf);
int ipmi_smbus_bread(device_t bus, int slave, int cmd, uchar_t *count,
    uchar_t *buf);
int ipmi_smbus_release_bus(device_t bus, device_t dev);
int ipmi_smbus_request_bus(device_t bus, device_t dev, int how);
void ipmi_pause(char *name, uint_t delay);
void ipmi_device_printf(void *statep, const char *fmt, ...);

#endif	/* !__SYS_IPMI_BSD_H__ */
