/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */


#ifndef _SYS_USB_USBSER_EDGE_VAR_H
#define	_SYS_USB_USBSER_EDGE_VAR_H

/*
 * Edgeport implementation definitions
 */

#include <sys/types.h>
#include <sys/dditypes.h>
#include <sys/note.h>

#include <sys/usb/clients/usbser/usbser_dsdi.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_fw.h>
#include <sys/usb/clients/usbser/usbser_edge/usbvend.h>
#include <sys/usb/clients/usbser/usbser_edge/ionti.h>
#include <sys/usb/clients/usbser/usbser_edge/16654.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * forward typedefs needed to resolve recursive header dependencies
 */
typedef struct edgeti_boot edgeti_boot_t;
typedef struct edge_state edge_state_t;
typedef struct edge_port edge_port_t;

/* block/noblock flags */
enum edge_fblock {
	EDGE_BLOCK,
	EDGE_NOBLOCK
};

typedef enum edge_fblock edge_fblock_t;

#include <sys/usb/clients/usbser/usbser_edge/edge_pipe.h>

/*
 * temporary soft state for TI boot mode
 */
struct edgeti_boot {
	dev_info_t		*et_dip;		/* device info */
	int			et_instance;		/* instance */
	usb_client_dev_data_t	*et_dev_data;		/* registration data */
	usb_log_handle_t	et_lh;			/* USBA log handle */
	edge_pipe_t		et_def_pipe;		/* default pipe */
	edge_pipe_t		et_bulk_pipe;		/* bulk pipe */
	uint16_t		et_i2c_type;		/* type of I2C in UMP */
	edgeti_manuf_descriptor_t et_mfg_descr;		/* TI mfg descriptor */
};


/*
 * PM support
 */
typedef struct edge_power {
	uint8_t		pm_wakeup_enabled; /* remote wakeup enabled */
	uint8_t		pm_pwr_states;	/* bit mask of power states */
	boolean_t	pm_raise_power;	/* driver is about to raise power */
	uint8_t		pm_cur_power;	/* current power level */
	uint_t		pm_busy_cnt;	/* number of set_busy requests */
} edge_pm_t;

/*
 * override values
 *	These values may be set in usbser_edge.conf for particular devices
 */
typedef struct edge_ov {
	char	serialnumber[80];		/* serial number */
	uint_t	portnumber;		/* port number */
	uint8_t	mode;		/* mode */
} edge_ov_t;

/*
 * device firmware information
 */
typedef struct edge_fw_info {
	edge_fw_image_record_t	*fw_down_image;		/* download code */
	uint16_t		fw_down_image_size;	/* dl code size */
	edge_fw_version_t	*fw_down_version;	/* dl code version */
} edge_fw_info_t;


/*
 * data parsing: IOSP headers can be split between packets,
 * so we need to save parser state in this structure
 */
typedef struct edge_rx_parse {
	int			rx_state;	/* parser state */
	uint8_t			rx_hdr[3];	/* packet header bytes */
	uint8_t			rx_stat_code;	/* status code */
	uint_t			rx_port_num;	/* port number */
	int			rx_data_len;	/* expected data length */
} edge_rx_parse_t;


/* Edgeport type */
enum {
	EDGE_SP,	/* original ION Serial Protocol */
	EDGE_TI		/* TI Edgeport protocol */
};

/*
 * parser state:
 *
 * Each status/data block arriving on the BULK IN pipe starts with
 * a few (1-3) header bytes. Blocks may not be aligned on the
 * USB packet boundary, so we must track the state of the parser
 *
 * EDGE_RX_NEXT_HDR0 --> EDGE_RX_NEXT_HDR1 +--> EDGE_RX_NEXT_DATA --+
 *         ^                               |                        |
 *         |                               ---> EDGE_RX_NEXT_HDR2 --+
 *         |                                                        |
 *         +----------------------------<---------------------------+
 */
enum {
	EDGE_RX_NEXT_HDR0 = 0,	/* expecting 1st header byte */
	EDGE_RX_NEXT_HDR1,	/* expecting 2nd header byte */
	EDGE_RX_NEXT_DATA,	/* expecting data bytes */
	EDGE_RX_NEXT_HDR2	/* expecting 3rd header byte */
};

/*
 * per device state structure
 */
struct edge_state {
	kmutex_t		es_mutex;		/* structure lock */
	dev_info_t		*es_dip;		/* device info */
	uint_t			es_port_cnt;		/* port count */
	edge_port_t		*es_ports;		/* per port structs */
	boolean_t		es_is_ti;		/* is it TI-based? */
	/*
	 * we use semaphore to serialize pipe open/close by different ports.
	 * mutex could be used too, but it causes trouble when warlocking
	 * with USBA: some functions inside usb_pipe_close() wait on cv
	 *
	 * since semaphore is only used for serialization during
	 * open/close and suspend/resume, there is no deadlock hazard
	 */
	ksema_t			es_pipes_sema;
	int			es_pipes_users;		/* # of pipes users */
	/*
	 * USBA
	 */
	usb_client_dev_data_t	*es_dev_data;		/* registration data */
	usb_event_t		*es_usb_events;		/* usb events */
	edge_pipe_t		es_def_pipe;		/* default pipe */
	edge_pipe_t		es_bulkin_pipe;		/* bulk in pipe */
	edge_pipe_t		es_bulkout_pipe;	/* bulk out pipe */
	edge_pipe_t		es_intr_pipe;		/* interrupt pipe */
	usb_log_handle_t	es_lh;			/* USBA log handle */
	int			es_dev_state;		/* USB device state */
	edge_pm_t		*es_pm;			/* PM support */
	/*
	 * vendor descriptors
	 */
	edge_manuf_descriptor_t	es_mfg_descr;		/* mfg descriptor */
	edge_boot_descriptor_t	es_boot_descr;		/* boot descriptor */
	edgeti_manuf_descriptor_t es_ti_mfg_descr;	/* TI mfg descriptor */
	/*
	 * other
	 */
	edge_fw_info_t		es_fw;		/* firmware info */
	edge_rx_parse_t		es_rxp;		/* rx parsing info */
	int			es_rx_avail;	/* # of bytes avail. for read */
	uint_t			es_tx_last;	/* port last written to */
	/* TI specific data */
	uint16_t		es_i2c_type;		/* type of I2C in UMP */
	timeout_id_t		es_timeout_id;
	boolean_t		es_timeout_enable;
};

_NOTE(MUTEX_PROTECTS_DATA(edge_state::es_mutex, edge_state))
_NOTE(DATA_READABLE_WITHOUT_LOCK(edge_state::{
	es_dip
	es_is_ti
	es_dev_data
	es_usb_events
	es_port_cnt
	es_ports
	es_def_pipe
	es_lh
	es_pm
	es_mfg_descr
	es_boot_descr
	es_fw
	es_i2c_type
}))
_NOTE(SCHEME_PROTECTS_DATA("pipes_sema", edge_state::es_pipes_users))

/*
 * per port structure
 */
struct edge_port {
	kmutex_t	ep_mutex;	/* structure lock */
	edge_state_t	*ep_esp;	/* back pointer to the state */
	char		ep_lh_name[16];	/* log handle name */
	usb_log_handle_t ep_lh;		/* log handle */
	uint_t		ep_port_num;	/* port number */
	int		ep_state;	/* port state */
	int		ep_flags;	/* port flags */
	ds_cb_t		ep_cb;		/* DSD callbacks */
	kcondvar_t	ep_resp_cv;	/* cv to wait for responses */
	kcondvar_t	ep_tx_cv;	/* cv to wait for tx completion */
	/*
	 * data receipt and transmit
	 */
	mblk_t		*ep_rx_mp;	/* received data */
	mblk_t		*ep_tx_mp;	/* transmitted data */
	int		ep_tx_bufsz;	/* tx buffer size */
	int		ep_tx_credit_thre; /* credit threshold */
	int		ep_tx_credit;	/* # of bytes that can be transmitted */
	boolean_t	ep_no_more_reads; /* disable reads */
	/*
	 * device-specific data
	 */
	regs_16654_t	ep_regs;	/* shadow copies of registers */
	int		ep_chase_status; /* status of the CHASE command */
	/*
	 * TI specific data
	 */
	edge_pipe_t	ep_bulkin_pipe;	/* bulk in pipe */
	edge_pipe_t	ep_bulkout_pipe; /* bulk out pipe */
	uint16_t	ep_uart_base;	/* UART registers base address */
	uint16_t	ep_dma_addr;	/* UART DMA address */
	int		ep_read_len;	/* length of bulkin request */
	int		ep_write_len;	/* length of bulkout request */
	boolean_t	ep_lsr_event; /* indicates that LSR data is coming */
	uint8_t		ep_lsr_mask;	/* LSR value for the event */
	edgeti_ump_uart_config_t ep_uart_config; /* UART configuration */
	int		ep_speed;	/* current speed code */
};

_NOTE(MUTEX_PROTECTS_DATA(edge_port::ep_mutex, edge_port))
_NOTE(DATA_READABLE_WITHOUT_LOCK(edge_port::{
	ep_esp
	ep_lh
	ep_port_num
	ep_read_len
	ep_cb
	ep_bulkin_pipe.pipe_handle
	ep_dma_addr
}))

/* lock relationships */
_NOTE(LOCK_ORDER(edge_state::es_mutex edge_port::ep_mutex))
_NOTE(LOCK_ORDER(edge_port::ep_mutex edge_pipe::pipe_mutex))

/* port state */
enum {
	EDGE_PORT_NOT_INIT = 0,		/* port is not initialized */
	EDGE_PORT_CLOSED,		/* port is closed */
	EDGE_PORT_OPENING,		/* port is being opened */
	EDGE_PORT_OPEN			/* port is open */
};

/* port flags */
enum {
	EDGE_PORT_OPEN_RSP	= 0x0001,	/* open response received */
	EDGE_PORT_CHASE_RSP	= 0x0002,	/* chase response received */
	EDGE_PORT_TX_CB		= 0x0004,	/* tx cb needs to be called */
	EDGE_PORT_BREAK		= 0x0008,	/* break is on */
	EDGE_PORT_TX_STOPPED	= 0x0010	/* transmit not allowed */
};

/* various tunables */
enum {
	EDGE_BULK_TIMEOUT		= 2,	/* transfer timeout */
	EDGE_MAX_RETRY			= 3,	/* max request retries */
	EDGE_BULKIN_LEN			= 1024,	/* bulk in read length */
	EDGETI_BULKIN_MAX_LEN		= 4 * 1024, /* TI bulk in length */
	EDGETI_BULKOUT_MAX_LEN		= 4 * 1024, /* TI bulk out length */
	EDGE_OPEN_RSP_TIMEOUT		= 3,	/* open response timeout */
	EDGE_CLOSE_CHASE_TIMEOUT	= 6,	/* chase response timeout */
						/* during port close */
	EDGETI_TRANSACTION_TIMEOUT	= 2	/* DMA transaction timeout */
};

/* special parameters for DS_PARAM_FLOW_CTL command */
enum {
	EDGE_REG_TX_FLOW	= 0x81,
	EDGE_REG_RX_FLOW	= 0x82
};

/*
 * Vendor bmRequestType
 */
enum {
	EDGE_RQ_OUT	= USB_DEV_REQ_HOST_TO_DEV | USB_DEV_REQ_TYPE_VENDOR,
	EDGE_RQ_IN	= USB_DEV_REQ_DEV_TO_HOST | USB_DEV_REQ_TYPE_VENDOR,
	EDGE_RQ_WRITE_DEV	= EDGE_RQ_OUT | USB_DEV_REQ_RCPT_DEV,
	EDGE_RQ_WRITE_IF	= EDGE_RQ_OUT | USB_DEV_REQ_RCPT_IF,
	EDGE_RQ_WRITE_EP	= EDGE_RQ_OUT | USB_DEV_REQ_RCPT_EP,
	EDGE_RQ_WRITE_OTHER	= EDGE_RQ_OUT | USB_DEV_REQ_RCPT_OTHER,
	EDGE_RQ_READ_DEV	= EDGE_RQ_IN | USB_DEV_REQ_RCPT_DEV,
	EDGE_RQ_READ_IF		= EDGE_RQ_IN | USB_DEV_REQ_RCPT_IF,
	EDGE_RQ_READ_EP		= EDGE_RQ_IN | USB_DEV_REQ_RCPT_EP,
	EDGE_RQ_READ_OTHER	= EDGE_RQ_IN | USB_DEV_REQ_RCPT_OTHER
};

/* TI defines */
enum {
	EDGETI_CLEAR	= 0,
	EDGETI_SET	= 1
};

/*
 * debug printing masks
 */
#define	DPRINT_ATTACH		0x00000001
#define	DPRINT_OPEN		0x00000002
#define	DPRINT_CLOSE		0x00000004
#define	DPRINT_DEF_PIPE		0x00000010
#define	DPRINT_IN_PIPE		0x00000020
#define	DPRINT_OUT_PIPE		0x00000040
#define	DPRINT_INTR_PIPE	0x00000080
#define	DPRINT_PIPE_RESET	0x00000100
#define	DPRINT_IN_DATA		0x00000200
#define	DPRINT_OUT_DATA		0x00000400
#define	DPRINT_CTLOP		0x00000800
#define	DPRINT_HOTPLUG		0x00001000
#define	DPRINT_PM		0x00002000
#define	DPRINT_OV		0x00004000
#define	DPRINT_MASK_ALL		0xFFFFFFFF

/*
 * edgeport reset timeout
 */
#define	EDGE_RESET_TIMEOUT	15000000

/*
 * misc macros
 */
#define	NELEM(a)	(sizeof (a) / sizeof (*(a)))

/* common DSD functions */
int	edge_tx_copy_data(edge_port_t *, mblk_t *, int);
void	edge_tx_start(edge_port_t *, int *);
int	edge_dev_is_online(edge_state_t *);
void	edge_put_tail(mblk_t **, mblk_t *);
void	edge_put_head(mblk_t **, mblk_t *);

/* SP functions */
int	edgesp_attach_dev(edge_state_t *esp);
void	edgesp_detach_dev(edge_state_t *esp);
int	edgesp_open_pipes_serialized(edge_state_t *);
void	edgesp_close_pipes_serialized(edge_state_t *);
int	edgesp_open_hw_port(edge_port_t *, boolean_t);
void	edgesp_close_hw_port(edge_port_t *);
int	edgesp_write_regs(edge_port_t *, uint8_t *, int);
int	edgesp_send_cmd_sync(edge_port_t *, uint8_t, uint8_t);
int	edgesp_set_port_params(edge_port_t *, ds_port_params_t *);
int	edgesp_chase_port(edge_port_t *, int);
void	edgesp_tx_start(edge_port_t *, int *);
void	edgesp_bulkin_cb(usb_pipe_handle_t, usb_bulk_req_t *);
void	edgesp_bulkout_cb(usb_pipe_handle_t, usb_bulk_req_t *);
void	edgesp_intr_cb(usb_pipe_handle_t, usb_intr_req_t *);
void	edgesp_intr_ex_cb(usb_pipe_handle_t, usb_intr_req_t *);

/* TI functions */
boolean_t edgeti_is_ti(usb_client_dev_data_t *);
boolean_t edgeti_is_boot(usb_client_dev_data_t *);
int	edgeti_attach_dev(edge_state_t *);
void	edgeti_detach_dev(edge_state_t *);
void	edgeti_init_port_params(edge_port_t *);
int	edgeti_open_pipes_serialized(edge_port_t *);
int	edgeti_close_pipes_serialized(edge_port_t *);
int	edgeti_open_hw_port(edge_port_t *, boolean_t);
void	edgeti_close_hw_port(edge_port_t *);
int	edgeti_set_port_params(edge_port_t *, ds_port_params_t *);
int	edgeti_chase_port(edge_port_t *, int);
void	edgeti_tx_start(edge_port_t *, int *);
void	edgeti_bulkin_cb(usb_pipe_handle_t, usb_bulk_req_t *);
void	edgeti_bulkout_cb(usb_pipe_handle_t, usb_bulk_req_t *);
void	edgeti_intr_cb(usb_pipe_handle_t, usb_intr_req_t *);
void	edgeti_intr_ex_cb(usb_pipe_handle_t, usb_intr_req_t *);

int	edgeti_set_mcr(edge_port_t *, uint8_t);
int	edgeti_cmd_set_break(edge_port_t *, uint16_t);
int	edgeti_cmd_set_loopback(edge_port_t *, uint16_t);
int	edgeti_cmd_purge(edge_port_t *, uint16_t);

int	edgeti_restore_device(edge_state_t *);

void	edgeti_reset_timer(void *);
void	edgeti_start_reset_timer(edge_state_t *);
void	edgeti_stop_reset_timer(edge_state_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_USB_USBSER_EDGE_VAR_H */
