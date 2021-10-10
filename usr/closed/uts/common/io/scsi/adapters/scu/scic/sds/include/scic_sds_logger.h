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
#ifndef _SCIC_SDS_LOGGER_H_
#define _SCIC_SDS_LOGGER_H_

/**
 * @file
 *  
 * @brief This file contains some macros to remap the user callbacks for log  
 *        messages.
 */

#include "scic_logger.h"
#include "scic_user_callback.h"

#if defined (SCI_LOGGING)

#define SCIC_LOG_ERROR(x)    scic_cb_logger_log_error x
#define SCIC_LOG_WARNING(x)  scic_cb_logger_log_warning x
#define SCIC_LOG_INFO(x)     scic_cb_logger_log_info x
#define SCIC_LOG_TRACE(x)    scic_cb_logger_log_trace x
#define SCIC_LOG_STATES(x)   scic_cb_logger_log_states x

#else // defined (SCI_LOGGING)

#define SCIC_LOG_ERROR(x)
#define SCIC_LOG_WARNING(x)
#define SCIC_LOG_INFO(x)
#define SCIC_LOG_TRACE(x)
#define SCIC_LOG_STATES(x)

#endif // defined (SCI_LOGGING)

#endif // _SCIC_SDS_LOGGER_H_
