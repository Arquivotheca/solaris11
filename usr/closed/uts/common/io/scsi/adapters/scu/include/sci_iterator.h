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
 * @brief This file contains the interface to the iterator class.
 *        Methods Provided:
 *        - sci_iterator_get_object_size()
 *        - sci_iterator_get_current()
 *        - sci_iterator_first()
 *        - sci_iterator_next()
 */

#ifndef _SCI_ITERATOR_H_
#define _SCI_ITERATOR_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#if !defined(DISABLE_SCI_ITERATORS)

//******************************************************************************
//*
//*     I N C L U D E S
//*
//******************************************************************************

#include "sci_types.h"

//******************************************************************************
//*
//*     C O N S T A N T S
//*
//******************************************************************************

//******************************************************************************
//*
//*     T Y P E S
//*
//******************************************************************************

//******************************************************************************
//*
//*     P U B L I C       M E T H O D S
//*
//******************************************************************************

U32 sci_iterator_get_object_size(
   void
);

void * sci_iterator_get_current(
   SCI_ITERATOR_HANDLE_T iterator_handle
);

void sci_iterator_first(
   SCI_ITERATOR_HANDLE_T iterator_handle
);

void sci_iterator_next(
   SCI_ITERATOR_HANDLE_T iterator_handle
);

#else // !defined(DISABLE_SCI_ITERATORS)

#define sci_iterator_get_object_size() 0
#define sci_iterator_get_current(the_iterator) NULL
#define sci_iterator_first(the_iterator)
#define sci_iterator_next(the_iterator)

#endif // !defined(DISABLE_SCI_ITERATORS)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _ITERATOR_H_
