/*
  ssh-pk-prv-gen.c 

  Author: Vesa Suontama <vsuontam@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                     All rights reserved

  Created Thu Oct 31 11:00:33 2002. 

  Kludge: This file includes ssh-pk-prv.c two times. First when
  GENERATE is defined and then when GENERATE is undefined.

  The intention of this tweaking is to avoid linking of the heavy
  private key generation functions. If we just use keys generated by
  others, the linker can drop the generate functions, because the
  ssh_pk_provider_register is not called. 
  
 */

#define GENERATE
#include "ssh-pk-prv.c"
