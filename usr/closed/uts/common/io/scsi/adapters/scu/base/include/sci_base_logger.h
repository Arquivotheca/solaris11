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
#ifndef _SCI_BASE_LOGGER_H_
#define _SCI_BASE_LOGGER_H_

/**
 * @file
 *
 * @brief This file contains all of the constants, structures, and methods
 *        common to all base logger objects.
 */


#include "sci_logger.h"
#include "sci_base_object.h"


#define SCI_BASE_LOGGER_MAX_VERBOSITY_LEVELS 5

/**
 * @struct SCI_BASE_LOGGER
 *
 * @brief This structure contains the set of log objects and verbosities that
 *        are enabled for logging.  It's parent is the SCI_BASE_OBJECT_T.
 */
typedef struct SCI_BASE_LOGGER
{
#if defined(SCI_LOGGING)
   /**
    * The field specifies that the parent object for the base logger
    * is the base object itself.
    */
   SCI_BASE_OBJECT_T parent;

   /**
    * This filed specifies an array of objects mask.  There is one object
    * mask for each verbosity level (e.g. ERROR, WARNING, etc.).  This
    * allows for more flexible logging.
    */
   U32  object_mask[SCI_BASE_LOGGER_MAX_VERBOSITY_LEVELS];

   /**
    * This filed specifies which verbosity levels are currently enabled
    * for logging 
    */
   U32  verbosity_mask;
#else // defined(SCI_LOGGING)
   U8  dummy;
#endif // defined(SCI_LOGGING)

} SCI_BASE_LOGGER_T;

#if defined(SCI_LOGGING)

/**
 * @brief This method simply performs initialization of the base logger object.
 *
 * @param[in] this_logger This parameter specifies the logger that we are
 *            going to construct
 *
 * @return none
 */
void sci_base_logger_construct(
   SCI_BASE_LOGGER_T *this_logger
);

#else // SCI_LOGGING

#define sci_base_logger_construct

#endif

#endif // _SCI_BASE_LOGGER_H_
