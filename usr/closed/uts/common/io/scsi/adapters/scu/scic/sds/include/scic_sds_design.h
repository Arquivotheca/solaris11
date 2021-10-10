/*
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _SCIC_SDS_DESIGN_H_
#define _SCIC_SDS_DESIGN_H_

/**
@page scic_sds_design_page SCIC SDS High Level Design

<b>Authors:</b>
- Richard Boyd

<b>Key Contributors:</b>
- Roger Jeppsen
- Nathan Marushak

@section scic_sds_scope_and_audience Scope and Audience

This document provides design information relating to the SCU specific
implementation of the SCI Framework.  Driver developers are the primary
audience for this document.  The reader is expected to have an understanding
of the SCU Software Architecture Specification, the Storage Controller
Interface Specification, and the SCI Base Design.

@section scic_sds_overview Overview

To begin, it's important to discuss the utilization of state machines in
the design.  State machines are pervasive in this design, because of the
abilities they provide.  A properly implemented state machine allows the
developer to code for a specific task.  The developer is not encumbered
with needed to handle other situations all in a single function.  For
example, if a specific event can only occur when the object is in a specific
state, then the event handler is added to handle such an event.  Thus, a
single function is not spliced to handle multiple events under various
potentially disparate conditions.

Additionally, the SCI Base Design document specifies a number of state
machines, objects, and methods that are heavily utilized by this design.
Please refer to Base Design specification for further information.

Most of the core objects have state machines associated with them. As a 
result, there are a number of state entrance and exit methods as well as event 
handlers for each individual state.  This design places all of the state 
entrance and exit methods for a given state machine into a single file (e.g. 
scic_sds_controller_states.c).  Furthermore, all of the state event handler 
methods are also placed into a single file (e.g. 
scic_sds_controller_state_handlers.c).  This format is reused for each
object that contains state machine(s).

Some of the SDS core objects contain sub-state machines.  These sub-state 
machines are started upon entrance to the super-state and stopped upon 
exit of the super-state.

@note Currently a large number of function pointers are utilized during the
course of a normal IO request.  Once stability of the driver is achieved,
performance improvements will be made as needed.  This likely will include
removal of the function pointers from the IO path.

@section scic_sds_use_cases Use Cases

The following use case diagrams depict the high-level user interactions with
the SDS core.  These diagrams do not encompass all use cases implemented in 
the system.  The low-level design section will contain detailed use cases for 
each significant object and their associated detailed sequences and/or 
activities. 

@image html Use_Case_Diagram__scic_sds__SCIC_SDS.jpg "SCIC SDS Use Cases"

@section scic_sds_class_hierarchy Class Hierarchy

This section delineates the high-level class organization for the SCIC SDS
component.  Details concerning each class will be found in the corresponding
low-level design sections.  Furthermore, additional classes not germane to
the overall architecture of the component will also be defined in these
low-level design sections.

@image html Class_Diagram__scic_sds__SCIC_SDS_Class_Hierarchy.jpg "SCIC SDS Class Diagram"

For more information on each object appearing in the diagram, please
reference the subsequent sections.

@section scic_sds_library SCIC SDS Library

First, the SCIC_SDS_LIBRARY object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the library object.

The SCIC_SDS_LIBRARY object is broken down into 2 individual source files
and one direct header file.  These files delineate the methods, members, etc.
associated with this object.  Please reference these files directly for
further design information:
- scic_sds_library.h
- scic_sds_library.c

@section scic_sds_controller SCIC SDS Controller

First, the SCIC_SDS_CONTROLLER object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on the 
SCIC_CONTROLLER object. 

The SCIC_SDS_CONTROLLER object is broken down into 3 individual source files
and one direct header file and one controller register header file.  These 
files delineate the methods, members, etc. associated with this object. Please
reference these files directly for further design information: 
- scic_sds_controller.h
- scic_sds_controller_registers.h
- scic_sds_controller.c
- scic_sds_controller_state_handlers.c
- scic_sds_controller_states.c

@section scic_sds_domain SCIC SDS Port

First, the SCIC_SDS_PORT object provides an implementation
for the roles and responsibilities defined in the Storage Controller Interface 
(SCI) specification.  It is suggested that the user read the storage 
controller interface specification for background information on the SCIC_PORT 
object. 

The SCIC_SDS_PORT object is broken down into 5 individual source files and one
direct header file and one port register header file.  These files delineate 
the methods, members, etc. associated with this object.  Please reference 
these files directly for further design information: 
- scic_sds_port.h
- scic_sds_port_registers.h
- scic_sds_port.c
- scic_sds_port_ready_substates.c
- scic_sds_port_ready_substate_handlers.c
- scic_sds_port_states.c
- scic_sds_port_state_handlers.c

The SCIC_SDS_PORT object has a sub-state machines defined for the READY 
super-state.  For more information on the super-state machine please refer to 
SCI_BASE_PORT_STATES in the SCI Base design document. 

In the SCIC_SDS_PORT_READY_SUBSTATES sub-state machine when there are no 
active phys on this port will be in a WAITING state until a phy on the port 
transitions to a READY state. Once any single phy on the port becomes ready 
then the port will be OPERATIONAL.  The port must be in the OPERATIONAL state 
to accept any IO requests. 

@image html State_Machine_Diagram__SCIC_SDS_PORT_STATES__SCIC_SDS_PORT_STATES.jpg "SCIC SDS Port State Machine"

@section scic_sds_phy SCIC SDS Phy

First, the SCIC_SDS_PHY object provides an implementation for the roles and 
responsibilities defined in the Storage Controller Interface (SCI) 
specification. It is suggested that the user read the storage controller 
interface specification for background information on the SCIC_PHY object.
 
The SCIC_SDS_PHY object is broken down into 7 individual source files and one 
direct header file and one phy register header file.  These files delineate 
the methods, members, etc. associated with this object. Please reference these 
files directly for further design information. 
- scic_sds_phy.h
- scic_sds_phy_registers.h
- scic_sds_phy.c
- scic_sds_phy_resetting_substates.c
- scic_sds_phy_resetting_substate_handlers.c
- scic_sds_phy_starting_substates.c
- scic_sds_phy_starting_substate_handlers.c
- scic_sds_phy_states.c
- scic_sds_phy_state_handlers.c

The SCIC_SDS_PHY object has a sub-state machine defined for the STARTING 
super-state.  For more information on the super-state machine please refer to 
the SCI_BASE_PHY_STATES in the SCI Base design document. 
 
In the SCIC_SDS_PHY_STARTING_SUBSTATES sub-state machine the SCIC_SDS_PHY 
processes SCU hardware messages and SCIC_SDS_CONTROLLER power control messages 
and unsolicited frame messages until the correct sequence of messages is 
receieved that tells the software state machine that the phy is READY for 
operation. 
 
@image html State_Machine_Diagram__SCIC_SDS_PHY_States__SCIC_SDS_PHY_States.jpg "SCIC SDS Phy State Machine"
 
@section scic_sds_remote_device SCIC SDS Remote Device

First, the SCIC_SDS_REMOTE_DEVICE object provides an 
implementation for the roles and responsibilities defined in the Storage 
Controller Interface (SCI) specification for the SCU hardware.  It is 
suggested that the user read the storage controller interface specification 
for background information on the SCIC_REMOTE_DEVICE object. 

The SCIC_SDS_REMOTE_DEVICE object is broken down into 5 individual source files
and one direct header file.  These files delineate the methods, members, etc.
associated with this object.  Please reference these files directly for 
further design information: 
- scic_sds_remote_device.h
- scic_sds_remote_device.c
- scic_sds_remote_device_ready_substates.c
- scic_sds_remote_device_ready_substate_handlers.c
- scic_sds_remote_device_states.c
- scic_sds_remote_device_state_handlers.c

The SCIC_SDS_REMOTE_DEVICE object has a sub-state machines defined for the 
READY super-state.  For more information on the super-state machine please 
refer to SCI_BASE_REMOTE_DEVICE_STATES in the SCI Base design document. 

In the SCIC_SDS_REMOTE_DEVICE_READY_SUBSTATES sub-state machine, the remote 
device follows the SCU hardware suspend and running states for the remote 
device object. When the remote device is in a suspended state it will only 
accept task management requests. 

For more information on the ready sub-state machine states please refer to the
scic_sds_remote_device.h::_SCIC_SDS_REMOTE_DEVICE_READY_SUBSTATES enumeration.

@image html State_Machine_Diagram__SCIC_SDS_REMOTE_DEVICE_States__SCIC_SDS_REMOTE_DEVICE_States.jpg "SCIC SDS Remote Device State Machine Diagram" 

@section scic_sds_io_request SCIC SDS IO Request

First, the SCIC_SDS_IO_REQUEST object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the SCIC_IO_REQUEST object.

The SCIC_SDS_IO_REQUEST object is broken down into 5 individual
source files and one direct header file.  These files delineate the
methods, members, etc. associated with this object.  Please reference
these files directly for further design information:
- scic_sds_io_request.h
- scic_sds_io_request.c
- scic_sds_io_request_started_task_mgmt_substates.c
- scic_sds_io_request_started_task_mgmt_substate_handlers.c
- scic_sds_io_request_states.c
- scic_sds_io_request_state_handlers.c

@image html State_Machine_Diagram__SCIC_SDS_IO_REQUEST_SSP_IO_States__SCIC_SDS_IO_REQUEST_SSP_IO_States.jpg "SCIC SDS IO Request State Machine"
 
@section scu_hardware_information SCU Hardware Data

The SCU hardware register interface and data structures are defined in the 
following 8 header files.  Please reference these files directly for more 
information. 

- scu_constants.h
- scu_completion_codes.h
- scu_event_codes.h
- scu_registers.h
- scu_remote_node_context.h
- scu_task_context.h
- scu_unsolicited_frame.h
- scu_viit_data.h

*/

#endif // _SCIC_SDS_DESIGN_H_
