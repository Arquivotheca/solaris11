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
#ifndef _SCIC_OVERVIEW_H_
#define _SCIC_OVERVIEW_H_

/**

@page core_section SCI Core

@section scic_introduction_section Introduction

The Storage Controller Interface Core (SCIC) provides the underlying fundamental
hardware abstractions required to implement a standard SAS/SATA storage driver.

The following is a list of features that may be found in a core implementation:
-# hardware interrupt handling
-# hardware event handling
-# hardware state machine handling
-# IO and task management state machine handling
-# Phy staggered spin up

@image latex sci_core.eps "SCI Core Class Diagram" width=16cm

@note
For the SCU Driver Standard implementation of the SCI Core interface the
following definitions should be used to augment the cardinalities described
in the previous diagram:
-# There are exactly 4 scic_phy objects in the scic_controller.
-# There are exactly 4 scic_port objects in the scic_controller.
-# There can be a maximum of 4 scic_phy objects managed in a single scic_port.
-# The maximum number of supported controllers in a library is a truly flexible
   value, but the likely maximum number is 4.

*/

#endif // _SCIC_OVERVIEW_H_

