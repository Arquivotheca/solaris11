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
#ifndef _SCI_BASE_DESIGN_H_
#define _SCI_BASE_DESIGN_H_

/**
@page sci_base_design_page SCI Base High Level Design

<b>Authors:</b>
- Nathan Marushak
- Richard Boyd

@section sci_base_scope_and_audience Scope and Audience

This document provides design information relating to the common components
of all SCI component implementations.  Driver developers are the primary
audience for this document.  The reader is expected to have an understanding
of the SCU Software Architecture Specification and the Storage Controller
Interface Specification.

@section sci_base_overview Overview

To begin, it's important to discuss the utilization of state machines in
the design.  State machines are pervasive in this design, because of the
abilities they provide.  A properly implemented state machine allows the
developer to code for a specific task.  The developer is not encumbered
with needing to handle other situations all in a single function.  For
example, if a specific event can only occur when the object is in a specific
state, then the event handler is added to handle such an event.  Thus, a
single function is not spliced to handle multiple events under various
potentially disparate conditions.

The SCI Base component provides an implementation of the features and
functionalities common for both the SCI Framework and SCI Core objects.
All objects contained in the base have child implementations in the SCI
Framework and/or SCI Core.  Please note that in several cases an object
may only be implemented in either the Framework or the Core.  The idea
behind this abstraction is that although the object is currently only
contained in one component, it is possible that the other component might
add support for that same object.

@section sci_base_use_cases Use Cases

@section sci_base_class_hierarchy Class Hierarchy

@section sci_base_object SCI BASE Object

The SCI_BASE_OBJECT object implements the features and functionalities
common to all SCI objects.  Each and every object in the system "derives"
from the SCI_BASE_OBJECT.  Please reference the sci_base_object.h and
sci_base_object.c files for additional design details.

@section sci_base_logger SCI BASE Logger

The SCI_BASE_LOGGER object implements the features and functionalities
necessary to provide a dynamically changeable log messages for SCI.
The sci_base_logger.h an sci_base_logger.c implement the SCI Logger interface.

@section sci_base_state SCI BASE State

The SCI_BASE_STATE object provides the features, data, and functionality
common to all state variables in an SCI component (i.e. Base, Core, or
Framework).  For additional information, please reference sci_base_state.h.

@section sci_base_state_machine SCI BASE State Machine

The SCI_BASE_STATE_MACHINE object provides the features, data, and
functionality common to all state machines in an SCI component (i.e. Base,
Core, or Framework).  For additional information, please reference
sci_base_state_machine.h and sci_base_state_machine.c files.

@section sci_base_subject SCI BASE Subject

The SCI_BASE_SUBJECT object provides the features, data, and
functionality common to all subjects in an SCI component (i.e. Core
or Framework).  For additional information concerning the details of
the subject object, please reference the sci_base_subject.h and
sci_base_subject.c files.  The subject is a participant in the Observer
design pattern.

@section sci_base_observer SCI BASE Observer

The SCI_BASE_OBSERVER object provides the features, data, and
functionality common to all observers in an SCI component (i.e. Core
or Framework).  For additional information concerning the details of
the observer object, please reference the sci_base_observer.h and
sci_base_observer.c files.  The observer is a participant in the Observer
design pattern.

@section sci_base_state_machine_observer SCI BASE State Machine Observer

The SCI_BASE_STATE_MACHINE_OBSERVER object provides the features,
data, and functionality common to all state machine observer objects in
an SCI component (i.e. Core or Framework).  For additional information
concerning the details of the state machine observer object, please
reference the sci_base_state_machine_observer.h and
sci_base_state_machine_observer.c files.  The state machine observer
object implements the interface defined by the SCI_BASE_OBSERVER object.

@section sci_base_library SCI BASE Library

The SCI_BASE_LIBRARY object provides the common features, data, and
functionality common to all SCI library objects.  Please refer to the
sci_base_library.h and sci_base_library.c files for additional design
information.

@section sci_base_controller SCI BASE Controller

The SCI_BASE_CONTROLLER object implements the features and functionalities
common to all controller objects (e.g. Framework and Core).  A controller
models the behaviors and responsibilities at the adapter level.  As
previously mentioned, controllers are aggregated into libraries, but all
other objects in the system are aggregated into a controller.  For
additional design information please refer to the sci_base_controller.h and
sci_base_controller.c files.

The following state machine diagram depicts the super state machine common
to all controller object types.

For more information concerning the roles and responsibilities of each
state please reference the sci_base_controller.h::_SCI_BASE_CONTROLLER_STATES
enumeration.

@image html State_Machine_Diagram__States__SCI_BASE_CONTROLLER_States.jpg "SCI Base Controller State Machine"

@section sci_base_memory_descriptor_list SCI 

@section sci_base_domain SCI BASE Domain

The SCI_BASE_DOMAIN object implements the features and functionalities
common to all domain objects (e.g. Framework).  Currently, there is only
a framework implementation of the SCI_BASE_DOMAIN object.  A domain models
the behaviors and responsibilities associated with the topology of devices
visible through a given port on the controller.  It is similar in nature to
that of a bus, but it's only a representation for the group of remote devices.
For additional design detail for the domain object, please reference the
sci_base_domain.h and sci_base_domain.c files.

The following state machine diagram depicts the super state machine common
to all domain object types.

@image html State_Machine_Diagram__SCI_BASE_DOMAIN_States__SCI_BASE_DOMAIN_States.jpg "SCI Base Domain State Machine"

@section sci_base_port SCI BASE Port

The SCI_BASE_PORT object implements the features and functionalities
common to all port objects (e.g. Core).  Currently, there is only
a core implementation of the SCI_BASE_PORT object.  A port models
the behaviors and responsibilities associated with a group of phys through
which a set of devices (i.e. a domain) are accessible.
For additional design detail for the port object, please reference the
sci_base_port.h and sci_base_port.c files.

The following state machine diagram depicts the super state machine common
to all port object types.

@image html State_Machine_Diagram__States__SCI_BASE_PORT_States.jpg "SCI Base Port State Machine"

@section sci_base_phy SCI BASE Phy

The SCI_BASE_PHY object implements the features and functionalities
common to all phy objects (e.g. Core).  Currently, there is only
a core implementation of the SCI_BASE_PHY object.  A port models
the behaviors and responsibilities associated with a single physical link.
For additional design detail for the phy object, please reference the
sci_base_phy.h and sci_base_phy.c files.

The following state machine diagram depicts the super state machine common
to all phy object types.

@image html State_Machine_Diagram__State__SCI_BASE_PHY_States.jpg "SCI Base Phy State Machine"

@section sci_base_remote_device SCI BASE Remote Device

The SCI_BASE_REMOTE_DEVICE object implements the features and functionalities
common to all remote device objects (e.g. Framework, Core).  A remote device
models the behaviors and responsibilities associated with a device visible
through a port and contained in the domain.  For additional design detail for
the remote device object, please reference the sci_base_remote_device.h and
sci_base_remote_device.c files.

The following state machine diagram depicts the super state machine common
to all remote device object types.

@image html State_Machine_Diagram__States__SCI_BASE_REMOTE_DEVICE_States.jpg "SCI Base Remote Device State Machine"

@section sci_base_request SCI BASE Request

The SCI_BASE_REQUEST object implements the features and functionalities
common to all request objects (e.g. Framework, Core, IO and task management).
A request models the common behaviors and responsibilities associated with a
single request (e.g. READ, WRITE, LUN RESET, SET FEATURES etc.).
For additional design detail for the request object, please reference the
sci_base_request.h and sci_base_request.c files.

The following state machine diagram depicts the super state machine common
to all request object types.

@image html State_Machine_Diagram__SCI_BASE_REQUEST_States__SCI_BASE_REQUEST_States.jpg "SCI Base Request State Machine"

*/

#endif // _SCI_BASE_DESIGN_H_
