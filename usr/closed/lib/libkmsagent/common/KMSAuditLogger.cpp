/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/**
 * \file KMSAuditLogger.cpp
 */

#ifndef WIN32
//#include <syslog.h>
#include <stdarg.h>
#endif

#include <stdio.h>

#ifndef METAWARE
#include <sys/timeb.h>
#endif

#include <time.h>

#include "KMSAuditLogger.h"
#include "ApplianceParameters.h"

#define AGENT_LOG_FILE              "KMSAgentLog.log"

// globals for file logging
static FILE* g_fpLogFileHandle = NULL;
static K_MUTEX_HANDLE g_stLogFileMutex;
static char g_sLogFileName[MAX_LOG_FILE_NAME_LENGTH];

// Find header in AuditLogger.h
int InitializeFileLogging( const char* const i_sWorkingDirectory )
{
    FATAL_ASSERT( i_sWorkingDirectory );
    if ( g_fpLogFileHandle != NULL )
    {
        return false;
    }    

    char sLogFileName[MAX_LOG_FILE_NAME_LENGTH];
    strncpy( sLogFileName, i_sWorkingDirectory, MAX_LOG_FILE_NAME_LENGTH );

    if ( sLogFileName[ strlen( sLogFileName )-1 ] != PATH_SEPARATOR )
    {
        sLogFileName[ strlen(sLogFileName) ] = PATH_SEPARATOR ;
        sLogFileName[ strlen(sLogFileName) + 1 ] = '\0';
    }

    strncat( sLogFileName, AGENT_LOG_FILE, MAX_LOG_FILE_NAME_LENGTH );

    strcpy(g_sLogFileName, sLogFileName);

    if ( K_CreateMutex( &g_stLogFileMutex ) != K_SYS_OK )
    {
        return false;
    }

    if ( NULL == ( g_fpLogFileHandle = fopen( g_sLogFileName, "a+t" ) ) )
    {
        return false;
    }

    return true;
}

// Find header in AuditLogger.h
int FinalizeFileLogging()
{
    FATAL_ASSERT( g_fpLogFileHandle != NULL );

    K_DestroyMutex( g_stLogFileMutex );

    bool bSuccess = ( 0 == fclose( g_fpLogFileHandle ) );

    g_fpLogFileHandle = NULL;

    return bSuccess;
}

// Find header in AuditLogger.h
extern "C" int LogToFile( int i_iErrno, 
               const char* const i_sLogLine )
{
    if ( g_fpLogFileHandle == NULL )
    {
        return false;
    }        

    CAutoMutex oAutoMutex( g_stLogFileMutex );

    if (0 > fputs( i_sLogLine, g_fpLogFileHandle ) )
    {
        return false;
    }

    if ( 0 > fputs( "\n", g_fpLogFileHandle ) )
    {
        return false;
    }

    if ( fflush( g_fpLogFileHandle ) != 0 )
    {
        return false;
    }

    return true;
}

static const int g_iMaxLogFileLineLength = MAX_LOG_FILE_LINE_LENGTH;


int Log_function(
   int i_iErrno,
   const char* const i_sOperation,
   const char* const i_sEntityID,
   const char* const i_sNetworkAddress,
   const char* const i_sMessage )
{
    char sFileLogEntry[500];
    const int iTempSize = 100;
 
    timeb stTime;
    ftime(&stTime);

    struct tm* pstTime = gmtime( &(stTime.time) );

    K_snprintf( 
        sFileLogEntry, 
        iTempSize,
        "%04d-%02d-%02d %02d:%02d:%02d.%03dZ",
        pstTime->tm_year+1900,
        pstTime->tm_mon+1,
        pstTime->tm_mday,
        pstTime->tm_hour,
        pstTime->tm_min,
        pstTime->tm_sec,
        stTime.millitm);

    if ( i_sEntityID )
    {
        strcat(sFileLogEntry," AgentID=");
        strcat(sFileLogEntry,i_sEntityID);
    }

    if ( i_sNetworkAddress )
    {
        strcat(sFileLogEntry," KMA Address=");
        strcat(sFileLogEntry, i_sNetworkAddress);
    }
    if ( i_sOperation )
    {
        strcat(sFileLogEntry, " Operation=");
        strcat(sFileLogEntry,i_sOperation);
    }

    if ( i_sMessage )
    {
        strcat(sFileLogEntry, " Msg=");
        strcat(sFileLogEntry, i_sMessage);
    }

    return LogToFile( i_iErrno, sFileLogEntry );
}

int Log2(char* msg1,
         char* msg2)
{
   return 0;
}
