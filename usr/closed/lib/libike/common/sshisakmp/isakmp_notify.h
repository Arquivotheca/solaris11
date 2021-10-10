/*
 * Author: Tero Kivinen <kivinen@iki.fi>
 *
*  Copyright:
*          Copyright (c) 2002, 2003 SFNT Finland Oy.
 */
/*
 *        Program: sshisakmp
 *        $Source: /project/cvs/src/ipsec/lib/sshisakmp/isakmp_notify.h,v $
 *        $Author: irwin $
 *
 *        Creation          : 22:54 Sep 12 2000 kivinen
 *        Last Modification : 04:02 Sep 13 2000 kivinen
 *        Last check in     : $Date: 2003/11/28 20:00:47 $
 *        Revision number   : $Revision: 1.1.6.1 $
 *        State             : $State: Exp $
 *        Version           : 1.11
 *        
 *
 *        Description       : Notify message definitions
 *
 *
 *        $Log: isakmp_notify.h,v $ *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        $EndLog$
 */

#ifndef ISAKMP_NOTIFY_H
#define ISAKMP_NOTIFY_H

typedef enum {
  SSH_IKE_NOTIFY_CLASSES_TYPE_OF_OFFENDING_PAYLOAD = 3,
  SSH_IKE_NOTIFY_CLASSES_OFFENDING_PAYLOAD_DATA = 4,
  SSH_IKE_NOTIFY_CLASSES_ERROR_POSITION_OFFSET = 5,
  SSH_IKE_NOTIFY_CLASSES_ERROR_TEXT = 6,
  SSH_IKE_NOTIFY_CLASSES_ERROR_TEXT_LANGUAGE = 7,
  SSH_IKE_NOTIFY_CLASSES_MESSAGE_ID = 8,
  SSH_IKE_NOTIFY_CLASSES_EXCHANGE_TYPE = 9,
  SSH_IKE_NOTIFY_CLASSES_INVALID_FLAG_BITS = 10,
  SSH_IKE_NOTIFY_CLASSES_SUGGESTED_PROPOSAL = 11,
  SSH_IKE_NOTIFY_CLASSES_VERSION = 12
} SshIkeNotifyAttributeClasses;

typedef enum {
  SSH_IKE_NOTIFY_VALUES_VERSION_1 = 1
} SshIkeNotifyValuesNotificationAttributeVersion;

#endif /* ISAKMP_NOTIFY_H */
