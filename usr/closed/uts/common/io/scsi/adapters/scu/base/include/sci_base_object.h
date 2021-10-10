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
#ifndef _SCI_BASE_OBJECT_H_
#define _SCI_BASE_OBJECT_H_

/**
 * @file
 *
 * @brief This file contains all of the method and constants associated with
 *        the SCI base object.  The SCI base object is the class from which
 *        all other objects derive in the Storage Controller Interface.
 */

#include "sci_object.h"

// Forward declare the logger object
struct SCI_BASE_LOGGER;

/**
 * @struct SCI_BASE_OBJECT
 *
 * @brief The SCI_BASE_OBJECT object represents the data and functionality
 *        that is common to all SCI objects.  It is the base class.
 */
typedef struct SCI_BASE_OBJECT
{
   /**
    * This field represents an association created by the user for this
    * object.  The association can be whatever the user wishes.  Think of
    * it as a cookie.
    */
   void * associated_object;

   /**
    * This field simply contains a handle to the logger object to be
    * utilized when utilizing the logger interface.
    */
   struct SCI_BASE_LOGGER * logger;

} SCI_BASE_OBJECT_T;


/**
 * @brief This method constructs the sci base object.
 *
 * @param[in]  base_object This parameter specifies the SCI base 
 *              object which we whish to construct.
 * @param[in]  logger This parameter specifies the logger object to be
 *             saved and utilized for this base object.
 *
 * @return none
 */
void sci_base_object_construct(
   SCI_BASE_OBJECT_T      * base_object,
   struct SCI_BASE_LOGGER * logger
);

#if defined(SCI_LOGGING)
/**
 * @brief This method returns the logger to which a previous 
 *         association was created.
 *
 * @param[in]  base_object This parameter specifies the SCI base object for
 *             which to retrieve the logger.
 *
 * @return This method returns a pointer to the logger that was 
 *          previously associated to the supplied base_object
 *          parameter.
 * @retval NULL This value is returned when there is no logger
 *         association for the supplied base_object instance.
 */
#define sci_base_object_get_logger(this_object) \
   (((SCI_BASE_OBJECT_T *)(this_object))->logger)

#else // defined(SCI_LOGGING)

#define sci_base_object_get_logger(this_object) NULL

#endif // defined(SCI_LOGGING)


#endif // _SCI_BASE_OBJECT_H_

