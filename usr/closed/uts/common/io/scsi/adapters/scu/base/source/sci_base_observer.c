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
/**
 * @file
 *
 * @brief This file implements the functionality common to all observer
 *        objects.
 */

#include "sci_types.h"
#include "sci_base_subject.h"
#include "sci_base_observer.h"

#if defined(SCI_LOGGING)

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

PLACEMENT_HINTS((INITIALIZATION))
void sci_base_observer_construct(
   struct SCI_BASE_OBSERVER *this_observer,
   SCI_BASE_OBSERVER_UPDATE_T update
)
{
   this_observer->next = NULL;
   this_observer->update = update;
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((INITIALIZATION))
void sci_base_observer_initialize(
   SCI_BASE_OBSERVER_T        * the_observer,
   SCI_BASE_OBSERVER_UPDATE_T   update,
   SCI_BASE_SUBJECT_T         * the_subject
)
{
   sci_base_observer_construct(the_observer, update);
   sci_base_subject_attach_observer(the_subject, the_observer);
}

// ---------------------------------------------------------------------------

PLACEMENT_HINTS((LOGGING))
void sci_base_observer_update(
   SCI_BASE_OBSERVER_T *this_observer,
   SCI_BASE_SUBJECT_T  *the_subject
)
{
   if (this_observer->update != NULL)
   {
      this_observer->update(this_observer, the_subject);
   }
}

#endif // defined(SCI_LOGGING)
