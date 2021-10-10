/*

gafpclient.c

Author: Timo J. Rinne <tri@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
All rights reserved

Lookup an error message with error code.

*/

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshgafp.h"

/* Return readable error string according to error code.
   Always returns a 0-terminated string.  Never fails. */
const char *ssh_gafp_error_string(SshGafpError error)
{
  switch (error)
    {
    case SSH_GAFP_ERROR_OK:                
      return "Operation completed successfully";
    case SSH_GAFP_ERROR_TIMEOUT:           
      return "Operation timed out";
    case SSH_GAFP_ERROR_KEY_NOT_FOUND:     
      return "Private key is not available";
    case SSH_GAFP_ERROR_DECRYPT_FAILED:    
      return "Decryption failed";
    case SSH_GAFP_ERROR_SIZE_ERROR:        
      return "Data size is inappropriate";
    case SSH_GAFP_ERROR_KEY_NOT_SUITABLE:  
      return "Key is not suitable for request";
    case SSH_GAFP_ERROR_DENIED:            
      return "Administratively prohibited";
    case SSH_GAFP_ERROR_FAILURE:           
      return "Unspecific error";
    case SSH_GAFP_ERROR_UNSUPPORTED_OP:    
      return "Operation not supported";
    case SSH_GAFP_ERROR_PROTOCOL:             
      return "Protocol error";
    case SSH_GAFP_ERROR_OPERATION_ACTIVE:    
      return "Protocol error (operation active)";
    case SSH_GAFP_ERROR_OPERATION_NOT_FOUND:
      return "Protocol error (operation not found)";
    case SSH_GAFP_ERROR_EOF:
      return "EOF received";
    case SSH_GAFP_ERROR_SEQUENCE_NUMBER:
      return "Out-of-sequence fragment received";
    case SSH_GAFP_ERROR_SIZE_DENIED:
      return "Data size limit exceeded";
    }
  /* This is not in default branch of switch, because on new
     error codes, we want compiler to warn about missing branch. */
  return "Unknown error";
}

/* eof (gafperror.c) */
