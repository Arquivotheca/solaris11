/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Solaris x86 Toshiba Hotkey support
 */
#include <sys/hotkey_drv.h>

/*
 * Toshiba HCI/SPFC interface definitions
 */
#define	TSH_HOTKEY_HCI_1	"\\_SB_.VALZ"
#define	TSH_HOTKEY_HCI_2	"\\_SB_.VALD"
#define	TSH_HOTKEY_HID		"TOS6208"
#define	TSH_HOTKEY_ODM_HID	"TOS1900"	/* ODM models */
#define	ACPI_METHOD_HCI		"GHCI"
#define	ACPI_METHOD_SPFC	"SPFC"
#define	ACPI_METHOD_ENAB	"ENAB"
#define	ACPI_METHOD_INFO	"INFO"

#define	TSH_NOTIFY_EVENT	0x80

/*
 * This object is used for backlight on/off. It's also called Hot start (HS).
 */
#define	TSH_HS86		"\\_SB_.HS86"

/*
 * HCI/SPFC register definitions
 */
#define	TSH_METHOD_WORDS	6		/* Number of registers */
#define	METHOD_REG_AX		0		/* Arg 0 */
#define	METHOD_REG_BX		1		/* Arg 1 */
#define	METHOD_REG_CX		2		/* Arg 2 */
#define	METHOD_REG_DX		3		/* Arg 3 */
#define	METHOD_REG_SI		4		/* Arg 4 */
#define	METHOD_REG_DI		5		/* Arg 5 */

/* Operations: arg0 -> [AX] */
#define	HCI_GET			0xFE44
#define	HCI_SET			0xFF44
#define	SCI_GET			0xF344
#define	SCI_SET			0xF444
#define	SCI_OPEN		0xF144
#define	SCI_CLOSE		0xF244
#define	SPFC_GET		0xFE00
#define	SPFC_SET		0xFF00
#define	SPFC_TPAD_GET		0xF300
#define	SPFC_TPAD_SET		0xF400

/* Functions: arg1-> [BX] */
#define	HCI_SYSTEM_EVENT	0x0016
#define	HCI_LCD_BRIGHTNESS	0x002A
#define	HCI_LCD_BACKLIGHT	0x0005
#define	HCI_WIFI		0x0056
#define	SCI_TPAD		0x050E
#define	SPFC_WIFI		0x0056
#define	SPFC_TPAD		0x050E
#define	SPFC_HOTKEY_EVENT	0x001E

/* Arg2 -> [CX] */
#define	POWER_ON		0x0001
#define	POWER_OFF		0x0000
#define	FUNC_ENABLE		0x0001
#define	FUNC_DISABLE		0x0000

/* Arg3 -> [DX] */
#define	WIFI_RF_POWER		0x0200
#define	WIFI_RF_STAT		0x0001

#define	WIFI_RF_POWER_ON	0x200
#define	WIFI_RF_SWITCH_ON	0x001

/* Hotkey Keycode definition on Toshiba Tecra */
				/* DOWN */	/* UP */
#define	KEY_Fn_Space		0x0139		/* 0x01B9 */
#define	KEY_Fn_Esc		0x0101		/* 0x0181 */
#define	KEY_Fn_Tab		0x010F		/* 0x018F */
#define	KEY_Fn_1		0x0102		/* 0x0182 */
#define	KEY_Fn_2		0x0103		/* 0x0183 */
#define	KEY_Fn_F1		0x013B		/* 0x01BB */
#define	KEY_Fn_F2		0x013C		/* 0x01BC */
#define	KEY_Fn_F3		0x013D		/* 0x01BD */
#define	KEY_Fn_F4		0x013E		/* 0x01BE */
#define	KEY_Fn_F5		0x013F		/* 0x01BF */
#define	KEY_Fn_F6		0x0140		/* 0x01C0 */
#define	KEY_Fn_F7		0x0141		/* 0x01C1 */
#define	KEY_Fn_F8		0x0142		/* 0x01C2 */
#define	KEY_Fn_F9		0x0143		/* 0x01C3 */
#define	KEY_Fn_Only		0x01FF		/* 0x0100 */
#define	KEY_Fn_None		0x0000		/* 0x0000 */

/* Local variables */
static boolean_t tsh_odm = B_FALSE;
/* Disable ODM hotkey method support */
boolean_t tsh_odm_enable = B_FALSE;
/* Disable WIFI hotkey handling by default due to lack of GUI support */
boolean_t tsh_wifi_enable = B_FALSE;

boolean_t tsh_backlight = B_TRUE;

static struct acpi_drv_dev hs86_dev;


static ACPI_STATUS
tsh_method_set(ACPI_HANDLE hdl, char *acpi_method, int func_ax, int func_bx,
    int arg_cx, int arg_dx)
{
	ACPI_OBJECT_LIST hci_list;
	ACPI_OBJECT method[TSH_METHOD_WORDS];
	ACPI_BUFFER results;
	ACPI_OBJECT *out_obj;
	ACPI_STATUS status = AE_ERROR;
	int i, ret;

	for (i = 0; i < TSH_METHOD_WORDS; i++) {
		method[i].Type = ACPI_TYPE_INTEGER;
		method[i].Integer.Value = 0;
	}

	method[METHOD_REG_AX].Integer.Value = func_ax;
	method[METHOD_REG_BX].Integer.Value = func_bx;
	method[METHOD_REG_CX].Integer.Value = arg_cx;
	method[METHOD_REG_DX].Integer.Value = arg_dx;

	hci_list.Count = TSH_METHOD_WORDS;
	hci_list.Pointer = method;
	results.Pointer = NULL;
	results.Length = ACPI_ALLOCATE_BUFFER;
	if (ACPI_FAILURE(status = AcpiEvaluateObjectTyped(hdl, acpi_method,
	    &hci_list, &results, ACPI_TYPE_PACKAGE)))
		goto end;

	out_obj = (ACPI_OBJECT *)results.Pointer;
	if (out_obj == NULL || out_obj->Type != ACPI_TYPE_PACKAGE ||
	    out_obj->Package.Count < TSH_METHOD_WORDS) {
		if (hotkey_drv_debug) {
			cmn_err(CE_WARN, "tsh_method_set: bad obj return "
			    "from AcpiEvaluateObject\n");
		}
		goto end;
	}
	ret = out_obj->Package.Elements[METHOD_REG_AX].Integer.Value;
	if (ret & 0xff00) {
		cmn_err(CE_NOTE, "!tsh_method_set failed: func_ax %x, "
		    "func_bx %x, arg_cx %x, arg_dx %x, ret %x", func_ax,
		    func_bx, arg_cx, arg_dx, ret);
	} else {
		status = AE_OK;
		if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
			cmn_err(CE_NOTE, "!tsh_method_set succeeded: "
			    "func_ax %x, func_bx %x, arg_cx %x, arg_dx %x,"
			    "ret %x", func_ax, func_bx, arg_cx, arg_dx, ret);
		}
	}

end:
	if (results.Pointer != NULL) {
		AcpiOsFree(results.Pointer);
	}

	return (status);
}

static ACPI_STATUS
tsh_method_get(ACPI_HANDLE hdl, char *acpi_method, int func_ax, int func_bx,
    int *out_cx, int arg_dx)
{
	ACPI_OBJECT_LIST hci_list;
	ACPI_OBJECT method[TSH_METHOD_WORDS];
	ACPI_BUFFER results;
	ACPI_OBJECT *out_obj;
	ACPI_STATUS status = AE_ERROR;
	int i, ret;

	for (i = 0; i < TSH_METHOD_WORDS; i++) {
		method[i].Type = ACPI_TYPE_INTEGER;
		method[i].Integer.Value = 0;
	}

	method[METHOD_REG_AX].Integer.Value = func_ax;
	method[METHOD_REG_BX].Integer.Value = func_bx;
	method[METHOD_REG_DX].Integer.Value = arg_dx;

	hci_list.Count = TSH_METHOD_WORDS;
	hci_list.Pointer = method;
	results.Pointer = NULL;
	results.Length = ACPI_ALLOCATE_BUFFER;
	if (ACPI_FAILURE(status = AcpiEvaluateObjectTyped(hdl, acpi_method,
	    &hci_list, &results, ACPI_TYPE_PACKAGE))) {
		cmn_err(CE_NOTE, "!tsh_method_get failed "
		    "AcpiEvaluateObjectTyped failed status %d", status);
		goto end;
	}
	out_obj = (ACPI_OBJECT *)results.Pointer;
	if (out_obj == NULL || out_obj->Type != ACPI_TYPE_PACKAGE ||
	    out_obj->Package.Count < TSH_METHOD_WORDS) {
		cmn_err(CE_WARN, "!tsh_method_get: bad obj return from "
		    "AcpiEvaluateObject\n");
		goto end;
	}
	ret = out_obj->Package.Elements[METHOD_REG_AX].Integer.Value;
	if (ret & 0xFF00) {
		cmn_err(CE_NOTE, "!tsh_method_get failed: func_ax %x, "
		    "func_bx %x, arg_dx %x, ret %x", func_ax, func_bx, arg_dx,
		    ret);
	} else {
		if (out_cx != NULL) {
			*out_cx = out_obj->Package.Elements[METHOD_REG_CX].
			    Integer.Value;
		}
		if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
			cmn_err(CE_NOTE, "!tsh_method_get succeeded: "
			    "func_ax %x, func_bx %x, ret %x, [CX] %x, [DX] %x",
			    func_ax, func_bx, ret,
			    (int)out_obj->Package.Elements[METHOD_REG_CX].
			    Integer.Value,
			    (int)out_obj->Package.Elements[METHOD_REG_DX].
			    Integer.Value);
		}
		status = AE_OK;
	}

end:
	if (results.Pointer != NULL) {
		AcpiOsFree(results.Pointer);
	}

	return (status);
}


/* Touchpad hotkey handing by using HCI method */
static int
tsh_hci_tpad(ACPI_HANDLE hdl)
{
	int tpad_status = 0;

	if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_HCI, SCI_OPEN, 0,
	    0, 0))) {
		return (HOTKEY_DRV_ERR);
	}

	if (ACPI_FAILURE(tsh_method_get(hdl, ACPI_METHOD_HCI, SCI_GET, SCI_TPAD,
	    &tpad_status, 0))) {
		return (HOTKEY_DRV_ERR);
	}

	/* Touchpad is enabled */
	if (tpad_status) {
		/* Now disable it */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_HCI, SCI_SET,
		    SCI_TPAD, FUNC_DISABLE, 0))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: TouchPad OFF [Fn+F9].");

	} else {
		/* Now enable it */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_HCI, SCI_SET,
		    SCI_TPAD, FUNC_ENABLE, 0))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: TouchPad ON [Fn+F9].");
	}
	(void) tsh_method_set(hdl, ACPI_METHOD_HCI, SCI_CLOSE, 0, 0, 0);

	return (HOTKEY_DRV_OK);
}

/* WIFI hotkey handing by using HCI method */
static int
tsh_hci_wifi(ACPI_HANDLE hdl)
{
	int wifi_status = 0;

	if (ACPI_FAILURE(tsh_method_get(hdl, ACPI_METHOD_HCI, HCI_GET, HCI_WIFI,
	    &wifi_status, WIFI_RF_STAT))) {
		return (HOTKEY_DRV_ERR);
	}

	/* check if the RF switch is OFF, if yes, just return. */
	if (!(wifi_status & WIFI_RF_SWITCH_ON)) {
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: WIFI RF switch is OFF."
		    " Please switch it ON first.");
		return (HOTKEY_DRV_OK);
	}

	/* RF is ON */
	if (wifi_status & WIFI_RF_POWER_ON) {
		/* turn off RF */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_HCI, HCI_SET,
		    HCI_WIFI, POWER_OFF, WIFI_RF_POWER))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: WIFI Disabled [Fn+F8].");
	} else {
		/* turn on RF */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_HCI, HCI_SET,
		    HCI_WIFI, POWER_ON, WIFI_RF_POWER))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: WIFI Enabled [Fn+F8].");
	}

	return (HOTKEY_DRV_OK);
}

/* Touchpad hotkey handing by using SPFC method */
static int
tsh_spfc_tpad(ACPI_HANDLE hdl)
{
	int tpad_status = 0;

	if (ACPI_FAILURE(tsh_method_get(hdl, ACPI_METHOD_SPFC, SPFC_TPAD_GET,
	    SPFC_TPAD, &tpad_status, 0))) {
		return (HOTKEY_DRV_ERR);
	}

	/* Touchpad is enabled */
	if (tpad_status & FUNC_ENABLE) {
		/* Now disable it */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_SPFC,
		    SPFC_TPAD_SET, SPFC_TPAD, FUNC_DISABLE, 0))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: TouchPad OFF [Fn+F9].");

	} else {
		/* now enable it */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_SPFC,
		    SPFC_TPAD_SET, SPFC_TPAD, FUNC_ENABLE, 0))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: TouchPad ON [Fn+F9].");
	}

	return (HOTKEY_DRV_OK);
}

/* WIFI hotkey handing by using SPFC method */
static int
tsh_spfc_wifi(ACPI_HANDLE hdl)
{
	int wifi_status = 0;

	if (ACPI_FAILURE(tsh_method_get(hdl, ACPI_METHOD_SPFC, SPFC_GET,
	    SPFC_WIFI, &wifi_status, WIFI_RF_STAT))) {
		return (HOTKEY_DRV_ERR);
	}

	/* check if the RF switch is OFF, if yes, just return. */
	if (!(wifi_status & WIFI_RF_SWITCH_ON)) {
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: WIFI RF switch is OFF."
		    " Please switch it ON first.");
		return (HOTKEY_DRV_OK);
	}

	/* RF is ON */
	if (wifi_status & WIFI_RF_POWER_ON) {
		/* turn off RF */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_SPFC, SPFC_SET,
		    SPFC_WIFI, POWER_OFF, WIFI_RF_POWER))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: WIFI Disabled [Fn+F8].");
	} else {
		/* turn on RF */
		if (ACPI_FAILURE(tsh_method_set(hdl, ACPI_METHOD_SPFC, SPFC_SET,
		    SPFC_WIFI, POWER_ON, WIFI_RF_POWER))) {
			return (HOTKEY_DRV_ERR);
		}
		/*
		 * Just log here instead of sysevent as currently no GUI
		 */
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: WIFI Enabled [Fn+F8].");
	}

	return (HOTKEY_DRV_OK);
}

/*ARGSUSED*/
static void
tsh_backlight_control(ACPI_HANDLE hdl, UINT32 notify, void *ctx)
{
	hotkey_drv_t *htkp = ctx;

	if (notify != TSH_NOTIFY_EVENT) {
		return;
	}

	if (tsh_backlight) {
		(void) tsh_method_set(htkp->dev.hdl, ACPI_METHOD_HCI,
		    HCI_SET, HCI_LCD_BACKLIGHT, 0, 0);

		tsh_backlight = B_FALSE;
	} else {
		(void) tsh_method_set(htkp->dev.hdl, ACPI_METHOD_HCI,
		    HCI_SET, HCI_LCD_BACKLIGHT, 1, 0);

		tsh_backlight = B_TRUE;
	}
}

static void
tsh_hotkey_notify(ACPI_HANDLE hdl, UINT32 notify, void *ctx)
{
	int key;
	ACPI_BUFFER results;
	ACPI_OBJECT out;
	ACPI_STATUS status;
	hotkey_drv_t *htkp = ctx;

	if (notify != TSH_NOTIFY_EVENT) {
		cmn_err(CE_NOTE, "!tsh_hotkey_notify: unknown notify event %x",
		    notify);
		return;
	}

	mutex_enter(htkp->hotkey_lock);
	results.Length = sizeof (out);
	results.Pointer = &out;

	/*
	 * Invoke the INFO method repeatedly until it returns an empty
	 * event to ensure all events are returned.
	 */
	/*CONSTANTCONDITION*/
	while (1) {
		if (ACPI_FAILURE(status = AcpiEvaluateObjectTyped(hdl,
		    ACPI_METHOD_INFO, NULL, &results, ACPI_TYPE_INTEGER))) {
			cmn_err(CE_WARN, "!tsh_hotkey_notify: "
			    "Evaluate ACPI_METHOD_INFO failed %x.", status);
			mutex_exit(htkp->hotkey_lock);
			return;
		}
		key = (int)out.Integer.Value;

		if (key == KEY_Fn_None) {	/* Done: no more hotkey event */
			mutex_exit(htkp->hotkey_lock);
			return;
		}

		switch (key) {
		case KEY_Fn_Space:	/* resolution change */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + Space (%04x)", key);
			}
			break;

		case KEY_Fn_Esc:	/* audio mute */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + Esc (%04x)", key);
			}
			/*
			 * Since ODM models cannot generate keyboard scan codes
			 * (via kb8042) for the MUTE key when hotkey event mode
			 * is enabled, we log a sysevent here when we detect the
			 * MUTE button has been pressed so that key event
			 * subscribers can see it.
			 */
			if (tsh_odm) {
				hotkey_drv_gen_sysevent(htkp->dip,
				    ESC_ACPIEV_AUDIO_MUTE);
			}
			break;

		case KEY_Fn_Tab:
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + Tab (%04x)", key);
			}
			break;

		case KEY_Fn_1:		/* zooming - */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + 1 (%04x)", key);
			}
			break;

		case KEY_Fn_2:		/* zooming + */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + 2 (%04x)", key);
			}
			break;

		case KEY_Fn_F1:		/* screen lock */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F1 (%04x)", key);
			}
			hotkey_drv_gen_sysevent(htkp->dip,
			    ESC_ACPIEV_SCREEN_LOCK);
			break;

		case KEY_Fn_F2:		/* power saver - change profile */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F2 (%04x)", key);
			}
			break;

		case KEY_Fn_F3:		/* sleep (suspend to RAM) */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F3 (%04x)", key);
			}
			hotkey_drv_gen_sysevent(htkp->dip, ESC_ACPIEV_SLEEP);
			break;

		case KEY_Fn_F4:		/* hibernation (supspend to DISK) */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F4 (%04x)", key);
			}
			break;

		case KEY_Fn_F5:		/* display switch */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F5 (%04x)", key);
			}
			hotkey_drv_gen_sysevent(htkp->dip,
			    ESC_ACPIEV_DISPLAY_SWITCH);
			break;

		case KEY_Fn_F6:		/* brighness down */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F6 (%04x)", key);
			}
			(void) hotkey_brightness_dec(htkp);
			acpi_drv_gen_sysevent(&htkp->dev,
			    ESC_PWRCTL_BRIGHTNESS_DOWN, 0);
			break;

		case KEY_Fn_F7:		/* brighness up */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F7 (%04x)", key);
			}
			(void) hotkey_brightness_inc(htkp);
			acpi_drv_gen_sysevent(&htkp->dev,
			    ESC_PWRCTL_BRIGHTNESS_UP, 0);
			break;

		case KEY_Fn_F8:		/* WIFI RF ON/OFF */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F8 (%04x)", key);
			}

			if (tsh_wifi_enable) {
				if (tsh_odm)
					(void) tsh_spfc_wifi(hdl);
				else
					(void) tsh_hci_wifi(hdl);
			}
			break;

		case KEY_Fn_F9:		/* touchpad disable/enable */
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn + F9 (%04x)", key);
			}
			if (tsh_odm)
				(void) tsh_spfc_tpad(hdl);
			else
				(void) tsh_hci_tpad(hdl);
			break;

		case KEY_Fn_Only:
			if (hotkey_drv_debug & HOTKEY_DBG_NOTICE) {
				cmn_err(CE_NOTE, "!tsh_hotkey_notify: "
				    "Fn only pressed (%04x)", key);
			}
			break;

		default:
			break;
		}	/* end of switch (key) */
	}	/* end of while (1) */
}

static int
tsh_hotkey_ioctl(struct hotkey_drv *htkp, int cmd, intptr_t arg, int mode,
    cred_t *cr,    int *rval)
{
	if (htkp->acpi_video) {
		return (acpi_video_ioctl(htkp->acpi_video, cmd, arg, mode, cr,
		    rval));
	} else {
		return (ENXIO);
	}
}

static int
tsh_hotkey_fini(hotkey_drv_t *htkp)
{
	(void) AcpiRemoveNotifyHandler(htkp->dev.hdl, ACPI_DEVICE_NOTIFY,
	    tsh_hotkey_notify);

	return (HOTKEY_DRV_OK);
}

static int
tsh_hotkey_init(hotkey_drv_t *htkp)
{
	ACPI_STATUS status;
	ACPI_HANDLE hdl = htkp->dev.hdl;

	/* Execute "ENAB" to enable hotkey method */
	status = AcpiEvaluateObject(hdl, ACPI_METHOD_ENAB, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		cmn_err(CE_WARN, "!tsh_hotkey_init: method enable failed %x",
		    status);
		return (HOTKEY_DRV_ERR);
	}

	/* Set hotkey event mode */
	if (tsh_odm) {
		status = tsh_method_set(hdl, ACPI_METHOD_SPFC, SPFC_SET,
		    SPFC_HOTKEY_EVENT, 1, 0);
	} else {
		status = tsh_method_set(hdl, ACPI_METHOD_HCI, HCI_SET,
		    HCI_SYSTEM_EVENT, 1, 0);
	}
	if (ACPI_FAILURE(status)) {
		cmn_err(CE_WARN, "!tsh_hotkey_init: set event mode failed %x",
		    status);
		return (HOTKEY_DRV_ERR);
	}

	/* Install hotkey event handler */
	status = AcpiInstallNotifyHandler(htkp->dev.hdl, ACPI_DEVICE_NOTIFY,
	    tsh_hotkey_notify, htkp);
	if (ACPI_FAILURE(status)) {
		cmn_err(CE_WARN, "!tsh_hotkey_init: notify handler install "
		    "failed %x", status);
		return (HOTKEY_DRV_ERR);
	}

	htkp->hotkey_method |= HOTKEY_METHOD_VENDOR;
	htkp->vendor_ioctl = tsh_hotkey_ioctl;
	htkp->check_acpi_video = B_TRUE;

	if (hs86_dev.hdl != NULL) {
		(void) AcpiInstallNotifyHandler(hs86_dev.hdl,
		    ACPI_DEVICE_NOTIFY, tsh_backlight_control, htkp);
	}

	return (HOTKEY_DRV_OK);
}

/*ARGSUSED*/
static ACPI_STATUS
tsh_find_hotkey_method(ACPI_HANDLE hdl, UINT32 nest, void *ctx,
    void **rv)
{
	*((ACPI_HANDLE *)ctx) = hdl;

	return (AE_OK);
}

static int
tsh_hotkey_check(hotkey_drv_t *htkp)
{
	ACPI_HANDLE hdl = NULL;

	/* Try to find SPFC method */
	(void) AcpiGetDevices(TSH_HOTKEY_ODM_HID,
	    tsh_find_hotkey_method, &hdl, NULL);
	if (hdl != NULL) {
		tsh_odm = B_TRUE;
		if (tsh_odm_enable == B_FALSE)
			return (HOTKEY_DRV_ERR);
	} else {
		/* Try to find HCI method */
		(void) AcpiGetDevices(TSH_HOTKEY_HID,
		    tsh_find_hotkey_method, &hdl, NULL);
		if (hdl == NULL)
			(void) AcpiGetHandle(NULL, TSH_HOTKEY_HCI_1, &hdl);
		if (hdl == NULL)
			(void) AcpiGetHandle(NULL, TSH_HOTKEY_HCI_2, &hdl);
		if (hdl == NULL)	/* No Toshiba hotkey method is found */
			return (HOTKEY_DRV_ERR);
	}

	htkp->dev.hdl = hdl;
	(void) acpi_drv_dev_init(&htkp->dev);

	hs86_dev.hdl = NULL;
	if (ACPI_SUCCESS(AcpiGetHandle(NULL, TSH_HS86, &hs86_dev.hdl)))
		(void) acpi_drv_dev_init(&hs86_dev);

	return (tsh_hotkey_init(htkp));
}

/*
 * Module linkage information for the kernel.
 */
static struct modlmisc modlmisc = {
	&mod_miscops,
	"acpi_toshiba"
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modlmisc,
	NULL
};

int
_init(void)
{
	int status;

	if (tsh_hotkey_check(&acpi_hotkey) != HOTKEY_DRV_OK) {
		return (-1);
	}

	status = mod_install(&modlinkage);
	if (status != 0) {
		(void) tsh_hotkey_fini(&acpi_hotkey);
	}

	return (status);
}

int
_fini(void)
{
	(void) tsh_hotkey_fini(&acpi_hotkey);

	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
