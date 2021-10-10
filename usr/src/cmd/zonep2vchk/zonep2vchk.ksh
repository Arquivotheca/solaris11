#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

# Micro version replaced by makefile.  Micro value set by applying
# Solaris version and changeset id.
VERSION="1.0.__MICRO__"

unset LD_LIBRARY_PATH
PATH=/usr/bin:/usr/sbin:/usr/ccs/bin
export PATH
unalias -a

# Use separate text domain to allow translations to be included with unbundled
# version for s10.
export TEXTDOMAIN=SUNW_OST_ZONEP2VCHK

m_usage=$(gettext "\
Usage:\n\
    zonep2vchk -V\n\
\n\
    zonep2vchk [ -T release ] -c\n\
\n\
    zonep2vchk [ -T release ] [ -P ] [ -b ]\n\
               [ -s path[,path...] ] [ -S file ]\n\
               [ -r {time}(h|m_s) ] [ -x ]\n\
               [ -e execname[,execname...] ] [ -E file ]\n\
\n\
Options:\n\
     -V   Output command version and exits.\n\
     -T   Specify the target release such as \"S10\" or \"S11\".\n\
     -c   Output a template zone configuration based on the system's\n\
          configuration.\n\
     -P   Output machine parsable output.\n\
     -b   Perform basic system configuration checks.\n\
     -r   Perform runtime checks for specified duration.\n\
     -x   Perform runtime checks until terminated with SIGINT/ctrl-c.\n\
     -e   Limit runtime checks to specified execnames.\n\
     -E   Limit runtime checks to execnames specified in \"file\".\n\
     -s   Perform static binary analysis on the specified files and\n\
          directories.  Directories are recursed.\n\
     -S   Perform static binary analysis on files and directories\n\
          listed in \"file\".  Directories are recursed.\n")


m_cancel=$(gettext "Analysis cancelled due to interrupt.")
e_bad_opt=$(gettext "Error: Invalid option: %s")
e_opt_arg=$(gettext "Error: Option missing operand: %s")
e_incompat_options=$(gettext "Error: mutually exclusive options.\n%s")
e_needroot=$(gettext "Error: root required to execute zonep2vchk.")
e_unsupported=$(gettext "\
Error: zonep2vchk requires Solaris 8 or newer.")
e_nonglobal_zone=$(gettext "\
Error: zonep2vchk must be run in the global zone.")
e_release=$(gettext "\
Error: invalid target release: (%s), it must be \"%s\" for this release.")
e_trusted_extensions=$(gettext "\
Error: migration of a system running Trusted Extensions is not supported.")
e_badtime=$(gettext "Error: invalid time (%s) for dynamic analysis")
e_nodyn=$(gettext "\
Error: this option uses DTrace and is not available on %s.")
e_no_cmd=$(gettext "Error: missing required command %s.")
e_no_tmpfile=$(gettext "Error: unable to create temporary file.")
e_or=$(gettext "%s or %s")

w_indented1="        %s"
w_indented2="        %s %s"
w_formatted2="        %-36s %s"
w_etcsystemfmt="        %s\n            %s\n                %s %s"

w_tune_noinfo=$(gettext "zonep2vchk has no information on tunable")
w_tune_obsolete=$(gettext "Tunable is obsolete on target host")
w_tune_replaced=$(gettext "Replacement tunable exists on target host:")
w_tune_alternate=$(gettext "Alternate tunable exists inside a non-global zone:")
w_tune_noalternate=$(gettext "No alternate tunable exists.")

w_be_header=$(gettext "\
  - The following boot environments will not be usable.  Only the active\n\
    boot environment will be usable in the target non-global zone:")

w_zones_header=$(gettext "\
  - The following zones will be unusable.  Each zone should be migrated\n\
    separately to the target host using detach and attach.  See zoneadm(1M),\n\
    solaris(5) and solaris10(5):\n\
\n\
        Zone                                 State")


w_lofi_header=$(gettext "\
  - The system has the following lofi devices configured.\n\
    Lofi devices cannot be configured in the destination zone.  Lofi devices\n\
    must be created in the global zone and added to the zone using\n\
    \"zonecfg add device\".  See lofiadm(1M) and zonecfg(1M) for details:\n\
\n\
        Device                               File")

w_nfs_header=$(gettext "\
  - The system is sharing file systems with NFS.  This is not possible in\n\
    the destination zone.  The shares should be evaluated to determine if\n\
    they are necessary.  If so, this system may not be suitable for\n\
    consolidation into a zone, or an alternate approach for supporting the\n\
    shares will need to be used, such as sharing via the global zone or\n\
    another host.  Use \"zonep2vchk -P\" to get a list of the shared\n\
    filesystems.")

w_smb_header=$(gettext "\
  - The system is sharing file systems with SMB/CIFS.  This is not possible\n\
    in the destination zone.  The shares should be evaluated to determine if\n\
    they are necessary.  If so, this system may not be suitable for\n\
    consolidation into a zone, or an alternate approach for supporting the\n\
    shares will need to be used, such as sharing via the global zone or\n\
    another host. Use \"zonep2vchk -P \" to get a list of the shared\n\
    filesytems.")

w_dhcp_server_header=$(gettext "\
  - The following dhcp server service is enabled.  In order to provide dhcp\n\
    service, a zone must have ip-type=exclusive, or have the privilege\n\
    \"net_rawaccess\" and the device \"/dev/ip\".  Note that the latter\n\
    will allow a shared stack zone to read and write raw ip packets on\n\
    the network, similar to an exclusive stack zone or global zone.  See\n\
    zonecfg(1m) for description of the \"limitpriv\" property and\n\
    \"add device\" resource":)

w_ntp_header_s10=$(gettext "\
  - The following ntp client service is enabled.  This service updates\n\
    the system clock.  Since all zones share the same system clock, it is\n\
    recommended that this service be disabled after p2v.  If it is\n\
    desired that the zone update the system clock on the target host,\n\
    the zone will need the privilege \"sys_time\", and the service will\n\
    need to be enabled inside the zone after p2v.  See the description\n\
    of the \"limitpriv\" property in zonecfg(1m):")

w_ntp_header_s11=$(gettext "\
  - The following ntp client service is enabled.  This service updates\n\
    the system clock.  Since all zones share the same system clock, this\n\
    service is disabled automatically during p2v.  If it is desired\n\
    that the zone update the system clock on the target host, the\n\
    zone will need the privilege \"sys_time\", and the service will\n\
    need to be enabled inside the zone after p2v.  See the description\n\
    of the \"limitpriv\" property in zonecfg(1m):")

w_vfstab_header=$(gettext "\
  - If needed, the following non-standard vfstab entries will impact\n\
    the zone configuration:\n\
\n\
        Device                               Mountpoint")

w_svc_header=$(gettext "\
  - The following SMF services will not work in a zone:")

w_svcpriv_header=$(gettext "\
  - The following SMF service(s) require the listed privileges to work in\n\
    a zone.  See zonecfg(1M) for a description of the limitpriv property:\n\
\n\
        Service                              Privileges")

w_svcexclip_header=$(gettext "\
  - The following SMF services require ip-type \"exclusive\" to work in\n\
    a zone. If they are needed to support communication after migrating\n\
    to a shared-IP zone, configure them in the destination system's global\n\
    zone instead:")

w_pkg_header=$(gettext "
  - The software in the following packages is not usable in a zone:")

w_etcsystem_header=$(gettext "\
  - The following /etc/system tunables exist.  These tunables will not\n\
    function inside a zone.  The /etc/system tunable may be transfered to\n\
    the target global zone, but it will affect the entire system,\n\
    including all zones and the global zone.  If there is an\n\
    alternate tunable that can be configured from within the zone,\n\
    this tunable is described:")

w_zpool_header=$(gettext "\
  - The system is configured with the following non-root ZFS pools.\n\
    Pools cannot be configured inside a zone, but a zone can be configured\n\
    to use a pool that was set up in the global zone:")

w_resourcepool_header=$(gettext "\
  - The system is configured with the following resource pools.\n\
    Pools cannot be configured inside a zone but a zone can be configured\n\
    to use a pool that was set up in the global zone.  See pooladm(1M) for\n\
    details:")

w_pset_header_10=$(gettext "\
  - The system is configured with the following processor sets.  Processor\n\
    sets cannot be configured inside a zone:\n\
\n\
        Processor Set                        CPU List")

w_pset_header_11=$(gettext "\
  - The system is configured with the following processor sets.  Processor\n\
    sets cannot be configured inside a zone.  Processor sets can be\n\
    configured in the global zone, and a non-global zone may bind to them if\n\
    given the privilege \"sys_res_bind\" via the zonecfg(1M) limitpriv\n\
    property:\n\
\n\
        Processor Set                        CPU List")

w_ramdisk_header=$(gettext "\
  - The system is configured with the following ramdisk devices.  A zone\n\
    cannot configure ramdisk devices.  Ramdisk devices must be configured\n\
    from the global zone, and can be made available to a zone via\n\
    zonecfg(1M) \"add device\" or \"add fs\":")

w_sched=$(gettext "\
  - The system sets the following default scheduling class.  To set the\n\
    default scheduling class for a zone, use the \"scheduler\" zonecfg(1M)\n\
    property:")

w_svm_header=$(gettext "\
  - The system is configured with SVM metadevices.\n\
    A zone cannot configure SVM metadevices, but a zone can be configured\n\
    to use existing metadevices.  See metastat(1M) for details")

w_iscsi_initiator_header=$(gettext "\
  - The system is configured with the following ISCSI initiators.  A zone\n\
    cannot access ISCSI targets.  ISCSI targets must be discovered and\n\
    configured from the global zone.  See iscsiadm(1M) for details:")

w_iscsi_target_header_10=$(gettext "\
  - The system is configured with the following ISCSI targets.  A zone\n\
    cannot share iscsi targets.  ISCSI targets must be shared from the\n\
    global zone.  See iscsitadm(1m) for details:")

w_iscsi_target_header_11=$(gettext "\
  - The system is configured with the following ISCSI targets.  A zone\n\
    cannot share iscsi targets.  ISCSI targets must be shared from the\n\
    global zone.  See itadm(1m) for details:")

w_fcoe_initiator_header=$(gettext "\
  - The system is configured with the following FCOE initiators.  A zone\n\
    cannot configure FCOE initiators.  They must be configured from the\n\
    global zone.  See fcadm(1M) for details:")

w_fcoe_target_header=$(gettext "\
  - The system is configured with the following FCOE targets.  A zone cannot\n\
    configure FCOE targets.  They must be configured from the global zone.\n\
    See fcadm(1M) for details:")

w_npiv_header=$(gettext "\
  - The system is configured with the following virtual NPIV fiberchannel\n\
    ports.  A zone cannot configure NPIV ports.  They must be configured from\n\
    the global zone.  See fcadm(1M) for details:")

w_fc_target_header=$(gettext "\
  - The system is configured with the following fiberchannel targets.  A\n\
    zone cannot configure fiberchannel targets.  They must be configured\n\
    from the global zone.  See fcadm(1M) for details:")

w_fc_initiator_header=$(gettext "\
  - The system has the following hba fiberchannel ports online.  If\n\
    fiberchannel storage is connected, it must be migrated to the target\n\
    global zone.  The storage can then be added to the zone using\n\
    zonecfg(1M) \"add fs\", \"add dataset\", or \"add device\":")

w_comstar_header=$(gettext "\
  - The system has configured the following objects as SCSI targets.  A zone\n\
    cannot configure SCSI targets.  They must be configured from the global\n\
    zone.  See sbdadm(1M) for details:")

w_ndd_header_s10=$(gettext "\
  - System startup scripts were modified to set network driver parameters\n\
    with ndd(1M). These commands are not supported in shared-IP zones. Apply\n\
    network parameter changes in the global zone if needed to support\n\
    communication after migration to a shared-IP zone. ndd commands to modify\n\
    IP and transport layer parameters will work in the startup scripts of an\n\
    exclusive-IP zone. Physical device and datalink level parameters may\n\
    only be changed in the global zone:\n\
\n\
        Script File                 Driver      Parameter")

w_ndd_header_s11=$(gettext "\
  - System startup scripts were modified to set network driver parameters\n\
    with ndd(1M). These ndd commands will fail when run in a zone. Use\n\
    dladm(1M) to apply physical device and datalink level parameters in\n\
    the global zone of the destination system if needed to support\n\
    communication after migration:\n\
\n\
    Script File                 Driver      Parameter")

w_ndd_fmt=$(gettext "        %-27s %-11s %s")

w_patch=$(gettext "\
  - The system does not have the required minimum patch %s.")

run_header=$(gettext "\
--Executing Version: %s\n\
\n\
  - Source System: %s\n\
      Solaris Version: %s\n\
      Solaris Kernel:  %s\n\
      Platform:        %s\n\
\n\
  - Target System:\n\
      Solaris_Version: %s\n\
      Zone Brand:      %s\n\
      IP type:         %s")


basic_header=$(gettext "--Executing basic checks")
basic_footer=$(gettext "  Basic checks compete, %s issue(s) detected")

static_header=$(gettext "--Executing static binary checks")
static_footer=$(gettext "  Static binary checks compete, %s issue(s) detected")

dyn_header=$(gettext "--Executing run-time checks for %s")
dyn_int_header=$(gettext "--Executing run-time checks until interrupted...")
dyn_footer=$(gettext "  Run-time checks complete, %s issue(s) detected")

total_header=$(gettext "--Total issue(s) detected: %s")

w_dyn_privna_header=$(gettext "\
  - The following programs were found using privileges that cannot be added\n\
    to a zone.  The use of these privileges may be related to the program\n\
    command line options or configuration.\n\
\n\
        Program                              Disallowed Privilege")

w_dyn_privex_header=$(gettext "\
  - The following programs were found using privileges that require an\n\
    exclusive IP stack.  The use of these privileges may be related to the\n\
    program command line options or configuration.  See zonecfg(1M) for a\n\
    description of the ip-type property:\n\
\n\
        Program                              Exclusive IP Privilege")

w_dyn_privopt_header=$(gettext "\
  - The following programs were found using privileges that are not\n\
    availabile in a zone by default.  The use of these privileges may be\n\
    related to the program command line options or configuration.  See\n\
    zonecfg(1M) for a description of the limitpriv property:\n\
\n\
        Program                              Optional Privilege")

w_dyn_devna_header=$(gettext "\
  - The following programs were found using devices that cannot be added to\n\
    a zone.  The use of these devices may be related to the program command\n\
    line options or configuration:\n\
\n\
        Program                              Disallowed Device")

w_dyn_devex_header=$(gettext "\
  - The following programs were found using devices which require an\n\
    exclusive IP stack to access.  The use of these devices may be related\n\
    to the program command line options or configuration.  See zonecfg(1M)\n\
    for a description of the ip-type property:\n\
\n\
        Program                              Exclusive IP Device")

w_dyn_devopt_header=$(gettext "\
  - The following programs were found using devices that are not available\n\
    in a zone by default.  If any of these devices is a network device, then\n\
    access will require an exclusive IP stack.  The use of these devices may\n\
    be related to the program command line options or configuration.  See\n\
    zonecfg(1M) for a description of the \"add device\" resource:\n\
\n\
        Program                              Optional Device")

w_unsupported_header=$(gettext "\
  - The following Solaris features are configured, and will not function in\n\
    a non-global zone:");

w_physif_header=$(gettext "\
  - When migrating to an exclusive-IP zone, the target system must have an\n\
    available physical interface for each of the following source system\n\
    interfaces:")

w_ifname_header=$(gettext "\
  - When migrating to an exclusive-IP zone, interface name changes may\n\
    impact the following configuration files:")

w_dynaddr_header_s10=$(gettext "\
  - Dynamically assigned IP addresses are configured on the following\n\
    interfaces. These addresses are not supported with shared-IP zones.\n\
    Use an exclusive-IP zone or replace any dynamically assigned addresses\n\
    with statically assigned addresses. These IP addresses could change\n\
    as a result of MAC address changes. You may need to modify this\n\
    system's address information on the DHCP server and on the DNS,\n\
    LDAP, or NIS name servers:")

w_dynaddr_header_s11=$(gettext "\
  - Dynamically assigned IP addresses are configured on the following\n\
    interfaces. These IP addresses could change as a result of MAC\n\
    address changes. You may need to modify this system's address\n\
    information on the DHCP server and on the DNS, LDAP, or NIS name\n\
    servers:")

w_datalink_header_s10=$(gettext "\
  - The following datalink features will not be configured automatically\n\
    on the destination system. Edit the target zone's configuration with\n\
    zonecfg(1M) or configure these features on the global zone's datalink\n\
    interfaces if needed for target zone communication:")

w_datalink_header_s11=$(gettext "\
  - The following datalink features will not be configured automatically\n\
    on the destination system. Edit the target zone's configuration with\n\
    zonecfg(1M) or use dladm(1M) to configure these features on the global\n\
    zone's datalink interfaces if needed for target zone communication:")

w_netdevalloc_header_s10=$(gettext "\
  - The following networking features are not supported for use with a\n\
    shared-IP zone. They require an exclusive-IP zone with the underlying\n\
    device allocated to the zone.  See zonecfg(1M) for information on how\n\
    to allocate a device resource:")

w_netdevalloc_header_s11=$(gettext "\
  - The following networking features require an exclusive-IP zone with the\n\
    underlying device allocated to the zone.  See zonecfg(1M) for\n\
    information on how to allocate a device resource:")

w_exclusiveonly_header=$(gettext "\
  - The following networking features are not supported for use with\n\
    a shared-IP zone.")

w_sharedonly_header=$(gettext "\
  - The following networking features are not supported for use with\n\
    an exclusive-IP zone.  When migrating to a shared-IP zone, the\n\
    feature must be configured in the global zone to support\n\
    communication:")

w_sharedip_header=$(gettext "\
  - When migrating to a shared-IP zone, the following network features must\n\
    be configured in the global zone if needed to support communication.\n\
    They will not be configured automatically during migration. Nothing\n\
    needs to be done for these features when migrating to an exclusive-IP\n\
    zone where the configuration inside the migrated system image will be\n\
    used:")

w_driverconf_header_s10=$(gettext "\
  - The following driver configuration files were modified after\n\
    installation.  These files will not be used after migration to a zone.\n\
    Any changes will have to be applied in the destination system's global\n\
    zone if needed to support processing in a zone after migration:")

w_driverconf_header_s11=$(gettext "\
  - The following driver configuration files were modified after\n\
    installation.  These files will not be used after migration to a zone.\n\
    Any changes will have to be applied in the destination system's global\n\
    zone if needed to support processing in a zone after migration.  It may\n\
    also be possible to migrate settings via dladm(1M) properties:")

w_linkprop_header=$(gettext "\
  - Some datalink interfaces were configured with non-default properties.\n\
    These properties must be configured in the global zone if needed to\n\
    support non-global zone communication. They will not be configured\n\
    automatically during migration. See the parsable output from\n\
    \"zonep2vchk -P\" for a complete list of non-default properties.")

w_mobile_ip=$(gettext "Mobile IP")
w_dhcp_assigned=$(gettext "DHCP assigned address on")
w_rarp_assigned=$(gettext "Reverse ARP assigned address on")
w_ipv6_autoconf=$(gettext "Autoconfigured IPv6 addresses on")
w_link_aggregation=$(gettext "Link aggregation")
w_etherstub=$(gettext "Ethernet stub")
w_bridge=$(gettext "LAN bridge")
w_ibiface=$(gettext "Infiniband interface")
w_ibpart=$(gettext "Infiniband partition")
w_secobj=$(gettext "WPA or WEP secure object")
w_vnic=$(gettext "VNIC interface")
w_vlan=$(gettext "VLAN interface")
w_simnet=$(gettext "Simnet interface")
w_ipv4_forwarding=$(gettext "IPv4 forwarding on")
w_ipv6_forwarding=$(gettext "IPv6 forwarding on")
w_ipmp_group=$(gettext "IP Multipath group")
w_iptun=$(gettext "IP tunnel interface")
w_vni=$(gettext "Virtual network interface")
w_cgtp=$(gettext "CGTP interface")
w_ppp=$(gettext "Point to Point Protocol (PPP)")
w_static_routes=$(gettext "Static routes")

c_hostid=$(gettext "# Uncomment the following to retain original host hostid:")
c_sched=$(gettext "# A non-default scheduling class on source host")
c_max_procs_11=$(gettext "# Max procs and lwps based on max_uproc/v_proc")
c_max_procs_10=$(gettext "# Max lwps based on max_uproc/v_proc")
c_capped_cpu1=$(gettext "# Only one of dedicated or capped cpu can be used.")
c_capped_cpu2=$(gettext "# Uncomment the following to use cpu caps:")
c_ded_cpu=$(gettext "# Uncomment the following to use dedicated cpu:")
c_capped_mem1=$(gettext "# Uncomment the following to use memory caps.")
c_capped_mem2=$(gettext "# Values based on physical memory plus swap devices:")
c_original_config=$(gettext "# Original %s interface configuration:")
c_dhcp_address=$(gettext "#    DHCP assigned %s")
c_dhcp_address_x=$(gettext "#  * DHCP assigned %s")
c_dhcp_clientid=$(gettext "#    DHCP Client ID %s (CID type %s)")
c_rarp_enabled=$(gettext "#    Reverse ARP is enabled")
c_static_address=$(gettext "#    Statically defined %s")
c_autoconf_address=$(gettext "#    Autoconfigured %s")
c_autoconf_address_x=$(gettext "#  * Autoconfigured %s")
c_test_address=$(gettext "#    IPMP test address %s")
c_ipmp_interface=$(gettext "#    %s Multipath group %s interface")
c_ipmp_member=$(gettext "#    %s Multipath group %s member")
c_mac_address=$(gettext "#    %s MAC address %s")
c_mac_addr_fixed=$(gettext "Manually defined")
c_mac_addr_factory=$(gettext "Factory assigned")
c_mac_addr_random=$(gettext "Randomly assigned")
c_mac_addr_infiniband=$(gettext "Infiniband")
c_mac_addr_vrrp=$(gettext "VRRP based")
c_dynamic_addr_notice=$(gettext "\
#    Unable to migrate addresses marked with \"*\".\n\
#    Shared IP zones require statically assigned addresses.")
c_uncomment_linkprop=$(gettext "\
# Uncomment the following to retain original link configuration:")

w_syscall_header=$(gettext "\
  - System or library calls used by the following programs will not work in\n\
    a zone:")

w_syscallargs_header=$(gettext "\
  - System or library calls used by the following programs will not work in\n\
    a zone if called with the following parameters:")

w_syscallpriv_header=$(gettext "\
  - System or library calls used in the following programs will require\n\
    modifications to the zone configuration for the application to work in\n\
    a zone.  See zonecfg(1M) for a description of the limitpriv property:")

w_syscallexclip_header=$(gettext "\
  - System or library calls used in the following programs will require an\n\
    an exclusive ip stack to function in a zone.  See zonecfg(1M) for a\n\
    description of the ip-type property:")

w_file_header=$(gettext "File: ")

w_syscall=$(gettext "system call not allowed")

w_pool_set_binding_sys=$(gettext "\
            pool_set_binding: libpool.so library call not allowed")

w_p_online_args=$(gettext "\
            p_online: system call not permitted with second argument other\n\
                than P_STATUS")

w_uadmin_args=$(gettext "\
            uadmin: system call not permitted with first argument other than\n\
                A_SHUTDOWN or A_REBOOT")

w_pset_assign_args=$(gettext "\
            pset_assign: system call not permitted with first argument other\n\
                than PS_QUERY")

w_swapctl_args=$(gettext "\
            swapctl: system call not permitted with first argument SC_ADD or\n\
                SC_REMOVE")

w_pset_bind_args=$(gettext "\
            pset_bind: system call not permitted with first argument other\n\
                than PS_QUERY")

w_pool_conf_open_args=$(gettext "\
            pool_conf_open: libpool.so library call not permitted if second\n\
                argument is \"pool_dynamic_location()\" and third argument\n\
                includes flag PO_RDWR")

w_pool_conf_commit_args=$(gettext "\
            pool_conf_commit: libpool.so library call not permitted if\n\
                second argument is non-zero")

w_priocntl_priv=$(gettext "\
            priocntl: system call requires privilege \"proc_priocntl\" is\n\
                called with third argument of PC_SETPARAMS, PC_SETXPARAMS,\n\
                or PC_ADMIN")

w_priocntlset_priv=$(gettext "\
            priocntlset: system call requires privilege \"proc_priocntl\"\n\
                is called with second argument of PC_SETPARAMS,\n\
                PC_SETXPARAMS, or PC_ADMIN")

w_time_priv=$(gettext "system call requires privilege \"sys_time\"")

w_adjtime_priv="\
            adjtime: $w_time_priv"

w_ntp_adjtime_priv="\
            ntp_adjtime: $w_time_priv"

w_stime_priv="\
            stime_adjtime: $w_time_priv"

w_settimeofday_priv="\
            settimeofday: $w_time_priv"

w_clock_settime_priv="\
        clock_settime: $w_time_priv"

w_cpc_bind_cpu_priv=$(gettext "\
            cpc_bind_cpu: system call requires privilege \"cpc_cpu\"")

w_msgctl_priv=$(gettext "\
            msgctl: system call requires privilege \"sys_ipc_config\" if\n\
                called with second argument IPC_SET to raise the value of\n\
                msg_qbytes")

w_timer_create_priv=$(gettext "\
            timer_create: system call requires privilege\n\
                \"proc_clock_highres\" if first argument is CLOCK_HIGHRES")

w_pset_bind_priv=$(gettext "\
            pset_bind: system call requires privilege \"sys_res_bind\" if\n\
                first argument is other than PS_QUERY")

w_t_open_arg=$(gettext "\
            t_open: system call requires zone setting iptype=exclusive")

w_lib_header=$(gettext "
  - Libraries used by the following programs will not work in a zone:")

b_s10_container=$(gettext "(Solaris 10 Container)")
b_default=$(gettext "(default)")
#
# Counts the checks for a particular mode
#
setup_mode()
{
	# Save number of checks fired so far
	(( g_total_fired = g_total_fired + g_check_fired ))
	(( g_check_count = 0 ))
	(( g_check_fired = 0 ))
}

#
# Set up the correct header and msg info for each specific check.
#
setup_chk()
{
	(( g_check_count = g_check_count + 1 ))

	case "$1" in
	"etcsystem")	category="incompatible"
			issue="etcsystem"
			header="$w_etcsystem_header"
			itemfmt="$w_etcsystemfmt"
			;;
	"unsupported")	category="incompatible"
			issue="unsupported"
			header="$w_unsupported_header"
			itemfmt="$w_indented1"
			;;
	"be")		category="incompatible"
			issue="be"
			header="$w_be_header"
			itemfmt="$w_indented1"
			;;
	"nfs")		category="incompatible"
			issue="nfs"
			header="$w_nfs_header"
			itemfmt="$w_indented1"
			;;
	"smb")		category="incompatible"
			issue="smb"
			header="$w_smb_header"
			itemfmt="$w_indented1"
			;;
	"pkg")		category="incompatible"
			issue="pkg"
			header="$w_pkg_header"
			itemfmt="$w_indented1"
			;;
	"iscsi-initiator")
			category="incompatible"
			issue="iscsi-initiator"
			header="$w_iscsi_initiator_header"
			itemfmt="$w_indented1"
			;;
	"iscsi-target")	category="incompatible"
			issue="iscsi-target"
			if (( g_source_release >= 11 )) ; then
				header="$w_iscsi_target_header_11"
			else
				header="$w_iscsi_target_header_10"
			fi
			itemfmt="$w_indented1"
			;;
	"fcoe-initiator")
			category="incompatible"
			issue="fcoe-initiator"
			header="$w_fcoe_initiator_header"
			itemfmt="$w_indented2"
			;;
	"fcoe-target")	category="incompatible"
			issue="fcoe-target"
			header="$w_fcoe_target_header"
			itemfmt="$w_indented2"
			;;
	"npiv")		category="incompatible"
			issue="npiv"
			header="$w_npiv_header"
			itemfmt="$w_indented2"
			;;
	"fc-initiator")	category="configuration"
			issue="fc-initiator"
			header="$w_fc_initiator_header"
			itemfmt="$w_indented1"
			;;
	"fc-target")	category="incompatible"
			issue="fc-target"
			header="$w_fc_target_header"
			itemfmt="$w_indented1"
			;;
	"scsi")		category="incompatible"
			issue="scsi-block-device"
			header="$w_comstar_header"
			itemfmt="$w_indented1"
			;;
	"svcnotallowed")
			category="incompatible"
			issue="svcnotallowed"
			header="$w_svc_header"
			itemfmt="$w_indented1"
			;;
	"lofi")		category="incompatible"
			issue="lofi"
			header="$w_lofi_header"
			itemfmt="$w_formatted2"
			;;
	"ramdisk")	category="configuration"
			issue="ramdisk"
			header="$w_ramdisk_header"
			itemfmt="$w_indented1"
			;;
	"zones")	category="incompatible"
			issue="zones"
			header="$w_zones_header"
			itemfmt="$w_formatted2"
			;;
	"datalink")	category="configuration"
			issue="datalink"
			if (( g_target_release >= 11 )) ; then
				header="$w_datalink_header_s11"
			else
				header="$w_datalink_header_s10"
			fi
			itemfmt="$w_indented2"
			;;
	"driverconf")	category="configuration"
			issue="driverconf"
			if (( g_target_release >= 11 )) ; then
				header="$w_driverconf_header_s11"
			else
				header="$w_driverconf_header_s10"
			fi
			itemfmt="$w_indented2"
			;;
	"ifname")	category="configuration"
			issue="ifname"
			header="$w_ifname_header"
			itemfmt="$w_indented2"
			;;
	"linkprop")	category="configuration"
			issue="linkprop"
			header="$w_linkprop_header"
			itemfmt="$w_indented2"
			;;
	"ndd")		category="configuration"
			issue="ndd"
			if (( g_target_release >= 11 )) ; then
				header="$w_ndd_header_s11"
			else
				header="$w_ndd_header_s10"
			fi
			itemfmt="$w_ndd_fmt"
			;;
	"dynaddr")	category="configuration"
			issue="dynaddr"
			if (( g_target_release >= 11 )) ; then
				header="$w_dynaddr_header_s11"
			else
				header="$w_dynaddr_header_s10"
			fi
			itemfmt="$w_indented2"
			;;
	"patch")	category="configuration"
			issue="patch"
			header="PATCH"
			;;
	"physif")	category="configuration"
			issue="physif"
			header="$w_physif_header"
			itemfmt="$w_indented2"
			;;
	"svcpriv")	category="configuration"
			issue="svcpriv"
			header="$w_svcpriv_header"
			itemfmt="$w_formatted2"
			;;
	"ntp-client")	category="configuration"
			issue="ntp-client"
			if (( g_target_release >= 11 )) ; then
				header="$w_ntp_header_s11"
			else
				header="$w_ntp_header_s10"
			fi
			itemfmt="$w_indented1"
			;;
	"dhcp-server")	category="configuration"
			issue="dhcp-server"
			header="$w_dhcp_server_header"
			itemfmt="$w_indented1"
			;;
	"sched")	category="configuration"
			issue="sched"
			header="$w_sched"
			itemfmt="$w_indented1"
			;;
	"sharedip")	category="configuration"
			issue="sharedip"
			header="$w_sharedip_header"
			itemfmt="$w_indented2"
			;;
	"netdevalloc")	category="configuration"
			issue="netdevalloc"
			if (( g_target_release >= 11 )) ; then
				header="$w_netdevalloc_header_s11"
			else
				header="$w_netdevalloc_header_s10"
			fi
			itemfmt="$w_indented1"
			;;
	"sharedonly")	category="configuration"
			issue="sharedonly"
			header="$w_sharedonly_header"
			itemfmt="$w_indented2"
			;;
	"exclusiveonly")
			category="configuration"
			issue="exclusiveonly"
			header="$w_exclusiveonly_header"
			itemfmt="$w_indented2"
			;;
	"svcexclip")	category="configuration"
			issue="svcexclip"
			header="$w_svcexclip_header"
			itemfmt="$w_indented1"
			;;
	"svm")		category="configuration"
			issue="svm"
			header="$w_svm_header"
			itemfmt="$w_indented1"
			;;
	"vfstab")	category="configuration"
			issue="vfstab"
			header="$w_vfstab_header"
			itemfmt="$w_formatted2"
			;;
	"zpool")	category="configuration"
			issue="zpool"
			header="$w_zpool_header"
			itemfmt="$w_indented1"
			;;
	"resourcepool")	category="incompatible"
			issue="resourcepool"
			header="$w_resourcepool_header"
			itemfmt="$w_indented1"
			;;
	"pset")		category="incompatible"
			issue="pset"
			if (( g_target_release >= 11 )) ; then
				header="$w_pset_header_11"
			else
				header="$w_pset_header_10"
			fi
			itemfmt="$w_formatted2"
			;;
	"syscall")	category="incompatible"
			issue="syscall"
			header="$w_syscall_header"
			last_file=""
			;;
	"syscallargs")	category="incompatible"
			issue="syscallargs"
			header="$w_syscallargs_header"
			last_file=""
			;;
	"syscallpriv")	category="configuration"
			issue="syscallpriv"
			header="$w_syscallpriv_header"
			last_file=""
			;;
	"syscallexclip") category="configuration"
			issue="syscallexclip"
			header="$w_syscallexclip_header"
			last_file=""
			;;
	"lib")		category="incomaptible"
			issue="lib"
			header="$w_lib_header"
			last_file=""
			;;
	"privna")	category="incompatible"
			issue="privnotallowed"
			header="$w_dyn_privna_header"
			itemfmt="$w_formatted2"
			;;
	"privex")	category="configuration"
			issue="privexclip"
			header="$w_dyn_privex_header"
			itemfmt="$w_formatted2"
			;;
	"privopt")	category="configuration"
			issue="privoptional"
			header="$w_dyn_privopt_header"
			itemfmt="$w_formatted2"
			;;
	"devna")	category="incompatible"
			issue="devnotallowed"
			header="$w_dyn_devna_header"
			itemfmt="$w_formatted2"
			;;
	"devex")	category="configuration"
			issue="devexclip"
			header="$w_dyn_devex_header"
			itemfmt="$w_formatted2"
			;;
	"devopt")	category="configuration"
			issue="devoptional"
			header="$w_dyn_devopt_header"
			itemfmt="$w_formatted2"
			;;
	*)		fatal "Internal error: unknown error code $1"
			;;
	esac
}

# Clean up on interrupt
trap_cleanup()
{
	# Dynamic mode can be interrupted
	if (( opt_dynamic_mode == 1 )) ; then
		(( g_interrupted = 1 ))
		(( g_dtrace_pid != 0 )) &&
		    kill -15 $g_dtrace_pid >/dev/null 2>&1
		return
	fi

	(( opt_parse_mode == 0 )) && fatal "$m_cancel"
}

fatal()
{
	typeset fmt="$1"
	shift

	printf "${fmt}\n" "$@" >&2
	exit 1
}

#
# Concatenate arguments into a parsable string using ":" as the
# field separator. Any ":" characters embedded within an argument
# will be escaped with a backslash.
#
parsable()
{
	[[ -z $1 ]] && return
	print -n "$( print $1 | sed -e "s/:/\\\\:/g" )"
	shift
	while [[ -n "$1" ]]; do
		print -n ":$( print $1 | sed -e "s/:/\\\\:/g" )"
		shift
	done
}

#
# This function displays a single item related to the current issue in
# either parsable format or human readable format. The human readable
# "header" will exist if this is the first item found relative to the
# issue. If needed, print the header then clear it so that will not
# be printed for subsequent items.
#
warn_item()
{
	(( g_check_fired = g_check_fired + 1 ))

	if (( opt_parse_mode == 1 )) ; then
		print -- "$category:$issue:$(parsable "$@")"
		return
	fi

	if [[ -n "$header" ]]; then
		# Put a blank line before header
		printf "\n$header\n\n"
		header=""
	fi
	printf "$itemfmt\n" "$@"
}

#
# This function is similar to the warn_item function in that it outputs
# each issue when generating machine-parsable output.  However, when
# generating human-readable output it doesn't output each line, just the
# header.  The idea here is we use this function when there is potentially
# many lines of output which won't each be useful to the user.  For example,
# we don't want to list each ZFS dataset or NFS share to the user, just tell
# them that these could be an issue.  But, for machine parsable output we will
# list each one since they may be collecting that data for other processing.
#
warn_item_compact()
{
	(( g_check_fired = g_check_fired + 1 ))

	if (( opt_parse_mode == 1 )) ; then
		print -- "$category:$issue:$(parsable "$@")"
		return
	fi

	if [[ -n "$header" ]]; then
		# Put a blank line before header
		printf "\n$header\n"
		header=""
	fi
}

#
# This function displays a network feature with its optional list of
# impacted interfaces as either:
#  - A single item in human-readable form.
#  - A series of items in parsable form.
#
warn_feature()
{
	typeset feature_code="$1"
	typeset feature_text="$2"
	typeset interfaces="${3#\ }"
	typeset interface
	typeset iface_count

	if (( opt_parse_mode == 1 )) ; then
		if [[ -z $interfaces ]]; then
			warn_item "$feature_code"
		else
			# Print one parsable item line per interface.
			for interface in $interfaces; do
				warn_item "$feature_code" "$interface"
			done
		fi
	else
		if [[ -z $interfaces ]]; then
			warn_item "$feature_text"
		else
			# Print all interfaces as a comma separated list
			# in a single item. Split the interface list into
			# multiple indented lines if it exceeds fifty
			# characters. Use a temporary "prefix" string
			# during the fmt operation to provide
			# extra space on the first line for "feature_text".
			typeset -i prefixlen
			prefixlen=$(( ${#feature_text} - 10 ))
			(( prefixlen < 2 )) && prefixlen=2
			prefix=$(printf "%${prefixlen}.${prefixlen}s" \
			    '!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!')
			interfaces=$( print "$prefix" $interfaces | \
			    sed -e 's/ /, /g' | \
			    fmt -s -w 50 | \
			    sed -e "s/${prefix}, //" | \
			    sed -e '2,$s/^/		/' )
			warn_item "$feature_text" "$interfaces"
		        iface_count=$( print $interfaces | wc -w )
			(( g_check_fired = g_check_fired + iface_count - 1 )) 
		fi
	fi
}

#
# This function takes a message parameter along with 1 or more additional
# parameters.  These are used to generate the message to the user.
#
warn_issue()
{
	typeset text="$1"
	shift

	(( g_check_fired = g_check_fired + 1 ))

	# Clobber header to inform setup_chk that check has fired
	if [[ -n "$header" ]]; then
		# Put a blank line before  first issue in current mode
		(( opt_parse_mode == 0 )) && print
		header=""
	fi


	if (( opt_parse_mode == 1 )) ; then
		print -- "$category:$issue:$(parsable "$@")"
	else
		printf "$text\n" "$@"
	fi
}

#
# This function takes a disallowed syscall and generates the human readable
# output.
#
warn_syscall()
{
	typeset s=$1
	typeset f=$2

	if (( opt_parse_mode == 1 )) ; then
		warn_item $f $s
		return
	fi

	(( g_check_fired = g_check_fired + 1 ))

	if [[ -n "$header" ]]; then
		# Put a blank line before header
		printf "\n$header\n"
		header=""
	fi

	if [[ $last_file != "$f" ]] ; then
		printf "\n        %s%s\n" "$w_file_header" "$f"
		last_file="$f"
	fi

	# See if there is a message specific call
	eval m=\"\$w_${s}_sys\"
	if [[ $m == "" ]] ; then
		printf "            %s: %s\n" "$s" "$w_syscall"
	else
		print "$m"
	fi
}

#
# This function takes a disallowed library and generates the human readable
# output.
#
warn_lib()
{
	typeset l=$1
	typeset f=$2

	if (( opt_parse_mode == 1 )) ; then
		warn_item $f $l
		return
	fi

	(( g_check_fired = g_check_fired + 1 ))

	if [[ -n "$header" ]]; then
		# Put a blank line before header
		printf "\n$header\n"
		header=""
	fi

	if [[ $last_file != "$f" ]] ; then
		printf "\n        %s%s\n" "$w_file_header" "$f"
		last_file="$f"
	fi

	printf "            %s\n" "$l"
}

#
# This function takes a syscall with disallowed args and generates the human
# readable output.  Third argument is used to specify to use messages related
# to required privileges.
#
warn_syscallargs()
{
	typeset s=$1
	typeset f=$2
	typeset p=$3

	if (( opt_parse_mode == 1 )) ; then
		warn_item $f $s $p
		return
	fi

	(( g_check_fired = g_check_fired + 1 ))

	if [[ -n "$header" ]]; then
		# Put a blank line before header
		printf "\n$header\n"
		header=""
	fi

	if [[ $last_file != $f ]] ; then
		printf "\n        %s%s\n" "$w_file_header" $f
		last_file=$f
	fi

	# print the warning associated with the particular system call
	if [[ -n "$p" ]] ; then
		eval print \"\$w_${s}_priv\" 
	else
		eval print \"\$w_${s}_args\" 
	fi
}

#
# Generate the list of privileges to watch. Since the privs vary based on
# which kernel we're running, we use ppriv to see which privs are available
# then match those up to the current latest list of privileges that we know
# about.
#
gen_priv_names()
{
	ppriv -l | nawk '
	{
		printf("privnames[%d] = \"%s\";\n", NR - 1, $1);
	}
	'
}

#
# Generate a list of execnames specified by user  For each execname specified,
# an element in the execnames array will have value 1.
#
gen_execnames()
{
	typeset -i i=1

	while (( i <= g_execname_count )) ; do
		print "execnames[\"${g_execname_list[$i]}\"] = 1;"
		(( i = i + 1 ))
	done
}

gen_dyn_script()
{
	typeset open
	typeset arg

	if (( g_source_release == 10 )) ; then
		open="open"
		arg="arg0"
	elif (( g_source_release >= 11 )) ; then
		open="openat"
		arg="arg1"
	fi

	cat <<- ENDD
	#!/usr/sbin/dtrace -s
	#pragma D option quiet

	BEGIN {

		seconds = $g_dynamic_seconds;

	ENDD
	gen_priv_names
	gen_execnames
	cat <<- ENDD

		/*
		 * The following list of privileges are not available in a zone.
		 */
		privna["dtrace_kernel"] = 1;
		privna["proc_zone"] = 1;
		privna["sys_config"] = 1;
		privna["sys_devices"] = 1;
		privna["sys_linkdir"] = 1;
		privna["sys_net_config"] = 1;
		privna["sys_res_config"] = 1;
		privna["sys_suser_compat"] = 1;
		privna["xvm_control"] = 1;
		privna["virt_manage"] = 1;

		/*
		 * The following list of privileges are only available 
		 * in an exclusive IP stack zone.
		 */
		privex["sys_ip_config"] = 1;
		privex["sys_iptun_config"] = 1;
		privex["net_rawaccess"] = 1;
		privex["sys_ppp_config"] = 1;

		/*
		 * The following list of privileges are available by default
		 * in a zone.
		 */
		privdefault["contract_event"] = 1;
		privdefault["contract_identity"] = 1;
		privdefault["contract_observer"] = 1;
		privdefault["file_chown"] = 1;
		privdefault["file_chown_self"] = 1;
		privdefault["file_dac_execute"] = 1;
		privdefault["file_dac_read"] = 1;
		privdefault["file_dac_search"] = 1;
		privdefault["file_dac_write"] = 1;
		privdefault["file_owner"] = 1;
		privdefault["file_setid"] = 1;
		privdefault["ipc_dac_read"] = 1;
		privdefault["ipc_dac_write"] = 1;
		privdefault["ipc_owner"] = 1;
		privdefault["net_bindmlp"] = 1;
		privdefault["net_icmpaccess"] = 1;
		privdefault["net_mac_aware"] = 1;
		privdefault["net_observability"] = 1;
		privdefault["net_privaddr"] = 1;
		privdefault["proc_audit"] = 1;
		privdefault["proc_chroot"] = 1;
		privdefault["proc_exec"] = 1;
		privdefault["proc_fork"] = 1;
		privdefault["proc_info"] = 1;
		privdefault["proc_lock_memory"] = 1;
		privdefault["proc_owner"] = 1;
		privdefault["proc_setid"] = 1;
		privdefault["proc_taskid"] = 1;
		privdefault["sys_acct"] = 1;
		privdefault["sys_admin"] = 1;
		privdefault["sys_audit"] = 1;
		privdefault["sys_mount"] = 1;
		privdefault["sys_nfs"] = 1;
		privdefault["sys_resource"] = 1;

		/*
		 * The following list of device nodes are available by 
		 * default in a zone.
		 */
		devdefault["/dev/arp"] = 1;
		devdefault["/dev/conslog"] = 1;
		devdefault["/dev/console"] = 1;
		devdefault["/dev/cpu"] = 1;
		devdefault["/dev/cpu/self"] = 1;
		devdefault["/dev/cpu/self/cpuid"] = 1;
		devdefault["/dev/crypto"] = 1;
		devdefault["/dev/cryptoadm"] = 1;
		devdefault["/dev/dtrace"] = 1;
		devdefault["/dev/dtremote"] = 1;
		devdefault["/dev/fd"] = 1;
		devdefault["/dev/kstat"] = 1;
		devdefault["/dev/log"] = 1;
		devdefault["/dev/logindmux"] = 1;
		devdefault["/dev/msglog"] = 1;
		devdefault["/dev/null"] = 1;
		devdefault["/dev/openprom"] = 1;
		devdefault["/dev/poll"] = 1;
		devdefault["/dev/pool"] = 1;
		devdefault["/dev/ptmx"] = 1;
		devdefault["/dev/pts"] = 1;
		devdefault["/dev/random"] = 1;
                devdefault["/dev/rdsk"] = 1;
                devdefault["/dev/rmt"] = 1;
		devdefault["/dev/sad"] = 1;
		devdefault["/dev/sad/user"] = 1;
		devdefault["/dev/stderr"] = 1;
		devdefault["/dev/stdin"] = 1;
		devdefault["/dev/stdout"] = 1;
		devdefault["/dev/swap"] = 1;
		devdefault["/dev/syscon"] = 1;
		devdefault["/dev/sysevent"] = 1;
		devdefault["/dev/sysmsg"] = 1;
		devdefault["/dev/systty"] = 1;
		devdefault["/dev/tcp"] = 1;
		devdefault["/dev/tcp6"] = 1;
		devdefault["/dev/term"] = 1;
		devdefault["/dev/ticlts"] = 1;
		devdefault["/dev/ticots"] = 1;
		devdefault["/dev/ticotsord"] = 1;
		devdefault["/dev/tty"] = 1;
		devdefault["/dev/udp"] = 1;
		devdefault["/dev/udp6"] = 1;
		devdefault["/dev/urandom"] = 1;
		devdefault["/dev/zconsole"] = 1;
		devdefault["/dev/zero"] = 1;
		devdefault["/dev/zfs"] = 1;

		/*
		 * These devices are in an exclusive ip zone by default
		 */
		devex["/dev/icmp"] = 1;
		devex["/dev/icmp6"] = 1;
		devex["/dev/ip"] = 1;
		devex["/dev/ip6"] = 1;
		devex["/dev/ipauth"] = 1;
		devex["/dev/ipf"] = 1;
		devex["/dev/ipl"] = 1;
		devex["/dev/iplookup"] = 1;
		devex["/dev/ipmpstub"] = 1;
		devex["/dev/ipnat"] = 1;
		devex["/dev/ipscan"] = 1;
		devex["/dev/ipsecah"] = 1;
		devex["/dev/ipsecesp"] = 1;
		devex["/dev/ipstate"] = 1;
		devex["/dev/ipsync"] = 1;
		devex["/dev/keysock"] = 1;
		devex["/dev/rawip"] = 1;
		devex["/dev/rawip6"] = 1;
		devex["/dev/rts"] = 1;
		devex["/dev/spdsock"] = 1;
		devex["/dev/sppp"] = 1;
		devex["/dev/sppptun"] = 1;

		/*
		 * These devices are not permitted in a zone
		 */
		devna["/dev/mem"] = 1;
		devna["/dev/kmem"] = 1;
		devna["/dev/allkmem"] = 1;
		devna["/dev/poolctl"] = 1;
	}

	BEGIN
	/  $g_source_release <= 10 /
	{
		/* Available in exclusive ip zones on S10 */
		devex["/dev/sctp"] = 1;
		devex["/dev/sctp6"] = 1;
	}
	BEGIN
	/ $g_target_release == 10 /
	{
		/* Not allowed inside a zone on S10 */
		devna["/dev/lofictl"] = 1;	
	}

	BEGIN
	/ $g_source_release >= 11 /
	{

		/* In a zone by default on S11 */
		devdefault["/dev/bpf"] = 1;
		devdefault["/dev/ipnet"] = 1;
		devdefault["/dev/ipnet/lo0"] = 1;
		devdefault["/dev/lo0"] = 1;
		devdefault["/dev/lofi"] = 1;
		devdefault["/dev/lofictl"] = 1;
		devdefault["/dev/nsmb"] = 1;
               	devdefault["/dev/rlofi"] = 1;
               	devdefault["/dev/zvol"] = 1;

		/* In an exclusive ip zone by default on S11 */
		devex["/dev/dld"] = 1;
		devex["/dev/net"] = 1;
		devex["/dev/vni"] = 1;
	}

	ENDD

	if [[ $arg_dynamic_time != "inf" ]] ; then
		cat <<- ENDD
		/* Counting */
		profile:::tick-1sec
		{
			seconds--;
		}
	
		/* End */
		profile:::tick-1sec
		/seconds < 1/
		{
			exit(0);
		}
		ENDD
	fi

	# check for execname match if necessary
	if (( opt_execnames == 1 )) ; then
		cat <<- ENDD
		/*
		 * Check for execname match.  Only match processes in global
		 * zone.  If no match, then turn off the remaining probes
		 */
		syscall::$open:entry
		/ curpsinfo->pr_zoneid == 0 && pid != \$pid &&
		    execnames[execname] == 1 /
		{
			self->in_open = 1;
			self->open_done = 0;
		}	
		fbt::priv_policy:entry
		/ curpsinfo->pr_zoneid == 0 && pid != \$pid &&
		    execnames[execname] == 1 /
		{
			self->in_priv = 1;
			self->priv_done = 0;
		}	
		ENDD
	else
		cat <<-ENDD
		/*
		 * No execnames specified.  Always treat as match if in
		 * global zone.
		 */
		syscall::$open:entry
		/ curpsinfo->pr_zoneid == 0 && pid != \$pid /
		{
			self->in_open = 1;
			self->open_done = 0;
		}	
		fbt::priv_policy:entry
		/ curpsinfo->pr_zoneid == 0 && pid != \$pid /
		{
			self->in_priv = 1;
			self->priv_done = 0;
		}	
		ENDD
	fi

	cat <<- ENDD

	/*
	 * log privilege that is not permitted in a zone.
	 */
	fbt::priv_policy:entry
	/ self->in_priv && privna[privnames[arg1]] == 1 / 
	{
		@priv_na[curpsinfo->pr_psargs, privnames[arg1]] = count();
		self->priv_done = 1;
	}

	/*
 	 * log privilege that requires an exclusive ip stack.
	 */
	fbt::priv_policy:entry
	/ self->in_priv && self->priv_done != 1 &&
	    privex[privnames[arg1]] == 1 / 
	{
		@priv_ex[curpsinfo->pr_psargs, privnames[arg1]] = count();
		self->priv_done = 1;
	}

	/*
	 * If a privilege is not in the default list, log it as one that
	 * needs to be added to the zone to support the calling program.
	 */
	fbt::priv_policy:entry
	/ self->in_priv && self->priv_done != 1 &&
	  privdefault[privnames[arg1]] != 1 / 
	{
		@priv_opt[curpsinfo->pr_psargs, privnames[arg1]] = count();
		self->priv_done = 1;
	}

	/*
	 * Clear the thread for the next privilege call.
	 */
	fbt::priv_policy:entry
	/ self->in_priv /
	{
		self->priv_done = 0;
		self->in_priv = 0;
	}	

	/*
	 * Save arg for return call so that a mapping to the user address will
	 * exist.
	 */
	syscall::$open:entry
	/ self->in_open && $arg != NULL /
	{
		self->in = $arg;
	}

	/*
	 * Get the path of the opened file.
	 */
	syscall::$open:return
	/ self->in_open && self->in != NULL /
	{
		self->arg = cleanpath(copyinstr(self->in));
	}

	syscall::$open:return
	/ self->in_open && self->arg == NULL /
	{
		self->open_done = 1;
	}
	/*
	 * Ignore fd or pts devices.
	 */
	syscall::$open:return
	/ self->in_open && self->open_done != 1 && (
	  substr(self->arg, 0, 8) == "/dev/fd/" ||
	  substr(self->arg, 0, 9) == "/dev/pts/" ) /
	{
		self->open_done = 1;
	}

	/* /dev/net devices require exclusive ip stack. */
	syscall::$open:return
	/ self->in_open && self->open_done != 1 && (
	  substr(self->arg, 0, 9) == "/dev/net/" || devex[self->arg] == 1 ) /
	{
		@open_ex[curpsinfo->pr_psargs, self->arg] = count();
		self->open_done = 1;
	}

	/* Check for not allowed devices. */
	syscall::$open:return
	/ self->in_open && self->open_done != 1 && (
	  substr(self->arg, 0, 9) == "/devices/" || devna[self->arg] == 1 )/
	{
		@open_na[curpsinfo->pr_psargs, self->arg] = count();
		self->open_done = 1;
	}

	/*
	 * Check for default devices.  If the device is not in the default
	 * device list, than save it to report as a device that needs to be
	 * added to the zone to support the calling program.
	 */
	syscall::$open:return
	/ self->in_open && self->open_done != 1 &&
	  substr(self->arg, 0, 5) == "/dev/" && devdefault[self->arg] != 1 /
	{
		@open_opt[curpsinfo->pr_psargs, self->arg] = count();
		self->open_done = 1;
	}

	/* Clear thread for next call to open. */
	syscall::$open:return
	/ self->in_open /
	{
		self->in_open = 0;
		self->open_done = 0;
		self->arg = NULL;
	}

	/*
	 * Output logged privileges and devices in the parsable format.
	 */
	END {
		printa("incompatible|privnotallowed|%s|%s\n", @priv_na);
		printa("configuration|privoptional|%s|%s\n", @priv_opt);
		printa("incompatible|devnotallowed|%s|%s\n", @open_na);
		printa("configuration|devoptional|%s|%s\n", @open_opt);
	}
	END
	/ $g_target_release == 10 /
	{
		printa("configuration|privexclip|%s|%s\n", @priv_ex);
		printa("configuration|devexclip|%s|%s\n", @open_ex);
	}
	ENDD
}

is_elf()
{
	typeset taste=$(od -N 4 -x $1)

	if [[ "$g_arch" == "sparc" ]]; then
		[[ ${taste##0000000 7f45 4c46} != "$taste" ]] && return 0
	else
		[[ ${taste##0000000 457f 464c} != "$taste" ]] && return 0
	fi

	return 1
}

static_check()
{
	typeset static_check
	typeset file

	#
	# We don't support statically linked x86 apps inside a solaris10
	# branded zone because we don't interpose on the LCALL trap.  Flag to
	# check if the app is statically linked with libc so report an issue.
	#
	static_check=0
	(( g_source_release == 10 && g_target_release == 11 )) &&
	   [[ "$g_arch" == "i386" ]] && static_check=1

	file="$1"

	# Inspect elf file and produce parsable output for zonep2vchk
	elfdump -d -s -N .dynsym "$file" | nawk -v targ="$g_target_release" \
	    -v schk="$static_check" -v file="$file" '
	BEGIN {
		#
		# Parseable output for each zone-incompatible system
		# call
		#
		line="incompatible|syscall|" file
		sym["pset_create",	"libc.so.1"]=line "|pset_create"
		sym["pset_destroy",	"libc.so.1"]=line "|pset_destroy"
		sym["pset_setattr",	"libc.so.1"]=line "|pset_setattr"
		sym["mknod",		"libc.so.1"]=line "|mknod"
		sym["_xmknod",		"libc.so.1"]=line "|mknod"
		sym["pool_set_binding", "libpool.so.1"]=line "|pool_set_binding"

		#
		# Parseable output for system calls not allowing certain args
		# inside zones
		#
		line="incompatible|syscallargs|" file
		sym["p_online",		"libc.so.1"]=line "|p_online"
		sym["uadmin",		"libc.so.1"]=line "|uadmin"
		sym["pset_assign",	"libc.so.1"]=line "|pset_assign"
		sym["swapctl",		"libc.so.1"]=line "|swapctl"
		sym["pool_conf_open",   "libpool.so.1"]=line "|pool_conf_open"
		sym["pool_conf_commit", "libpool.so.1"]=line "|pool_conf_commit"

		if (targ == 10)
			sym["pset_bind", "libc.so.1"]=line "|pset_bind"

		#
		# Parseable output for each system call requiring additional
		# privilege in a zone.
		#
		line="configuration|syscallpriv|" file
		sym["priocntl", "libc.so.1"]=\
		    line "|priocntl|proc_priocntl"
		sym["priocntlset", "libc.so.1"]=\
		    line "|priocntlset|proc_priocntl"
		sym["adjtime", "libc.so.1"]=\
		    line "|adjtime|sys_time"
		sym["ntp_adjtime", "libc.so.1"]=\
		    line "|ntp_adjtime|sys_time"
		sym["stime", "libc.so.1"]=\
		    line "|stime|sys_time"
		sym["settimeofday", "libc.so.1"]=\
		    line "|settimeofday|sys_time"
		sym["clock_settime", "libc.so.1"]=\
		    line "|clock_settime|sys_time"
		sym["clock_settime", "librt.so.1"]=\
		    line "|clock_settime|sys_time"
		sym["clock_settime", "libposix4.so.1"]=\
		    line "|clock_settime|sys_time"
		sym["cpc_bind_cpu", "libcpc.so.1"]=\
		    line "|cpc_bind_cpu|cpc_cpu"
		sym["msgctl", "libc.so.1"]=\
		    line "|msgctl|sys_ipc_config"
		sym["timer_create",	"libc.so.1"]=\
		    line "|timer_create|proc_clock_highres"
		sym["timer_create",	"librt.so.1"]=\
		    line "|timer_create|proc_clock_highres"
		sym["timer_create",	"libposix4.so.1"]=\
		    line "|timer_create|proc_clock_highres"
		if (targ >= 11)
			sym["pset_bind", "libc.so.1"]=\
			    line "|pset_bind|sys_res_bind"

		if (targ == 10 ) {
			# Parseable output for calls requiring exclusive ip
			line="configuration|syscallexclip|" file
			sym["t_open",		"libnsl.so.1"]=\
			    line  "|t_open"
		}

		# Parseable output for zones-incompatible libraries
		line="incompatible|lib|" file
		# List of libraries to report
		lib["libdevinfo.so.1"]=line "|libdevinfo.so.1"
		lib["libcfgadm.so.1"]=line "|libcfgadm.so.1"
		lib["libtnfctl.so.1"]=line "|libtnfctl.so.1"
		lib["libkvm.so.1"]=line "|libkvm.so.1"
		lib["libsysevent.so.1"]=line "|libsysevent.so.1"
	}
	{
		# extract UNDEF symbol names:
		# (-s -N .dynsym option in elfdump)
		if ($4 == "FUNC" || $4 == "OBJT" || $4 == "NOTY") {
			if ($5 == "GLOB" || $5 == "WEAK") {
				if ($8 == "UNDEF") {
					found_sym[$9]=1
					next
				}
			}
		}
		if ($2 == "NEEDED") {
			found_lib[$4]=1
			next
		}
	}
	END {
		#
		# Report any of the listed symbols or libraries that are used
		# by the binary.
		#
		saw_dyn_libc=0
		for (s in found_sym) {
			for (l in found_lib) {
				if ((s, l) in sym) {
					printf("%s\n", sym[s, l]); 
					break
				}
			}
		}
		for (l in found_lib) {
			if (l in lib)
				printf("%s\n", lib[l]);
			if (l == "libc.so.1")
				saw_dyn_libc=1
		}

		if (schk && !saw_dyn_libc)
			printf("incompatible|staticlink|" file);
	}'
}

process_file()
{
	typeset file="$1"

	is_elf "$file"
	(( $? != 0 )) && return
	static_check "$file"
}

process_arg()
{
	typeset file="$1"
	typeset i

	if [[ -f "$file" ]]; then
		process_file "$file"
	elif [[ -d "$file" ]]; then
		file "$file" -type -f -print 2>/dev/null | while read i ; do
			# skip if not executable & not a *.so file
			[[ ! -x "$i" && "${i%*.so}" == "$i" ]] && continue

			# skip if not readable
			[[ ! -r "$i" ]] && continue

			process_file "$i"
		done
	fi
}

static_check_files()
{
	typeset i
	typeset category
	typeset issue
	typeset file
	typeset syscall
	typeset priv
	typeset lib
	typeset tmpfile=$(mktemp -t)
	(( $? == 1 )) && fatal "$e_no_tmpfile"

	setup_mode "static"

	(( opt_parse_mode == 0 )) && printf -- "$static_header\n";

	printf "" > $tmpfile

	if [[ "$1" == "-" ]]; then
		# Read file|dir list from standard input.
		cat | while read i ; do
			process_arg "$i" >> $tmpfile
		done
	elif [[ "$1" != "" ]] ; then
		cat "$1" | while read i ; do
			process_arg "$i" >> $tmpfile
		done
	fi

	shift

	for i in "$@"
	do
		process_arg "$i" >> $tmpfile
	done

	# Convert parsable output to human readable messages
	setup_chk "syscall"
	grep '^incompatible|syscall|' $tmpfile |
	    while IFS='|' read category issue file syscall ; do
		warn_syscall "$syscall" "$file"
	done

	setup_chk "syscallargs"
	grep '^incompatible|syscallargs|' $tmpfile |
	    while IFS='|' read category issue file syscall ; do
		warn_syscallargs "$syscall" "$file"
	done
	
	setup_chk "syscallpriv"
	grep '^configuration|syscallpriv|' $tmpfile |
	    while IFS='|' read category issue file syscall priv ; do
		warn_syscallargs "$syscall" "$file" $priv
	done

	setup_chk "syscallexclip"
	grep '^configuration|syscallexclip|' $tmpfile |
	    while IFS='|' read category issue file syscall ; do
		warn_syscallargs "$syscall" "$file"
	done
	
	setup_chk "lib"
	grep '^incompatible|lib|' $tmpfile |
	    while IFS='|' read category issue file lib ; do
		warn_lib "$lib" "$file"
	done
	
	rm -f $tmpfile

	# Add a blank line after last message
	if (( opt_parse_mode == 0 )) ; then
		(( g_check_fired > 0 )) && print
		printf -- "$static_footer\n\n" $g_check_fired;
	fi
}

#
# Get physical IP interface names on an S10 system by scanning the
# existing /etc/hostname[6].* file names. Suppress duplicate interface
# names. Each VLAN is considered a separate interface.
#
get_s10_interfaces()
{
	typeset interface
	typeset interface_list=""
	typeset file

	for file in /etc/hostname.* /etc/hostname6.*; do
		# Handle case where no files exist.
		[[ ! -e $file ]] && continue
		# Sanity check the interface name.
		interface=${file#/etc/hostname?(6).}
		interface=${interface%:+([0-9])}
		[[ $interface !=  \
		    @([a-z|A-Z])*([a-z|A-Z|0-9|.])@([0-9]) ]] && continue 
		# Skip duplicate interface names.
		[[ " $interface_list " == *\ $interface\ * ]] && continue
		interface_list="$interface_list $interface"
	done
	print "$interface_list"
}

#
# Get physical IP interface names on an S11 system.
#
get_s11_interfaces()
{
	typeset interface_list=""
	typeset interface
	typeset persistent

	# Scan for permanently defined interfaces in ipadm.
	# Skip duplicates.
	ipadm show-if -p -o ifname,persistent | \
	    while IFS=":" read interface persistent; do
		if [[ ( $persistent == *4* || $persistent == *6* ) && \
		    " $interface_list " != *\ $interface\ * ]] ; then
			interface_list="$interface_list $interface"
		fi
	done
	print "$interface_list"
}

#
# Get online physical IP interface names on an S11 source system that
# has nwam enabled.
#
get_s11_nwam()
{
	typeset profile=""
	typeset interface_list=""
	typeset p_type
	typeset p_name
	typeset p_state

	# Scan each line of netadm output for interface information
	# on the active network configuration profile.
	netadm list -p ncp | \
	{
		# Find the active ncp.
		while read p_type p_name p_state; do
			if [[ $p_type == ncp && $p_state == online ]]; then
				break
			fi
		done

		# Build list of online interface ncu's under the active ncp.
		while read p_type p_name p_state; do
			if [[ $p_type == ncu:ip && $p_state == online ]]; then
				interface_list="$interface_list $p_name"
			fi
			# Stop scanning on the next ncp line we find.
			if [[ $p_type == ncp ]]; then
				break
			fi
		done
	}
	print "$interface_list"
}

#
# Convert an IPv4 netmask to a prefix length.
# The base address is passed to support "+" syntax.
#
# Print nothing if any errors are encountered.
#
netmask_to_prefixlen()
{
	typeset address="$1"
	typeset netmask="$2"
	typeset prefixlen=0
	typeset newnetmask
	typeset scratch1
	typeset scratch2

	# convert to lower caes
	netmask=$(echo $netmask | tr '[A-Z]' '[a-z]') 

	# If netmask is "+", lookup address in /etc/netmasks for the netmask
	if [[ "$netmask" == "+" ]]; then
		getent netmasks "$address" | read scratch1 netmask
	fi

	# If a netmask name was specified, lookup the name in /etc/hosts
	# first. If not found, try /etc/networks.
	if [[ "$netmask" == @([a-z])*([a-z|0-9|-]) ]]; then
		getent hosts "$netmask" | read newnetmask scratch1
		if [[ -z $newnetmask ]]; then
			getent networks "$netmask" | \
			    read scratch1 newnetmask scratch2
		fi
		netmask="$newnetmask"
	fi

	# If the above logic failed to provide a netmask string,
	# return now without displaying a prefix.
	[[ -z $netmask ]] && return

	case $netmask in

	    # Hexadecimal netmask. e.g., 0xfffffc00 -> 22
	    0x+([f|e|c|8|0]) )
		netmask="${netmask#0x}"
		while [[ $netmask == f* ]]; do
			prefixlen=$(( $prefixlen + 4 ))
			netmask="${netmask#f}"
		done
		case $netmask in
		    e*) prefixlen=$(( $prefixlen + 3 ));;
		    c*) prefixlen=$(( $prefixlen + 2 ));;
		    8*) prefixlen=$(( $prefixlen + 1 ));;
		esac
		;;

	    # IPv4 dotted notation. e.g., 255.255.254.0 -> 22
	    +([0-9.]) )
		while [[ $netmask == 255\.* ]]; do
			prefixlen=$(( $prefixlen + 8 ))
			netmask="${netmask#255\.}"
		done
		case $netmask in
		    255* ) prefixlen=$(( $prefixlen + 8 ));;
		    254* ) prefixlen=$(( $prefixlen + 7 ));;
		    252* ) prefixlen=$(( $prefixlen + 6 ));;
		    248* ) prefixlen=$(( $prefixlen + 5 ));;
		    240* ) prefixlen=$(( $prefixlen + 4 ));;
		    224* ) prefixlen=$(( $prefixlen + 3 ));;
		    192* ) prefixlen=$(( $prefixlen + 2 ));;
		    128* ) prefixlen=$(( $prefixlen + 1 ));;
		    0*   ) prefixlen=$(( $prefixlen + 0 ));;
		    # Anything else here is a bogus netmask
		    * ) return;;
		esac
		# Make sure the remaining octets contain nothing but zeros.
		if [[ "$netmask" == *\.* ]]; then
			netmask="${netmask#*\.}"
			[[ "$netmask" != +([0.]) ]] && return
		fi
		;;

	    # Anything else is an error.
	    * )
		return
		;;
	esac
	print -n "$prefixlen"
	return
}

#
# Lookup a hostname or address in the currently active name services and
# output the information gathered in one of the following formats:
#
# <address>/<prefixlen> (hostname)
#	When no switch is specified. This string is inteded for display
#	on a zonecfg file net or anet resource comment line.
#
# <address>/<prefixlen>
# <hostname>/<prefixlen>
#	When the -a switch was specified. This string is intended for
#	use on a net resource's address property.
#
lookup_host()
{
	typeset switch=""
	if [[ "$1" == -* ]] ; then
		switch="$1"
		shift
	fi
	typeset family="$1"
	typeset address="$2"
	typeset prefix="$3"
	typeset ip_address=""
	typeset ip_hostname=""
	typeset txt1
	typeset SP='(^| |	|$)'

	# Lookup the address and hostname from name services.
	#
	# When a hostname is used in /etc/hostname.<if> or the "ipadm
	# create-addr" command, we expect a one-to-one mapping of hostname
	# to address, but there is no guarantee this is always true. Only
	# one address will be configured by ifconfig or ipadm. We therefore
	# use the first address we find for the requested address family.
	# It is possible with ipadm to have a hostname and not know the
	# address family before this lookup. For that case, we use the
	# first address returned for either address family.
	#
	# When an address is provided, we want to get the hostname to
	# help identify the address in the zonecfg file comments. Once
	# again, we only display the first hostname returned. The format
	# of addresses found in /etc/hostname* files can vary widely. Use
	# the address returned by getent if possible for a more consistent
	# display format.

	getent ipnodes $address | \
	    while read ip_address ip_hostname txt1 2>/dev/null
	do
		[[ -z $family ]] && break
		[[ $family == inet &&
		   $ip_address == +([0-9|.]) ]] && break
		[[ $family == inet6 &&
		   $ip_address == *:* ]] && break
		ip_address=""
		ip_hostname=""
	done

	# Use original addr if nothing found in nameservers.
	if [[ -z $ip_address ]]; then
		ip_address="$address"
	fi

	# Build output based on available information.
	if [[ "$switch" == "-a" ]]; then

		# Build output for a net resource address property
		# in the form  <address>/<prefix-length>
		#
		# If original address was specified as a hostname
		# (not a numeric IPv4 or IPv6 address), and the
		# hostname definition exits in local files, then
		# use the original hostname instead of the IP address.
		# This is a very effective way to make it so that you
		# don't have multiple places where the system's IP
		# address is configured on the destination system.

		if [[ "$address" != +([0-9.]) && "$address" != *:* ]] &&
		    egrep "$SP($address)$SP" /etc/inet/hosts \
		    > /dev/null 2>&1 ; then
			print -n "${address}"
		else
			print -n "${ip_address}"
		fi
		if [[ -n $prefix ]]; then
			print -n "/${prefix}"
		fi

	else
		# Build output for a zonecfg file comment line in
		# the form  <ip-address>/<prefix-length> (ip-hostname)

		print -n "${ip_address}"
		if [[ -n $prefix ]]; then
			print -n "/${prefix}"
		fi
		if [[ -n $ip_hostname ]]; then
			print -n " (${ip_hostname})"
		fi
	fi
}

#
# Display DHCP assigned address currently configured on the interface.
# This function is intended for use on Solaris 10 or earlier.
#
pdhcp_addrs()
{
	typeset interface="$1"
	typeset family="$2"
	typeset logical_interface
	typeset output=""
	typeset ip_address=""
	typeset ip_hostname=""
	typeset ip_prefixlen=""
	typeset ip_nextmask
	typeset switch="-a4"
	typeset txt1
	typeset txt2
	typeset txt3

	# Scan ifconfig command output to find all currently running
	# logical DHCP client interfaces.
	[[ $family == inet6 ]] && switch="-a6"
	for logical_interface in \
	    $( ifconfig $switch 2>/dev/null | \
	    grep "^${interface}:.*RUNNING.*DHCP" | \
	    sed -e 's/: flags.*$//' ); do

		if [[ $family == inet ]]; then
			# Extract address and netmask from ifconfig's
			# inet line.
			ifconfig "$logical_interface" \
			    "$family" 2>/dev/null | \
			    grep "^	inet " | \
			    read txt1 ip_address txt2 ip_netmask txt3
			# Convert prefix hexstring to prefix length.
			ip_prefixlen=$( \
			    netmask_to_prefixlen "" "0x${ip_netmask}" )
		else
			# Extract address and prefix from ifconfig's
			# inet6 line.
			ifconfig "$logical_interface" \
			    "$family" 2>/dev/null | \
			    grep "^	inet6 " | \
			    read txt1 output txt2
			ip_address="${output%/*}"
			ip_prefixlen="${output##*/}"
		fi

		if [[ $g_target_ip_type == exclusive ]]; then
			printf "$c_dhcp_address\n" "$( lookup_host \
			    "$family" "$ip_address" "$ip_prefixlen" )"
		else
			# Dynamically assigned IP addresses are not used
			# when configuring shared-IP zone addresses. Set
			# this flag to support generating a meaningful
			# explanation later.
			g_ip_dynamic="true"
			printf "$c_dhcp_address_x\n" "$( lookup_host \
			    "$family" "$ip_address" "$ip_prefixlen" )"
		fi
	done
}

#
# Display IPv6 autoconfigured addresses currently configured on the interface.
# This function is intended for use on Solaris 10 or earlier.
#
pautoconf_addrs()
{
	typeset interface="$1"
	ip_address=""
	ip_hostname=""
	typeset logical_interface
	typeset output=""
	typeset txt1
	typeset txt2

	# The first IPv6 address configured on the interface is
	# always a kernel-generated link-local address. This
	# address is not flagged "AUTOCONF" even though it was
	# generated according to rfc 4862 rules. Extract the
	# address and prefix from ifconfig's inet6 line.
	ifconfig $interface inet6 2>/dev/null | \
	    grep "^	inet6 " | read txt1 output txt2
	[[ -z $output ]] && return
	ip_address="${output%/*}"
	ip_prefixlen="${output##*/}"
	if [[ $ip_address == fe80:* ]]; then
		if [[ $g_target_ip_type == exclusive ]] ; then
			printf "$c_autoconf_address\n" "$( lookup_host \
			    inet6 "$ip_address" "$ip_prefixlen" )"
		else
			# Dynamically assigned IP addresses are not used
			# when configuring shared-IP zone addresses. Set
			# this flag to support generating a meaningful
			# explanation later.
			g_ip_dynamic="true"
			printf "$c_autoconf_address_x\n" "$( lookup_host \
			    inet6 "$ip_address" "$ip_prefixlen" )"
		fi
	fi
		
	# Scan ifconfig output to find all currently running logical
	# autoconfigured interfaces.
	for logical_interface in \
	    $( ifconfig -a6 2>/dev/null | \
	    grep "^${interface}:.*RUNNING.*ADDRCONF" | \
	    sed -e 's/: flags.*$//' ); do

		# Extract address and prefix from ifconfig's inet6 line.
		ifconfig "$logical_interface" \
		    inet6 2>/dev/null | \
		    grep "^	inet6 " | \
		    read txt1 output txt2
		ip_address="${output%/*}"
		ip_prefixlen="${output##*/}"
		if [[ $g_target_ip_type == exclusive ]]; then
			printf "$c_autoconf_address\n" "$( lookup_host \
			    inet6 "$ip_address" "$ip_prefixlen" )"
		else
			# Dynamically assigned IP addresses are not used
			# when configuring shared-IP zone addresses. Set
			# this flag to support generating a meaningful
			# explanation later.
			g_ip_dynamic="true"
			printf "$c_autoconf_address_x\n" "$( lookup_host \
			    inet6 "$ip_address" "$ip_prefixlen" )"
		fi
	done
}

# Scan lines in a /etc/hostname.* file for a keyword or keyword/value pair.
# The following options are supported. The keyword may occur multiple
# times. The last setting found will be used.
#
#   value Parameter	Search For			Output on Success
#   ---------------	----------			-----------------
#   not provided	keyword only			keyword
#   set to '*'		keyword with non-blank value	value found
#   set to any string	keyword with specific value	value found
#
scan_hostname()
{
	typeset keyword="$1"
	typeset value="$2"
	typeset line
	typeset result=""

	while read line; do
		set -- $line
		while [[ -n $1 ]]; do
			if [[ "$1" == $keyword && -z "$value" ]]; then
				result="$1"
			elif [[ "$1" == $keyword && -n $2 ]]; then
				if [[ "$2" == $value ]]; then
					result="$2"
				fi
				shift
			fi
			shift
		done
	done
	print "$result"
}

#
# Display a single address from a hostname file.
#
# This function appends any statically defined addresses found
# to ${g_ip_addr_list} for the caller.
#
paddr()
{
	typeset addr="$1"
	typeset prefixlen="$2"
	typeset ipmp_group="$3"
	typeset deprecated="$4"
	typeset failover="$5"

	# If we found an address, it could be a numeric IP address or
	# an IP hostname.  Lookup the address in the name servers
	# and output the results in a zonecfg command comment line.
	if [[ -n $addr ]]; then
		outstr="$( lookup_host "$family" \
		    "$addr" "$prefixlen" )"
		if [[ -n $ipmp_group && \
		    -n $deprecated && -z $failover ]] then
			printf "$c_test_address\n" "$outstr"
		else
			printf "$c_static_address\n" "$outstr"

		# Remember statically defined addresses for
		# potential use in zonecfg net resources.
		outstr="$( lookup_host -a "$family" \
		    "$addr" "$prefixlen" )"
			g_ip_addr_list="$g_ip_addr_list $outstr"
		fi
	fi
}

#
# Display static addresses configured in a /etc/hostname.* or
# /etc/hostname6.* file. The caller sets up the hostname.*
# file on stdin for this function.
#
# This script appends any statically defined addresses found
# to ${g_ip_addr_list} for the caller.
#
phostname_addrs()
{
	typeset interface="$1"
	typeset family="$2"

	typeset args
	typeset outstr
	typeset file
	typeset ipmp_group=""

	if [[ $family == inet ]]; then
		file=/etc/hostname.${interface}
	else
		file=/etc/hostname6.${interface}
	fi

	# Find and display the IPMP group name first. The
	# IPMP group name may reside in the main file or
	# in the zeroth logical interface file. Since the
	# last group name found replaces all others during
	# startup, any group name found in the logical
	# interface file takes precedence.
	if [[ -r $file:0 ]]; then
		ipmp_group=$( scan_hostname "group" '*' < $file:0 2>/dev/null )
	fi
	if [[ -z $ipmp_group && -r $file ]]; then
		ipmp_group=$( scan_hostname "group" '*' < $file 2>/dev/null )
	fi
	if [[ -n $ipmp_group ]]; then
		if [[ $family == inet ]]; then
			printf "$c_ipmp_member\n" "IPv4" $ipmp_group
		else
			printf "$c_ipmp_member\n" "IPv6" $ipmp_group
		fi
	fi

	# Read each line of the /etc/hostname.* files. Cover both the
	# physical and the logical interfaces.
	cat $file $file:+([0-9]) 2>/dev/null |
	    while read args ; do
		typeset addr=""
		typeset dest=""
		typeset prefixlen=""
		typeset deprecated=""
	
		set -- $args
	
		# For IPMP enabled interfaces: each address is allowed to
		# failover to another interface by default.
		typeset failover="true"
	
		# Process each token in the line
		while [[ -n $1 ]]; do
			case "$1" in
	
			    # Keywords to be ignored that have no arguments.
			    all-zones | anycast | "-anycast" | qarp | \
			    "-arp" | down | forever | inet | inet6 | local | \
			    "-local" | nud | "-nud" | plumb | preferred | \
			    "-preferred" | private | "-private" | router | \
			    "-router" | standby | "-standby" | trailers | \
			    "-trailers" | up | xmit | "-xmit" | "-zone"  )
				;;
	
			    # Keywords to be ignored that have one argument.
			    auth_algs | broadcast | destination | encaplimit | \
			    encr_algs | encr_auth_algs | index | metric | \
			    modinsert | modremove | mtu | tdst | thoplimit | \
			    token | tsrc | usesrc )
				shift
				;;

			    # Keywords that do not make sense in a hostname.*
			    # file. Drop the entire line.
			    modlist | removeif | unplumb )
				continue 2
				;;
	
			    # Ignore lines with this keyword.
			    # Any attempt to define a non-global zone
			    # address before the zone boots would fail.
			    zone )
				continue 2
				;;
	
			    # Commands to configure a DHCP interface. Handle
			    # the remainder of this line as if it occurred
			    # in a /etc/dhcp.<if> file. We currently ignore
			    # the contents of /etc/dhcp.<if> files.
			    #
			    # Note that the following ifconfig keywords
			    # are only recognized as keywords when preceded
			    # by "auto-dhcp" or "dhcp". They therefore
			    # should be treated as a hostname if encountered
			    # during processing by this case statement:
			    # "drop", "extend", "inform", "ping", "primary"
			    # "release", "start", "status", "wait".
			    auto-dhcp | dhcp )
				pdhcp_addrs "$interface" "$family"
				continue 2
				;;

			    # Flag the interface as a reverse-ARP interface.
			    # There is no way to find which addresses were
			    # configured with reverse-ARP.	
			    auto-revarp )
				print "$c_rarp_enabled"
				;;
	
			    # Set the address on the current logical interface.
			    set )
				# Must have an argument after "set".
				# We may have multiple set subcommands on one
				# line. The last set overwrites any previous
				# addresses for the logical interface.
				if [[ -z $2 ]]; then
					continue 2
				fi
				addr="$2"
				# We could have "addr/prefixlen".
				if [[ "$addr" == */* ]]; then
					prefixlen="${addr#*/}"
					addr="${addr%/*}"
				fi
				shift
				;;

			    # Start definition of a new logical interface.
			    addif )
				# Display any address defined before this
				# point.
				if [[ -n $addr ]]; then
					paddr "$addr" "$prefixlen" \
					    "$ipmp_group" "$deprecated" \
					    "$failover"
				fi

				# Must have an argument after "addif".
				# May have multiple addif blocks on one line.
				# Cannot have multiple addresses per addif.
				if [[ -z $2 ]]; then
					continue 2
				fi

				addr="$2"
				prefixlen=""
				deprecated=""
				failover="true"

				# We could have "addr/prefixlen".
				if [[ "$addr" == */* ]]; then
					prefixlen="${addr#*/}"
					addr="${addr%/*}"
				fi
				shift
				;;
	
			    # Remember manually defined ethernet address
			    # for use by caler of this function.
			    ether )
				if [[ "$2" == *([0-9a-fA-F:]) ]]; then
					g_mac_address="$2"
					g_mac_addr_type="$c_mac_addr_fixed"
	
					shift
				fi
				;;
	
			    # Remember deprecated and failover state for
			    # this line. These flags are used on IPMP
			    # interfaces.
			    deprecated )
				deprecated="true"
				;;
			    "-deprecated" )
				deprecated=""
				;;
			    failover )
				failover="true"
				;;
			    "-failover" )
				failover=""
				;;
	
			    # We already did a pre-scan for IPMP group names.
			    # Ignore the keyword on this detailed scan.
			    group )
				# Must have an argument for "group" keyword.
				if [[ -z $2 ]]; then
					break 2
				fi
				shift
				;;

			    # Convert netmask argument to prefix length.
			    netmask )
				# Must have an argument after "netmask".
				# Can't have netmask without address first.
				# We can have multiple netmasks. Each one
				# overrides any previous value.
				if [[ -z $2 || -z $addr ]]; then
					continue 2
				fi
				prefixlen="$( \
				    netmask_to_prefixlen "$addr" "$2" )"
				shift
				;;

			    # Subnets are specified as <addr>/<prefixlen>.
			    # ifconfig ignores the address part of the subnet.
			    # Extract the prefixlen.
			    subnet )
				# Must have an argument after "subnet".
				# Can't have netmask without address first.
				# We can have multiple subnets. Each one
				# overrides any previous value.
				if [[ -z $2 || -z $addr || \
				    "$2" != */+([0-9]) ]]; then
					continue 2
				fi
				prefixlen=${2#*/}
				shift
				;;
	
			    # What remains is not a recognized keyword.
			    # Assume we have an address of some form.
			    * )
				# The first addr on the line is a local addr
				if [[ -z $addr ]]; then
					addr="$1"
					# We could have "addr/prefixlen".
					if [[ "$addr" == */* ]]; then
						prefixlen="${addr#*/}"
						addr="${addr%/*}"
					fi
	
				# The second addr is a destination addr.
				elif [[ -z $dest ]]; then
					dest="$1"
	
				# Anything else is an error.
				else
					break 2
				fi
			esac
		shift
		done

		# We reached the end of the current line.
		# If we found an address, print it.
		if [[ -n $addr ]]; then
			paddr "$addr" "$prefixlen" "$ipmp_group" \
			    "$deprecated" "$failover"
		fi
	done
}

#
# Scan s10 interface configuration files to generate a network
# interface definition for an S10 or S11 zonecfg script.
#
pnetconfig_s10_interface()
{
	typeset interface="$1"
	typeset if_type
	typeset if_num
	typeset vlan_id
	typeset txt1
	typeset ipaddr

	# MAC address and IP address list may get set by one of the
	# called functions.
	g_mac_address=""
	g_mac_addr_type=""
	g_ip_addr_list=""
	# Flag to indicate a dynamically assigned IP address was found.
	g_ip_dynamic="false"

	#
	# Deconstruct the interface name into it's component parts.
	# For example, the name "e1000g5001" provides
	#	interface type = e1000g
	#	interface number = 1
	#	vlan id = 5
	#
	if_type="${interface%%+([0-9])}"
	vlan_id=""
	typeset -i if_num
	if_num="${interface#${if_type}}"
	if (( if_num >= 1000 )) ; then
		vlan_id=$(( $if_num / 1000 ))
		if_num=$(( $if_num % 1000 ))
	fi

	printf "$c_original_config\n" "$interface"

	# Scan the /etc/hostname.* file for statically defined addresses.
	phostname_addrs "$interface" "inet"

	# If there is a /etc/dhcp.* file, scan running interface
	# for an address assigned by DHCP.
	if [[ -e /etc/dhcp.${interface} ]]; then
		pdhcp_addrs "$interface" "inet"
	fi

	# Scan the /etc/hostname6.* file for statically defined addresses.
	phostname_addrs "$interface" "inet6"
	pautoconf_addrs "$interface"

	# If we did not find a persistently defined MAC address,
	# get the hardware defined ethernet or infiniband mac address
	# if possible.
	# "ifconfig -a" appears to be the only way to display
	# ethernet addresses on inet6 interfaces.
	if [[ -z $g_mac_address ]]; then
		ifconfig -a | awk '
			/^[a-zA-Z]/ { iface = $1 }
			/^\tether / { print iface " " $2 }
			/^\tipib /  { print iface " " $2 }' | \
		    grep "^${interface}:" | \
		    read txt1 g_mac_address
		if [[ -n $g_mac_address ]]; then
 			# The MAC address could have been manually changed in
			# other ways. Verify the "locally administered" bit
			# is clear before calling it factory assigned.
			typeset -i firstbyte=16#${g_mac_address%%:*}
			if [[ $interface == ibd* ]] ; then
				g_mac_addr_type="$c_mac_addr_infiniband"
			elif (( (firstbyte & 2) == 0 )) &&
			    [[ $interface != ibd* ]] ; then
				g_mac_addr_type="$c_mac_addr_factory"
			else
				g_mac_addr_type="$c_mac_addr_fixed"
			fi
		fi
	fi
	if [[ -n $g_mac_address ]]; then
		printf "$c_mac_address\n" "$g_mac_addr_type" "$g_mac_address"
	fi

	# Notice that dynamically assigned addresses do not work on shared-IP.
	if [[ $g_ip_dynamic == true ]]; then
		printf "$c_dynamic_addr_notice\n"
	fi

	if (( g_target_release >= 11 )) ; then
		if [[ $interface == ibd* ]] ; then
			# Infiniband partitions must be allocated to the
			# zone through a net resource.
			printf "add net\n"
			printf "\tset physical=%s\n" "$interface"
			printf "\tend\n"
		else
			# Generate an exclusive-IP style anet resource
			# for the interface.
			printf "add anet\n"
			printf "\tset linkname=%s\n" "$interface"
			printf "\tset lower-link=change-me\n"
			if [[ -n $g_mac_address || -n $vlan_id ]]; then
				printf "\t$c_uncomment_linkprop\n"
			fi
			if [[ -n $g_mac_address ]]; then
				printf "\t# set mac-address=%s\n" \
				    "$g_mac_address"
			fi
			if [[ -n $vlan_id ]]; then
				printf "\t# set vlan-id=%s\n" "$vlan_id"
			fi
			printf "\tend\n"
		fi
	else	# (( g_target_release == 10 ))
		# Generate shared-IP style net resources for each statically
		# defined IP address found.
		for ipaddr in $g_ip_addr_list; do
			printf "add net\n"
			printf "\tset address=%s\n" "$ipaddr"
			printf "\tset physical=change-me\n"
			printf "\tend\n"
		done
	fi
}

#
# Display addresses that are configured under ipadm, including:
#  - persistently configured static addresses
#  - dhcp assigned addresses
#  - IPv6 autoconfigured addresses
#
pipadm_addrs()
{
	typeset interface="$1"
	typeset netsvc="$2"
	typeset ip_addrobj
	typeset ip_type
	typeset ip_flags
	typeset ip_addr
	typeset pflags
	typeset ipmp_group
	typeset ip_prefixlen
	typeset output_str
	typeset cid_type
	typeset cid_value

	# Exit if this interface was not persistently defined.
	pflags=$( ipadm show-\if \ -po persistent,group $interface 2>/dev/null )
	if [[ $netsvc == default && "$pflags" == +(-) ]]; then
		return
	fi

	# Determine if the interface is a member of an IPMP group.
	ipmp_group=$( ipadm show-ifprop \
	    -co persistent -m ip -p group $interface 2>/dev/null )
	if [[ -n $ipmp_group ]]; then
		printf "$c_ipmp_member\n" "IP" "$ipmp_group"
	fi

	# Walk the addresses on the interface.
	ipadm show-addr -p \
	    -o addrobj,type,persistent,addr ${interface}/ 2>/dev/null | \
	    while IFS=":" read ip_addrobj ip_type ip_flags ip_addr; do
		# Skip to next address if we only have "?".
		# This can happen on dhcp interfaces that have not
		# aquired an address yet.
		[[ $ip_addr == \? ]] && continue

		# We expect "address" followed by optional "/prefixlen".
		ip_prefixlen="${ip_addr##*/}"
		if [[ $ip_prefixlen == $ip_addr ]]; then
			ip_prefixlen=""
		else
			ip_addr="${ip_addr%/${ip_prefixlen}}"
		fi

		# The address could be a numeric IP address or an IP hostname.
		# Lookup the address in the name servers and output the
		# results in a zonecfg command comment line.
		output_str="$( lookup_host "" "$ip_addr" "$ip_prefixlen" )"
		case $ip_type in
		    static)
			# Use only persistently defined static addresses
			# when running under network/physical:default
			if [[ $netsvc != default ||
			    $ip_flags != +(-) ]]; then
				# Addresses on IPMP member interfaces are
				# used as test addresses. This function is
				# never called with an IPMP interface.
				if [[ -n $ipmp_group ]]; then
					printf "$c_test_address\n" \
					    "$output_str"
				else
					printf "$c_static_address\n" \
					    "$output_str"
				fi
			fi
			;;
		    dhcp)
			printf "$c_dhcp_address\n" "$output_str"
			ipadm show-addr -p \
			    -o cid-type,cid-value $ip_addrobj |
			    IFS=":" read cid_type cid_value 2> /dev/null
			printf "$c_dhcp_clientid\n" "$cid_value" "$cid_type"
			;;
		    addrconf)
			printf "$c_autoconf_address\n" "$output_str"
			;;
		esac
	done
}

#
# Scan s11 interface configuration files and persistent information in dladm
# and ipadm on an S11 source system to generate a network interface definition
# for an S11 zonecfg script.
#
pnetconfig_s11_interface()
{
	typeset interface="$1"
	typeset netsvc="$2"
	typeset class=""
	typeset device
	typeset if_type
	typeset hostname_found
	typeset vlan_id
	typeset link
	typeset over
	typeset mac_link
	typeset mac_type

	# MAC address and IP address list may get set by one of the
	# called functions.
	g_mac_address=""
	g_mac_addr_type=""
	g_ip_addr_list=""

	# This flag is a noop under this function. It exists to mirror
	# the environment set up by pnetconfig_s10_interface()
	g_ip_dynamic="false"

	# Skip virtual interfaces types.
	class=$( ipadm show-if -po class $interface )
	if [[ " loopback vni ipmp " == *\ $class\ * ]]; then
		return
	fi

	# Skip IP Tunnel interfaces.
	# We prefer the persistent configuration information, but will use
	# temporary for dynamically configured addresses.
	class=$( dladm show-link \
	    -Ppo class $interface 2>/dev/null )
	if [[ -z $class ]]; then
		class=$( dladm show-link \
		    -po class $interface 2>/dev/null )
	fi
	if [[ $class == iptun ]]; then
		return
	fi

	# Skip cgtp interfaces
	if [[ $class == "phys" ]]; then
		device==$( dladm show-phys -Ppo device $interface )
		if [[ ${device%%+([0-9])} == cgtp ]]; then
			return
		fi
	fi

	# Skip ppp interfaces.
	if_type="${interface%%+([0-9])}"
	if [[ " ppp " == *\ $if_type\ * ]]; then
		return
	fi

	printf "$c_original_config\n" "$interface"

	# Scan addresses under ipadm.
	pipadm_addrs "$interface" "$netsvc"

	# Get the persistently defined generic MAC address property if any.
	# This overrides any link-specific MAC address property.
	if [[ -z $g_mac_address ]]; then
		g_mac_address=$(dladm show-linkprop \
		    -Pco value -p mac-address $interface)
		if [[ -n $g_mac_address ]]; then
			g_mac_addr_type="$c_mac_addr_fixed"
		fi
	fi

	# Collect datalink parameters based on link type.
	vlan_id=""
	dladm show-link -Ppo link,class,over $interface | \
	    IFS=":" read link class over
	case $class in
	    aggr )
		if [[ -z $g_mac_address ]]; then
			# dladm show-aggr provides the MAC address policy
			# used when creating the aggregation.
			g_mac_address="$(dladm show-linkprop \
			    -Pco default -p mac-address $link)"
			case $( dladm show-aggr -Ppo addrpolicy $link ) in
			    auto ) g_mac_addr_type="$c_mac_addr_factory" ;;
			    fixed ) g_mac_addr_type="$c_mac_addr_fixed" ;;
			esac
		fi
		;;
	    vlan )
		# VLANs always use the MAC address of the physical link.
		vlan_id="$(dladm show-vlan -Ppo vid $link)"
		g_mac_address="$(dladm show-linkprop \
		    -Pco value -p mac-address $over)"
		if [[ -n $g_mac_address ]]; then
			g_mac_addr_type="$c_mac_addr_fixed"
		else
			g_mac_address="$(dladm show-linkprop \
			    -Pco default -p mac-address $over)"
			g_mac_addr_type="$c_mac_addr_factory"
		fi
		;;
	    vnic )
		# VNICs offer the most flexible MAC assignment policies.
		vlan_id="$(dladm show-vnic -Ppo vid $link)"
		if [[ -z $g_mac_address ]]; then
			dladm show-vnic \
			    -Ppo macaddress,macaddrtype $link | \
			    IFS=":" read g_mac_address mac_type
			case $mac_type in
			    factory ) g_mac_addr_type="$c_mac_addr_factory" ;;
			    fixed ) g_mac_addr_type="$_mac_addr_fixed" ;;
			    random ) g_mac_addr_type="$c_mac_addr_random" ;;
			    vrrp ) g_mac_addr_type="$c_mac_addr_vrrp" ;;
			esac
		fi
		;;
	    simnet )
		# simnet is an undocumented feature with uncommitted stability
		if [[ -z $g_mac_address ]]; then
			g_mac_address="$( dladm show-simnet \
			    -Ppo macaddress $link 2>/dev/null )"
			g_mac_addr_type="$_mac_addr_random"
		fi
		;;
	    part )
		# Inifiband partition
		if [[ -z $g_mac_address ]]; then
			# MAC addresses on infiniband are dynamically
			# generated and can change from one boot to another.
			# Grab the current value of the MAC address.
			g_mac_address="$(dladm show-linkprop \
			    -co value -p mac-address $interface 2>/dev/null)"
			g_mac_addr_type="$c_mac_addr_infiniband"
		fi
		;;
	    * )
		# The remaining interface types may not have a MAC
		# address. If they do, assume the default MAC is "factory".
		if [[ -z $g_mac_address ]]; then
			g_mac_address="$(dladm show-linkprop \
			    -Pco default -p mac-address $interface)"
			g_mac_addr_type="$c_mac_addr_factory"
		fi
		;;
	esac

	if [[ -n $g_mac_address ]]; then
		# The MAC address could have been manually changed in
		# other ways. Verify the "locally administered" bit on
		# a MAC address we think is factory assigned is clear
		# before officially calling it factory assigned.
		typeset -i firstbyte=16#${g_mac_address%%:*}
		if [[ $g_mac_addr_type == $c_mac_addr_factory ]] && \
		    (( (firstbyte & 2) != 0 )) ; then
			g_mac_addr_type="$c_mac_addr_fixed"
		fi

		# Display MAC address from datalink information.
		printf "$c_mac_address\n" "$g_mac_addr_type" "$g_mac_address"
	fi

	if [[ "$class" == "part" ]] ; then
		# Infiniband partitions must be allocated to the
		# zone through a net resource.
		printf "add net\n"
		printf "\tset physical=%s\n" "$interface"
		printf "\tend\n"
	else
		# All other types if interfaces are handled as
		# anet resources.
		printf "add anet\n"
		printf "\tset linkname=%s\n" "$interface"
		printf "\tset lower-link=change-me\n"
		if [[ -n $g_mac_address || 
		    ( -n $vlan_id && $vlan_id != 0 ) ]]; then
			printf "\t$c_uncomment_linkprop\n"
		fi
		if [[ -n $g_mac_address ]]; then
			printf "\t# set mac-address=%s\n" "$g_mac_address"
		fi
		if [[ -n $vlan_id && $vlan_id != 0 ]]; then
			printf "\t# set vlan-id=%s\n" "$vlan_id"
		fi
		printf "\tend\n"
	fi
}

#
# Print zonecfg commands to support existing network configuration.
#
pnetconfig()
{
	typeset interface
	typeset tunnel_ifs="ip.tun ip6.tun ip.6to4tun"

	if (( g_source_release >= 11 )) ; then
		# Must have read access to smf repository.
		[[ ! -r /etc/svc/repository.db ]] && return
		if [[ $(svcprop -p netcfg/active_ncp \
		    svc:/network/physical:default 2> /dev/null) \
		     == "DefaultFixed" ]]; then
			for interface in $( get_s11_interfaces ); do
				pnetconfig_s11_interface "$interface" "default"
			done
		else
			for interface in $( get_s11_nwam ); do
				pnetconfig_s11_interface "$interface" "nwam"
			done
		fi
	elif (( g_target_release >= 11 )) ; then
		for interface in $( get_s10_interfaces ); do
			# Skip virtual, tunnel, cgtp, and ppp interfaces.
			if [[ " xx lo vni $tunnel_ifs cgtp ppp " != \
			    *\ ${interface%%+([0-9])}\ * ]]; then
				pnetconfig_s10_interface "$interface"
			fi
		done
	else
		for interface in $( get_s10_interfaces ); do
			# Skip loopback, ppp, and tunnel interfaces.
			# vni addresses do need to be configured
			# with shared-IP net resources.
			if [[ " xx lo ppp $tunnel_ifs " != \
			    *\ ${interface%%+([0-9])}\ * ]]; then
				pnetconfig_s10_interface "$interface"
			fi
		done
	fi
}


#
# Print the system configuration as an exported zonecfg on stdout.
#
pconfig()
{
	typeset brand
	typeset sched
	typeset procs
	typeset cpu_val
	typeset mem_val
	typeset swap_val
	typeset v

	if (( g_source_release == 10 && g_target_release == 11 )); then
		brand="solaris10"
	elif (( g_source_release == 9 )); then
		brand="solaris9"
	elif (( g_source_release == 8 )); then
		brand="solaris8"
	else
		brand="none"
	fi

	printf "create -b\n"
	printf "set zonepath=/zones/%s\n" "$g_hname"
	if [ $brand != "none" ]; then
		printf "set brand=%s\n" "$brand"
	fi

	printf "add attr\n"
	printf "\tset name=\"zonep2vchk-info\"\n"
	printf "\tset type=string\n"
	printf "\tset value=\"p2v of host %s\"\n" "$g_hname"
	printf "\tend\n"

	printf "set ip-type=%s\n" "$g_target_ip_type"

	print $c_hostid
	printf "# set hostid=%s\n" "$(hostid)"

	#
	# Get configured scheduling class
	#
	if [[ -s /etc/dispadmin.conf ]] ; then
		sched=$(grep '^DEFAULT_SCHEDULER=' \
		    /etc/dispadmin.conf 2>/dev/null | awk -F= '{ print $2 }')
	fi
	if [[ -n $sched ]] ; then
		print $c_sched
		printf "set scheduler=%s\n" "$sched"
	fi

	# Get number of processes
	procs=$(kstat -m unix -n var | grep v_proc | awk '{ print $2 }')
	# Reduce max-processes if it is a modern default
	(( procs >= 30000 )) && (( procs = procs - 10000 ))
	# S11 supports max-processes rctl
	if (( g_target_release >= 11 )) ; then
		print $c_max_procs_11
		printf "set max-processes=%s\n" "$procs"
	else
		print $c_max_procs_10
	fi
	(( procs = procs * 2 ))
	printf "set max-lwps=%s\n" "$procs"

	#
	# Get number of CPUs
	#
	typeset -i cpu_val=$(psrinfo | wc -l)
	printf "add attr\n"
	printf "\tset name=num-cpus\n"
	printf "\tset type=string\n"
	printf "\tset value=\"original system had %s cpus\"\n" "$cpu_val"
	printf "\tend\n"

	print $c_capped_cpu1
	print $c_capped_cpu2
	printf "# add capped-cpu\n"
	printf "#\tset ncpus=%s.0\n" "$cpu_val"
	printf "#\tend\n"
	print $c_ded_cpu
	printf "# add dedicated-cpu\n"
	printf "#\tset ncpus=%s\n" "$cpu_val"
	printf "#\tend\n"

	#
	# Get memory info.
	#
	mem_val=$(prtconf | nawk '{
		if (substr($0, 1, 13) == "Memory size: ") {
			printf $3
			exit
		}
	}')

	# compute swap as memory plus disk swap
	swap_val=0
	swap -l | tail +2 | awk '{ print $4 }' | while read v ; do
		# convert blocks to kilobytes
		(( swap_val = swap_val * 2 ))
		# Add size of swap device to total swap
		(( swap_val = swap_val + v ))
	done
	# Convert swap from kb to mb
	(( swap_val = swap_val / 1024 ))
	(( swap_val = swap_val + mem_val ))
		
	print $c_capped_mem1
	print $c_capped_mem2
	printf "# add capped-memory\n"
	printf "#\tset physical=%sM\n" "$mem_val"
	printf "#\tset swap=%sM\n" "$swap_val"
	printf "#\tend\n"

	pnetconfig

	#
	# All done
	#
	printf "exit\n"
}

warn_be()
{
	typeset be_active=""
	typeset be_list=""
	typeset i

	setup_chk "be"

	# Check only valid on s10+
	(( g_source_release < 10 )) && return

	# "lustatus" can only be run as root.  Since we want the basic checks
	# to work for a non-privileged user, we look at the lutab directly.

	# Check for any lu stuff on solaris 10.
	if (( g_source_release == 10 )) ; then
		[[ ! -f /etc/lutab  || ! -f /etc/lu/.BE_CONFIG ]] && return
	fi

	#
	# Get the name of the current boot environment and the list of
	# boot environments
	#
	if (( g_source_release == 10 )) ; then
		# Active be from .BE_CONFIG
		be_active=$(grep LUBECF_BE_NAME /etc/lu/.BE_CONFIG |
		    cut -d'=' -f 2)
		be_list=$(grep ':C:0$' /etc/lutab | cut -d':' -f2)
	else
		# Active be marked with "N"
		be_active=$(beadm list -H | cut -d';' -f 1,3 |
		    grep ';.*N.*' | cut -d';' -f 1)
		be_list=$(beadm list -H | cut -d';' -f 1)
	fi	
	for i in $be_list ; do
		[[ $i == "$be_active" ]] && continue
		warn_item "$i"
	done
}

#
# Identify physical interfaces that would have to be available on
# the destination system with exclusive-IP zones.
# This is not an issue with Solaris 11 destinations.
#
warn_physif()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset if_type
	typeset if_num
	typeset vlan_id
	typeset if_num
	typeset interface

	impacted_ifs=""
	for interface in $if_list; do
		# Skip loopback, vni, cgtp, and tunnel interfaces
		if_type="${interface%%+([0-9])}"
		if [[ " xx lo vni cgtp ip.tun ip6.tun ip.6to4tun " == \
		    *\ $if_type\ * ]]; then
			continue
		fi

		# There is no need to have a separate physical interface for
		# each VLAN. Remove the vlan-id from the interface name.
		if [[ "$interface" == *[0-9][0-9][0-9][0-9] ]]; then
			if_type="${interface%%+([0-9])}"
			if_num="${interface#${if_type}}"
			vlan_id=${if_num%[0-9][0-9][0-9]}
			if_num="${if_num##${vlan_id}?(0)?(0)}"
			interface="${if_type}${if_num}"
		fi

		# Skip duplicate interface names.
		if [[ " $impacted_ifs " != *\ $interface\ * ]]; then
			impacted_ifs="$impacted_ifs $interface"
			warn_item "$interface"
		fi
	done
}

#
# Identify configuration files that would be impacted by changing
# the interface names in an exclusive-IP zone.
# This is not an issue with Solaris 11 destinations.
#
warn_ifname()
{
	typeset if_list="$1"
	typeset if_type
	typeset impacted_ifs
	typeset prefix
	typeset interface
	typeset dirs
	typeset tmpfile
	typeset SP='(^| |	|:|$)'
	typeset pattern
	typeset patterns
	typeset file

	if (( g_target_release >= 11 )) ; then
		return
	fi

	# Collect list of interfaces to search for.
	# Skip loopback, vni, cgtp, and tunnel interfaces
	tmpfile="/tmp/zonep2vchk_ifs.$$"
	for interface in $if_list; do
		if_type="${interface%%+([0-9])}"
		if [[ " xx lo vni cgtp ip.tun ip6.tun ip.6to4tun " \
		    != *\ $if_type\ * ]]; then
			impacted_ifs="$impacted_ifs $interface"
			print "$SP($interface)$SP" >> $tmpfile
		fi
	done
	if_list="$impacted_ifs"

	# Display names of files that have an embeded interface
	# name in the file name.
	set -A patterns \
	    "/etc/hostname.*" \
	    "/etc/dhcp.*" \
	    "/etc/hostname6.*" \
	    "/etc/inet/gnome-system-tools/defaultrouter.*" \
	    "/etc/inet/gnome-system-tools/hostname.*"

	for pattern in "${patterns[@]}"; do
		prefix="${pattern%\*}"
		for file in ${pattern}; do
			[[ ! -e $file ]] && continue
			interface=${file#$prefix}
			interface=${interface%:+([0-9])}
			if [[ " $if_list " == *\ $interface\ * ]]; then
				warn_item "$file"
			fi
		done
	done

	# Display names of files that contain an interface
	# name inside the file.
	dirs="
	    /etc/inet
	    /etc/ipf
	    /etc/gateways"

	for dir in $dirs; do
		find $dir -type f -print 2>/dev/null
	done | while read file; do
		if ( egrep -f $tmpfile $file > /dev/null 2>&1 ); then
			warn_item "$file"
		fi
	done

	# Clean up our file litter.
	rm $tmpfile
}

# Check if Mobile-IP is configured.
warn_mobile_ip()
{
	if [[ -e /etc/inet/mipagent.conf ]]; then
		warn_feature "mobileip" "$w_mobile_ip"
	fi
}

# Identify interfaces that obtain their IPv4 addresses from a DHCP server.
warn_dhcp_assigned()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset interface
	typeset SP='(^| |	|$)'
	typeset pflags
	typeset addrtype

	# Get list of interfaces that have DHCP client enabled.
	impacted_ifs=""
	for interface in $if_list; do
		if (( g_source_release < 11 )) ; then

			# Look for "dhcp" keywords in the /etc/hostname.* file
			# or for existance of a /etc/dhcp.* file.
			if [[ -e /etc/hostname.${interface} ]]; then
				if [[ -e /etc/dhcp.${interface} ]] || \
				    egrep "$SP(auto-dhcp|dhcp)$SP" \
				    /etc/hostname.${interface} \
				    > /dev/null 2>&1; then
					impacted_ifs="$impacted_ifs $interface"
					continue
				fi
			fi
		else	# (( g_source_release >= 11 ))
			# Look for dhcp addresses that are persistently
			# defined by ipadm on the interface.
			ipadm show-addr -p \
			    -o persistent,type ${interface}/ 2>/dev/null | \
			    while IFS=":" read pflags addrtype; do
				if [[ "$pflags" != +(-) && \
				    "$addrtype" == "dhcp" ]]; then
					impacted_ifs="$impacted_ifs $interface"
					continue 2
				fi
			done
		fi
	done
	if [[ -n $impacted_ifs ]]; then
		warn_feature "dhcp" "$w_dhcp_assigned" "$impacted_ifs"
	fi
}

# Identify interfaces with reverse-ARP enabled.
# Persistently defined reverse ARP interfaces are not supported on S11.
warn_rarp_assigned()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset interface
	typeset SP='(^| |	|$)'

	# Get list of interfaces that have reverse-ARP enabled.
	impacted_ifs=""
	for interface in $if_list; do

		# Look for "auto-revarp" keyword in the /etc/hostname.* file.
		if (egrep "$SP(auto-revarp)$SP" /etc/hostname.${interface} ) \
		    > /dev/null 2>&1; then
			impacted_ifs="$impacted_ifs $interface"
			continue
		fi

	done
	if [[ -n $impacted_ifs ]]; then
		warn_feature "rarp" "$w_rarp_assigned" "$impacted_ifs"
	fi
}

# Scan the ndpd.conf file for the value of a keyword associated with
# the specified interface. Look for the "ifdefault" line if no interface
# is specified.
scan_ndpd_conf()
{
	typeset interface="$1"
	typeset keyword="$2"
	typeset found_interface=""
	typeset line

	while read line; do
		set -- $line
		while [[ -n $1 ]]; do
			case $1 in
			    "ifdefault")
				if [[ -z $interface ]]; then
					found_interface=true
				fi
				;;
			    "if")
				if [[ -n "$2" && "$2" == "$interface" ]] then
					found_interface=true
				fi
				shift
				;;
			    "prefixdefault")
				found_interface=""
				;;
			    "prefix")
				found_interface=""
				;;
			    "$keyword")
				if [[ -n $2 ]]; then
					if [[ -n $found_interface ]]; then
						print "$2"
						return
					fi
					shift
				fi
				;;
			    \#*)
				continue 2
				;;
			esac
			shift
		done
	done
}

# Identify interfaces that have IPv6 stateless address autoconfiguration
# enabled.
warn_ipv6_autoconf()
{
	typeset if_list="$1"
	typeset ndpd_conf
	typeset autoconf_default
	typeset impacted_ifs
	typeset autoconf_value
	typeset device
	typeset pflags
	typeset addrtype
	typeset interface
	typeset files

	# in.ndpd must be enabled for autoconfiguration to work.
	if [[ "$(svcs -Ho state svc:/network/routing/ndp:default)" \
	    != online ]] then
		return
	fi

	# Get the location of the ndpd.conf file.
	ndpd_conf="$( \
	    svccfg -s svc:/network/routing/ndp:default \
	    listprop routing/config_file 2>/dev/null | awk '{print $3}')"
	if [[ -z $ndpd_conf ]]; then
		ndpd_conf="/etc/inet/ndpd.conf"
	fi

	# Get the default StatelessAddrConf setting if any from ndp.conf.
	if [[ -e $ndpd_conf ]]; then
		autoconf_default="$( scan_ndpd_conf \
		    "" "StatelessAddrConf" < $ndpd_conf )"
	fi

	# The StatelessAddrConf setting of the ndp service overrides
	# the default setting in ndp.conf.
	if [[ -z $autoconf_default && \
	    "$(svccfg -s svc:/network/routing/ndp:default \
	    listprop routing/stateless_addr_conf 2> /dev/null)" \
	    == *true ]]; then
		autoconf_default=true
	fi

	# Get list of interfaces that have autoconfiguration enabled.
	impacted_ifs=""
	for interface in $if_list; do
		autoconf_value=""

		if (( g_source_release < 11 )); then
			# Skip loopback, vni, and tunnel interfaces
			device="${interface%%+([0-9])}"
			[[ " xx lo vni ip.tun ip6.tun ip.6to4tun " \
			    == *\ $device\ * ]] && continue

			files=$( ls /etc/hostname6.${interface} \
			    /etc/hostname6.${interface}:+([0-9]) 2>/dev/null )
			if [[ -n $files ]]; then
				# Does the ndpd.conf file have a
				# StatelessAddrConf setting for the current
				# interface?
				if [[ -e $ndpd_conf ]]; then
					autoconf_value="$( scan_ndpd_conf \
					    "$interface" "StatelessAddrConf" \
					    < $ndpd_conf )"
				fi

				# Use the setting of the interface
				# StatelessAddrConf value if one exists,
				# otherwise follow the default setting.
				if [[ "$autoconf_value" == "true" ]]; then
					impacted_ifs="$impacted_ifs $interface"
				elif [[ -z "$autoconf_value" && \
				    "$autoconf_default" == "true" ]]; then
					autoconf_value="true"
					impacted_ifs="$impacted_ifs $interface"
				fi
			fi

		else	# (( g_source_release >= 11 ))
			# Look for autoconfigured addresses that are
			# persistently defined by ipadm on the interface.
			ipadm show-addr -p \
			    -o persistent,type ${interface}/ 2> /dev/null | \
			    while IFS=":" read pflags addrtype; do
				if [[ "$pflags" != +(-) && \
				    "$addrtype" == "addrconf" ]]; then
					impacted_ifs="$impacted_ifs $interface"
					continue 2
				fi
			done
		fi
	done

	if [[ -n $impacted_ifs ]]; then
		warn_feature "v6autoconf" "$w_ipv6_autoconf" "$impacted_ifs"
	fi
}

# Identify link aggregation interfaces.
# warn_datalink() is used instead of this function on Solaris 11 systems.
warn_aggregation()
{
	impacted_ifs=""
	aggr=""
	typeset line

	# Parsable form of dladm show-aggr command contains
	# zero or more aggregation definitions. Each definition
	# consists of an "aggr" line followed by one or more
	# "dev" lines.
	dladm show-aggr -p 2>/dev/null | \
	    while read line; do
		set $line
		if [[ $1 == aggr ]]; then
			shift
			# The aggregation interface name available to
			# the IP layer is the interger value of the
			# aggregation key appended to the string "aggr".
			while [[ -n $1 ]]; do
				if [[ "${1%%=*}" == key ]]; then
					aggr="aggr${1#*=}"
					break
					fi
				shift
			done
			impacted_ifs="$impacted_ifs $aggr"
		fi
	done
	if [[ -n $impacted_ifs ]]; then
		warn_feature "aggr" "$w_link_aggregation" "$impacted_ifs"
	fi
}

# Identify datalink features that must be configured in the global zone.
# Skip physical links because they automatically exist when hardware exists.
# Skip vlan links because their special checks are handled by warn_vlan.
# Skip iptun links because they can be configured in a zone.
# This function is called on S11 systems or later.
warn_datalink()
{
	typeset class
	typeset ifs
	typeset secobjs
	typeset impacted_ifs

	for class in aggr part vnic simnet etherstub bridge; do
		ifs=$( dladm show-link -pPo link,class | \
		    grep ":$class" | sed 's/:.*//' )
		[[ -z $ifs ]] && continue
		case $class in
		    aggr)
			warn_feature "aggr" "$w_link_aggregation" "$ifs";;
		    part)
			warn_feature "ibpart" "$w_ibpart" "$ifs";;
		    vnic)
			warn_feature "vnic" "$w_vnic" "$ifs" ;;
		    simnet )
			# simnet is an undocumented feature
			# with uncommitted stability
			warn_feature "simnet" "$w_simnet" "$ifs" ;;
 		    etherstub)
			warn_feature "etherstub" "$w_etherstub" "$ifs" ;;
		    bridge)
			warn_feature "bridge" "$w_bridge" "$ifs" ;;
		esac
	done
}

# Identify Wireless WPA or WEP secure objects.
# Wireless interfaces do not exist before Solaris 11.
warn_secobj()
{
	# Extract secure object names from the dladm output.
	if (( g_source_release >= 11 )) ; then
		secobjs=$( dladm show-secobj -Ppo object )
	fi
	if [[ -n $secobjs ]]; then
		impacted_ifs=$( print $secobjs )
		warn_feature "secobj" "$w_secobj" "$secobjs"
	fi
}

# Identify VLAN interfaces.
warn_vlan()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset if_type
	typeset interface
	typeset vlan
	typeset vid

	impacted_ifs=""
	if (( g_source_release >= 11 )) ; then
		# Extract vlan names from the dladm output.
		dladm show-vlan -Ppo link,vid | \
		    while IFS=":" read vlan vid; do
			impacted_ifs="$impacted_ifs $vlan"
		done

	else # S8, S9, S10
		for interface in $if_list; do
			# Skip loopback, vni, cgtp, and tunnel interfaces
			if_type="${interface%%+([0-9])}"
			if [[ " xx lo vni cgtp ip.tun ip6.tun ip.6to4tun " \
			    == *\ $if_type\ * ]]; then
				continue
			fi
			if [[ "$interface" == *[0-9][0-9][0-9][0-9] ]]; then
				impacted_ifs="$impacted_ifs $interface"
			fi
		done
	fi
	if [[ -n $impacted_ifs ]]; then
		warn_feature "vlan" "$w_vlan" "$impacted_ifs"
	fi
}

# Identify IPv4 and IPv6 interfaces that have IP forwarding enabled.
# This is not an issue with Solaris 11 destinations that primarily
# use exclusive IP zones.
warn_forwarding()
{
	typeset if_list="$1"
	typeset forwarding_default
	typeset impacted_ifs
	typeset if_type
	typeset SP='(^| |	|$)'
	typeset ipv
	typeset interface
	typeset files

	# Repeat for IPv4 and IPv6.
	for ipv in 4 6; do

		# Get default setting for forwarding from the
		# the forwarding service definition.
		forwarding_default=""
		if [[ "$(svccfg -s \
		    svc:/network/ipv${ipv}-forwarding:default \
		    listprop general/enabled 2> /dev/null)" == *true ]]; then
			forwarding_default=true
		fi

		# Get list of interfaces that have forwarding enabled.
		impacted_ifs=""
		for interface in $if_list; do
			# Skip loopback and vni interfaces.
			if_type="${interface%%+([0-9])}"
			if [[ " xx lo vni " == *\ $if_type\ * ]]; then
				continue
			fi

			# Identify hostname files to search.
			if [[ $ipv == 4 ]]; then
				files=$( ls /etc/hostname.${interface} \
				    /etc/hostname.${interface}:+([0-9]) \
				    2>/dev/null )
			else
				files=$( ls /etc/hostname6.${interface} \
				    /etc/hostname.${interface}6:+([0-9]) \
				    2>/dev/null )
			fi
			[[ -z $files ]] && continue

			# The interface forwarding is determined by the
			# following (in order of priority):
			#   1) "router" keyword setting in /etc/hostname* file
			#   2) default setting from forwarding service
			#   3) false
			if ( cat $files | egrep "$SP(router)$SP" ) \
			    >/dev/null 2>&1; then
				impacted_ifs="$impacted_ifs $interface"
			elif ( cat $files | egrep "$SP(-router)$SP" ) \
			    >/dev/null 2>&1; then
				true
			elif [[ $forwarding_default == true ]]; then
				impacted_ifs="$impacted_ifs $interface"
			fi
		done

		# Write list of impacted interfaces.
		if [[ -n $impacted_ifs ]]; then
			if [[ $ipv == 4 ]]; then
				warn_feature "v4forward" \
				    "$w_ipv4_forwarding" "$impacted_ifs"
			else
				warn_feature "v6forward" \
				    "$w_ipv6_forwarding" "$impacted_ifs"
			fi
		fi
	done
}

# Determine if any static routes other than a default route are
# configured on the system.
# This is not an issue with Solaris 11 destinations
# that primarily use exclusive IP zones.
warn_static_routes()
{
	typeset SP='(^| |	|$)'

	if ( route -p show | \
	    grep -v "No persistent routes are defined" |
	    grep -v "$SP(default)$SP" > /dev/null ); then
		warn_feature "staticroute" "$w_static_routes"
	fi
}

# Identify IPMP groups.
# This is not an issue with Solaris 11 destinations
# that primarily use exclusive IP zones.
warn_ipmp_group()
{
	typeset if_list="$1"
	typeset group
	typeset group_list
	typeset interface
	typeset files

	# Find all IPMP group names.
	typeset group_list=""
	for interface in $if_list; do
		files=$( ls /etc/hostname.${interface} \
		    /etc/hostname.${interface}:0 \
		    /etc/hostname6.${interface} \
		    /etc/hostname6.${interface}:0 2>/dev/null )
		if [[ -n $files ]]; then
			group=$( cat $files | scan_hostname "group" '*' )
			if [[ -n $group && \
			    " $group_list " != *\ $group\ * ]]; then
				group_list="$ipmp_group_list $group"
			fi
		fi
	done

	if [[ -n "$group_list" ]]; then
		warn_feature "ipmpgroup" "$w_ipmp_group" "$group_list"
	fi
}

# Identify IP tunnel interfaces.
# This is not an issue with Solaris 11 destinations
# that primarily use exclusive IP zones.
warn_iptun()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset interface
	
	impacted_ifs=""
	for interface in $if_list; do
		if [[ "$interface" == ip.tun*([0-9]) ||
		    "$interface" == ip6.tun*([0-9]) ||
		    "$interface" == ip.6to4tun*([0-9]) ]]; then
			impacted_ifs="$impacted_ifs $interface"
		fi
	done
	if [[ -n $impacted_ifs ]]; then
		warn_feature "iptun" "$w_iptun" "$impacted_ifs"
	fi
}

# Identify VNI interfaces.
# VNI is not an issue with Solaris 11 destinations
# that primarily use exclusive IP zones.
warn_vni()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset interface

	impacted_ifs=""
	for interface in $if_list; do
		if [[ "$interface" == vni*([0-9]) ]]; then
			impacted_ifs="$impacted_ifs $interface"
		fi
	done
	if [[ -n $impacted_ifs ]]; then
		warn_feature "vni" "$w_vni" "$impacted_ifs"
	fi
}

# Identify cgtp interfaces.
warn_cgtp()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset interface
	typeset device
	typeset flags

	impacted_ifs=""
	if (( g_source_release >= 11 )) ; then
		dladm show-phys -Ppo link,device,flags | \
		    while IFS=":" read interface device flags; do
			if [[ "$device" == cgtp*([0-9]) && \
			    "$flags" != *r* ]]; then
				impacted_ifs="$impacted_ifs $interface"
			fi
		done
	else
		for interface in $if_list; do
			if [[ "$interface" == cgtp*([0-9]) ]]; then
				impacted_ifs="$impacted_ifs $interface"
			fi
		done
	fi
	if [[ -n $impacted_ifs ]]; then
		warn_feature "cgtp" "$w_cgtp" "$impacted_ifs"
	fi
}

#
# warn_datalink() is used instead of this function on Solaris 11 systems.
# On Solaris 10, IP is configured directly on infiniband interfaces.
# On Solaris 11, IP is configured on infiniband partitions, with multiple
# partitions supported over a physical infiniband interface.
warn_ibiface()
{
	typeset if_list="$1"
	typeset impacted_ifs
	typeset interface
	typeset device
	typeset flags

	impacted_ifs=""
	for interface in $if_list; do
		if [[ "$interface" == ibd*([0-9]) ]]; then
			impacted_ifs="$impacted_ifs $interface"
		fi
	done
	if [[ -n $impacted_ifs ]]; then
		warn_feature "ibiface" "$w_ibiface" "$impacted_ifs"
	fi
}

# Identify if ppp is configured.
warn_ppp()
{
	typeset files

	files="$(ls /etc/ppp/options /etc/ppp/options.* /etc/ppp/peers/* \
	    2>/dev/null | grep -v ".tmpl")"
	if [[ -n $files ]]; then
		warn_feature "ppp" "$w_ppp"
	fi
}

# Simulate function of Solaris 10's pkgchk using Solaris 11's pkg utility.
# Return 1 (fail) if the file specified in the argument was changed since
# installation.
pkg_check()
{
	typeset file="$1"
	typeset pkgname=""

	# Was the file installed by an IPS package?
	if (( g_source_release >= 11 )); then
		pkgname=$( pkg search -l "$file" 2>/dev/null | \
		    tail -1 | awk '{print $4}' )
	fi

	if [[ -n $pkgname ]] then
		# File installed by IPS, pkg verify will list file if changed.
		if ( pkg verify $pkgname | \
		    grep ${file#/} ) >/dev/null 2>&1; then
			return 1
		fi
	elif grep "^${file}[= ]" /var/sadm/install/contents >/dev/null 2>&1; then
		# File is from a system-V package,
		# pkgchk returns error status if changed.
		if ! pkgchk -p $file >/dev/null 2>&1 ; then
			return 1
		fi
	else
		# Treat all other files same as changed files.
		return 1
	fi
	return 0
}

# Identify changed network driver configuration files.
warn_driverconf()
{
	typeset if_list="$1"
	typeset device_list
	typeset device
	typeset interface
	typeset file

	if (( g_source_release >= 11 )) ; then
		device_list=" "
		for interface in $( dladm show-phys -Ppo device ); do

			# Skip interface if device already seen.
			device="${interface%%+([0-9])}"
			[[ "$device_list" == *\ $device\ * ]] && continue
			device_list="${device_list}${device} "

			# Warn if device.conf exists and was changed
			# since installation.
			for file in \
			    /kernel/drv/${device}.conf \
			    /usr/kernel/drv/${device}.conf; do
				if [[ -e $file ]] && \
				    ! pkg_check $file; then
					warn_item "$file"
				fi
			done
		done

	else # Solaris 8, 9, 10
		device_list=" "
		for interface in $if_list; do

			# Skip loopback, vni, cgtp, and tunnel interfaces
			device="${interface%%+([0-9])}"
			[[ " xx lo vni cgtp ip.tun ip6.tun ip.6to4tun " \
			    == *\ $device\ * ]] && continue

			# Skip interface if device already seen.
			[[ "$device_list" == *\ $device\ * ]] && continue
			device_list="${device_list}${device} "

			# Warn if device.conf exists and was changed
			# since installation.
			for file in \
			    /kernel/drv/${device}.conf \
			    /usr/kernel/drv/${device}.conf; do
				if [[ -e $file ]] && \
				    ! pkg_check $file > /dev/null 2>&1; then
					warn_item "$file"
				fi
			done
		done
	fi
}

# Identify datalink properties with non-default values.
# dladm linkprop is a new feature with Solaris 11.
warn_linkprop()
{
	typeset found_links=""
	typeset dl_link
	typeset dl_class
	typeset dl_prop
	typeset dl_value
	typeset dl_def

	# Scan all permanently defined properties of all links.
	dladm show-link -pPo link,class | \
	    while IFS=":" read dl_link dl_class; do
		# Skip over iptun links, they can be configured in a zone.
		[[ "$dl_class" == "iptun" ]] && continue
		dladm show-linkprop \
		    -Pco property,value,default $dl_link 2>/dev/null | \
		    while IFS=":" read dl_prop dl_value dl_def; do
			# A property is considered non-default if its
			# persistent value is not equal to null, "?", or
			# the default value.
			if [[ -n "$dl_value" && \
			    "$dl_value" != "?" && \
			    "$dl_value" != "$dl_def" ]]; then
				warn_item_compact "$dl_link" \
				    "$dl_prop" "$dl_value"
			fi
		done
	done
}

# Identify potential issues with the network configuration.
# The list of relevant issues depends on the capabilities
# of both the source system and the target system.
warn_net()
{
	typeset interface_list

	# Get a list of configured IP interface names.
	if (( g_source_release >= 11 )) ; then
		# Must have read access to smf repository.
		[[ ! -r /etc/svc/repository.db ]] && return
		if [[ $(svcprop -p netcfg/active_ncp \
		    svc:/network/physical:default 2> /dev/null) \
		    == "DefaultFixed" ]]; then
			interface_list="$( get_s11_interfaces )"
		else
			interface_list="$( get_s11_nwam )"
		fi
	else
		interface_list="$( get_s10_interfaces )"
	fi

	# S11 destination
	if (( g_target_release >= 11 )) ; then

		setup_chk "sharedonly"
		warn_cgtp "$interface_list"
	
		setup_chk "datalink"
		warn_vlan "$interface_list"
		if (( g_source_release >= 11 )) ; then
			warn_datalink "$interface_list"
			warn_secobj "$interface_list"
		else
			warn_ibiface "$interface_list"
			warn_aggregation "$interface_list"
		fi

		if (( g_source_release >= 11 )) ; then
			setup_chk "linkprop"
			warn_linkprop "$interface_list"
		fi

		setup_chk "dynaddr"
		warn_dhcp_assigned "$interface_list"
		if (( g_source_release < 11 )) ; then
			warn_rarp_assigned "$interface_list"
		fi
		warn_ipv6_autoconf "$interface_list"

		setup_chk "netdevalloc"
		warn_ppp

		if (( opt_fast_mode == 0 )) ; then
			setup_chk "driverconf"
			warn_driverconf "$interface_list"
		fi
	# S10 destination
	else
		setup_chk "physif"
		warn_physif "$interface_list"

		setup_chk "ifname"
		warn_ifname "$interface_list"

		setup_chk "dynaddr"
		warn_dhcp_assigned "$interface_list"
		warn_rarp_assigned "$interface_list"
		warn_ipv6_autoconf "$interface_list"

		setup_chk "sharedip"
		warn_ipmp_group "$interface_list"
		warn_vni "$interface_list"
		warn_forwarding "$interface_list"
		warn_static_routes

		setup_chk "exclusiveonly"
		warn_iptun "$interface_list"

		setup_chk "sharedonly"
		warn_cgtp "$interface_list"

		setup_chk "datalink"
		warn_vlan "$interface_list"
		warn_aggregation "$interface_list"
		warn_ibiface "$interface_list"

		setup_chk "netdevalloc"
		warn_ppp

		if (( opt_fast_mode == 0 )) ; then
			setup_chk "driverconf"
			warn_driverconf "$interface_list"
		fi
	fi
}

warn_nfs()
{
	setup_chk "nfs"
	typeset i

	# NFS server works on S11
	(( g_source_release >= 11 )) && return

	# NFS server will eventually work in for s10c zone
	# (( g_source_release == 10 && g_target_release == 11 )) && return
	
	# Check live system shares
	[[ ! -f /usr/sbin/share ]] && return

	for i in $(share -F nfs| nawk '{print $2}')
	do
		warn_item_compact "$i"
	done
}

# lofi does not work on S10 targets
warn_lofi()
{
	typeset dev
	typeset file
	typeset dummy

	setup_chk "lofi"

	# lofi works on s11 and s10c
	(( g_target_release >= 11 )) && return

	# Check for lofi utility
	[[ ! -f /usr/sbin/lofiadm ]] && return

	lofiadm | tail +2 | while read dev file dummy ; do
		warn_item $dev $file
	done
}

warn_ramdisk()
{
	typeset dev
	typeset dummy


	setup_chk "ramdisk"

	[[ ! -f /usr/sbin/ramdiskadm ]] && return

	ramdiskadm | tail +2 | while read dev dummy ; do
		warn_item $dev
	done
}

warn_smb()
{
	typeset shares
	typeset i

	setup_chk "smb"

	(( g_source_release < 11 )) && return

	[[ ! -f /usr/sbin/share ]] && return

	for i in $(share -F smb | tail +2 | nawk '{print $2}')
	do
		warn_item_compact "$i"
	done
}

warn_vfstab()
{
	typeset dev
	typeset mnt

	setup_chk "vfstab"

	nawk '{
		if (substr($1, 0, 1) == "#" || length($1) == 0)
			next

		if ($1 == "fd" || $1 == "/proc" || $1 == "swap" ||
		    $1 == "ctfs" || $1 == "objfs" || $1 == "sharefs" ||
		    $1 == "/devices" || $3 == "/" ||
		    $4 == "nfs" || $4 == "lofs" || $4 == "swap")
			next

		print $0, $1
	}' /etc/vfstab | \
	while read dev mnt
	do
		warn_item "$dev" "$mnt"
	done
}

#
# Warn if iscsi client

warn_iscsi_initiator()
{
	typeset targets
	typeset i

	setup_chk "iscsi-initiator"

	(( g_source_release == 8 || g_source_release == 9 )) && return

	[[ ! -f /usr/sbin/iscsiadm ]] && return

	targets=$(iscsiadm list target | grep '^Target:' |
	    awk '{ print $2 }')	

	for i in $targets ; do
		warn_item $i
	done
}


#
# Warn if iscsi server
#
warn_iscsi_target()
{
	typeset targets
	typeset i

	setup_chk "iscsi-target"

	(( g_source_release == 8 || g_source_release == 9 )) && return

	if (( g_source_release == 10 )) ; then

		[[ ! -f /usr/sbin/iscsitadm ]] && return
		targets=$(iscsitadm list target 2>/dev/null |
		    grep 'iSCSI Name:' | awk '{ print $2 }')
	else
		[[ ! -f /usr/sbin/itadm ]] && return
		targets=$(itadm list-target 2>/dev/null | tail +2 |
		   awk '{ print $1 }')
	fi
	for i in $targets ; do
		warn_item $i
	done
}

#
# warn fcoe client or server
#
warn_fcoe()
{
	typeset type
	typeset mac
	typeset wwn

	# must be on of "fcoe-initiator" or "fcoe-target"
	setup_chk $1

	if [[ $1 == "fcoe-initiator" ]] ; then
		type="Initiator"
	else
		type="Target"
	fi

	(( g_source_release < 11 )) && return

	[[ ! -f /usr/sbin/fcadm ]] && return

	fcadm list-fcoe-ports 2>/dev/null | nawk -v m=$type '
	{
            if ( $1 == "HBA" && $2 == "Port" ) {
                name=$4;
                getline;
                t=$3;
                getline;
                mac=$3;
		if ( t == m ) {
                	print "MAC=" mac "|WWN=" name;
		}
            }
        }' | while IFS='|' read mac wwn ; do
		warn_item $mac $wwn
	done
}

#
# Warn for npiv in use
#
warn_npiv()
{
	typeset phys
	typeset virt

	setup_chk "npiv"

	(( g_source_release < 11 )) && return

	[[ ! -f usr/sbin/fcinfo ]] && return

	fcinfo hba-port 2>/dev/null | nawk '
	{
            if ( $1 == "HBA" && $2 == "Port" ) {
		phys=$4;
		npiv=0;
	    } else if ( $1 == "NPIV" && $2 == "Port" ) {
		npiv=1;		
	    } else if ( $1 == "Port" && npiv == 1 ) {
		print "Physical=" phys "|Virtual=" $3;
            }
        }' | while IFS='|' read phys virt ; do
		print PHYS $phys
		warn_item $phys $virt
	done
}

#
# Warn for fc target configured
#
warn_fc_target()
{
	typeset list
	typeset i

	setup_chk "fc-target"

	(( g_source_release < 11 )) && return

	[[ ! -f /usr/sbin/fcinfo ]] && return

	list=$(fcinfo hba-port -t 2>/dev/null | nawk '
	{
            if ( $1 == "HBA" && $2 == "Port" ) {
		print $4
	    }
        }')

	for i in $list ; do
		warn_item $i
	done
}

warn_fc_initiator()
{
	typeset wwn

	setup_chk "fc-initiator"

	(( g_source_release < 10 )) && return

	[[ ! -f /usr/sbin/fcinfo ]] && return

	fcinfo hba-port -i 2>/dev/null | nawk '
	{
		if ( $1 == "HBA" && $2 == "Port" ) {
			wwn=$4;
		}
		if ( $1 == "State:" && $2 == "online") {
			print wwn;
		}
	}' | while read wwn ; do
		warn_item $wwn
	done
}

warn_comstar()
{
	typeset list
	typeset i

	(( g_source_release < 11 )) && return

	setup_chk "scsi"

	[[ ! -f /usr/sbin/sbdadm ]] && return

	list=$(sbdadm list-lu | grep '^[0-9]' | awk '{ print $3 }')

	for i in $list ; do
		warn_item $i
	done
}

# Check if a particular service is enabled. If a colon exists in the
# name, then we check only that specific service instance. If no
# colon exists, check all instances of that service.
check_svc()
{
	typeset svc=$1
	typeset service
	typeset svc_state

	if [[ $svc != *:* ]]; then
		svc="$svc:*"
	fi
	svcs -Ho fmri,state "$svc" 2>/dev/null |
	    while read service svc_state; do
		[ "$svc_state" == "disabled" ] && continue
		warn_item "$service"
	done
}

warn_svcs()
{
	typeset SVCS
	typeset i

	# svcs that can't work at all

	if (( g_source_release == 8 || g_source_release == 9 )); then
		# Not yet supporting s8/s9
		return
	elif (( g_source_release == 10 )); then
		SVCS="\
		    ldoms/ldmd\
		    system/virtualbox/vboxservice\
		    application/virtualbox/zoneaccess\
		    network/ipmievd \
		    network/iscsi/initiator \
		    network/nfs/server \
		    system/iscsitgt\
		    system/pools\
		    system/pools/dynamic\
		"
	else
		SVCS="\
		    ldoms/ldmd\
		    system/virtualbox/vboxservice\
		    application/virtualbox/zoneaccess\
		    network/ipmievd \
		    network/iscsi/target \
		    network/smb/server\
		    network/tnd\
		    system/iscsi/target\
		    system/labeld\
		    system/pools\
		    system/pools/dynamic\
		    system/stmf\
		    system/wusbd
		"

		# Remove when s10c can be an nfs server
		if (( g_source_release == 10 && g_target_release == 11 )) ; then
		SVCS="$SVCS\
		    network/nfs/server
		"
		fi
	fi

	setup_chk "svcnotallowed"

	for i in $SVCS
	do
		check_svc $i
	done
}

warn_svcs_priv()
{
	typeset SVCS_NTP
	typeset SVCS_DHCP
	typeset SVCS_EXLCUSIVE
	typeset svc
	typeset priv

	# ntp requires extra privileges
	SVCS_NTP="\
	    network/ntp \
	    network/ntp4"

	# dhcp requires extra privileges
	SVCS_DHCP="\
	    network/dhcp-server"
	
	# svcs that require an exclusive-ip zone
	SVCS_EXCLUSIVE="\
	    network/ipfilter \
	    network/ipsec/ike \
	    network/ipsec/ipsecalgs \
	    network/ipsec/manual-key \
	    network/ipsec/policy \
	    network/ipv4-forwarding \
	    network/ipv6-forwarding \
	    network/rarp \
	    network/routing/bgp \
	    network/routing/legacy-routing \
	    network/routing/ndp \
	    network/routing/ospf \
	    network/routing/ospf6 \
	    network/routing/rdisc \
	    network/routing/rip \
	    network/routing/ripng \
	    network/routing/route \
	    network/routing-setup \
	    network/routing/zebra
	"

	# Future: check services on s8/s9 with ps?
	(( g_source_release == 8 || g_source_release == 9 )) && return

	setup_chk "ntp-client"
	for svc in $SVCS_NTP ; do
		check_svc $svc
	done

	# svcs that require an exclusive IP stack
	if (( g_target_release < 11 )) ; then

		setup_chk "dhcp-server"
		for svc in $SVCS_DHCP ; do
			check_svc $svc
		done

		setup_chk "svcexclip"
		for svc in $SVCS_EXCLUSIVE
		do
			check_svc $svc
		done
	fi
}

#
# Look for packages which we know can't be used in a zone.
#
warn_pkgs()
{
	typeset PKGS="\
	    VRTSvxvm\
	"
	typeset i

	setup_chk "pkg"

	for i in $PKGS
	do
		[[ -d /var/sadm/pkg/$i ]] && warn_item "$i"
	done

	(( g_source_release == 8 || g_source_release == 9 )) && return

}

warn_zones()
{
	typeset zoneid
	typeset zonename
	typeset zonestate
	typeset dummy

	setup_chk "zones"

	(( g_source_release < 10 )) && return

	[[ ! -f /usr/sbin/zoneadm ]] && return

	zoneadm list -p | grep -v '^0:global:' |
	    while IFS=: read zoneid zonename zonestate dummy ; do
		warn_item "$zonename" "$zonestate"
	done
}

warn_etcsystem()
{
	typeset line

	setup_chk "etcsystem"

	nawk -v targ="$g_target_release" '
	BEGIN {
		FS="[ \t=]*"
		code["exclude:"]="noalternate"
		code["include:"]="noalternate"
		code["forceload:"]="noalternate"
		code["rootdev:"]="noalternate"
		code["rootfs:"]="noalternate"
		code["moddir:"]="noalternate"
	
	        code["rlim_fd_cur"] = "alternate"
	        code["rlim_fd_max"] = "alternate"
	        code["semsys:seminfo_semmsl"] = "alternate"
	        code["semsys:seminfo_semopm"] = "alternate"
	        code["semsys:seminfo_semmni"] = "alternate"
	        code["semsys:seminfo_semmns"] = "obsolete"
	        code["semsys:seminfo_semmnu"] = "obsolete"
	        code["semsys:seminfo_semume"] = "obsolete"
	        code["semsys:seminfo_semvmx"] = "obsolete"
	        code["semsys:seminfo_semaem"] = "obsolete"
	        code["shmsys:shminfo_shmmin"] = "obsolete"
	        code["shmsys:shminfo_shmseg"] = "obsolete"
	        code["msgsys:msginfo_msgmax"] = "obsolete"
	        code["symsys:shminfo_shmmni"] = "alternate"
	        code["symsys:shminfo_shmmni"] = "alternate"
		code["symsys:shminfo_shmmax"] = "alternate"
	        code["msgsys:msginfo_msgmnb"] = "alternate"
	        code["msgsys:msginfo_msgtql"] = "alternate"
	        code["msgsys:msginfo_msgmni"] = "alternate"

		if (targ == 10) {
			code["c2audit:audit_load"] = "noalternate"
		} else if (targ >= 11) {
			code["c2audit:audit_load"] = "replaced"
			type["c2audit:audit_load"] = "utility"
			tune["c2audit:audit_load"] = "auditconfig(1m)"
		}

	        type["rlim_fd_cur"] = "rctl"
	        type["rlim_fd_max"] = "rctl"
	        type["semsys:seminfo_semmsl"] = "rctl"
	        type["semsys:seminfo_semopm"] = "rctl"
	        type["semsys:seminfo_semmni"] = "rctl"
	        type["symsys:shminfo_shmmni"] = "rctl"
	        type["symsys:shminfo_shmmni"] = "rctl"
		type["symsys:shminfo_shmmax"] = "rctl"
	        type["msgsys:msginfo_msgmnb"] = "rctl"
	        type["msgsys:msginfo_msgtql"] = "rctl"
	        type["msgsys:msginfo_msgmni"] = "rctl"

		tune["rlim_fd_cur"] = "process.max-file-descriptor"
		tune["rlim_fd_max"] = "process.max-file-descriptor"
	        tune["semsys:seminfo_semmsl"] = "process.max-sem-nsems"
	        tune["semsys:seminfo_semopm"] = "process.max-sem-ops"
	        tune["semsys:seminfo_semmni"] = "project.max-sem-ids"
	        tune["shmsys:shminfo_shmmni"] = "project.max-shm-ids"
	        tune["shmsys:shminfo_shmmni"] = "project.max-shm-memory"
		tune["shmsys:shminfo_shmmax"] = "project.max-shm-memory"
	        tune["msgsys:msginfo_msgmnb"] = "process.max-msg-qbytes"
	        tune["msgsys:msginfo_msgtql"] = "process.max-msg-messages"
	        tune["msgsys:msginfo_msgmni"] = "project.max-msg-ids"
	}
	{
		if (substr($1, 0, 1) == "*" || length($1) == 0)
			next
		if (substr($1, 0, 1) == "#" || length($1) == 0)
			next

		print $0
		cmd=$1
		if ( cmd == "set" ) {
			cmd=$2
		}
		if (cmd in code) {
			outcode=code[cmd];
		} else {
			outcode="noinfo"
		}
		if (cmd in type) {
			outtype=type[cmd];
		} else {
			outtype=""
		}
		if (cmd in tune) {
			outtune=tune[cmd];
		} else {
			outtune=""
		}
		print outcode, outtype, outtune
	}' /etc/system | \
	while read line
	do
		typeset tunecode=""
		typeset tunetype=""
		typeset tunedescription=""
		read tunecode tunetype tunedescription

		if (( opt_parse_mode == 0 )) ; then
			case $tunecode in
			"noinfo")	tunecode="$w_tune_noinfo";;
			"obsolete")	tunecode="$w_tune_obsolete";;
			"replaced")	tunecode="$w_tune_replaced";;
			"alternate")	tunecode="$w_tune_alternate";;
			"noalternate")	tunecode="$w_tune_noalternate";;
			esac

		fi
	
		warn_item "$line" "$tunecode" "$tunetype" "$tunedescription"
		# add a line return for tunables with alternates.
		(( opt_parse_mode == 0 )) && [[ -n $tunedescription ]] && print
	done
}

warn_unsupported()
{
	setup_chk "unsupported"

	warn_mobile_ip
}

warn_svm()
{
	setup_chk "svm"

	[ ! -d /etc/lvm -o ! -f /etc/lvm/mddb.cf ] && return
	nawk '{
		if (substr($1, 0, 1) == "#" || length($1) == 0)
			next
		exit 1
	}' /etc/lvm/mddb.cf
	(( $? != 0 )) && warn_item_compact "SVM"
}

# XXX need any guidance for separate /var configurations ??
warn_var()
{
	true
}

# Warn about non-root zpools
warn_zpools()
{
	typeset zpool
	typeset rpool
	typeset grep

	setup_chk "zpool"

	(( g_source_release < 10 )) && return

	[[ ! -f /usr/sbin/zpool ]] && return

	rpool="$(df -h / | nawk '$NF == "/" { sub("/.*", "", $1); print $1 }')"

	if [[ -z "$rpool" ]] ; then
		grep="cat"
	else
		grep="grep -v $rpool"
	fi
	# Don't warn about root pool if there is one
	zpool list -H | awk '{ print $1}' | $grep | while read zpool ; do
		warn_item $zpool
	done
}

warn_resource_pools()
{
	typeset pool

	setup_chk "resourcepool"

	[[ ! -f /usr/sbin/pooladm ]] && return

	pooladm 2>/dev/null | nawk '
	{
		if ( $1 == "pool" && $2 != "pool_default" ) {
			print $2
		}
	}' | while read pool ; do
	    warn_item $pool
	done
}

# Warn about processor sets.
warn_psets()
{
	typeset cpus
	typeset d1
	typeset d2
	typeset d3
	typeset d4

	setup_chk "pset"
	(( g_source_release < 10 )) && return

	[[ ! -f /usr/sbin/psrset ]] && return

	psrset | while read d1 d2 d3 id d4 cpus ; do
		[[ -z $cpus ]] && cpus="none"
		warn_item ${id%:} $cpus
	done
}

# Warn if default scheduler set
warn_sched()
{
	typeset sched

	setup_chk "sched"

	
	sched=$(dispadmin -d 2>/dev/null | awk '{ print $1 }')
	if [[ -n "$sched" ]] ; then
	    warn_item $sched
	fi
}

warn_patches()
{
	typeset req_patch
	typeset patches
	typeset patch

	setup_chk "patch"

	# We only need to check for patches if we're going into a solaris10
	# branded zone.
	if  (( g_source_release != 10 || g_target_release != 11 )) ; then
		return
	fi
	# Putting S10 in a solaris10 branded zone requires a minimum patch
	# level.

	#
	# Make sure we have the minimal KU patch we support.  These are the
	# KUs for S10u9.
	#
	if [[ $(uname -p) == "i386" ]]; then
		req_patch="142910-17"
	else
		req_patch="142909-17"
	fi

	#
	# Check the core kernel pkg for the required KU patch.
	#
	if [[ ! -f /var/sadm/pkg/SUNWcakr/pkginfo ]]; then
		warn_issue "$w_patch" "$req_patch"
		return
	fi

	patches=$(nawk -F= '{if ($1 == "PATCHLIST") print $2}' \
	    /var/sadm/pkg/SUNWcakr/pkginfo)
	for patch in $patches
	do
		[[ $patch == $req_patch ]] && return
	done

	warn_issue "$w_patch" "$req_patch"
}

# Identify ndd tunable settings in the boot scripts.
warn_ndd()
{
	typeset file
	typeset line
	typeset driver
	typeset parameter
	typeset lastskipped=""
	typeset bootfiles="/etc/rc*.d/S* /lib/svc/method/*"
	typeset ul_drivers="/dev/arp /dev/ip /dev/ip6 /dev/iptun"
	ul_drivers="$ul_drivers /dev/tcp /dev/udp /dev/sctp"

	setup_chk "ndd"
	# Scan bootfiles for ndd references.
	# Suppress trailing backslashes which can mess up read command.
	grep 'ndd -set' $bootfiles 2> /dev/null | \
	    sed -e 's/\\$//' | \
	    while IFS=': 	' read file line; do

		# Skip warning for this parameter if the boot file was
		# not changed since installation.
		if [[ "$lastskipped" == "$file" ]]; then
		    continue
		fi
		if pkg_check $file; then
			lastskipped="$file"
			continue
		fi

		# Extract driver name and parameter from ndd line
		set -- $line
		while [[ -n "$2" && "$1" != "*ndd" && $2 != "-set" ]]; do
			shift
		done
		driver="$3"
		parameter="$4"

		# ndd commands for IP and Transport layer drivers are
		# supported in exclusive-IP zones. Skip the warning
		# for these drivers when the destination is S11.
		if (( g_target_release >= 11 )) && \
		    [[ " $ul_drivers " == *\ $driver\ * ]]; then
			continue
		fi

		warn_item "$file" "$driver" "$parameter"
	done
}

basic_checks()
{

	setup_mode "basic" 

	(( opt_parse_mode == 0 )) && printf -- "$basic_header\n"

	# critical problems
	warn_etcsystem
	warn_unsupported
	warn_be
	warn_nfs
	warn_smb
	warn_pkgs
	warn_sched
	warn_iscsi_initiator
	warn_iscsi_target
	warn_fcoe "fcoe-initiator"
	warn_fcoe "fcoe-target"
	warn_npiv
	warn_fc_target
	warn_fc_initiator
	warn_comstar
	warn_lofi
	warn_ramdisk
	warn_svcs
	warn_zones

	# potential configuration issues
	warn_svcs_priv
	warn_svm
	warn_vfstab
	warn_resource_pools
	warn_psets
	warn_zpools
	warn_patches
	warn_net
	(( opt_fast_mode == 0 )) && warn_ndd

	if (( opt_parse_mode == 0 )) ; then
		# Add a blank line after last message
		(( g_check_fired > 0 )) && print
		printf -- "$basic_footer\n\n" $g_check_fired
	fi
}

#
# The main body of the script starts here.
#


trap trap_cleanup 1 2 15

# These global variables are set by command line options
typeset -i opt_debug_mode=0
typeset -i opt_version_mode=0
typeset -i opt_parse_mode=0
typeset -i opt_basic_mode=0
typeset -i opt_config_mode=0
typeset -i opt_version_mode=0
typeset -i opt_static_mode=0
typeset -i opt_dynamic_mode=0
typeset -i opt_execnames=0
typeset -i opt_fast_mode=0

# These global variables are set by arguments to command line options
typeset -u arg_target_release="" # Arg of -T
typeset arg_dynamic_time=""	# Arg of -r
typeset arg_static_file_in=""	# File passed to -S
typeset arg_execname_file_in="" # File passed to -E

# These global varables are used by the implementation
typeset -i g_source_release	# Solaris release of source host
typeset -i g_target_release	# Solaris release of target host
typeset g_target_ip_type	# exclusive or shared ip target zone
typeset g_arch			# uname -p
typeset -i g_interrupted=0	# Set by SIGINT handler

# Globals use by network interface inspection functions
typeset g_mac_address		# Mac of interface
typeset g_mac_addr_type		# Mac assignment type
typeset g_ip_addr_list		# List of ip adresses on interrace
typeset g_ip_dynamic		# true if ip address assignment is dynamic
typeset g_static_file_list	# array of files specified by -s
typeset g_execname_list		# array of execnames specified by -e
typeset -i g_static_file_count=0
typeset -i g_execname_count=0
typeset g_hname=""
typeset g_dynamic_seconds
typeset -i g_check_count=0 	# number of checks done for current mode
typeset -i g_check_fired=0 	# number of checks that fired for current mode
typeset -i g_total_fired=0	# number of check fired total
typeset -i g_dtrace_pid=0	# pid of dtrace script to kill on SIGINT
g_arch=$(uname -p)
g_source_release=$(uname -r | nawk -F. '{print $2}')

# some locals
typeset rels
typeset dyn_time
typeset dyn_units

while getopts ":DVT:bchr:xs:S:e:E:Pf" opt
do
	case "$opt" in
		V)	opt_version_mode=1;;
		T)	arg_target_release="$OPTARG";;
		b)	opt_basic_mode=1;;
		c)	opt_config_mode=1;;
		D)	opt_debug_mode=1;;
		f)	opt_fast_mode=1;;
		h)	print "$m_usage"
			exit 0;;
		r)	opt_dynamic_mode=1
			arg_dynamic_time="$OPTARG"
			;;
		x)	opt_dynamic_mode=1
			arg_dynamic_time="inf"
			;;
		s)	opt_static_mode=1
			echo "$OPTARG" | tr ',' '\n' | while read file ; do
				(( g_static_file_count = \
				    g_static_file_count + 1 ))
				g_static_file_list[$g_static_file_count]="$file"
			done
			;;
		S)	opt_static_mode=1
			arg_static_file_in="$OPTARG";;
		e)	opt_execnames=1
			echo "$OPTARG" | tr ',' '\n' | while read file ; do
				(( g_execname_count = \
				    g_execname_count + 1 ))
				g_execname_list[$g_execname_count]="$file"
			done
			;;
		E)	opt_execnames=1
			arg_execname_file_in="$OPTARG"
			cat "$OPTARG" | while read file ; do
				(( g_execname_count = \
				    g_execname_count + 1 ))
				g_execname_list[$g_execname_count]="$file"

			done
			;;
		P)	opt_parse_mode=1;;
		:)	printf "$e_opt_arg\n" "$OPTARG" >&2
			print "$m_usage" >&2
			exit 2;;
		\?)	printf "$e_bad_opt\n" "$OPTARG" >&2
			print "$m_usage" >&2
			exit 2;;
		*)	print "$m_usage" >&2
			exit 2;;
	esac
done
shift OPTIND-1
if (( opt_version_mode == 1 )) ; then
	print "Version: $VERSION"
	exit 0
fi

# -c is exclusive with the other checking options
if (( opt_config_mode == 1 )) ; then
	(( opt_basic_mode == 1 || opt_static_mode == 1 || \
	    opt_dynamic_mode == 1 )) && \
	   fatal "$e_incompat_options" "$m_usage"
fi

# Minimum Solaris 10
if (( g_source_release < 10 )) ; then
	fatal "$e_unsupported"
fi

# Must be global zone
if [[ $(zonename) != "global" ]]; then
	fatal "$e_nonglobal_zone"
fi

# Set default target release if not specified with -T
if [[ -z $arg_target_release ]] ; then
	if (( g_source_release == 8 || g_source_release == 9 )); then
		arg_target_release="S10"
		g_target_release=10
	elif (( g_source_release >= 10 )) ; then
		arg_target_release="S$g_source_release"
		g_target_release=$g_source_release
	fi
		
else
	if [[ $arg_target_release == "S10" ]] ; then
		g_target_release=10
	elif [[ $arg_target_release == "S11" ]] ; then
		g_target_release=11
	elif [[ $arg_target_release == "S$g_source_release" ]] ; then
		# For future versions of solaris.  They must
		# be run on themselves, unless we build
		# additional brands.
		g_target_release=$g_source_release
	else 
		rels=$(printf "$e_or" "S10" "S11")
		fatal "$e_release" "$arg_target_release" "$rels"
	fi

	# Verify target release supports current release
	if (( g_source_release == 8 || g_source_release == 9 )); then
		# Solaris 8 and 9 can only be hosted by Solaris 10
		if (( g_target_release != 10 )) ; then
			fatal "$e_release" "$arg_target_release" "S10"
		fi
	elif (( g_source_release == 10 )) ; then
		# Solaris 10 can be run on Solaris 10 or 11
		if (( g_target_release != 10 && g_target_release != 11 )) ; then
			rels=$(printf "$e_or" "S10" "S11")
			fatal "$e_release" "$arg_target_release" $rels
		fi
	else
		# Solaris 11 or greater can be run only itself
		if (( g_target_release != g_source_release )) ; then
			fatal "$e_release" "$arg_target_release" \
			    "S$g_source_release"
		fi
	fi
fi

# We assume a shared-IP target zone for Solaris 10 target systems.
# Otherwise we assume an exclusive-IP target zone.
if (( g_target_release >= 11 )) ; then
	g_target_ip_type="exclusive"
else
	g_target_ip_type="shared"
fi

# System must not be running Trusted Extensions.
if (( g_source_release >= 10 )) &&
    [[ -x /bin/plabel ]] && /bin/plabel > /dev/null 2>&1; then
	fatal "$e_trusted_extensions"
fi

# Validate generic commands we need.
[[ ! -x /usr/bin/nawk ]] && fatal "$e_no_cmd" "nawk(1)"

#
# Must be root
#
typeset -i euid
euid=$(ps -o uid= -p $$)
(( euid != 0 )) && fatal "$e_needroot"

#
# Validate things we need to perform static analysis, which uses elfdump.
#
if (( opt_static_mode == 1 )) ; then
	[[ ! -x /usr/bin/elfdump && ! -x /usr/ccs/bin/elfdump ]] &&
	    fatal "$e_no_cmd" "elfdump(1)"
	[[ ! -x /usr/bin/od ]] && fatal "$e_no_cmd" "od(1)"
fi

#
# Validate things we need to perform dynamic analysis, which uses DTrace.
#
if (( opt_dynamic_mode == 1 )) ; then
	# DTrace is not available on S8/S9.
	(( g_source_release == 8 || g_source_release == 9 )) &&
	    fatal "$e_nodyn" $(uname -r)

	[[ ! -x /usr/bin/ppriv ]] && fatal "$e_no_cmd" "ppriv(1)"
	[[ ! -x /usr/sbin/dtrace ]] && fatal "$e_no_cmd" "dtrace(1M)"

	if [[ $arg_dynamic_time == "inf" ]] ; then
		dyn_time="0"
		dyn_units="s"
	else
		# validate value and units for the time DTrace should run
		dyn_time=${arg_dynamic_time%%[hms]}
		dyn_units=${arg_dynamic_time##+([[:digit:]])}
		[[ $dyn_time == $arg_dynamic_time ]] &&
		    fatal "$e_badtime" "$arg_dynamic_time"
		[[ $dyn_units != "h" && $dyn_units != "m" &&
		    $dyn_units != "s" ]] &&
		    fatal "$e_badtime" "$arg_dynamic_time"
	fi
fi

g_hname=$(hostname)
g_hname=${g_hname%%.*}

if (( opt_config_mode == 1 )) ; then
	pconfig
	exit 0
fi

#
# print header
#

nodename=$(uname -n)
if [[ -z $nodename ]] ; then
	nodename="<unknown>"
fi
solaris_version=$(head -1 /etc/release)
# strip whitespace
solaris_version=$(echo $solaris_version)
if [[ -z $solaris_version ]] ; then
	solaris_version="<unknown>"
fi
kernel_version=$(uname -rv)
if [[ -z $kernel_version ]] ; then
	kernel_version="<unknown>"
fi
kernel_platform=$(uname -im)
if [[ -z $kernel_platform ]] ; then
	kernel_platform="<unknown>"
fi

target_version="Solaris $g_target_release"

if (( g_source_release == 10 && g_target_release == 11 )) ; then
	target_brand="solaris10 $b_s10_container"
	target_brand_parse="solaris10"
elif (( g_source_release == 10 )) ; then
	target_brand="native $b_default"
	target_brand_parse="native"
else
	target_brand="solaris $b_default"
	target_brand_parse="solaris"
fi

if (( opt_parse_mode == 0 )) ; then
	# Print human readable header
	printf -- "$run_header\n" "$VERSION" "$nodename" "$solaris_version" \
	    "$kernel_version" "$kernel_platform" "$target_version" \
	    "$target_brand" "$g_target_ip_type"
	print
else
	print "header:version:$VERSION"
	print "header:source:$nodename:$solaris_version:$kernel_version:$kernel_platform"
	print "header:target:$target_version:$target_brand_parse:$g_target_ip_type"
fi

# If no options, default to basic mode.
(( opt_basic_mode == 0 && opt_static_mode == 0 && opt_dynamic_mode == 0 )) && \
    opt_basic_mode=1

(( opt_basic_mode == 1 )) && basic_checks

#
# Perform static binary file checks.
#
if (( opt_static_mode == 1 )) ; then
	if (( g_static_file_count > 0 )) ; then
		set -- "${g_static_file_list[@]}"
	else
		set --
	fi
	static_check_files "$arg_static_file_in" $@
fi

#
# Perform dynamic runtime analysis on the system.
#
if (( opt_dynamic_mode == 1 )) ; then
	tmpfile=$(mktemp -t)
	(( $? == 1 )) && fatal "$e_no_tmpfile"
	tmpout=$(mktemp -t)
	(( $? == 1 )) && fatal "$e_no_tmpfile"

	# convert time to seconds for how long DTrace should run
	if [[ $dyn_units == "h" ]]; then
		# hours
		dyn_time=$(($dyn_time * 3600))
	elif [[ $dyn_units == "m" ]] ; then
		# minutes
		dyn_time=$(($dyn_time * 60))
	else
		# seconds
		dyn_time=$dyn_time
	fi
	g_dynamic_seconds=$dyn_time
	gen_dyn_script > $tmpfile

	setup_mode "dynamic"

	if (( opt_parse_mode == 0)) ; then
		if [[ $arg_dynamic_time == "inf" ]] ; then
			printf -- "$dyn_int_header\n"
		else
		 	printf -- "$dyn_header\n" "$arg_dynamic_time"
		fi
	fi
	# run script
	chmod +x $tmpfile
	if (( opt_debug_mode == 1 )); then
		$tmpfile > $tmpout &
		g_dtrace_pid=$!
	else
		$tmpfile > $tmpout 2>/dev/null &
		g_dtrace_pid=$!
	fi
	wait
	if (( opt_debug_mode == 1 )); then
		echo "DTrace script: $tmpfile"
	else
		rm -f $tmpfile
	fi
	g_dtrace_pid=0
	if (( opt_debug_mode == 1 )); then
		echo "DTraace output: $tmpout"
	fi

	typeset category
	typeset issue
	typeset prog
	typeset priv
	typeset dev

	setup_chk "privna"
	grep '^incompatible|privnotallowed|' $tmpout |
	    while IFS='|' read category issue prog priv ; do
		warn_item "$prog" "$priv"
	done
	setup_chk  "privex"
	grep '^configuration|privexclip|' $tmpout |
	    while IFS='|' read category issue prog priv ; do
		warn_item "$prog" "$priv"
	done
	setup_chk  "privopt"
	grep '^configuration|privoptional|' $tmpout |
	    while IFS='|' read category issue prog priv ; do
		warn_item "$prog" "$priv"
	done

	setup_chk  "devna"
	grep '^incompatible|devnotallowed|' $tmpout |
	    while IFS='|' read category issue prog dev ; do
		warn_item "$prog" "$dev"
	done
	setup_chk  "devex"
	grep '^configuration|devexclip|' $tmpout |
	    while IFS='|' read category issue prog dev ; do
		warn_item "$prog" "$dev"
	done
	setup_chk  "devopt"
	grep '^configuration|devoptional|' $tmpout |
	    while IFS='|' read category issue prog dev ; do
		warn_item "$prog" "$dev"
	done
	if (( opt_debug_mode == 0 )); then
		rm -rf $tmpout
	fi

	if (( opt_parse_mode == 0)) ; then
		# Add a blank line after last message
		(( g_check_fired > 0 )) && print
		printf -- "$dyn_footer\n\n" $g_check_fired
	fi
fi

# Final tally of issues
setup_mode "final"

# Print issue totals
if ((opt_basic_mode || opt_static_mode || opt_dynamic_mode)) ; then
	if (( opt_parse_mode == 0 )) ; then
		printf -- "$total_header\n" $g_total_fired
	else
		print "footer:issues:$g_total_fired"
	fi

	(( g_total_fired > 0 )) && exit 3
fi

exit 0
