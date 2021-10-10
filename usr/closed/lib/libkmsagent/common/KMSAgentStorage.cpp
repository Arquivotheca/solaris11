/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/**
 *  \file   KMSAgentStorage.cpp
 *  This file provides an implementation of the KMSAgentStorage.h 
 *  interface utilizing a filesystem for storage of KMS Client 
 *  Profile elements.
 *
 *  For storage of Certificates and Private key material the PKICommon 
 *  interface is used.
 */

#include <stdio.h>
#include <string.h>

#ifndef METAWARE
#include <errno.h>
#endif

#ifdef K_SOLARIS_PLATFORM
#ifndef SOLARIS10
#include <cryptoutil.h>
#endif
#include <pthread.h>
#include <fcntl.h>
#endif

#include "stdsoap2.h"

#include "KMSClientProfile.h"  // must be before agentstorage
#include "KMSAgentPKICommon.h" // must be before agentstorage
#include "KMSAgentStorage.h"

#include "SYSCommon.h"
#include "AutoMutex.h"
#include "KMSAuditLogger.h"
#include "KMSClientProfileImpl.h"

#include "KMSAgent_direct.h"
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
#include "KMSAgent.h"
#endif
#include "k_setupssl.h"        // K_ssl_client_context

#ifdef METAWARE
extern "C" int K_ssl_client_context(struct soap *soap,
                                    int flags,
                                    const char *keyfile,  // NULL - SERVER
                                    const char *password, // NULL - SERVER
                                    const char *cafile,
                                    const char *capath,   // ALWAYS NULL 
                                    const char *randfile); // ALWAYS NULL
#include "debug.h"
#endif


#define CA_CERTIFICATE_FILE    "ca.crt"
#define CLIENT_KEY_FILE        "clientkey.pem"

#define PROFILE_CONFIG_FILE         "profile.cfg"
#define PROFILE_CLUSTER_CONFIG_FILE "cluster.cfg"

static char g_sWorkingDirectory[KMS_MAX_PATH_LENGTH+1];
static char g_sStringbuf[10000]; // too large to be on the 9840D stack

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
pthread_rwlock_t		ca_rwlock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t		p12_rwlock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t		cfg_rwlock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t		cluster_rwlock = PTHREAD_RWLOCK_INITIALIZER;
#endif

static void BuildFullProfilePathWithName(utf8cstr          o_pProfilePath,
                                         const char* const i_pWorkingDirectory,
                                         const char* const i_pProfileName)
{
   int len;
   FATAL_ASSERT( o_pProfilePath );
   FATAL_ASSERT( i_pWorkingDirectory );
   FATAL_ASSERT( i_pProfileName );
   FATAL_ASSERT( (strlen(i_pWorkingDirectory) > 0) );
   FATAL_ASSERT( (strlen(i_pProfileName) > 0) );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, BuildFullProfilePathWithName );
#endif

   strncpy(o_pProfilePath, i_pWorkingDirectory, 
           KMS_MAX_FILE_NAME );
   
   if ( o_pProfilePath[ strlen(o_pProfilePath) -1 ] != PATH_SEPARATOR )
   {
      len = strlen(o_pProfilePath);
      o_pProfilePath[ len ] = PATH_SEPARATOR ;
      o_pProfilePath[ len + 1 ] = '\0';
   }
   
   strncat( o_pProfilePath, i_pProfileName, KMS_MAX_FILE_NAME );
   len = strlen(o_pProfilePath);
   o_pProfilePath[ len ] = PATH_SEPARATOR ;
   o_pProfilePath[ len +1 ] = '\0';
   
   return;
}

static void BuildFullProfilePath(utf8cstr          o_sProfilePath,
                                 const char* const i_pWorkingDirectory,
                                 const char* const i_pProfileName)
{
   FATAL_ASSERT( o_sProfilePath );
   FATAL_ASSERT( i_pWorkingDirectory );
   FATAL_ASSERT( i_pProfileName );
   FATAL_ASSERT( (strlen(i_pProfileName) > 0) );

   BuildFullProfilePathWithName( o_sProfilePath, 
                                 i_pWorkingDirectory, 
                                 i_pProfileName );

   return;
}

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
static FILE *
open_and_lock_fp(char *i_fileName, int i_flags, mode_t i_mode,
    pthread_rwlock_t *rwlock)
{
	int		st;
	int		fd = -1;
	short		fl_type;
	struct flock	fl;
	FILE		*fp = NULL;
	bool		is_readonly;
	int		flags = 0;

	if ((i_fileName == NULL) || (i_fileName[0] == '\0')) {
		return (NULL);
	}

	if (i_flags == O_RDONLY) {
		is_readonly = true;
		flags = O_RDONLY;
		fl_type = F_RDLCK;
		(void) pthread_rwlock_rdlock(rwlock);
	} else {
		is_readonly = false;
		/* for writes only, create file if it doesn't exist */
		flags = O_RDWR|O_CREAT;
		fl_type = F_WRLCK;
		(void) pthread_rwlock_wrlock(rwlock);
	}

	do {
		if (i_mode != 0) {
			fd = open(i_fileName, flags, i_mode);
		} else {
			fd = open(i_fileName, flags);
		}
	} while ((fd == -1) && (errno == EINTR));

	if (fd != -1) {
		(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

		memset(&fl, 0, sizeof (struct flock));
		fl.l_type = fl_type;
		fl.l_whence = SEEK_SET;

		do {
			st = fcntl(fd, F_SETLKW, &fl);
		} while ((st == -1) && (errno = EINTR));

		/* truncate if requested */
		if ((st == 0) && (i_flags & O_TRUNC)) {
			st = ftruncate(fd, 0);
		}

		if (st == 0) {
			if (is_readonly) {
				fp = fdopen(fd, "r");
			} else {
				fp = fdopen(fd, "w+");
			}
		}
	}

	if (fp == NULL) {
		if (fd != -1) {
			close(fd);
		}
		pthread_rwlock_unlock(rwlock);
	}

	return (fp);
}

static void
close_and_unlock_fp(FILE *fp, pthread_rwlock_t *rwlock)
{
	int		st;
	int		fd = -1;
	struct flock	fl;

	if (fp == NULL) {
		return;
	}

	fd = fileno(fp);
	if (fd != -1) {
		fflush(fp);

		memset(&fl, 0, sizeof (struct flock));
		fl.l_type = F_UNLCK;
		fl.l_whence = SEEK_SET;

		do {
			st = fcntl(fd, F_SETLKW, &fl);
		} while ((st == -1) && (errno = EINTR));

		fclose(fp);
	}

	pthread_rwlock_unlock(rwlock);
}
#endif

static bool Profile_WriteConfigFile(KMSClientProfile *i_pProfile, 
                                    const char *i_pFileName)
{
   FATAL_ASSERT( i_pProfile );
   FATAL_ASSERT( i_pFileName );
   
   CAutoMutex oAutoMutex( (K_MUTEX_HANDLE)i_pProfile->m_pLock );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, Profile_WriteConfigFile );
#endif

   char *sp = g_sStringbuf;
   size_t  bytesWritten = 0;
   
   // save config parameters
   
   myFILE *fp;

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   fp = open_and_lock_fp((char *)i_pFileName, O_RDWR|O_TRUNC, 0600, &cfg_rwlock);
#else
   fp = fopen(i_pFileName, "w");
#endif

   if (fp == NULL)
   {
      LogError(i_pProfile,
               AUDIT_PROFILE_WRITE_CONFIG_FILE_OPEN_CONFIGURATION_FILE_FAILED,
               NULL,
               NULL,
               i_pFileName);
      
      return false;
   }

const char* const sProfileName = i_pProfile->m_wsProfileName;
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "ProfileName=%s\n", sProfileName);
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "AgentID=%s\n", i_pProfile->m_wsEntityID);
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "ClusterDiscoveryFrequency=%d\n",
                 i_pProfile->m_iClusterDiscoveryFrequency);
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "CAServicePortNumber=%d\n",
                 i_pProfile->m_iPortForCAService);
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "CertificateServicePortNumber=%d\n",
                 i_pProfile->m_iPortForCertificateService);
   
   if(i_pProfile->m_iPortForAgentService != 0)
   {
      sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "AgentServicePortNumber=%d\n",
                    i_pProfile->m_iPortForAgentService);
   }
   
   if(i_pProfile->m_iPortForDiscoveryService != 0)
   {
      sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "DiscoveryServicePortNumber=%d\n",
                    i_pProfile->m_iPortForDiscoveryService);
   }
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "ApplianceAddress=%s\n", i_pProfile->m_wsApplianceAddress);
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "Timeout=%d\n", i_pProfile->m_iTransactionTimeout);
   
   sp += K_snprintf(sp, sizeof(i_pProfile->m_wsProfileName), "FailoverLimt=%d\n", i_pProfile->m_iFailoverLimit);

   bytesWritten = fputs(g_sStringbuf, fp);

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   close_and_unlock_fp(fp, &cfg_rwlock);
#else
   fclose(fp);
#endif

#if !defined(WIN32) && !defined(K_LINUX_PLATFORM)
   if ( strlen(g_sStringbuf) != bytesWritten )
#else
   if ( bytesWritten < 0 )
#endif
   {
      return false;
   }
   
   return true;
}

static bool Profile_ReadConfigFile
( KMSClientProfile *i_pProfile, 
  const char *i_pFileName)
{
   FATAL_ASSERT( i_pProfile  );
   FATAL_ASSERT( i_pFileName );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, Profile_ReadConfigFile ) ;
#endif
   
   CAutoMutex oAutoMutex( (K_MUTEX_HANDLE)i_pProfile->m_pLock );
   
   const int iMaxLineSize = 1024;
   
   myFILE *fp;
   char acBuffer[iMaxLineSize+1];
   
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   fp = open_and_lock_fp((char *)i_pFileName, O_RDONLY, 0, &cfg_rwlock);
#else
   fp = fopen(i_pFileName, "r");
#endif

   if(fp == NULL)
   {
      LogError(i_pProfile,
               AUDIT_PROFILE_READ_CONFIG_FILE_OPEN_CONFIGURATION_FILE_FAILED,
               NULL,
               NULL,
               i_pFileName);
      return false;
   }
   
   // read file one line by one line
   while(1)
   {
      int i;
      char *pName, *pValue;
      
      memset(acBuffer, 0, iMaxLineSize+1);
      
      //---------------------------
      // get info from the file
      //---------------------------
      if(fgets(acBuffer, iMaxLineSize+1, fp) == NULL)
         break;
      
      if(strlen(acBuffer) < 3)
         continue;
      
      if(acBuffer[0] == '#' || 
         acBuffer[0] == ';' || 
         acBuffer[0] == '[')  // jump comments
         continue;
      
      pName = acBuffer; 
      pValue = NULL;
      
      for(i = 0; acBuffer[i] != '\0'; i++)
      {
         if(acBuffer[i] == '=')
            pValue = acBuffer + i + 1;
         
         if(acBuffer[i] == '=' ||
            acBuffer[i] == '\r' || 
            acBuffer[i] == '\n')
            acBuffer[i] = '\0';
      }
      
      if(pValue == NULL)
      {
         LogError(i_pProfile,
                  AUDIT_PROFILE_READ_CONFIG_FILE_INVALID_CONFIGURATION_FILE_FORMAT,
                  NULL,
                  NULL,
                  i_pFileName);
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	close_and_unlock_fp(fp, &cfg_rwlock);
#else
         fclose(fp);
#endif
         return false;
      }

      if(strcmp(pName, "ProfileName") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(i_pProfile->m_wsProfileName, wsValue, KMS_MAX_ENTITY_ID);
         i_pProfile->m_wsProfileName[KMS_MAX_ENTITY_ID] = 0;
      }

      if(strcmp(pName, "AgentID") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(i_pProfile->m_wsEntityID, wsValue, KMS_MAX_ENTITY_ID);
         i_pProfile->m_wsEntityID[KMS_MAX_ENTITY_ID] = 0;
      }
      
      if(strcmp(pName, "ClusterDiscoveryFrequency") == 0)
      {
         sscanf(pValue, "%d", &(i_pProfile->m_iClusterDiscoveryFrequency));
      }
      
      if(strcmp(pName, "CAServicePortNumber") == 0)
      {
         sscanf(pValue, "%d", &(i_pProfile->m_iPortForCAService));
      }

      if(strcmp(pName, "CertificateServicePortNumber") == 0)
      {
         sscanf(pValue, "%d", &(i_pProfile->m_iPortForCertificateService));
      }

      if(strcmp(pName, "AgentServicePortNumber") == 0)
      {
         sscanf(pValue, "%d", &(i_pProfile->m_iPortForAgentService));
      }

      if(strcmp(pName, "DiscoveryServicePortNumber") == 0)
      {
         sscanf(pValue, "%d", &(i_pProfile->m_iPortForDiscoveryService));
      }

      if(strcmp(pName, "ApplianceAddress") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(i_pProfile->m_wsApplianceAddress, 
                 wsValue, KMS_MAX_NETWORK_ADDRESS);
         i_pProfile->m_wsApplianceAddress[KMS_MAX_NETWORK_ADDRESS] = 0;
      }

      if(strcmp(pName, "Timeout") == 0)
      {
         sscanf(pValue, "%d", &(i_pProfile->m_iTransactionTimeout));
      }

      if(strcmp(pName, "FailoverLimt") == 0)
      {
         sscanf(pValue, "%d", &(i_pProfile->m_iFailoverLimit));
      }

   }
   
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   close_and_unlock_fp(fp, &cfg_rwlock);
#else
   fclose(fp);
#endif

   return true;
}

/*! ProfileExists
 *
 */
extern "C" bool ProfileExists(
   const char* const i_pWorkingDirectory,
   const char* const i_pProfileName)
{
   FATAL_ASSERT( i_pWorkingDirectory );
   FATAL_ASSERT( i_pProfileName );
   
#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, ProfileExists );
#endif


   // the profile is stored in the working folder
   strncpy( g_sWorkingDirectory, 
            i_pWorkingDirectory, 
            KMS_MAX_PATH_LENGTH );
   
   char sFullProfileDir[KMS_MAX_FILE_NAME+1];
   BuildFullProfilePath( sFullProfileDir, 
                         i_pWorkingDirectory, 
                         i_pProfileName ); 
   
   char sConfigFile[KMS_MAX_FILE_NAME+1];
   strncpy( sConfigFile, sFullProfileDir, KMS_MAX_FILE_NAME );
   strncat( sConfigFile, PROFILE_CONFIG_FILE, KMS_MAX_FILE_NAME );
   
   // just try to open the file to test if it exists
   
   bool bProfileExists = false;
   
   myFILE* pfFile = fopen( sConfigFile, "rb" );
   
   if ( pfFile != NULL )
   {
      bProfileExists = true;
      
      fclose(pfFile);
   }
   
   return bProfileExists;
}


/*! CreateProfile
 *
 */
bool CreateProfile(
   KMSClientProfile* const io_pProfile,
   const char* const       i_pWorkingDirectory,
   const char* const       i_pProfileName)
{
   FATAL_ASSERT( io_pProfile );
   FATAL_ASSERT( i_pWorkingDirectory );
   FATAL_ASSERT( i_pProfileName );
   FATAL_ASSERT( (strlen(i_pProfileName) > 0) );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, CreateProfile );
   
#endif
   
   bool bSuccess = false;
   CAutoMutex oAutoMutex( (K_MUTEX_HANDLE)io_pProfile->m_pLock );

   char sFullProfileDir[KMS_MAX_FILE_NAME];
   BuildFullProfilePath( sFullProfileDir,
                         i_pWorkingDirectory, 
                         i_pProfileName ); 

   bSuccess = ( K_CreateDirectory( sFullProfileDir ) == 0 );

   if ( !bSuccess )
   {
      Log(AUDIT_CLIENT_LOAD_PROFILE_CREATE_DIRECTORY_FAILED,
          NULL,
          NULL,
          NULL );
   }
   strncpy( g_sWorkingDirectory, i_pWorkingDirectory, KMS_MAX_PATH_LENGTH );

   bSuccess = StoreConfig( io_pProfile );
   if ( !bSuccess )
   {
      Log(AUDIT_CLIENT_LOAD_PROFILE_CREATE_PROFILE_CONFIG_FAILED,
          NULL,
          NULL,
          NULL );
   }
   else
   {
      Log(AUDIT_CLIENT_LOAD_PROFILE_CREATE_PROFILE_CONFIG_SUCCEEDED,
          NULL,
          NULL,
          NULL );
   }

   return bSuccess;
}


/*! StoreConfig
 * Store the configuration to persistent storage
 */
bool StoreConfig(
   KMSClientProfile* const i_pProfile )
{
   FATAL_ASSERT( i_pProfile );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, StoreConfig ) ;
#endif
   
   char sConfigFile[KMS_MAX_FILE_NAME + 1];
   BuildFullProfilePath( sConfigFile, 
                         g_sWorkingDirectory, i_pProfile->m_wsProfileName );
   
   strncat( sConfigFile, PROFILE_CONFIG_FILE, KMS_MAX_FILE_NAME );
   
   return Profile_WriteConfigFile(i_pProfile, sConfigFile );
}

/*! StoreCluster
 * Store the cluster to persistent storage
 */
bool StoreCluster(            
   KMSClientProfile* const i_pProfile )
{
   FATAL_ASSERT( i_pProfile );
   
   myFILE *fp;
   int sCount;
   char *sp = g_sStringbuf;
   bool status = true;
   
   char sFullProfileDir[KMS_MAX_FILE_NAME+1];
   BuildFullProfilePath( sFullProfileDir, 
                         g_sWorkingDirectory, i_pProfile->m_wsProfileName );

   char sClusterFile[KMS_MAX_FILE_NAME+1];
   strncpy( sClusterFile, sFullProfileDir, KMS_MAX_FILE_NAME ); 
   strncat( sClusterFile, PROFILE_CLUSTER_CONFIG_FILE, KMS_MAX_FILE_NAME );   
   
#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, StoreCluster );
#endif

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   fp = open_and_lock_fp(sClusterFile, O_RDWR|O_TRUNC, 0600, &cluster_rwlock);
#else
   fp = fopen(sClusterFile, "w");
#endif

   if (fp == NULL)
   {
      LogError(i_pProfile,
               AUDIT_CLIENT_SAVE_CLUSTER_INFORMATION_OPEN_CLUSTER_FILE_FAILED,
               NULL,
               NULL,
               sClusterFile );
      return false;
   }

   sp += K_snprintf(sp, sizeof(g_sStringbuf), "EntitySiteID=%s\n\n", i_pProfile->m_wsEntitySiteID);
   
   for (int i = 0;  i < i_pProfile->m_iClusterNum; i++)
   {
      if ( i > 0 )
      {
         sp += K_snprintf(sp, sizeof(g_sStringbuf), "\n");
      }
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf),"<StartAppliance>\n")) < 0 )
      {
	      status = false;
	      break;
      }
      sp += sCount;

#ifdef WIN32
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "ApplianceID=%I64d\n",
                             i_pProfile->m_aCluster[i].m_lApplianceID)) < 0 ) 
#else
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "ApplianceID=%lld\n",
                             i_pProfile->m_aCluster[i].m_lApplianceID)) < 0 )
#endif
      {
		status = false;
		break;
      }
      sp += sCount;

      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "Enabled=%d\n",
                             i_pProfile->m_aCluster[i].m_iEnabled)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "Responding=%d\n",
                             i_pProfile->m_aCluster[i].m_iResponding)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "Load=%lld\n",
                             i_pProfile->m_aCluster[i].m_lLoad)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "ApplianceAlias=%s\n",
                             i_pProfile->m_aCluster[i].m_wsApplianceAlias)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "ApplianceNetworkAddress=%s\n",
                             i_pProfile->m_aCluster[i].m_wsApplianceNetworkAddress)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "ApplianceSiteID=%s\n",
                             i_pProfile->m_aCluster[i].m_wsApplianceSiteID)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "KMAVersion=%s\n",
                             i_pProfile->m_aCluster[i].m_sKMAVersion)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
      
      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "KMALocked=%d\n",
                             i_pProfile->m_aCluster[i].m_iKMALocked)) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;

      if (( sCount = K_snprintf(sp, sizeof(g_sStringbuf), "<EndAppliance>\n")) < 0 )
	{
		status = false;
		break;
	}
      sp += sCount;
   }

   if (status == true) {
	fputs(g_sStringbuf, fp);
   	Log(AUDIT_CLIENT_SAVE_CLUSTER_INFORMATION_SUCCEEDED,
	  NULL,
	  NULL,
          NULL );
   }

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   close_and_unlock_fp(fp, &cluster_rwlock);
#else
   fclose(fp);
#endif

   return (status);
}

/*! GetConfig
 * get the configuration file from persistent storage
 */
bool GetConfig(
   KMSClientProfile* const io_pProfile )
{
   FATAL_ASSERT( io_pProfile );
   char sFullProfileDir[KMS_MAX_FILE_NAME+1];
   
   BuildFullProfilePath( sFullProfileDir, 
                         g_sWorkingDirectory, 
                         io_pProfile->m_wsProfileName ); 

   char sConfigFile[KMS_MAX_FILE_NAME+1];
   
   strncpy( sConfigFile, sFullProfileDir, KMS_MAX_FILE_NAME );
   strncat( sConfigFile, PROFILE_CONFIG_FILE, KMS_MAX_FILE_NAME );

   return Profile_ReadConfigFile( io_pProfile, sConfigFile );
}

/** GetCluster
 * get the cluster information from persistent storage
 */
bool GetCluster(
   KMSClientProfile* const io_pProfile,
   int&                   o_bClusterInformationFound )

{
   FATAL_ASSERT( io_pProfile );

   const int iMaxLineSize = 1024;

   myFILE *fp;
   bool status = true;
   char acBuffer[iMaxLineSize+1];
   char sFullProfileDir[KMS_MAX_FILE_NAME+1];

   BuildFullProfilePath( sFullProfileDir, 
                         g_sWorkingDirectory, 
                         io_pProfile->m_wsProfileName );

   char sClusterFile[KMS_MAX_FILE_NAME+1];

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, GetCluster );
#endif

   strncpy( sClusterFile, sFullProfileDir, KMS_MAX_FILE_NAME );
   strncat( sClusterFile, PROFILE_CLUSTER_CONFIG_FILE, KMS_MAX_FILE_NAME );
   
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   fp = open_and_lock_fp(sClusterFile, O_RDONLY, 0, &cluster_rwlock);
#else
   fp = fopen( sClusterFile, "r" );
#endif

   if ( fp == NULL )
   {
#ifdef METAWARE
      // Assume file doesn't exist.  This isn't an error (no support for
      // errno in metaware).
      o_bClusterInformationFound = 0;
      return true;
#else
      if ( errno == ENOENT )
      {
         // File doesn't exist.  This isn't an error.
         o_bClusterInformationFound = 0;
         return true;
      }

      LogError(io_pProfile,
               AUDIT_CLIENT_LOAD_CLUSTER_INFORMATION_OPEN_CLUSTER_FILE_FAILED,
               NULL,
               NULL,
               sClusterFile );
      return false;
#endif
   }

   o_bClusterInformationFound = 1;
   int i;
   // KMAVersion is new to Cluster config with 2.1 KMS and will not exist
   // in persisted cluster configs from earlier agents
   for ( i = 0; i < KMS_MAX_CLUSTER_NUM; i++ )
   {
        io_pProfile->m_aCluster[i].m_sKMAVersion[0] = '\0';
   }
    
   int iClusterNum = 0;
   // read file one line by one line
   while(1)
   {
      int i;
      char *pName, *pValue;

      memset(acBuffer, 0, iMaxLineSize+1);

      // get info from the file
      if(fgets(acBuffer, iMaxLineSize+1, fp) == NULL)
         break;

      if(strlen(acBuffer) < 3)
         continue;

      if(acBuffer[0] == '#' || 
         acBuffer[0] == ';' || 
         acBuffer[0] == '[')  // jump comments
         continue;

      pName = acBuffer; pValue = NULL;
      for(i = 0; acBuffer[i] != '\0'; i++)
      {
         if(acBuffer[i] == '=')
            pValue = acBuffer + i + 1;

         if(acBuffer[i] == '=' || 
            acBuffer[i] == '\r' || 
            acBuffer[i] == '\n')
            acBuffer[i] = '\0';
      }

      if(strcmp(pName, "<StartAppliance>") == 0)
      {
         continue;
      }
      if(strcmp(pName, "<EndAppliance>") == 0)
      {
         iClusterNum++;
      }

      if(pValue == NULL)
      {
         if(strcmp(pName,"<StartAppliance>") == 0)
            continue;

         if(strcmp(pName,"<EndAppliance>") == 0)
            continue;
            
         LogError(io_pProfile,
                  AUDIT_CLIENT_LOAD_CLUSTER_INFORMATION_INVALID_CLUSTER_FILE_FORMAT,
                  NULL,
                  NULL,
                  sClusterFile );
         status = false;
	 break;
      }
        
      if(strcmp(pName, "EntitySiteID") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(io_pProfile->m_wsEntitySiteID, wsValue, KMS_MAX_ENTITY_SITE_ID);
         io_pProfile->m_wsEntitySiteID[KMS_MAX_ENTITY_SITE_ID] = 0;
      }
        
        
      if(strcmp(pName, "ApplianceID") == 0)
      {
#ifdef WIN32
         sscanf(pValue, "%lld",
                &(io_pProfile->m_aCluster[iClusterNum].m_lApplianceID));
#else
         sscanf(pValue, "%lld", 
                &(io_pProfile->m_aCluster[iClusterNum].m_lApplianceID));
#endif
      }
      if(strcmp(pName, "Enabled") == 0)
      {
         sscanf(pValue, "%d", 
                &(io_pProfile->m_aCluster[iClusterNum].m_iEnabled));
      }

      // assume it is responding by default  
      io_pProfile->m_aCluster[iClusterNum].
         m_iResponding = TRUE; 
        
      if(strcmp(pName, "Load") == 0)
      {
         sscanf(pValue, "%lld", 
                &(io_pProfile->m_aCluster[iClusterNum].m_lLoad));
      }
      if(strcmp(pName, "ApplianceAlias") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(io_pProfile->m_aCluster[iClusterNum].m_wsApplianceAlias, 
                 wsValue,
                 KMS_MAX_ENTITY_ID);
         io_pProfile->m_aCluster[iClusterNum].
            m_wsApplianceAlias[KMS_MAX_ENTITY_ID] = 0;
            
      }
      if(strcmp(pName, "ApplianceNetworkAddress") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(io_pProfile->m_aCluster[iClusterNum].
                 m_wsApplianceNetworkAddress, 
                 wsValue,
                 KMS_MAX_NETWORK_ADDRESS);
         io_pProfile->m_aCluster[iClusterNum].
            m_wsApplianceNetworkAddress[KMS_MAX_NETWORK_ADDRESS] = 0;
      }
      if(strcmp(pName, "ApplianceSiteID") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(io_pProfile->m_aCluster[iClusterNum].m_wsApplianceSiteID, 
                 wsValue,
                 KMS_MAX_ENTITY_SITE_ID);
         io_pProfile->m_aCluster[iClusterNum].
            m_wsApplianceSiteID[KMS_MAX_ENTITY_SITE_ID] = 0;
      }
      if(strcmp(pName, "KMAVersion") == 0)
      {
         utf8cstr wsValue = pValue;
         strncpy(io_pProfile->m_aCluster[iClusterNum].m_sKMAVersion, 
                 wsValue,
                 KMS_MAX_VERSION_LENGTH);
         io_pProfile->m_aCluster[iClusterNum].
            m_sKMAVersion[KMS_MAX_VERSION_LENGTH] = '\0';
      }
      if(strcmp(pName, "KMALocked") == 0)
      {
         sscanf(pValue, "%d",
            &(io_pProfile->m_aCluster[iClusterNum].m_iKMALocked));
      }
   }
   io_pProfile->m_iClusterNum = iClusterNum;
    
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	close_and_unlock_fp(fp, &cluster_rwlock);
#else
   fclose(fp);
#endif
    
   return (status);
}

/*! DeleteCluster
 *
 */
bool DeleteCluster( KMSClientProfile* const io_pProfile )                   
{
   FATAL_ASSERT( io_pProfile );
   FATAL_ASSERT( io_pProfile->m_wsProfileName );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, DeleteCluster );
#endif
   
   bool bSuccess = true;
   char sFullProfileDir[KMS_MAX_FILE_NAME+1]; 
   char sClusterInformationFile[KMS_MAX_FILE_NAME+1];
    
   BuildFullProfilePathWithName( sFullProfileDir, g_sWorkingDirectory, 
                                 io_pProfile->m_wsProfileName );
    
   strcpy( sClusterInformationFile, sFullProfileDir );
   strncat( sClusterInformationFile, PROFILE_CLUSTER_CONFIG_FILE, 
            KMS_MAX_FILE_NAME );
    
   FILE* pfFile;
 
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   pfFile = open_and_lock_fp(sClusterInformationFile, O_RDWR|O_TRUNC, 0600,
       &cluster_rwlock);
#else
   pfFile = fopen( sClusterInformationFile, "rb" );
#endif

   if ( pfFile != NULL )
   {
      if ( my_unlink(sClusterInformationFile) )
         bSuccess = false;
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
      close_and_unlock_fp(pfFile, &cluster_rwlock);
#else
      fclose(pfFile);
#endif
   }
    
   return true;
}

/*! StoreCACertificate
 *  Store CA Certificate to a persistent storage file
 *  @param i_pProfile
 *  @param i_pCACertificate
 * 
 *  @returns     boolean success or failure
 */
bool StoreCACertificate(
   KMSClientProfile* const i_pProfile,
   CCertificate* const     i_pCACertificate )
{
   FATAL_ASSERT( i_pProfile );
   FATAL_ASSERT( i_pCACertificate );

   char sCACertificateFile[KMS_MAX_FILE_NAME+1];
   FILE *fp = NULL;
   bool status = false;

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, StoreCACertificate );
#endif

   BuildFullProfilePath( sCACertificateFile, 
                         g_sWorkingDirectory, 
                         i_pProfile->m_wsProfileName );

   strncat( sCACertificateFile, CA_CERTIFICATE_FILE, KMS_MAX_FILE_NAME );

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   fp = open_and_lock_fp(sCACertificateFile, O_RDWR|O_TRUNC, 0600, &ca_rwlock);
#else
   fp = fopen(sCACertificateFile, "w");
#endif

   if (fp) {
   	// OVERLOADED Save method - 2 parameters means save to a file
   	status = i_pCACertificate->Save(fp, PKI_FORMAT);

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	close_and_unlock_fp(fp, &ca_rwlock);
#else
	fclose(fp);
#endif
   }

   if (status == false) {
	LogError(i_pProfile,
               AUDIT_CLIENT_LOAD_PROFILE_SAVE_CA_CERTIFICATE_FAILED,
               NULL,
               NULL,
               sCACertificateFile );
   }

   return (status);
}

/*! StoreAgentPKI
 *  Store Private Keys a persistent storage file
 *
 */
#if !defined(K_SOLARIS_PLATFORM) && !defined(K_LINUX_PLATFORM)
static
#endif
bool StoreAgentPKI(
   KMSClientProfile* const i_pProfile,
   CCertificate* const     i_pAgentCertificate,
   CPrivateKey* const      i_pAgentPrivateKey,
   const char* const       i_sHexHashedPassphrase )
{
   FATAL_ASSERT( i_pProfile );
   FATAL_ASSERT( i_pAgentCertificate );

   bool bSuccess = false;
   char sClientKeyFile[KMS_MAX_FILE_NAME+1];
   FILE *fp;

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, StoreAgentPKI ) ;
#endif

   BuildFullProfilePath( sClientKeyFile, 
         g_sWorkingDirectory,
         i_pProfile->m_wsProfileName );

   strncat( sClientKeyFile, 
#ifdef KMSUSERPKCS12
   	CLIENT_PK12_FILE,
#else
            CLIENT_KEY_FILE,
#endif
            KMS_MAX_FILE_NAME );

   CPKI oPKI;

   // save Certificate and Private Key to file named sClientKeyFile(CLIENT_KEY_FILE)
#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
   fp = open_and_lock_fp(sClientKeyFile, O_RDWR|O_TRUNC, 0600, &p12_rwlock);
#else
   fp = fopen(sClientKeyFile, "w");
#endif

   if (fp != NULL) {
	   bSuccess = oPKI.ExportCertAndKeyToFile(
	      i_pAgentCertificate,
	      i_pAgentPrivateKey,
	      fp,
	      i_sHexHashedPassphrase,
#ifdef KMSUSERPKCS12
	      PKCS12_FORMAT
#else
	      PKI_FORMAT
#endif
	      );

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	   close_and_unlock_fp(fp, &p12_rwlock);
#else
	   fclose(fp);
#endif
   }

   if ( !bSuccess )
   {
      LogError(i_pProfile,
               AUDIT_CLIENT_LOAD_PROFILE_EXPORT_CERTIFICATE_AND_KEY_FAILED,
               NULL,
               NULL,
               sClientKeyFile );
   }

   return bSuccess;
}

/*! StorePKIcerts
 * Store PKI objects to persistent storage files
 */
bool StorePKIcerts(
   KMSClientProfile* const     io_pProfile,
   CCertificate* const         i_pCACertificate,
   CCertificate* const         i_pAgentCertificate,
   CPrivateKey* const          i_pAgentPrivateKey,
   const char* const           i_sHexHashedPassphrase )
{
   FATAL_ASSERT( io_pProfile );
   FATAL_ASSERT( i_pAgentCertificate );

   bool bSuccess = false;

   bSuccess = StoreCACertificate( io_pProfile, i_pCACertificate );

   if ( bSuccess )
   {
      bSuccess = StoreAgentPKI( io_pProfile, 
                                i_pAgentCertificate, 
                                i_pAgentPrivateKey, 
                                i_sHexHashedPassphrase );
   }

   if ( bSuccess )
   {
       io_pProfile->m_iEnrolled = TRUE;
   }

   return bSuccess;
}

#ifdef KMSUSERPKCS12

/*
 * Test to see if the PKCS12 file exists.
 */
bool ClientKeyP12Exists(char *profileName)
{
	bool bSuccess = true;
	char sFullProfileDir[KMS_MAX_FILE_NAME+1];
	char sAgentPK12File[KMS_MAX_FILE_NAME+1];
	struct stat statp;

	BuildFullProfilePath(sFullProfileDir,
	    g_sWorkingDirectory, profileName);

	strncpy( sAgentPK12File, sFullProfileDir, KMS_MAX_FILE_NAME );
	strncat( sAgentPK12File, CLIENT_PK12_FILE, KMS_MAX_FILE_NAME );

	bSuccess = false;
	if (stat(sAgentPK12File, &statp) == -1)
		bSuccess = false;
	else if (statp.st_size > 0)
		bSuccess = true;

	return (bSuccess);
}

/*
 * Load the cert and the private key from the PKCS12 file.
 */
bool GetPKCS12CertAndKey(
	KMSClientProfile* const io_pProfile,
	utf8char	*i_pPassphrase,
	CCertificate	*i_pEntityCert,
	CPrivateKey	*i_pEntityPrivateKey)
{
	bool bSuccess = true;
	char sAgentPK12File[KMS_MAX_FILE_NAME+1];
	FILE *fp;

	BuildFullProfilePath(sAgentPK12File,
	    g_sWorkingDirectory, io_pProfile->m_wsProfileName );
	strncat( sAgentPK12File, CLIENT_PK12_FILE, KMS_MAX_FILE_NAME );

	fp = open_and_lock_fp(sAgentPK12File, O_RDONLY, 0, &p12_rwlock);
	if (fp == NULL) {
		return (false);
	}

	bSuccess = i_pEntityCert->LoadPKCS12CertAndKey(
	    fp, FILE_FORMAT_PKCS12, i_pEntityPrivateKey, i_pPassphrase);

	close_and_unlock_fp(fp, &p12_rwlock);

	if (!bSuccess)
		io_pProfile->m_iLastErrorCode = KMS_AGENT_LOCAL_AUTH_FAILURE;

	return (bSuccess);
}
#endif /* PKCS12 */

/** 
 *  GetPKIcerts verifies that CA and Agent certificates are available in
 *  persistent storage and updates profile with an indicator
 */
bool GetPKIcerts(
   KMSClientProfile* const     io_pProfile )
{
   FATAL_ASSERT( io_pProfile );

   bool bSuccess = true;
   char sFullProfileDir[KMS_MAX_FILE_NAME+1];
   char sCAcertFile[KMS_MAX_FILE_NAME+1];
   char sAgentCertFile[KMS_MAX_FILE_NAME+1];
#if !defined(K_SOLARIS_PLATFORM) && !defined(K_LINUX_PLATFORM)
   myFILE* pfFile;
#endif

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, GetPKIcerts );
#endif

  io_pProfile->m_iEnrolled = FALSE;

   BuildFullProfilePath( sFullProfileDir,
       g_sWorkingDirectory, io_pProfile->m_wsProfileName ); 
 
   strncpy( sCAcertFile, sFullProfileDir, KMS_MAX_FILE_NAME );
   strncat( sCAcertFile, CA_CERTIFICATE_FILE, KMS_MAX_FILE_NAME );

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	/*
	 * stat(2) is preferred over fopen(3C)
	 * fopen for checking if a file is present.
	 */
	struct stat statp;
	if (stat(sCAcertFile, &statp)) {
		LogError(io_pProfile,
			AUDIT_CLIENT_LOAD_PROFILE_FAILED,
			NULL,
			NULL,
			"Test for presence of CA Certificate failed" );
		return false;
	}

#else
   pfFile = fopen( sCAcertFile, "rb" );
   
   if ( pfFile != NULL )
   {      
      fclose(pfFile);
   }
   else
   {
      LogError(io_pProfile,
               AUDIT_CLIENT_LOAD_PROFILE_FAILED,
               NULL,
               NULL,
               "Test for presence of CA Certificate failed" );
      return false;
   }
#endif

   // open the file containing client certificate and private key
   // checking if the file exists.
   strncpy( sAgentCertFile, sFullProfileDir, KMS_MAX_FILE_NAME );
   strncat( sAgentCertFile, CLIENT_KEY_FILE, KMS_MAX_FILE_NAME ); 

#if defined(K_SOLARIS_PLATFORM) || defined(K_LINUX_PLATFORM)
	/*
	 * stat(2) is safer than "fopen" for checking if a file is
	 * present or not.
	 */
	if (stat(sAgentCertFile, &statp)) {
		LogError(io_pProfile,
			AUDIT_CLIENT_LOAD_PROFILE_FAILED,
			NULL,
			NULL,
			"Test for presence of Agent Certificate failed" );
		return false;
	}
#else

   pfFile = fopen( sAgentCertFile, "rb" );
   
   if ( pfFile != NULL )
   {      
      fclose(pfFile);
   }
   else
   {
      LogError(io_pProfile,
               AUDIT_CLIENT_LOAD_PROFILE_FAILED,
               NULL,
               NULL,
               "Test for presence of Agent Certificate failed" );
      return false;
   }
#endif

   io_pProfile->m_iEnrolled = TRUE;

   return bSuccess;
}

/**
 * DeleteStorageProfile
 */
bool DeleteStorageProfile( 
   const char* const i_pName)
{
   FATAL_ASSERT( i_pName );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, DeleteStorageProfile );
#endif

   bool bSuccess = true;
   char sFullProfileDir[KMS_MAX_FILE_NAME+1]; 
   char sConfigFile[KMS_MAX_FILE_NAME+1]; 
   char sClusterInformationFile[KMS_MAX_FILE_NAME+1];
   char sCACertificateFile[KMS_MAX_FILE_NAME+1];
#ifndef KMSUSERPKCS12
   char sClientKeyFile[KMS_MAX_FILE_NAME+1];
#else
   char sClientP12File[KMS_MAX_FILE_NAME+1];
#endif

   BuildFullProfilePathWithName( sFullProfileDir, 
                                 g_sWorkingDirectory, i_pName );
   strncpy( sConfigFile, sFullProfileDir, KMS_MAX_FILE_NAME );  
   strncat( sConfigFile, PROFILE_CONFIG_FILE, KMS_MAX_FILE_NAME );

   strncpy( sClusterInformationFile, sFullProfileDir, KMS_MAX_FILE_NAME );
   strncat( sClusterInformationFile, 
            PROFILE_CLUSTER_CONFIG_FILE, 
            KMS_MAX_FILE_NAME );

   strncpy( sCACertificateFile, sFullProfileDir, KMS_MAX_FILE_NAME ); 
   strncat( sCACertificateFile, CA_CERTIFICATE_FILE, KMS_MAX_FILE_NAME );

#ifndef KMSUSERPKCS12
   strncpy( sClientKeyFile, sFullProfileDir, KMS_MAX_FILE_NAME ); 
   strncat( sClientKeyFile, CLIENT_KEY_FILE, KMS_MAX_FILE_NAME );
#endif

   myFILE* pfFile = fopen( sConfigFile, "rb" );

   if ( pfFile != NULL )
   {
      fclose(pfFile);
      if ( my_unlink(sConfigFile) )
         bSuccess = false;
   }

   pfFile = fopen( sClusterInformationFile, "rb" );

   if ( pfFile != NULL )
   {
      fclose(pfFile);
      if ( my_unlink(sClusterInformationFile) )
         bSuccess = false;
   }

   pfFile = fopen( sCACertificateFile, "rb" );

   if ( pfFile != NULL )
   {
      fclose(pfFile);
      if ( my_unlink(sCACertificateFile) )
         bSuccess = false;
   }

#ifndef KMSUSERPKCS12
   pfFile = fopen( sClientKeyFile, "rb" );

   if ( pfFile != NULL )
   {
      fclose(pfFile);
      if ( my_unlink(sClientKeyFile) )
         bSuccess = false;
   }
#endif

#ifdef KMSUSERPKCS12
   strncpy( sClientP12File, sFullProfileDir, KMS_MAX_FILE_NAME );
   strncat( sClientP12File, CLIENT_PK12_FILE, KMS_MAX_FILE_NAME );

   /* Just unlink, no need to open/close first. */
   if ( my_unlink(sClientP12File) )
         bSuccess = false;
#endif

   pfFile = fopen( sFullProfileDir, "rb" );

   if ( pfFile != NULL )
   {
      fclose(pfFile);
      if ( my_rmdir(sFullProfileDir) )
         bSuccess = false;
   }

   return bSuccess;
}




/**
 * K_soap_ssl_client_context
 * Parse client context and send to soap, either using a soap call
 *  for openSSL or user implemented call for Treck SSL
 * 
 * @param i_pProfile     - pointer to KMSClientProfile
 * @param password	 - pointer to hex hashed passphrase.
 * @param io_pSoap       - pointer to soap structure
 * @param i_iFlags       - input flags (CLIENT or SERVER auth)
 *
 * @returns 0=success, non-zero=fail
 */
int K_soap_ssl_client_context
(  KMSClientProfile* const   i_pProfile,  // input KMSClientProfile
   char *		     i_pPassword, // input password
   struct soap *             io_pSoap,    // i/o soap profile
   unsigned short            i_iFlags )   // input flags
{
   FATAL_ASSERT( i_pProfile );
   FATAL_ASSERT( io_pSoap );

#if defined(DEBUG_TRACE) && defined(METAWARE)
   ECPT_TRACE_ENTRY   *trace = NULL;  
   ECPT_TRACE( trace, K_soap_ssl_client_context ) ;
#endif

   
   char sCACertificateFile[KMS_MAX_FILE_NAME+1];
   char sClientKeyFile[KMS_MAX_FILE_NAME+1];
   char *pCACertificateFile = NULL;
   char *pClientKeyFile = NULL;
   
   BuildFullProfilePath( sCACertificateFile,            // out
                         g_sWorkingDirectory,           // out
                         i_pProfile->m_wsProfileName ); // in

   strncpy(sClientKeyFile, sCACertificateFile, KMS_MAX_FILE_NAME);

   strncat( sCACertificateFile,   // path
            CA_CERTIFICATE_FILE,  // name
            KMS_MAX_FILE_NAME );

#ifndef KMSUSERPKCS12
   pCACertificateFile = sCACertificateFile;
#endif

   switch ( i_iFlags )
   {
      case SOAP_SSL_REQUIRE_CLIENT_AUTHENTICATION:
      {
#ifndef KMSUSERPKCS12
         strncat( sClientKeyFile,      // path
                  CLIENT_KEY_FILE,     // name
                  KMS_MAX_FILE_NAME );

	 pClientKeyFile = sClientKeyFile;
#endif

         // this sends the following to the SSL Layer
#ifdef METAWARE 
         return K_ssl_client_context(
            io_pSoap,                           // i/o
            i_iFlags,                           // flags
            sClientKeyFile,                     // keyfile - client cert and private key
            i_pPassword,			// password
            sCACertificateFile,                 // cafile - CA certificate
            NULL,                               // capath
            NULL );                             // randfile
#else
#ifdef KMSUSERPKCS12
	 // Pre-configure the SSL CTX for PKCS#11 clients
	 io_pSoap->ctx = (SSL_CTX *)K_SetupSSLCtx(sCACertificateFile,
	     i_pProfile->p12_cert, i_pProfile->p12_key, i_iFlags);
	 if (io_pSoap->ctx == NULL) {
		 return (1);
	 }
#endif
         return soap_ssl_client_context(
            io_pSoap,                           // i/o
#ifndef SOAP_SSL_SKIP_HOST_CHECK
            i_iFlags,                           // flags
#else
            i_iFlags | SOAP_SSL_SKIP_HOST_CHECK, // flags
#endif
            pClientKeyFile,                     // keyfile - client cert and private key
            i_pPassword,			// password
            pCACertificateFile,                 // cafile - CA certificate
            NULL,                               // capath
            NULL );                             // randfile
#endif
      }
      case SOAP_SSL_REQUIRE_SERVER_AUTHENTICATION:
      {
#ifdef METAWARE
         return K_ssl_client_context(
            io_pSoap,                           // i/o
            i_iFlags,                           // flags
            NULL,                               // keyfile
            NULL,                               // password
            sCACertificateFile,                 // cafile
            NULL,                               // capath
            NULL );                             // randfile
#else
         return soap_ssl_client_context(
            io_pSoap,                           // i/o
#ifndef SOAP_SSL_SKIP_HOST_CHECK
            i_iFlags,                           // flags
#else
            i_iFlags | SOAP_SSL_SKIP_HOST_CHECK, // flags
#endif
            NULL,                               // keyfile
            NULL,                               // password
            sCACertificateFile,                 // cafile
            NULL,                               // capath
            NULL );                             // randfile
#endif         
      }
      default:
         // unauthenticated sessions are not supported
         return 1;
   }
}
