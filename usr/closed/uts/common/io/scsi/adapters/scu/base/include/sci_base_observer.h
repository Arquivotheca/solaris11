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
#ifndef _SCI_BASE_OBSERVER_H_
#define _SCI_BASE_OBSERVER_H_

/**
 * @file
 *
 * @brief This file contains all of the structures, constants, and methods
 *        common to all base observer object definitions.
 */

#if defined(SCI_LOGGING)

struct SCI_BASE_OBSERVER;
struct SCI_BASE_SUBJECT;

/**
 * @typedef SCI_BASE_OBSERVER_UPDATE_T
 *
 * @brief This type definition defines the format for the update method
 *        that is invoked for all observers participating in the observer
 *        design pattern.
 */
typedef void (*SCI_BASE_OBSERVER_UPDATE_T)(
   struct SCI_BASE_OBSERVER *this_observer,
   struct SCI_BASE_SUBJECT  *the_subject
);

/**
 * @struct SCI_BASE_OBSERVER
 *
 * @brief This structure defines the fields necessary for an object that
 *        intends to participate as an observer.
 */
typedef struct SCI_BASE_OBSERVER
{
   /**
    * This filed points to the next observer if there is one
    */
    struct SCI_BASE_OBSERVER *next;

   /**
    * This field defines the function pointer that is invoked in order to
    * notify the observer of a change in the subject (i.e. observed object).
    */
   SCI_BASE_OBSERVER_UPDATE_T update;

} SCI_BASE_OBSERVER_T;

/**
 * @brief This method is the basic constructor for the observer
 *
 * @param[in] this_observer This parameter specifies the observer to
 *            be constructed.
 * @param[in] update This parameter specifies the update method to be
 *            invoked for this observer.
 *
 * @return none
 */
void sci_base_observer_construct(
   struct SCI_BASE_OBSERVER   *this_observer,
   SCI_BASE_OBSERVER_UPDATE_T  update
);

/**
 * @brief This method performs the actions of construction and attaches to the
 *        subject.
 *
 * @pre The the_subject to be observed must be constructed before this call.
 *
 * @param[in] this_observer This parameter specifies the observer to construct
 *       an attach to the subject.
 * @param[in] update This parameter is the update function that is passed to
 *       the constructor.
 * @param[in] the_subject This parameter specifies the subject to observe.
 */
void sci_base_observer_initialize(
   struct SCI_BASE_OBSERVER   *this_observer,
   SCI_BASE_OBSERVER_UPDATE_T  update,
   struct SCI_BASE_SUBJECT    *the_subject
);

/**
 * @brief This method will call the observers update function
 *
 * @param[in] this_observer This parameter specifies the observer to be
 *            notified.
 * @param[in] the_subject This parameter indicates the subject for which
 *            the update call is occurring.
 *
 * @return none
 */
void sci_base_observer_update(
   struct SCI_BASE_OBSERVER *this_observer,
   struct SCI_BASE_SUBJECT  *the_subject
);

#else // defined(SCI_LOGGING)

typedef U8 SCI_BASE_OBSERVER_T;
#define sci_base_observer_construct
#define sci_base_observer_initialize
#define sci_base_observer_update

#endif // defined(SCI_LOGGING)

#endif // _SCI_BASE_OBSERVER_H_
