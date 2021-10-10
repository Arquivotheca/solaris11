/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/**
 * \file KMSAgentFatalState.cpp
 */
#include <stdio.h>
#include <string.h>

#include "SYSCommon.h"
#include "KMSAgentStringUtilities.h"
#include "KMSAuditLogger.h"

#define MAX_TIME_STAMP_LENGTH 30

#ifndef METAWARE
/**
 *  append the state of the application in the <KMSAgentAuditLogger> log file. 
 */
void process_fatal_application_state(const char* sFile, 
                                     const char* sFunction, 
                                     int iLine,
									 const char* sAdditionalText)
{
	
   // File format: <date/time>,<operation>,<retention>,<audit id>,<network adddress>,<message>
   char sFileLogEntry[MAX_LOG_FILE_LINE_LENGTH];
   char sTimeStamp[MAX_TIME_STAMP_LENGTH];
   char sLine[20];
   
   GetCurrentDateTimeISO8601UTC(sTimeStamp, MAX_TIME_STAMP_LENGTH);
   Int64ToUTF8(sLine, iLine, false, false);

   strncpy(sFileLogEntry, "A fatal application error has occurred. Date: ", sizeof(sFileLogEntry));

   sFileLogEntry[sizeof(sFileLogEntry)-1] = '\0';
   
   strncat(sFileLogEntry, sTimeStamp, MAX_LOG_FILE_LINE_LENGTH - strlen(sFileLogEntry));
    
   strncat(sFileLogEntry, " File: ", MAX_LOG_FILE_LINE_LENGTH - strlen(sFileLogEntry));

   strncat(sFileLogEntry, sFile, MAX_LOG_FILE_LINE_LENGTH - strlen(sFileLogEntry));

   strncat(sFileLogEntry, " Function: ", MAX_LOG_FILE_LINE_LENGTH - strlen(sFileLogEntry));

   strncat(sFileLogEntry, sFunction, MAX_LOG_FILE_LINE_LENGTH - strlen(sFileLogEntry));

   strncat(sFileLogEntry, " Line: ", MAX_LOG_FILE_LINE_LENGTH - strlen(sFileLogEntry));

   strncat(sFileLogEntry, sLine, MAX_LOG_FILE_LINE_LENGTH - strlen(sFileLogEntry));

   LogToFile( 0, sFileLogEntry );

   exit( -1 );
}

#endif
