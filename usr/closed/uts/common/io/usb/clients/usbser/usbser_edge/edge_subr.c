/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */

/*
 *
 * EdgePort adapter driver subroutines
 *
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/byteorder.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/usb/clients/usbser/usbser_edge/edge_var.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_fw.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_subr.h>
#include <sys/usb/clients/usbser/usbser_edge/usbvend.h>
#include <sys/usb/clients/usbser/usbser_edge/ionsp.h>
#include <sys/usb/clients/usbser/usbser_edge/ionti.h>

/* common routines */
static mblk_t	*edge_read_memory(edge_pipe_t *, uint16_t,
		uint16_t, uint16_t, uint16_t, uint8_t, int);
static int	edge_write_memory(edge_pipe_t *, uint16_t, uint16_t, uchar_t *,
		uint16_t, uint16_t, uint8_t, int);

/* SP routines */
static mblk_t	*edgesp_read_memory(edge_state_t *, uint8_t, uint16_t,
			uint16_t);
static int	edgesp_write_memory(edge_state_t *, uint8_t, uint16_t, uint16_t,
			uchar_t *);
static void	edgesp_dump_mfg_descr(edge_manuf_descriptor_t *,
			usb_log_handle_t);
static void	edgesp_dump_boot_descr(edge_boot_descriptor_t *,
			usb_log_handle_t);
static int	edgesp_download_code(edge_state_t *, uint16_t,
			edge_fw_image_record_t *, uint16_t);
static int	edgesp_exec_download_code(edge_state_t *, uint32_t);
static int	edgesp_set_suspend_feature(edge_state_t *, boolean_t);

/* TI routines */
static mblk_t	*edgeti_read_boot_memory(edge_pipe_t *, uint16_t, uint16_t,
		uint16_t);
static mblk_t	*edgeti_read_download_memory(edge_pipe_t *, uint16_t, uint16_t,
		uint16_t);
static uint16_t	edgeti_get_descr_addr(edge_pipe_t *, int, edgeti_i2c_desc_t *);
static int	edgeti_valid_checksum(edgeti_i2c_desc_t *, uint8_t *);


/*
 * common routines
 * ---------------
 *
 * do a hex dump
 */
void
edge_dump_buf(usb_log_handle_t lh, int mask, uchar_t *ptr, int len)
{
	int	i, bufsz;
	char	*buf;

	ASSERT(len > 0);

	bufsz = (len + 1) * 4;
	buf = kmem_zalloc(bufsz, KM_SLEEP);

	USB_DPRINTF_L4(mask, lh,
	    "edge_dump_buf: ptr=%p, buf=%p, len=%d",
	    (void *)ptr, (void *)buf, len);

	for (i = 0; i < len; i++) {
		(void) sprintf(buf + strlen(buf), "%3x", *ptr++);
	}
	USB_DPRINTF_L4(mask, lh, "%s", buf);

	kmem_free(buf, bufsz);
}


/*
 * generic memory reading routine
 */
static mblk_t *
edge_read_memory(edge_pipe_t *pipe, uint16_t addr, uint16_t len, uint16_t ext,
	uint16_t segsz, uint8_t cmd, int flags)
{
	int		rval;
	uint16_t	cnt;		/* # of read bytes */
	uint16_t	nwanted;	/* # of bytes we want to read */
	uint16_t	nread;		/* # of bytes actually read */
	int		retry = 0;	/* # of retries */
	mblk_t		*data;		/* single data mblk */
	mblk_t		*head = NULL;	/* first mblk in chain */
	mblk_t		*tail = NULL;	/* last mblk in chain */
	usb_ctrl_setup_t setup = { EDGE_RQ_READ_DEV, 0, 0, 0, 0, 0 };
	usb_cb_flags_t	cb_flags;
	usb_cr_t	cr;

	setup.bRequest = cmd;

	if (flags & EDGE_RW_EXT_VALUE) {
		setup.wValue = ext;
	} else if (flags & EDGE_RW_EXT_INDEX) {
		setup.wIndex = ext;
	}

	for (cnt = 0; cnt < len; ) {
		nwanted = min(len - cnt, segsz);
		if (flags & EDGE_RW_ADDR_VALUE) {
			setup.wValue = addr;
		} else {
			setup.wIndex = addr;
		}
		setup.wLength = nwanted;
		data = NULL;

		rval = usb_pipe_ctrl_xfer_wait(pipe->pipe_handle, &setup, &data,
		    &cr, &cb_flags, 0);

		if ((rval == USB_SUCCESS) && data) {
			nread = MBLKL(data);
			addr += nread;
			cnt += nread;
			retry = 0;

			/* chain mblks */
			if (head) {
				tail->b_cont = data;
			} else {
				head = data;
			}
			tail = data;
		} else {
			if (data) {
				freemsg(data);
			}

			++retry;
			if (retry > EDGE_MAX_RETRY) {
				USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
				    "edge_read_memory: too many failures, "
				    "giving up");
				freemsg(head);

				return (NULL);
			}
		}
	}

	/* concatenate all the mblks into one */
	if ((data = msgpullup(head, -1)) == NULL) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
		    "edge_read_memory: msgpullup failed");
	}
	freemsg(head);

	return (data);
}


/*
 * generic memory writing routine
 */
static int
edge_write_memory(edge_pipe_t *pipe, uint16_t addr, uint16_t len, uchar_t *buf,
	uint16_t ext, uint16_t segsz, uint8_t cmd, int flags)
{
	uint_t		cnt;		/* # of xfered bytes */
	mblk_t		*data = NULL;	/* data for USBA */
	uint16_t	data_len;	/* # of bytes we want to write */
	int		retry;		/* # of retries */
	usb_ctrl_setup_t setup = { EDGE_RQ_WRITE_DEV, 0, 0, 0, 0, 0 };
	usb_cb_flags_t	cb_flags;
	usb_cr_t	cr;

	setup.bRequest = cmd;

	if (flags & EDGE_RW_EXT_VALUE) {
		setup.wValue = ext;
	} else if (flags & EDGE_RW_EXT_INDEX) {
		setup.wIndex = ext;
	}

	for (cnt = 0; cnt < len; cnt += data_len, addr += data_len) {
		data_len = min(len - cnt, segsz);

		/* reuse previous mblk if possible */
		if ((data = reallocb(data, data_len, 0)) == NULL) {

			return (USB_FAILURE);
		}
		bcopy(buf + cnt, data->b_rptr, data_len);
		data->b_wptr += data_len;

		if (flags & EDGE_RW_ADDR_VALUE) {
			setup.wValue = addr;
		} else {
			setup.wIndex = addr;
		}
		setup.wLength = data_len;
		retry = 0;
		while (usb_pipe_ctrl_xfer_wait(pipe->pipe_handle, &setup, &data,
		    &cr, &cb_flags, 0) != USB_SUCCESS) {

			if (++retry > EDGE_MAX_RETRY) {
				if (data) {
					freemsg(data);
				}
				USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
				    "edge_write_memory: too many failures, "
				    "giving up");

				return (USB_FAILURE);
			}
		}
	}
	if (data) {
		freemsg(data);
	}

	return (USB_SUCCESS);
}


/*
 * SP routines
 * -----------
 *
 *
 * Reads 'len' bytes from RAM/ROM on the device begining at 'addr'
 */
static mblk_t *
edgesp_read_memory(edge_state_t *esp, uint8_t cmd, uint16_t addr, uint16_t len)
{
	ASSERT((cmd == USB_REQUEST_ION_READ_ROM) ||
	    (cmd == USB_REQUEST_ION_READ_RAM));

	return (edge_read_memory(&esp->es_def_pipe, addr, len, 0,
	    MAX_SIZE_REQ_ION_READ_MEM, cmd, EDGE_RW_ADDR_VALUE));
}


/*
 * Write 'len' bytes of RAM/ROM on the device begining at 'addr'
 */
static int
edgesp_write_memory(edge_state_t *esp, uint8_t cmd, uint16_t addr,
	uint16_t len, uchar_t *buf)
{
	ASSERT((cmd == USB_REQUEST_ION_WRITE_ROM) ||
	    (cmd == USB_REQUEST_ION_WRITE_RAM));

	return (edge_write_memory(&esp->es_def_pipe, addr, len, buf, 0,
	    MAX_SIZE_REQ_ION_WRITE_MEM, cmd, EDGE_RW_ADDR_VALUE));
}


/*
 * Read the manufacturing descriptor
 */
static int
edgesp_read_mfg_descr(edge_state_t *esp, edge_manuf_descriptor_t *descr)
{
	mblk_t	*data;
	int	data_len;
	int	rval;

	/* read in the manufacturing descriptor from ROM */
	if ((data = edgesp_read_memory(esp, USB_REQUEST_ION_READ_ROM,
	    (uint16_t)EDGE_MANUF_DESC_ADDR, EDGE_MANUF_DESC_LEN)) == NULL) {

		return (USB_FAILURE);
	}

	data_len = MBLKL(data);
	rval = (data_len == EDGE_MANUF_DESC_LEN) ? USB_SUCCESS : USB_FAILURE;

	if (rval == USB_SUCCESS) {
		(void) usb_parse_data(EDGE_MANUF_DESC_FORMAT,
		    data->b_rptr, data_len, descr, EDGE_MANUF_DESC_LEN);

		edgesp_dump_mfg_descr(descr, esp->es_lh);
	} else {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgesp_read_mfg_descr: expect read: %ld got: %d",
		    (long)EDGE_MANUF_DESC_LEN, data_len);
	}
	freemsg(data);

	return (rval);
}


/*
 * Read the boot descriptor
 */
static int
edgesp_read_boot_descr(edge_state_t *esp, edge_boot_descriptor_t *descr)
{
	mblk_t	*data;
	int	data_len;
	int	rval;

	/* read in the manufacturing descriptor from ROM */
	if ((data = edgesp_read_memory(esp, USB_REQUEST_ION_READ_ROM,
	    (uint16_t)EDGE_BOOT_DESC_ADDR, EDGE_BOOT_DESC_LEN)) == NULL) {

		return (USB_FAILURE);
	}

	data_len = MBLKL(data);
	rval = (data_len == EDGE_BOOT_DESC_LEN) ? USB_SUCCESS : USB_FAILURE;

	if (rval == USB_SUCCESS) {
		(void) usb_parse_data(EDGE_BOOT_DESC_FORMAT,
		    data->b_rptr, data_len, descr, EDGE_BOOT_DESC_LEN);

		edgesp_dump_boot_descr(descr, esp->es_lh);
	} else {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgesp_read_mfg_descr: expect read: %ld got: %d",
		    (long)EDGE_BOOT_DESC_LEN, data_len);
	}
	freemsg(data);

	return (rval);
}


/*
 * Download the runtime code to the device memory
 */
static int
edgesp_download_code(edge_state_t *esp, uint16_t cmd,
	edge_fw_image_record_t *fw_image, uint16_t fw_image_size)
{
	edge_fw_image_record_t fw_rec;
	uchar_t	*image = (uchar_t *)fw_image;
	uchar_t	*image_end = image + fw_image_size;

	while (image < image_end) {
		/* parse image record */
		(void) usb_parse_data(EDGE_FW_IMAGE_RECORD_FORMAT,
		    image, _PTRDIFF(image_end, image),
		    &fw_rec, sizeof (edge_fw_image_record_t));

		USB_DPRINTF_L4(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgesp_download_code: fw_image: %p, ExtAddr: %x, Addr: %x,"
		    " Len: %d", (void *)image, fw_rec.ExtAddr, fw_rec.Addr,
		    fw_rec.Len);

		image += sizeof (edge_fw_image_record_t);

		/* write this part of image to the device */
		if (edgesp_write_memory(esp, cmd,
		    fw_rec.Addr, fw_rec.Len, image) != USB_SUCCESS) {
			/* failed in the middle of download - reset */
			(void) edgesp_reset_device(esp);

			return (USB_FAILURE);
		}

		image += fw_rec.Len;
	}

	return (USB_SUCCESS);
}


/*
 * Start executing the downloaded firmware on the device
 */
static int
edgesp_exec_download_code(edge_state_t *esp, uint32_t addr)
{
	int		rval;
	usb_ctrl_setup_t setup = { EDGE_RQ_WRITE_DEV,
	    USB_REQUEST_ION_EXEC_DL_CODE, 0, 0, 0, 0 };
	usb_cb_flags_t	cb_flags;
	usb_cr_t	cr;

	setup.wValue = (uint16_t)addr;		/* 16 lo bits of address */
	setup.wIndex = addr >> 16;	/* 16 hi bits of address */

	rval = usb_pipe_ctrl_xfer_wait(esp->es_def_pipe.pipe_handle, &setup,
	    NULL, &cr, &cb_flags, 0);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, esp->es_lh,
	    "edgesp_exec_download_code: rval=%d, cr=%d", rval, cr);

	return (rval);
}


/*
 * set suspend feature on or off
 */
static int
edgesp_set_suspend_feature(edge_state_t *esp, boolean_t on)
{
	int		rval;
	usb_ctrl_setup_t setup = { EDGE_RQ_WRITE_DEV,
	    USB_REQUEST_ION_ENABLE_SUSPEND, 0, 0, 0, 0 };
	usb_cb_flags_t	cb_flags;
	usb_cr_t	cr;

	setup.wValue = on ? 1 : 0;

	rval = usb_pipe_ctrl_xfer_wait(esp->es_def_pipe.pipe_handle, &setup,
	    NULL, &cr, &cb_flags, 0);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, esp->es_lh,
	    "edgesp_set_suspend_feature: on=%d rval=%d, cr=%d", on, rval, cr);

	return (rval);
}


/*
 * dump manufacturing descriptor
 */
static void
edgesp_dump_mfg_descr(edge_manuf_descriptor_t *descr, usb_log_handle_t lh)
{
	char	s[128];
	int	i;

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "RootDescTable: ");

	*s = '\0';
	for (i = 0; i < 0x10; i++) {
		(void) sprintf(s + strlen(s), "%5x", descr->RootDescTable[i]);
		if (strlen(s) > 60) {
			USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
			*s = '\0';
		}
	}

	if (strlen(s)) {
		USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
	}

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "DescriptorArea: ");

	*s = '\0';
	for (i = 0; i < 0x2E0; i++) {
		(void) sprintf(s + strlen(s), "%3x", descr->DescriptorArea[i]);
		if (strlen(s) > 60) {
			USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
			*s = '\0';
		}
	}

	if (strlen(s)) {
		USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
	}

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "Length: %d DescType: %d DescVer: %d",
	    descr->Length, descr->DescType, descr->DescVer);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "NumRootDescEntries: %d RomSize: %x RamSize: %x",
	    descr->NumRootDescEntries, descr->RomSize, descr->RamSize);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "CpuRev: %d BoardRev: %d NumPorts: %d",
	    descr->CpuRev, descr->BoardRev, descr->NumPorts);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "DescDate: %d/%d/%d SerNumLength: %d SerNumDescType: %d",
	    descr->DescDate[0], descr->DescDate[1], descr->DescDate[2],
	    descr->SerNumLength, descr->SerNumDescType);

	*s = '\0';
	for (i = 0; i < MAX_SERIALNUMBER_LEN; i++) {
		(void) sprintf(s + strlen(s), "%5x", descr->SerialNumber[i]);
	}
	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "SerialNumber: %s", s);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "AssemblyNumLength: %d AssemblyNumDescType: %d",
	    descr->AssemblyNumLength, descr->AssemblyNumDescType);

	*s = '\0';
	for (i = 0; i < MAX_ASSEMBLYNUMBER_LEN; i++) {
		(void) sprintf(s + strlen(s), "%5x", descr->AssemblyNumber[i]);
	}

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "AssemblyNumber: %s", s);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "OemAssyNumLength: %d OemAssyNumDescType: %d",
	    descr->OemAssyNumLength, descr->OemAssyNumDescType);

	*s = '\0';
	for (i = 0; i < MAX_ASSEMBLYNUMBER_LEN; i++) {
		(void) sprintf(s + strlen(s), "%5x", descr->OemAssyNumber[i]);
	}
	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "OemAssyNumber: %s", s);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "ManufDateLength: %d ManufDateDescType: %d",
	    descr->ManufDateLength, descr->ManufDateDescType);

	*s = '\0';
	for (i = 0; i < 6; i++) {
		(void) sprintf(s + strlen(s), "%5x", descr->ManufDate[i]);
	}
	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "ManufDate: %s", s);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "UartType: %x IonPid: %x IonConfig: %x",
	    descr->UartType, descr->IonPid, descr->IonConfig);

}


/*
 * dump boot descriptor
 */
static void
edgesp_dump_boot_descr(edge_boot_descriptor_t *descr,
	usb_log_handle_t lh)
{
	char	s[128];
	int	i;

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "Length: %d DescType: %d DescVer: %d Reserved1: %x",
	    descr->Length, descr->DescType, descr->DescVer, descr->Reserved1);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "BootCodeLength: %d MajorVersion: %d MinorVersion: %d "
	    "BuildNumber: %d", descr->BootCodeLength,
	    descr->MajorVersion, descr->MinorVersion, descr->BuildNumber);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "EnumRootDescTable: %d NumDescTypes: %d Reserved4: %x "
	    "Capabilities: %x", descr->EnumRootDescTable,
	    descr->NumDescTypes, descr->Reserved4, descr->Capabilities);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "Reserved2: ");
	*s = '\0';
	for (i = 0; i < 0x28; i++) {
		(void) sprintf(s + strlen(s), "%5x", descr->Reserved2[i]);
		if (strlen(s) > 60) {
			USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
			*s = '\0';
		}
	}

	if (strlen(s)) {
		USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
	}

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh,
	    "UConfig0: %d UConfig1: %d", descr->UConfig0, descr->UConfig1);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, "Reserved3: ");
	*s = '\0';
	for (i = 0; i < 6; i++) {
		(void) sprintf(s + strlen(s), "%3x", descr->Reserved3[i]);
		if (strlen(s) > 60) {
			USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
			*s = '\0';
		}
	}

	if (strlen(s)) {
		USB_DPRINTF_L4(DPRINT_DEF_PIPE, lh, s);
	}
}


/*
 * Issue a vendor specific RESET_DEVICE command to the device.
 * This causes soft reset back to boot code, USB addrs is unchanged
 */
int
edgesp_reset_device(edge_state_t *esp)
{
	int		rval;
	usb_ctrl_setup_t setup = { EDGE_RQ_WRITE_DEV,
	    USB_REQUEST_ION_RESET_DEVICE, 0, 0, 0, 0 };
	usb_cb_flags_t	cb_flags;
	usb_cr_t	cr;

	rval = usb_pipe_ctrl_xfer_wait(esp->es_def_pipe.pipe_handle, &setup,
	    NULL, &cr, &cb_flags, 0);

	USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh, "edgesp_reset_device:"
	    " rval = %d, cr = %d", rval, cr);

	return (rval);
}


/*
 * get all descriptors we are interested in
 */
int
edgesp_get_descriptors(edge_state_t *esp)
{
	int	rval;

	rval = edgesp_read_mfg_descr(esp, &esp->es_mfg_descr);

	if (rval == USB_SUCCESS) {
		rval = edgesp_read_boot_descr(esp, &esp->es_boot_descr);
	}

	return (rval);
}


/*
 * based on device descriptor, setup firmware image pointers
 */
int
edgesp_setup_firmware(edge_state_t *esp)
{
	usb_dev_descr_t	*usb_dev_descr = esp->es_dev_data->dev_descr;

	USB_DPRINTF_L4(DPRINT_ATTACH, esp->es_lh,
	    "edgesp_setup_firmware: idProduct = %x", usb_dev_descr->idProduct);

	mutex_enter(&esp->es_mutex);
	if (usb_dev_descr->idProduct & ION_DEVICE_ID_GENERATION_2) {
		USB_DPRINTF_L4(DPRINT_ATTACH, esp->es_lh,
		    "edgesp_setup_firmware: Generation 2 device");

		esp->es_fw.fw_down_image =
		    (edge_fw_image_record_t *)edge_fw_down_g2_image;
		esp->es_fw.fw_down_image_size = edge_fw_down_g2_image_size;
		esp->es_fw.fw_down_version = &edge_fw_down_g2_version;
	} else {
		USB_DPRINTF_L4(DPRINT_ATTACH, esp->es_lh,
		    "edgesp_setup_firmware: Generation 1 device");

		esp->es_fw.fw_down_image =
		    (edge_fw_image_record_t *)edge_fw_down_image;
		esp->es_fw.fw_down_image_size = edge_fw_down_image_size;
		esp->es_fw.fw_down_version = &edge_fw_down_version;
	}
	mutex_exit(&esp->es_mutex);

	return (USB_SUCCESS);
}


/*
 * make device ready for normal use
 */
int
edgesp_activate_device(edge_state_t *esp)
{
	int	rval;

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, esp->es_lh, "edgesp_activate_device");

	/* download run-time code */
	if ((rval = edgesp_download_code(esp, USB_REQUEST_ION_WRITE_RAM,
	    esp->es_fw.fw_down_image,
	    esp->es_fw.fw_down_image_size)) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgesp_activate_device: download_code fail (%d)", rval);

		return (rval);
	}

	/* transfer control to the donwloaded code */
	if ((rval = edgesp_exec_download_code(esp,
	    OPERATIONAL_EXEC_ADDRESS)) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgesp_activate_device: exec_download_code fail (%d)",
		    rval);

		return (rval);
	}

	/* disable suspend feature */
	if ((rval = edgesp_set_suspend_feature(esp, B_FALSE)) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgesp_activate_device: set_suspend fail (%d)", rval);

		return (rval);
	}

	return (USB_SUCCESS);
}


/*
 * TI routines
 * -----------
 *
 * return TRUE if device is TI-based
 */
boolean_t
edgeti_is_ti(usb_client_dev_data_t *dev_data)
{
	return (ION_PID_IS_TI(dev_data->dev_descr->idProduct));
}


/* return TRUE if device is in boot mode */
boolean_t
edgeti_is_boot(usb_client_dev_data_t *dev_data)
{
	usb_if_descr_t	*if_descr;

	if_descr = &dev_data->dev_cfg[0].cfg_if->if_alt[0].altif_descr;

	return (if_descr->bNumEndpoints == 1);
}


/*
 * Determine if TI Edgeport has type II or type III i2c
 */
void
edgeti_boot_determine_i2c_type(edgeti_boot_t *etp)
{
	usb_ctrl_setup_t setup = { EDGE_RQ_READ_DEV, 0, 0, 0, 0, 0 };
	mblk_t		*data = NULL;
	usb_cb_flags_t	cb_flags;
	usb_cr_t	cr;
	int		rval;

	setup.bRequest = UMPC_MEMORY_READ;
	setup.wValue = DTK_ADDR_SPACE_I2C_TYPE_II;
	setup.wIndex = 0;
	setup.wLength = 1;

	/* Try reading type II */
	rval = usb_pipe_ctrl_xfer_wait(etp->et_def_pipe.pipe_handle, &setup,
	    &data, &cr, &cb_flags, 0);

	if ((!rval) && (data->b_rptr[0] == 0x52)) {
		etp->et_i2c_type = DTK_ADDR_SPACE_I2C_TYPE_II;
		freemsg(data);

		return;
	}

	/* Try reading type III */
	freemsg(data);
	data = NULL;
	setup.wValue = DTK_ADDR_SPACE_I2C_TYPE_III;
	rval = usb_pipe_ctrl_xfer_wait(etp->et_def_pipe.pipe_handle, &setup,
	    &data, &cr, &cb_flags, 0);

	if ((!rval) && (data->b_rptr[0] == 0x52)) {
		etp->et_i2c_type = DTK_ADDR_SPACE_I2C_TYPE_III;
		freemsg(data);

		return;
	}

	freemsg(data);
}


static mblk_t *
edgeti_read_boot_memory(edge_pipe_t *pipe, uint16_t addr, uint16_t len,
	uint16_t mtype)
{
	return (edge_read_memory(pipe, addr, len, mtype, 1, UMPC_MEMORY_READ,
	    EDGE_RW_ADDR_INDEX | EDGE_RW_EXT_VALUE));
}


static mblk_t *
edgeti_read_download_memory(edge_pipe_t *pipe, uint16_t addr, uint16_t len,
	uint16_t mtype)
{
	/* addr must be little endian */
	return (edge_read_memory(pipe, LE_16(addr), len, mtype, 64,
	    UMPC_MEMORY_READ, EDGE_RW_ADDR_INDEX | EDGE_RW_EXT_VALUE));
}


mblk_t *
edgeti_read_ram(edge_pipe_t *pipe, uint16_t addr, uint16_t len)
{
	return (edgeti_read_download_memory(pipe, addr, len,
	    DTK_ADDR_SPACE_XDATA));
}


mblk_t *
edgeti_read_rom(edge_pipe_t *pipe, uint16_t addr, uint16_t len)
{
	if (pipe->pipe_esp) {

		return (edgeti_read_download_memory(pipe, addr, len,
		    pipe->pipe_esp->es_i2c_type));
	} else {
		/* must be in boot mode */
		ASSERT(pipe->pipe_etp != NULL);

		return (edgeti_read_boot_memory(pipe, addr, len,
		    pipe->pipe_etp->et_i2c_type));
	}
}


/* Read a descriptor header from I2C based on type */
static uint16_t
edgeti_get_descr_addr(edge_pipe_t *pipe, int desc_type, edgeti_i2c_desc_t *desc)
{
	uint16_t	addr = 2;
	mblk_t		*bp;

	do {
		if (pipe->pipe_esp) {
			/* Download mode */
			bp = edgeti_read_rom(pipe, BE_16(addr),
			    TI_I2C_DESC_LEN);
		} else {
			bp = edgeti_read_rom(pipe, addr, TI_I2C_DESC_LEN);
		}

		if (bp == NULL) {

			return (0);
		}
		if (MBLKL(bp) != TI_I2C_DESC_LEN) {
			freemsg(bp);

			return (0);
		}

		(void) usb_parse_data(TI_I2C_DESC_FORMAT, bp->b_rptr,
		    TI_I2C_DESC_LEN, desc, sizeof (edgeti_i2c_desc_t));
		freemsg(bp);

		USB_DPRINTF_L4(DPRINT_DEF_PIPE, pipe->pipe_lh,
		    "edgeti_get_descr_addr() [%d]: type=%x size=%x cs=%x\n",
		    addr, desc->Type, desc->Size, desc->CheckSum);

		if (desc->Type == desc_type) {

			return (addr);
		}

		addr += TI_I2C_DESC_LEN + desc->Size;
	} while ((addr < TI_MAX_I2C_SIZE) && (desc->Type != 0));

	return (0);
}


/* Validate descriptor checksum */
static int
edgeti_valid_checksum(edgeti_i2c_desc_t *desc, uint8_t *buf)
{
	uint16_t	i;
	uint8_t		cs = 0;

	/*
	 * descriptor always fails checksum, ignore for now
	 */
	if (desc->Type == TI_I2C_DESC_TYPE_STRING) {

		return (USB_SUCCESS);
	}

	for (i = 0; i < (uint8_t)desc->Size; i++) {
		cs += buf[i];
	}

	return (((uint8_t)cs == desc->CheckSum) ? USB_SUCCESS : USB_FAILURE);
}


/*
 * read manufacturing descriptor
 */
int
edgeti_read_mfg_descr(edge_pipe_t *pipe, edgeti_manuf_descriptor_t *desc)
{
	int		rval;
	int		addr;
	edgeti_i2c_desc_t rom_desc;
	mblk_t		*bp;

	addr = edgeti_get_descr_addr(pipe, TI_I2C_DESC_TYPE_ION, &rom_desc);
	if (addr == 0) {

		return (USB_FAILURE);
	}

	if (pipe->pipe_esp) {
		/* In download mode */
		bp = edgeti_read_rom(pipe, BE_16(addr + TI_I2C_DESC_LEN),
		    rom_desc.Size);
	} else {
		bp = edgeti_read_rom(pipe, addr + TI_I2C_DESC_LEN,
		    rom_desc.Size);
	}

	if (bp == NULL) {

		return (USB_FAILURE);
	}
	if (MBLKL(bp) != TI_MANUF_DESC_LEN) {
		freemsg(bp);

		return (USB_FAILURE);
	}

	(void) usb_parse_data(TI_MANUF_DESC_FORMAT, bp->b_rptr,
	    TI_MANUF_DESC_LEN, desc, TI_MANUF_DESC_LEN);

	rval = edgeti_valid_checksum(&rom_desc, bp->b_rptr);
	freemsg(bp);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, pipe->pipe_lh,
	    "edgeti_read_mfg_descr: %x %x %x %x %x %x\n",
	    desc->IonConfig, desc->Version, desc->CpuRev_BoardRev,
	    desc->NumPorts, desc->NumVirtualPorts, desc->TotalPorts);

	return (rval);
}


/*
 * Validate i2c's image by checksumming device descriptors
 */
int
edgeti_validate_i2c_image(edge_pipe_t *pipe)
{
	int		rval;
	mblk_t		*bp;
	uint16_t	addr = 2;
	edgeti_i2c_desc_t rom_desc;

	/* Read the first byte (Signature0) must be 0x52 */
	if ((bp = edgeti_read_rom(pipe, 0, 1)) == NULL) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
		    "edgeti_validate_i2c_image: err read Signature0");

		return (USB_FAILURE);
	}
	if (bp->b_rptr[0] != 0x52) {
		/* A 3410 based TI edgeport returns '0x10' as its first byte */
		if (bp->b_rptr[0] != 0x10) {
			USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
			    "edgeti_validate_i2c_image: wrong Sig0 %x",
			    bp->b_rptr[0]);
			freemsg(bp);

			return (USB_FAILURE);
		}
	}

	/* Validate the I2C */
	freemsg(bp);
	do {
		bp = edgeti_read_rom(pipe, BE_16(addr), TI_I2C_DESC_LEN);
		if (bp == NULL) {
			USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
			    "edgeti_validate_i2c_image: err read rom_desc");

			return (USB_FAILURE);
		}

		(void) usb_parse_data(TI_I2C_DESC_FORMAT, bp->b_rptr,
		    TI_I2C_DESC_LEN, &rom_desc, sizeof (edgeti_i2c_desc_t));
		freemsg(bp);

		if ((addr + TI_I2C_DESC_LEN + rom_desc.Size) >
		    TI_MAX_I2C_SIZE) {
			USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
			    "edgeti_validate_i2c_image: size too big %x",
			    rom_desc.Size);
			freemsg(bp);

			return (USB_FAILURE);
		}

		/* Skip type 2 record */
		if ((rom_desc.Type & 0x0f) == TI_I2C_DESC_TYPE_FIRMWARE_BASIC) {
			addr += TI_I2C_DESC_LEN + rom_desc.Size;
		} else {
			/* Read the descriptor data */
			addr += TI_I2C_DESC_LEN;
			bp = edgeti_read_rom(pipe, BE_16(addr), rom_desc.Size);
			if (bp == NULL) {
				USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
				    "edgeti_validate_i2c_image: err read desc");

				return (USB_FAILURE);
			}

			rval = edgeti_valid_checksum(&rom_desc, bp->b_rptr);
			if (rval != USB_SUCCESS) {
				USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
				    "edgeti_validate_i2c_image: err cksum");
				freemsg(bp);

				return (rval);
			}
			freemsg(bp);
			addr += rom_desc.Size;
		}
	} while ((rom_desc.Type != TI_I2C_DESC_TYPE_ION) &&
	    (addr < TI_MAX_I2C_SIZE));

	if ((rom_desc.Type != TI_I2C_DESC_TYPE_ION) ||
	    (addr > TI_MAX_I2C_SIZE)) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, pipe->pipe_lh,
		    "edgeti_validate_i2c_image: end err type=%x addr=%x\n",
		    rom_desc.Type, addr);

		return (USB_FAILURE);
	} else {

		return (USB_SUCCESS);
	}
}


/*
 * Download the runtime code to the device memory
 */
int
edgeti_download_code(dev_info_t *dip, usb_log_handle_t lh,
    usb_pipe_handle_t pipe_handle)
{
	uchar_t		*image = (uchar_t *)edge_fw_down_ti_image;
	uchar_t		*image_end = image + edge_fw_down_ti_image_size;
	int		len;
	mblk_t		*bp;
	usb_bulk_req_t	*req;
	int		rval;

	if ((bp = allocb(EDGE_FW_BULK_MAX_PACKET_SIZE, BPRI_LO)) == NULL) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, lh,
		    "edgeti_download_code: allocb failed\n");

		return (USB_FAILURE);
	}

	req = usb_alloc_bulk_req(dip, 0, USB_FLAGS_SLEEP);

	req->bulk_timeout = EDGE_BULK_TIMEOUT;
	req->bulk_attributes = USB_ATTRS_AUTOCLEARING;

	while (image < image_end) {
		len = (int)min(EDGE_FW_BULK_MAX_PACKET_SIZE,
		    _PTRDIFF(image_end, image));

		bcopy(image, bp->b_rptr, len);
		bp->b_wptr = bp->b_rptr + len;
		req->bulk_len = len;
		req->bulk_data = bp;

		if ((rval = usb_pipe_bulk_xfer(pipe_handle,
		    req, USB_FLAGS_SLEEP)) != USB_SUCCESS) {
			USB_DPRINTF_L2(DPRINT_DEF_PIPE, lh,
			    "edgeti_download_code: xfer failed %d %x\n",
			    req->bulk_completion_reason,
			    req->bulk_cb_flags);
			break;
		}
		image += len;
	}

	usb_free_bulk_req(req);

	return (rval);
}


int
edgeti_get_descriptors(edge_state_t *esp)
{
	return (edgeti_read_mfg_descr(&esp->es_def_pipe,
	    &esp->es_ti_mfg_descr));
}
