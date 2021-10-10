/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IPMI_CMD_H
#define	_IPMI_CMD_H


#ifdef __cplusplus
extern "C" {
#endif

#define	IPMI_NETFN_CHASSIS		0x0
#define	IPMI_NETFN_BRIDGE		0x2
#define	IPMI_NETFN_SE			0x4
#define	IPMI_NETFN_APP			0x6
#define	IPMI_NETFN_FIRMWARE		0x8
#define	IPMI_NETFN_STORAGE		0xa
#define	IPMI_NETFN_TRANSPORT		0xc

typedef struct command_priv_level {
	uint8_t	command;
	uint8_t req_level;
} ipmi_command_priv_level_t;

enum {
	IPMI_REQ_NORM,		/* Normal request */
	IPMI_REQ_PRIV,		/* Privileged request */
	IPMI_END_OF_LIST	/* End of command list marker */
};

/*
 * IPMI Command Privilege Level Tables
 *
 * These tables associate a privilege level with each of the supported
 * IPMI commands.
 *
 * IPMI commands are grouped by net function (netFn), and a Command
 * Privilege Level Table is provided for each of the supported net
 * functions.
 *
 * Two levels of privilege are supported: global access, IPMI_REQ_NORM;
 * and sys_admin privilege, IPMI_REQ_PRIV.
 * The assignment of privilege level to command follows the recommendations
 * in the IPMI Specification V2.0, Appendix G.
 */
static ipmi_command_priv_level_t ipmi_netfn_chassis[] = {
	{ 0x00, IPMI_REQ_NORM },	/* Get Chassis Capabilities */
	{ 0x01, IPMI_REQ_NORM },	/* Get Chassis Status */
	{ 0x02, IPMI_REQ_PRIV },	/* Chassis Control */
	{ 0x03, IPMI_REQ_PRIV },	/* Chassis Reset */
	{ 0x04, IPMI_REQ_PRIV },	/* Chassis Identify */
	{ 0x05, IPMI_REQ_PRIV },	/* Set Chassis Capabilities */
	{ 0x06, IPMI_REQ_PRIV },	/* Set Power Restore Policy */
	{ 0x07, IPMI_REQ_NORM },	/* Get System Restart Cause */
	{ 0x08, IPMI_REQ_PRIV },	/* Set System Boot Options */
	{ 0x09, IPMI_REQ_PRIV },	/* Get System Boot Options */
	{ 0x0a, IPMI_REQ_PRIV },	/* Set Front Panel Enables */
	{ 0x0b, IPMI_REQ_PRIV },	/* Set Power Cycle Interval */
	{ 0x0f, IPMI_REQ_NORM },	/* Get POH Counter */
	{ 0xff, IPMI_END_OF_LIST }
};

static ipmi_command_priv_level_t ipmi_netfn_bridge[] = {
	{ 0x00, IPMI_REQ_NORM },	/* Get Bridge State */
	{ 0x01, IPMI_REQ_PRIV },	/* Set Bridge State */
	{ 0x02, IPMI_REQ_NORM },	/* Get ICMB Address */
	{ 0x03, IPMI_REQ_PRIV },	/* Set ICMB Address */
	{ 0x04, IPMI_REQ_PRIV },	/* Set Bridge Proxy Address */
	{ 0x05, IPMI_REQ_NORM },	/* Get Bridge Statistics */
	{ 0x06, IPMI_REQ_NORM },	/* Get ICMB Capabilities */
	{ 0x08, IPMI_REQ_PRIV },	/* Clear Bridge Statistics */
	{ 0x09, IPMI_REQ_NORM },	/* Get Bridge Proxy Address */
	{ 0x0a, IPMI_REQ_NORM },	/* Get ICMB Connector Info */
	{ 0x0b, IPMI_REQ_NORM },	/* Get ICMB Connection ID */
	{ 0x0c, IPMI_REQ_NORM },	/* Send ICMB Connection ID */
	{ 0x10, IPMI_REQ_PRIV },	/* Prepare for Discovery */
	{ 0x11, IPMI_REQ_NORM },	/* Get Addresses */
	{ 0x12, IPMI_REQ_PRIV },	/* Set Discovered */
	{ 0x13, IPMI_REQ_NORM },	/* Get Chassis Device ID */
	{ 0x14, IPMI_REQ_PRIV },	/* Set Chassis Device ID */
	{ 0x20, IPMI_REQ_PRIV },	/* Bridge Request */
	{ 0x21, IPMI_REQ_PRIV },	/* Bridge Message */
	{ 0x30, IPMI_REQ_NORM },	/* Get Event Count */
	{ 0x31, IPMI_REQ_PRIV },	/* Set Event Destination */
	{ 0x32, IPMI_REQ_PRIV },	/* Set Event Reception State */
	{ 0x33, IPMI_REQ_PRIV },	/* Send ICMB Event Message */
	{ 0x34, IPMI_REQ_NORM },	/* Get Event Destination */
	{ 0x35, IPMI_REQ_NORM },	/* Get Event Reception State */
	{ 0xff, IPMI_END_OF_LIST }
};

static ipmi_command_priv_level_t ipmi_netfn_se[] = {
	{ 0x00, IPMI_REQ_PRIV },	/* Set Event Receiver */
	{ 0x01, IPMI_REQ_NORM },	/* Get Event Receiver */
	{ 0x02, IPMI_REQ_PRIV },	/* Platform Event */
	{ 0x10, IPMI_REQ_NORM },	/* Get PEF Capabilities */
	{ 0x11, IPMI_REQ_PRIV },	/* Arm PEF Postpone Timer */
	{ 0x12, IPMI_REQ_PRIV },	/* Set PEF Conf Parameters */
	{ 0x13, IPMI_REQ_PRIV },	/* Get PEF Conf Parameters */
	{ 0x14, IPMI_REQ_PRIV },	/* Set Last Processed Event ID */
	{ 0x15, IPMI_REQ_PRIV },	/* Get Last Processed Event ID */
	{ 0x16, IPMI_REQ_PRIV },	/* Alert Immediate */
	{ 0x17, IPMI_REQ_PRIV },	/* PET Acknowledge */
	{ 0x20, IPMI_REQ_NORM },	/* Get Device SDR Info */
	{ 0x21, IPMI_REQ_NORM },	/* Get Device SDR */
	{ 0x22, IPMI_REQ_NORM },	/* Reserve Device SDR Repository */
	{ 0x23, IPMI_REQ_NORM },	/* Get Sensor Reading Factors */
	{ 0x24, IPMI_REQ_PRIV },	/* Set Sensor Hysteresis */
	{ 0x25, IPMI_REQ_NORM },	/* Get Sensor Hysterisis */
	{ 0x26, IPMI_REQ_PRIV },	/* Set Sensor Threshold */
	{ 0x27, IPMI_REQ_NORM },	/* Get Sensor Threshold */
	{ 0x28, IPMI_REQ_PRIV },	/* Set Sensor Event Enable */
	{ 0x29, IPMI_REQ_NORM },	/* Get Sensor Event Enable */
	{ 0x2a, IPMI_REQ_PRIV },	/* Re-arm Sensor Events */
	{ 0x2b, IPMI_REQ_NORM },	/* Get Sensor Event Status */
	{ 0x2d, IPMI_REQ_NORM },	/* Get Sensor Reading */
	{ 0x2e, IPMI_REQ_PRIV },	/* Set Sensor Type */
	{ 0x2f, IPMI_REQ_NORM },	/* Get Event Reception State */
	{ 0xff, IPMI_END_OF_LIST }
};

static ipmi_command_priv_level_t ipmi_netfn_app[] = {
	{ 0x01, IPMI_REQ_NORM },	/* Get Device ID */
	{ 0x02, IPMI_REQ_PRIV },	/* Cold Reset */
	{ 0x03, IPMI_REQ_PRIV },	/* Warm Reset */
	{ 0x04, IPMI_REQ_NORM },	/* Get Self Test Results */
	{ 0x05, IPMI_REQ_PRIV },	/* Manufacturing Test On */
	{ 0x06, IPMI_REQ_PRIV },	/* Set ACPI Power State */
	{ 0x07, IPMI_REQ_NORM },	/* Get ACPI Power State */
	{ 0x08, IPMI_REQ_NORM },	/* Get Device GUID */
	{ 0x22, IPMI_REQ_PRIV },	/* Reset Watchdog Timer */
	{ 0x24, IPMI_REQ_PRIV },	/* Set Watchdog Timer */
	{ 0x25, IPMI_REQ_NORM },	/* Get Watchdog Timer */
	{ 0x2e, IPMI_REQ_PRIV },	/* Set IPMI Global Enables */
	{ 0x2f, IPMI_REQ_NORM },	/* Get IPMI Global Enables */
	{ 0x30, IPMI_REQ_PRIV },	/* Clear Message Flags */
	{ 0x31, IPMI_REQ_PRIV },	/* Get Message Flags */
	{ 0x32, IPMI_REQ_PRIV },	/* Enable Message Channel Receive */
	{ 0x33, IPMI_REQ_PRIV },	/* Get Message */
	{ 0x34, IPMI_REQ_NORM },	/* Send Message */
	{ 0x35, IPMI_REQ_PRIV },	/* Read Event Message Buffer */
	{ 0x36, IPMI_REQ_NORM },	/* Get BT Interface Capabilities */
	{ 0x37, IPMI_REQ_NORM },	/* Get System GUID */
	{ 0x38, IPMI_REQ_NORM },	/* Get Channel Auth Capabilities */
	{ 0x39, IPMI_REQ_NORM },	/* Get Session Challenge */
	{ 0x3a, IPMI_REQ_NORM },	/* Activate Session */
	{ 0x3b, IPMI_REQ_NORM },	/* Set Session Privilege Level */
	{ 0x3c, IPMI_REQ_PRIV },	/* Close Session */
	{ 0x3d, IPMI_REQ_NORM },	/* Get Session Info */
	{ 0x3f, IPMI_REQ_PRIV },	/* Get Auth Code */
	{ 0x40, IPMI_REQ_PRIV },	/* Set Channel Access */
	{ 0x41, IPMI_REQ_NORM },	/* Get Channel Access */
	{ 0x42, IPMI_REQ_NORM },	/* Get Channel Info Command */
	{ 0x43, IPMI_REQ_PRIV },	/* Set User Access Command */
	{ 0x44, IPMI_REQ_PRIV },	/* Get User Access Command */
	{ 0x45, IPMI_REQ_PRIV },	/* Set User Name */
	{ 0x46, IPMI_REQ_PRIV },	/* Get User Name Command */
	{ 0x47, IPMI_REQ_PRIV },	/* Set User Password Command */
	{ 0x48, IPMI_REQ_PRIV },	/* Activate Payload */
	{ 0x49, IPMI_REQ_PRIV },	/* Deactivate Payload */
	{ 0x4a, IPMI_REQ_NORM },	/* Get Payload Activation Status */
	{ 0x4b, IPMI_REQ_NORM },	/* Get Payload Instance Info */
	{ 0x4c, IPMI_REQ_PRIV },	/* Set User Payload Access */
	{ 0x4d, IPMI_REQ_PRIV },	/* Get User Payload Access */
	{ 0x4e, IPMI_REQ_NORM },	/* Get Channel Payload Support */
	{ 0x4f, IPMI_REQ_NORM },	/* Get Channel Payload Version */
	{ 0x50, IPMI_REQ_NORM },	/* Get Channel Payload OEM Info */
	{ 0x52, IPMI_REQ_PRIV },	/* Master Write-Read */
	{ 0x54, IPMI_REQ_NORM },	/* Get Channel Cipher Suites */
	{ 0x55, IPMI_REQ_NORM },	/* Suspend/Resume Payload Encryption */
	{ 0x56, IPMI_REQ_PRIV },	/* Set Channel Security Keys */
	{ 0x57, IPMI_REQ_NORM },	/* Get System Interface Capabilities */
	{ 0xff, IPMI_END_OF_LIST }
};

static ipmi_command_priv_level_t ipmi_netfn_storage[] = {
	{ 0x10, IPMI_REQ_NORM },	/* Get FRU Inventory Area Info */
	{ 0x11, IPMI_REQ_NORM },	/* Read FRU Data */
	{ 0x12, IPMI_REQ_PRIV },	/* Write FRU Data */
	{ 0x20, IPMI_REQ_NORM },	/* Get SDR Repository Info */
	{ 0x21, IPMI_REQ_NORM },	/* Get SDR Repository Alloc Info */
	{ 0x22, IPMI_REQ_NORM },	/* Reserve SDR Repository */
	{ 0x23, IPMI_REQ_NORM },	/* Get SDR */
	{ 0x24, IPMI_REQ_PRIV },	/* Add SDR */
	{ 0x25, IPMI_REQ_PRIV },	/* Partial Add SDR */
	{ 0x26, IPMI_REQ_PRIV },	/* Delete SDR */
	{ 0x27, IPMI_REQ_PRIV },	/* Clear SDR Repository */
	{ 0x28, IPMI_REQ_NORM },	/* Get SDR Repository Time */
	{ 0x29, IPMI_REQ_PRIV },	/* Set SDR Repository Time */
	{ 0x2a, IPMI_REQ_PRIV },	/* Enter SDR Repository Update Mode */
	{ 0x2b, IPMI_REQ_PRIV },	/* Exit SDR Repository Update Mode */
	{ 0x2c, IPMI_REQ_PRIV },	/* Run Initialization Agent */
	{ 0x40, IPMI_REQ_NORM },	/* Get SEL Info */
	{ 0x41, IPMI_REQ_NORM },	/* Get SEL Allocation Info */
	{ 0x42, IPMI_REQ_NORM },	/* Reserve SEL */
	{ 0x43, IPMI_REQ_NORM },	/* Get SEL Entry */
	{ 0x44, IPMI_REQ_PRIV },	/* Add SEL Entry */
	{ 0x45, IPMI_REQ_PRIV },	/* Partial Add SEL Entry */
	{ 0x46, IPMI_REQ_PRIV },	/* Delete SEL Entry */
	{ 0x47, IPMI_REQ_PRIV },	/* Clear SEL */
	{ 0x48, IPMI_REQ_NORM },	/* Get SEL Time */
	{ 0x49, IPMI_REQ_PRIV },	/* Set SEL Time */
	{ 0x5a, IPMI_REQ_NORM },	/* Get Auxiliary Log Status */
	{ 0x5b, IPMI_REQ_PRIV },	/* Set Auxiliary Log Status */
	{ 0xff, IPMI_END_OF_LIST }
};

static ipmi_command_priv_level_t ipmi_netfn_transport[] = {
	{ 0x01, IPMI_REQ_PRIV },	/* Set LAN Configuration Parameters */
	{ 0x02, IPMI_REQ_PRIV },	/* Get LAN Configuration Parameters */
	{ 0x03, IPMI_REQ_PRIV },	/* Suspend IPMI ARPs */
	{ 0x04, IPMI_REQ_NORM },	/* Get IP/UDP/RMCP Statistics */
	{ 0x10, IPMI_REQ_PRIV },	/* Set Serial/Modem Configuration */
	{ 0x11, IPMI_REQ_PRIV },	/* Get Serial/Modem Configuration */
	{ 0x12, IPMI_REQ_PRIV },	/* Set Serial/Modem Mux */
	{ 0x13, IPMI_REQ_NORM },	/* Get TAP Response Codes */
	{ 0x14, IPMI_REQ_PRIV },	/* Set PPP UDP Proxy Transmit Data */
	{ 0x15, IPMI_REQ_PRIV },	/* Get PPP UDP Proxy Transmit Data */
	{ 0x16, IPMI_REQ_PRIV },	/* Send PPP UDP Proxy Packet */
	{ 0x17, IPMI_REQ_PRIV },	/* Get PPP UDP Proxy Receive Data */
	{ 0x18, IPMI_REQ_PRIV },	/* Serial/Modem Connection Active */
	{ 0x19, IPMI_REQ_PRIV },	/* Callback */
	{ 0x1a, IPMI_REQ_PRIV },	/* Set User Callback Options */
	{ 0x1b, IPMI_REQ_NORM },	/* Get User Callback Options */
	{ 0x20, IPMI_REQ_PRIV },	/* SOL Activating */
	{ 0x21, IPMI_REQ_PRIV },	/* Set SOL Configuration Parameters */
	{ 0x22, IPMI_REQ_NORM },	/* Get SOL Configuration Parameters */
	{ 0xff, IPMI_END_OF_LIST }
};

#ifdef	__cplusplus
}
#endif

#endif	/* _IPMI_CMD_H */
