/*
  File: cmi-debug.h

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
        All rights reserved.
*/

#ifndef CMI_DEBUG_H
#define CMI_DEBUG_H
#include "sshenum.h"

extern const SshKeywordStruct ssh_cm_edb_data_types[];
extern const SshKeywordStruct ssh_cm_edb_key_types[];

int
ssh_cm_render_crl(char *buf, int len, int precision, void *datum);
int
ssh_cm_render_certificate(char *buf, int len, int precision, void *datum);
int
ssh_cm_render_state(char *buf, int len, int precision, void *datum);
int
ssh_cm_render_mp(char *buf, int len, int precision, void *datum);

int
ssh_cm_edb_distinguisher_render(char *buf, int buf_size, int precision,
                                void *datum);

#endif /* CMI_DEBUG_H */
/* eof */
