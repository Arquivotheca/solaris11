/*

Authors: Kenneth Oksanen <cessu@iki.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
All rights reserved.

Various kinds of assembler idioms specific to certain CPUs.

*/


#ifndef SSHASMIDIOMS_H
#define SSHASMIDIOMS_H


#ifdef i386
/* x86 (nor Pentia) have and with complement. */
#undef SSH_HAVE_AND_WITH_COMPLEMENT
#endif /* i386 */


#ifdef __alpha
#define SSH_HAVE_AND_WITH_COMPLEMENT  1
#endif /* __alpha */


#ifdef PPC
#ifndef VXWORKS
/* compiler/assembler supplied with vxworks development environment
   don't believe there is such an instruction as "rotlwi".
   XXX proper compiler version check needed */
#if __GNUC__ < 3
/* Older versions of gcc do not recognize the opportunities for rotate insns.
   Therefore do it manually.  */
#define SSH_ROL32_CONST(w,shmt)                         \
  ({                                                    \
    register SshUInt32 r;                               \
    SSH_HEAVY_ASSERT((shmt) < 32);                      \
    SSH_HEAVY_ASSERT((shmt) >= 0);                      \
    asm("rotlwi %0,%1," #shmt : "=r" (r) : "r" (w));    \
    r;                                                  \
  })
#define SSH_ROR32_CONST(w,shmt)                         \
  ({                                                    \
    register SshUInt32 r;                               \
    SSH_HEAVY_ASSERT((shmt) < 32);                      \
    SSH_HEAVY_ASSERT((shmt) >= 0);                      \
    asm("rotrwi %0,%1," #shmt : "=r" (r) : "r" (w));    \
    r;                                                  \
  })
#endif /* __GNUC__ < 3 */
#endif /* VXWORKS */

#define SSH_HAVE_AND_WITH_COMPLEMENT  1
#endif /* PPC */


#ifdef ARM
#define SSH_HAVE_AND_WITH_COMPLEMENT  1
#endif /* ARM */


/* Every construct above has a default fully ANSI C -compliant
   implementation. */
#ifndef SSH_ROL32_CONST
#define SSH_ROL32_CONST(w,shmt)                 \
  (SSH_HEAVY_ASSERT((shmt) < 32),               \
   SSH_HEAVY_ASSERT((shmt) >= 0),               \
   ((w) << (shmt)) | ((w) >> (32 - (shmt))))
#endif

#ifndef SSH_ROR32_CONST
#define SSH_ROR32_CONST(w,shmt)                 \
  (SSH_HEAVY_ASSERT((shmt) < 32),               \
   SSH_HEAVY_ASSERT((shmt) >= 0),               \
   ((w) >> (shmt)) | ((w) << (32 - (shmt))))
#endif


/* XXX This is here temporarily - its purpose is to allow recompiling
   places that refer to macros SSH_{INSN,DATA}_CACHE_SIZE witout the
   need to rerun prepare and configure.  It will go away within weeks 
   // Cessu */
#ifndef SSH_DATA_CACHE_SIZE
#define SSH_DATA_CACHE_SIZE  256
#endif
#ifndef SSH_INSN_CACHE_SIZE
#define SSH_INSN_CACHE_SIZE  256
#endif


#endif /* SSHASMIDIOMS_H */
