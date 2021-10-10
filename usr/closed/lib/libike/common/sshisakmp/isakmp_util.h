/*
 *
 * Author: Tero Kivinen <kivinen@iki.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 */
/*
 *        Program: sshisakmp
 *        $Source: /project/cvs/src/ipsec/lib/sshisakmp/isakmp_util.h,v $
 *        $Author: irwin $
 *
 *        Creation          : 00:55 Oct  4 1999 kivinen
 *        Last Modification : 17:47 Feb 27 2002 kivinen
 *        Last check in     : $Date: 2003/11/28 20:00:48 $
 *        Revision number   : $Revision: 1.1.6.1 $
 *        State             : $State: Exp $
 *        Version           : 1.14
 *        
 *
 *        Description       : Isakmp library utility functions
 *
 *
 *        $Log: isakmp_util.h,v $ *        
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
 *        
 *        
 *        
 *        
 *        
 *        $EndLog$
 */

#ifndef ISAKMP_UTIL_H
#define ISAKMP_UTIL_H

#include "isakmp.h"

typedef struct SshIkeSAAttributeListRec *SshIkeSAAttributeList;

/* Allocate SA data attribute list. This list can be used to create
   sa_attributes entry in the SshIkePayloadT structure. Allocate new list using
   this function, and then add entries to it by calling
   ssh_ike_data_attribute_list_add* functions. After all attributes has been
   added call function ssh_ike_data_attribute_list_get to get the final list
   out (which can be freed by just simple ssh_free, it will free both data
   structure, and the data). Then free the list itsef by calling
   ssh_ike_data_attribute_list_free. */
SshIkeSAAttributeList ssh_ike_data_attribute_list_allocate(void);

/* Add buffer entry to the SA data attribute list. This copies the data to the
   list. */
void ssh_ike_data_attribute_list_add(SshIkeSAAttributeList list,
                                     SshUInt16 type,
                                     unsigned char *buffer,
                                     size_t length);

/* Add basic interger (16 bit) to the SA data attribute list */
void ssh_ike_data_attribute_list_add_basic(SshIkeSAAttributeList list,
                                           SshUInt16 type,
                                           SshUInt16 number);

/* Add number to the SA data attribute list */
void ssh_ike_data_attribute_list_add_int(SshIkeSAAttributeList list,
                                         SshUInt16 type,
                                         SshUInt64 number);

/* Add mp integer to the SA data attribute list */
void ssh_ike_data_attribute_list_add_mpint(SshIkeSAAttributeList list,
                                           SshUInt16 type,
                                           SshMPIntC number);

/* Get SA data attribute data structure out from the SA data attribute list */
SshIkeDataAttribute ssh_ike_data_attribute_list_get(SshIkeSAAttributeList list,
                                                    int *number_of_attributes);

/* Free SA data attribute list */
void ssh_ike_data_attribute_list_free(SshIkeSAAttributeList list);

#endif /* ISAKMP_UTIL_H */
