/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */


#ifndef _SYS_USB_USBSER_EDGE_SUBR_H
#define	_SYS_USB_USBSER_EDGE_SUBR_H


/*
 * Edgeport adapter driver subroutines
 */

#include <sys/types.h>
#include <sys/dditypes.h>
#include <sys/stream.h>

#include <sys/usb/clients/usbser/usbser_edge/edge_fw.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* flags for memory read/write */
enum {
	EDGE_RW_ADDR_VALUE	= 0x01,
	EDGE_RW_ADDR_INDEX	= 0x02,
	EDGE_RW_EXT_VALUE	= 0x04,
	EDGE_RW_EXT_INDEX	= 0x08
};


/* common routines */
void	edge_dump_buf(usb_log_handle_t, int, uchar_t *, int);

/* SP routines */
int	edgesp_reset_device(edge_state_t *);
int	edgesp_get_descriptors(edge_state_t *);
int	edgesp_setup_firmware(edge_state_t *);
int	edgesp_activate_device(edge_state_t *);

/* TI routines */
void	edgeti_boot_determine_i2c_type(edgeti_boot_t *);
mblk_t	*edgeti_read_ram(edge_pipe_t *, uint16_t, uint16_t);
mblk_t	*edgeti_read_rom(edge_pipe_t *, uint16_t, uint16_t);
int	edgeti_read_mfg_descr(edge_pipe_t *, edgeti_manuf_descriptor_t *);
int	edgeti_validate_i2c_image(edge_pipe_t *);
int	edgeti_download_code(dev_info_t *, usb_log_handle_t, usb_pipe_handle_t);
int	edgeti_get_descriptors(edge_state_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_USB_USBSER_EDGE_SUBR_H */
