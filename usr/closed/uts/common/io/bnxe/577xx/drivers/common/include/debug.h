
#ifndef _DEBUG_H_ 
#define _DEBUG_H_ 
/* TBA - complete debug.h file content */

#ifdef __LINUX
#include <linux/types.h>
#include <linux/kernel.h>
#elif defined(USER_LINUX)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#elif defined(__SunOS)
#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#undef u /* see solaris/src/bnxe.h for explanation */
#endif
#include "bcmtype.h"


#define DBG_ERR_STR "(!)"
#define DBG_WRN_STR "(?)"

// convert __FILE__ to wchar_t - __WFILE__
#define WIDEN2(x) L ## x 
#define WIDEN(x) WIDEN2(x) 
#define __WFILE__ WIDEN(__FILE__) 

// This is a substitution for __FILE__ in order to get rid of entire file path
#if !(defined _VBD_CMD_)
#define __FILE_STRIPPED__  strrchr(__FILE__, '\\')   ?   strrchr(__FILE__, '\\')   + 1 : __FILE__
#define __WFILE_STRIPPED__ wcsrchr(__WFILE__, L'\\') ?   wcsrchr(__WFILE__, L'\\') + 1 : __WFILE__

#else // VBD_CMD

#define __FILE_STRIPPED__   __FILE__
#define __WFILE_STRIPPED__  __WFILE__

// Currently only VBD_CMD support it but maybe other USER_MODE like WineDiag should support it as well

void printf_color( unsigned long color, const char *format, ... );

#define printf_ex printf_color

#endif // !(_VBD_CMD_)

#define MAX_STR_DBG_LOGGER_NAME 100

// logger callback prototype
typedef u8_t (*debug_logger_cb_t)( void *context, long msg_code, long gui_code, u8_t b_raise_gui, u32_t string_cnt, u32_t data_cnt, ...) ;

// logger functins prototype
u8_t debug_deregister_logger          ( IN const void* context ) ;
u8_t debug_register_logger            ( debug_logger_cb_t debug_logger_cb,
                                        long              msg_code,
                                        long              gui_code,
                                        u8_t              b_raise_gui,
                                        void*             context,
                                        u32_t             times_to_log ) ;
u8_t debug_execute_loggers            ( unsigned short* wsz_file, unsigned long line, unsigned short* wsz_cond ) ;
void debug_register_logger_device_name( unsigned short* wstr_device_desc, unsigned long size ) ;


// max number of loggers
#define MAX_DEBUG_LOGGER_CNT 3

typedef struct _dbg_logger_t
{
    debug_logger_cb_t debug_logger_cb ; // callback
    void*             context ;         // unique context
    u32_t             msg_code ;        // msg_code
    u32_t             gui_code ;        // gui_code
    u8_t              b_raise_gui ;     // do raise gui message
    u32_t             times_to_log ;    // number of times to log
} dbg_logger_t ;

extern dbg_logger_t g_dbg_logger_arr[MAX_DEBUG_LOGGER_CNT] ;

/* Debug Break Filters */
extern u32_t g_dbg_flags;
#define MEMORY_ALLOCATION_FAILURE       0x1
#define FW_SANITY_UPLOAD_CHECK          0x2
#define UNDER_TEST                      0x4
#define INVALID_MESSAGE_ID              0x8
#define ABORTIVE_DISCONNECT_DURING_IND  0x10
#define DBG_BREAK_ON(_cond)  (GET_FLAGS(g_dbg_flags, _cond) != 0)

/* Storage defined in the module main c file */
extern u8_t     dbg_trace_level;
extern u32_t    dbg_code_path;
extern char     dbg_trace_module_name[];

/* code paths */
#define CP_INIT                 0x00000100    /* Initialization */
#define CP_NVM                  0x00000200    /* nvram          */
#define CP_L2_SP                0x00001000    /* L2 Slow Path   */
#define CP_L2_SEND              0x00002000    /* L2 Transmit    */
#define CP_L2_RECV              0x00004000    /* L2 Receive     */
#define CP_L2_INTERRUPT         0x00008000    /* L2 Interrupt   */
#define CP_L2                   0x0000f000    /* L2 all         */
#define CP_L4_SP                0x00010000    /* L4 Slow Path   */
#define CP_L4_SEND              0x00020000    /* L4 Transmit    */
#define CP_L4_RECV              0x00040000    /* L4 Receive     */
#define CP_L4_INTERRUPT         0x00080000    /* L4 Interrupt   */
#define CP_L4                   0x000f0000    /* L4 all         */
#define CP_L5_SP                0x00100000    /* L5 Slow Path   */
#define CP_L5_SEND              0x00200000    /* L5 Transmit    */
#define CP_L5_RECV              0x00400000    /* L5 Receive     */
#define CP_L5                   0x00f00000    /* L5 all         */
#define CP_VF                   0x01000000    /* VF all         */
#define CP_EQ                   0x02000000    /* Event Queue    */
#define CP_STAT                 0x04000000    /* Statistics     */
#define CP_ER                   0x08000000    /* Event Queue    */
#define CP_OMGR                 0x10000000    /* OOO Manager    */
#define CP_DIAG                 0x40000000    /* Diagnostics    */
#define CP_MISC                 0x80000000    /* Miscellaneous  */


/* more code paths can be added
 * bits that are still not defined can be privately used in each module */
#define CP_ALL                  0xffffff00
#define CP_MASK                 0xffffff00

/* Message levels. */
typedef enum 
{
    LV_VERBOSE = 0x04,
    LV_INFORM  = 0x03,
    LV_WARN    = 0x02,
    LV_FATAL   = 0x01
} msg_level_t;

#define LV_MASK                 0xff

/* Code path and messsage level combined.  These are the first argument
 * of the DbgMessage macro. */

#define VERBOSEi                (CP_INIT | LV_VERBOSE)
#define INFORMi                 (CP_INIT | LV_INFORM)
#define WARNi                   (CP_INIT | LV_WARN)

#define VERBOSEnv               (CP_NVM | LV_VERBOSE)
#define INFORMnv                (CP_NVM | LV_INFORM)
#define WARNnv                  (CP_NVM | LV_WARN)

#define VERBOSEl2sp             (CP_L2_SP | LV_VERBOSE)
#define INFORMl2sp              (CP_L2_SP | LV_INFORM)
#define WARNl2sp                (CP_L2_SP | LV_WARN)

#define VERBOSEl2tx             (CP_L2_SEND | LV_VERBOSE)
#define INFORMl2tx              (CP_L2_SEND | LV_INFORM)
#define WARNl2tx                (CP_L2_SEND | LV_WARN)

#define VERBOSEl2rx             (CP_L2_RECV | LV_VERBOSE)
#define INFORMl2rx              (CP_L2_RECV | LV_INFORM)
#define WARNl2rx                (CP_L2_RECV | LV_WARN)

#define VERBOSEl2int            (CP_L2_INTERRUPT | LV_VERBOSE)
#define INFORMl2int             (CP_L2_INTERRUPT | LV_INFORM)
#define WARNl2int               (CP_L2_INTERRUPT | LV_WARN)

#define VERBOSEl2               (CP_L2 | LV_VERBOSE)
#define INFORMl2                (CP_L2 | LV_INFORM)
#define WARNl2                  (CP_L2 | LV_WARN)

#define VERBOSEl4sp             (CP_L4_SP | LV_VERBOSE)
#define INFORMl4sp              (CP_L4_SP | LV_INFORM)
#define WARNl4sp                (CP_L4_SP | LV_WARN)

#define VERBOSEl4tx             (CP_L4_SEND | LV_VERBOSE)
#define INFORMl4tx              (CP_L4_SEND | LV_INFORM)
#define WARNl4tx                (CP_L4_SEND | LV_WARN)

#define VERBOSEl4rx             (CP_L4_RECV | LV_VERBOSE)
#define INFORMl4rx              (CP_L4_RECV | LV_INFORM)
#define WARNl4rx                (CP_L4_RECV | LV_WARN)

#define VERBOSEl4fp             (CP_L4_RECV | CP_L4_SEND | LV_VERBOSE)
#define INFORMl4fp              (CP_L4_RECV | CP_L4_SEND | LV_INFORM)
#define WARNl4fp                (CP_L4_RECV | CP_L4_SEND | LV_WARN)

#define VERBOSEl4int            (CP_L4_INTERRUPT | LV_VERBOSE)
#define INFORMl4int             (CP_L4_INTERRUPT | LV_INFORM)
#define WARNl4int               (CP_L4_INTERRUPT | LV_WARN)

#define VERBOSEl4               (CP_L4 | LV_VERBOSE)
#define INFORMl4                (CP_L4 | LV_INFORM)
#define WARNl4                  (CP_L4 | LV_WARN)

#define VERBOSEl5sp             (CP_L5_SP | LV_VERBOSE)
#define INFORMl5sp              (CP_L5_SP | LV_INFORM)
#define WARNl5sp                (CP_L5_SP | LV_WARN)

#define VERBOSEl5tx             (CP_L5_SEND | LV_VERBOSE)
#define INFORMl5tx              (CP_L5_SEND | LV_INFORM)
#define WARNl5tx                (CP_L5_SEND | LV_WARN)

#define VERBOSEl5rx             (CP_L5_RECV | LV_VERBOSE)
#define INFORMl5rx              (CP_L5_RECV | LV_INFORM)
#define WARNl5rx                (CP_L5_RECV | LV_WARN)

#define VERBOSEl5               (CP_L5 | LV_VERBOSE)
#define INFORMl5                (CP_L5 | LV_INFORM)
#define WARNl5                  (CP_L5 | LV_WARN)

#define VERBOSEvf               (CP_VF | LV_VERBOSE)
#define INFORMvf                (CP_VF | LV_INFORM)
#define WARNvf                  (CP_VF | LV_WARN)
#define FATALvf                 (CP_VF | LV_FATAL)

#define VERBOSEmi               (CP_MISC | LV_VERBOSE)
#define INFORMmi                (CP_MISC | LV_INFORM)
#define WARNmi                  (CP_MISC | LV_WARN)

#define VERBOSEeq               (CP_EQ | LV_VERBOSE)
#define INFORMeq                (CP_EQ | LV_INFORM)
#define WARNeq                  (CP_EQ | LV_WARN)

#define VERBOSOmgr              (CP_OMGR | LV_VERBOSE)
#define INFOROmgr               (CP_OMGR | LV_INFORM)
#define WAROmgr                 (CP_OMGR | LV_WARN)

#define VERBOSEstat             (CP_STAT | LV_VERBOSE)
#define INFORMstat              (CP_STAT | LV_INFORM)
#define WARNstat                (CP_STAT | LV_WARN)

/* Error Recovery */
#define VERBOSEer               (CP_ER | LV_VERBOSE)
#define INFORMer                (CP_ER | LV_INFORM)
#define WARNer                  (CP_ER | LV_WARN)

#define FATAL                   (CP_ALL | LV_FATAL)
/* This is an existing debug macro in 2.6.27 Linux kernel */
#ifdef WARN
#undef WARN
#endif
#define WARN                    (CP_ALL | LV_WARN)
#define INFORM                  (CP_ALL | LV_INFORM)
#define VERBOSE                 (CP_ALL | LV_VERBOSE)


#if DBG

/* These constants control the output of messages.
 * Set your debug message output level and code path here. */
#ifndef DBG_MSG_CP
#define DBG_MSG_CP              CP_ALL      /* Where to output messages. */
#endif

#ifndef DBG_MSG_LV
#ifdef _VBD_CMD_
#define DBG_MSG_LV              dbg_trace_level
#else
//change this to higher level than FATAL to open debug messages.
#define DBG_MSG_LV              LV_FATAL  /* Level of message output. */
#endif
#endif

//STATIC is now not empty define
#ifndef STATIC
#define STATIC static 
#endif
//#define DbgBreak(_c)

/* This is the original implementation where both code path and debug level can only be defined 
   once at compile time */
#if 0 
#define CODE_PATH(_m)           ((_m) & DBG_MSG_CP)
#define MSG_LEVEL(_m)           ((_m) & LV_MASK)
#define LOG_MSG(_m)             (CODE_PATH(_m) && \
                                MSG_LEVEL(_m) <= DBG_MSG_LV)
#endif

#define CODE_PATH(_m)           ((_m) & dbg_code_path)
#define MSG_LEVEL(_m)           ((_m) & LV_MASK)
#define LOG_MSG(_m)             (CODE_PATH(_m) && \
                                MSG_LEVEL(_m) <= dbg_trace_level)



void mm_print_bdf(int level, void* dev);

/* per OS methods */
#if defined(UEFI)
#include <stdio.h>
#include <assert.h>
#include <string.h>
void console_cleanup(void);
void
console_init(
    void
    );
u32_t
console_print(
    void * console_buffer_p
    );
void __cdecl
debug_msgx(
    unsigned long level,
    char *msg,
    ...);
#define debug_message(l, fmt) debug_msgx(l, fmt);
#define debug_message1(l, fmt, d1) debug_msgx(l, fmt, d1);
#define debug_message2(l, fmt, d1, d2) debug_msgx(l, fmt, d1, d2);
#define debug_message3(l, fmt, d1, d2, d3) debug_msgx(l, fmt, d1, d2, d3);
#define debug_message4(l, fmt, d1, d2, d3, d4) debug_msgx(l, fmt, d1, d2, d3, d4);
#define debug_message5(l, fmt, d1, d2, d3, d4, d5) debug_msgx(l, fmt, d1, d2, d3, d4, d5);
#define debug_message6(l, fmt, d1, d2, d3, d4, d5, d6) debug_msgx(l, fmt, d1, d2, d3, d4, d5, d6);
#define debug_message7(l, fmt, d1, d2, d3, d4, d5, d6, d7) debug_msgx(l, fmt, d1, d2, d3, d4, d5, d6, d7);
#define MessageHdr(_dev, _level) \
    do { \
	    debug_message3((_level), "TrLv<%d>, %s (%4d): ", ((_level) & LV_MASK), __FILE_STRIPPED__, __LINE__); \
	    mm_print_bdf(_level, (void*)(_dev)); \
    } while (0)
#define DbgMessage(_c, _m, _s)
#define DbgMessage1(_c, _m, _s, _d1)
#define DbgMessage2(_c, _m, _s, _d1, _d2)
#define DbgMessage3(_c, _m, _s, _d1, _d2, _d3)
#define DbgMessage4(_c, _m, _s, _d1, _d2, _d3, _d4)
#define DbgMessage5(_c, _m, _s, _d1, _d2, _d3, _d4, _d5)
#define DbgMessage6(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6)
#define DbgMessage7(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6, _d7)
#define debug_break() do { \
    debug_message(FATAL, "DEBUG BREAK!\n"); \
    console_print(NULL); \
    fflush(stdout); \
    assert(0); \
} while(0)
#define DbgBreak()            debug_break()
#define DbgBreakIf(_cond) do {\
    if(_cond) \
    { \
        MessageHdr(NULL,LV_FATAL); \
        debug_message(FATAL, "Condition failed: if("#_cond##")\n"); \
        debug_break(); \
    } \
} while(0)
#define DbgBreakMsg(_s) do { \
    MessageHdr(NULL,LV_FATAL); \
    debug_message(FATAL, "Debug Break Message: " _s); \
    debug_break(); \
} while(0)
#define DbgBreakFastPath()        DbgBreak()
#define DbgBreakIfFastPath(_cond) DbgBreakIf(_cond)
#define DbgBreakMsgFastPath(_s)   DbgBreakMsg(_s)
#define dbg_out(_c, _m, _s, _d1) debug_msgx((WARN), "TrLv<%d>, %s (%4d): %p"##_s" %s\n", ((WARN) & LV_MASK), __FILE_STRIPPED__, __LINE__, _c, _d1 )
#elif defined(DOS)
#include <stdio.h>
#include <assert.h>
#include <string.h>

void console_cleanup(void);

void 
console_init( 
    void 
    );

u32_t
console_print( 
    void * console_buffer_p
    );

void __cdecl
debug_msgx(
    unsigned long level,
    char *msg,
    ...);
// DOS
#define debug_message(l, fmt) debug_msgx(l, fmt); 
#define debug_message1(l, fmt, d1) debug_msgx(l, fmt, d1); 
#define debug_message2(l, fmt, d1, d2) debug_msgx(l, fmt, d1, d2); 
#define debug_message3(l, fmt, d1, d2, d3) debug_msgx(l, fmt, d1, d2, d3); 
#define debug_message4(l, fmt, d1, d2, d3, d4) debug_msgx(l, fmt, d1, d2, d3, d4); 
#define debug_message5(l, fmt, d1, d2, d3, d4, d5) debug_msgx(l, fmt, d1, d2, d3, d4, d5); 
#define debug_message6(l, fmt, d1, d2, d3, d4, d5, d6) debug_msgx(l, fmt, d1, d2, d3, d4, d5, d6); 
#define debug_message7(l, fmt, d1, d2, d3, d4, d5, d6, d7) debug_msgx(l, fmt, d1, d2, d3, d4, d5, d6, d7); 


// TODO: need to relate to dbg_trace_module_name
/*
#define MessageHdr(_dev, _level) \
    debug_message5("%s, TrLv<%d>, %s (%d): %p ", dbg_trace_module_name, (_level), __FILE_STRIPPED__, __LINE__, _dev )
*/

// DOS
#define MessageHdr(_dev, _level) \
    	do { \
	    debug_message3((_level), "TrLv<%d>, %s (%4d): ", ((_level) & LV_MASK), __FILE_STRIPPED__, __LINE__); \
	    mm_print_bdf(_level, (void*)(_dev)); \
    } while (0)

#define DbgMessage(_c, _m, _s)                                            \
        MessageHdr((_c), (_m));                                                 \
        debug_message((_m), _s);                                                    

#define DbgMessage1(_c, _m, _s, _d1)                                      \
        MessageHdr((_c), (_m));                                                 \
        debug_message1((_m), _s, _d1);                                          

#define DbgMessage2(_c, _m, _s, _d1, _d2)                                 \
        MessageHdr((_c), (_m));                                                 \
        debug_message2((_m), _s, _d1, _d2);                                     

#define DbgMessage3(_c, _m, _s, _d1, _d2, _d3)                            \
        MessageHdr((_c), (_m));                                                 \
        debug_message3((_m), _s, _d1, _d2, _d3);                                    

#define DbgMessage4(_c, _m, _s, _d1, _d2, _d3, _d4)                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message4((_m), _s, _d1, _d2, _d3, _d4);                             

#define DbgMessage5(_c, _m, _s, _d1, _d2, _d3, _d4, _d5)                  \
        MessageHdr((_c), (_m));                                                 \
        debug_message5((_m), _s, _d1, _d2, _d3, _d4, _d5);                        

#define DbgMessage6(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6)             \
        MessageHdr((_c), (_m));                                                 \
        debug_message6((_m), _s, _d1,_d2,_d3,_d4,_d5,_d6);                      

#define DbgMessage7(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6, _d7)             \
        MessageHdr((_c), (_m));                                                 \
        debug_message7((_m), _s, _d1,_d2,_d3,_d4,_d5,_d6,_d7);                      

// DOS
#define debug_break() do { \
    debug_message(FATAL, "DEBUG BREAK!\n"); \
    console_print(NULL); \
    fflush(stdout); \
    assert(0); \
} while(0)

// DOS
#define DbgBreak()            debug_break()
// DOS
#define DbgBreakIf(_cond) do {\
    if(_cond) \
    { \
        MessageHdr(NULL,LV_FATAL); \
        debug_message(FATAL, "Condition failed: if("#_cond##")\n"); \
        debug_break(); \
    } \
} while(0)
// DOS
#define DbgBreakMsg(_s) do { \
    MessageHdr(NULL,LV_FATAL); \
    debug_message(FATAL, "Debug Break Message: " _s); \
    debug_break(); \
} while(0)

// DOS
#define DbgBreakFastPath()        DbgBreak()
#define DbgBreakIfFastPath(_cond) DbgBreakIf(_cond)
#define DbgBreakMsgFastPath(_s)   DbgBreakMsg(_s)


// DOS
#define dbg_out(_c, _m, _s, _d1) debug_msgx((WARN), "TrLv<%d>, %s (%4d): %p"##_s" %s\n", ((WARN) & LV_MASK), __FILE_STRIPPED__, __LINE__, _c, _d1 )

#elif defined(__USER_MODE_DEBUG) 
#include <stdio.h>
#include <assert.h>
#include <string.h>
#define debug_message(fmt) printf(fmt); fflush(stdout);
#define debug_message1(fmt, d1) printf(fmt, d1); fflush(stdout);
#define debug_message2(fmt, d1, d2) printf(fmt, d1, d2); fflush(stdout);
#define debug_message3(fmt, d1, d2, d3) printf(fmt, d1, d2, d3); fflush(stdout);
#define debug_message4(fmt, d1, d2, d3, d4) printf(fmt, d1, d2, d3, d4); fflush(stdout);
#define debug_message5(fmt, d1, d2, d3, d4, d5) printf(fmt, d1, d2, d3, d4, d5); fflush(stdout);
#define debug_message6(fmt, d1, d2, d3, d4, d5, d6) printf(fmt, d1, d2, d3, d4, d5, d6); fflush(stdout);
#define debug_message7(fmt, d1, d2, d3, d4, d5, d6, d7) printf(fmt, d1, d2, d3, d4, d5, d6, d7); fflush(stdout);

// TODO: need to relate to dbg_trace_module_name
/*
#define MessageHdr(_dev, _level) \
    debug_message5("%s, TrLv<%d>, %s (%d): %p ", dbg_trace_module_name, (_level), __FILE_STRIPPED__, __LINE__, _dev )
*/
// __USER_MODE_DEBUG
#define MessageHdr(_dev, _level) \
    debug_message4("TrLv<%d>, %-12s (%4d): %p ", ((_level) & LV_MASK), __FILE_STRIPPED__, __LINE__, _dev )

#define DbgMessage(_c, _m, _s)                                            \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message(_s);                                                 \
    }
#define DbgMessage1(_c, _m, _s, _d1)                                      \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message1(_s, _d1);                                            \
    }
#define DbgMessage2(_c, _m, _s, _d1, _d2)                                 \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message2(_s, _d1, _d2);                                       \
    }
#define DbgMessage3(_c, _m, _s, _d1, _d2, _d3)                            \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message3(_s, _d1, _d2, _d3);                                  \
    }
#define DbgMessage4(_c, _m, _s, _d1, _d2, _d3, _d4)                       \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message4(_s, _d1, _d2, _d3, _d4);                             \
    }
#define DbgMessage5(_c, _m, _s, _d1, _d2, _d3, _d4, _d5)                  \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message5(_s, _d1, _d2, _d3, _d4, _d5);                        \
    }
#define DbgMessage6(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6)             \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message6(_s, _d1,_d2,_d3,_d4,_d5,_d6);                        \
    }
#define DbgMessage7(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6, _d7)             \
    if(LOG_MSG(_m))                                                   \
    {                                                                       \
        MessageHdr((_c), (_m));                                                 \
        debug_message7(_s, _d1,_d2,_d3,_d4,_d5,_d6,_d7);                        \
    }
// __USER_MODE_DEBUG
#define debug_break() do { \
    debug_message("DEBUG BREAK!\n"); \
    fflush(stdout); \
    assert(0); \
} while(0)

// __USER_MODE_DEBUG
#define DbgBreak()            debug_break()

#define DbgBreakIf(_cond) do {\
    if(_cond) \
    { \
        MessageHdr((void *)NULL,LV_FATAL); \
        debug_message("Condition failed: if("#_cond##")\n"); \
        debug_break(); \
    } \
} while(0)

// __USER_MODE_DEBUG
#define DbgBreakMsg(_s) do { \
    MessageHdr((void *)NULL,LV_FATAL); \
    debug_message("Debug Break Message: " _s); \
    debug_break(); \
} while(0)

// __USER_MODE_DEBUG
#define DbgBreakFastPath()        DbgBreak()
#define DbgBreakIfFastPath(_cond) DbgBreakIf(_cond)
#define DbgBreakMsgFastPath(_s)   DbgBreakMsg(_s)

//#define dbg_out(_c, _m, _s, _d1) DbgMessage1((_c), (_m), (_s), (_d1))
#define dbg_out(_c, _m, _s, _d1) printf(_s, _d1)

#elif defined(__LINUX) || defined(USER_LINUX)

#define	__cdecl

#define DbgBreak            debug_break

#undef __FILE_STRIPPED__
#ifdef __LINUX
char *os_if_strrchr(char *a, int   n);

#define __FILE_STRIPPED__  os_if_strrchr(__FILE__, '/')   ?   os_if_strrchr(__FILE__, '/')   + 1 : __FILE__
#else
#define __FILE_STRIPPED__  strrchr(__FILE__, '/')   ?   strrchr(__FILE__, '/')   + 1 : __FILE__
#endif

/*******************************************************************************
 * Debug break and output routines.
 ******************************************************************************/
void __cdecl
debug_msgx(
    unsigned long level,
    char *msg,
    ...);

#ifdef USER_LINUX

#define MessageHdr(_dev, _level) \
	printf("TrLv<%d>, %s (%4d): %p ", ((_level) & LV_MASK), __FILE_STRIPPED__, __LINE__, _dev )

#define debug_break() do { \
    MessageHdr(NULL, LV_FATAL); \
    debug_msgx(FATAL, "DEBUG BREAK!\n"); \
    fflush(stdout); \
    exit(0); \
} while(0)

#else /* KERNEL */

#define MessageHdr(_dev, _level) \
    do { \
	    printk(KERN_CRIT "TrLv<%d>, %s (%4d): ", ((_level) & LV_MASK), __FILE_STRIPPED__, __LINE__); \
	    mm_print_bdf(_level, (void*)(_dev)); \
    } while (0)

void debug_break(void);
#endif

#ifdef USER_LINUX
#define DbgMessageXX(_c, _m, _s...)                    \
    if(LOG_MSG(_m))                                    \
    {                                                  \
	MessageHdr(_c, _m);			       \
        debug_msgx(_m, ##_s);   \
    }
#else  /* __LINUX */
#define DbgMessageXX(_c, _m, _s...)                    \
    if(unlikely(LOG_MSG(_m)))                                    \
    {                                                  \
	MessageHdr(_c, _m);			       \
        debug_msgx(_m, ##_s);   \
    }
#endif 

#define DbgMessage  DbgMessageXX
#define DbgMessage1 DbgMessageXX
#define DbgMessage2 DbgMessageXX
#define DbgMessage3 DbgMessageXX
#define DbgMessage4 DbgMessageXX
#define DbgMessage5 DbgMessageXX
#define DbgMessage6 DbgMessageXX
#define DbgMessage7 DbgMessageXX

// LINUX
#ifdef USER_LINUX
#define DbgBreakIf(_c) \
    if(_c) \
    { \
        DbgMessage(NULL, FATAL, "if("#_c")\n"); \
        DbgBreak(); \
    }
#else /* __LINUX */
#define DbgBreakIf(_c) \
    if(unlikely(_c)) \
    { \
        DbgMessage(NULL, FATAL, "if("#_c")\n"); \
        DbgBreak(); \
    }
#endif

// LINUX
#define DbgBreakMsg(_m)     do {DbgMessage(NULL, FATAL, _m); DbgBreak();} while (0)

// LINUX
#define DbgBreakFastPath()        DbgBreak()
#define DbgBreakIfFastPath(_cond) DbgBreakIf(_cond)
#define DbgBreakMsgFastPath(_s)   DbgBreakMsg(_s)

#define dbg_out(_c, _m, _s, _d1) debug_msgx(_m, "TrLv<%d>, %s (%4d): %p"_s" %s\n", ((WARN) & LV_MASK), __FILE_STRIPPED__, __LINE__, _c, _d1 )

#elif defined(__SunOS)

#if defined(__SunOS_MDB)
#define DbgMessage(_c, _m, _s)
#define DbgMessage1(_c, _m, _s, _d1)
#define DbgMessage2(_c, _m, _s, _d1, _d2)
#define DbgMessage3(_c, _m, _s, _d1, _d2, _d3)
#define DbgMessage4(_c, _m, _s, _d1, _d2, _d3, _d4)
#define DbgMessage5(_c, _m, _s, _d1, _d2, _d3, _d4, _d5)
#define DbgMessage6(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6)
#define DbgMessage7(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6, _d7)
#define DbgBreak()
#define DbgBreakIf(_cond)
#define DbgBreakMsg(_s)
#define DbgBreakFastPath()
#define DbgBreakIfFastPath(_cond)
#define DbgBreakMsgFastPath(_s)
#define dbg_out(_c, _m, _s, _d1) cmn_err(CE_NOTE, _s, _d1)
#else
/* under //servers/main/nx2/577xx/drivers/solaris/src */
#include "bnxe_debug.h"
#endif

#elif defined(__WINDOWS)

#else

/*******************************************************************************
 * Debug break and output routines.
 ******************************************************************************/
void
debug_break(
    void *ctx);

void __cdecl
debug_msg(
    void *ctx,
    unsigned long level,
    char *file,
    unsigned long line,
    char *msg,
    ...);

void __cdecl
debug_msgx(
    void *ctx,
    unsigned long level,
    char *msg,
    ...);

// WINDDK
#define DbgMessage(_c, _m, _s)                                              \
    if(LOG_MSG(_m))                                                         \
    {                                                                       \
        debug_msg(_c, _m, __FILE__, __LINE__, _s);                          \
    }
#define DbgMessage1(_c, _m, _s, _d1)                                        \
    if(LOG_MSG(_m))                                                         \
    {                                                                       \
        debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1);                     \
    }
#define DbgMessage2(_c, _m, _s, _d1, _d2)                                   \
    if(LOG_MSG(_m))                                                         \
    {                                                                       \
        debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1, _d2);                \
    }
#define DbgMessage3(_c, _m, _s, _d1, _d2, _d3)                              \
    if(LOG_MSG(_m))                                                         \
    {                                                                       \
        debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1, _d2, _d3);           \
    }
#define DbgMessage4(_c, _m, _s, _d1, _d2, _d3, _d4)                         \
    if(LOG_MSG(_m))                                                         \
    {                                                                       \
        debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1, _d2, _d3, _d4);      \
    }
#define DbgMessage5(_c, _m, _s, _d1, _d2, _d3, _d4, _d5)                    \
    if(LOG_MSG(_m))                                                         \
    {                                                                       \
        debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1, _d2, _d3, _d4, _d5); \
    }
#define DbgMessage6(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6)               \
    if(LOG_MSG(_m))                                                         \
    {                                                                       \
        debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1,_d2,_d3,_d4,_d5,_d6); \
    }
#define DbgMessage7(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6,_d7)               \
    if(LOG_MSG(_m))                                                             \
    {                                                                           \
        debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1,_d2,_d3,_d4,_d5,_d6,_d7); \
    }

// WINDDK
#define DbgBreakIf(_c) \
    if(_c) \
    { \
        debug_msg(NULL, FATAL, __FILE__, __LINE__, "if("#_c##")\n"); \
        debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, WIDEN(#_c##) );\
        __debugbreak(); \
    }
// WINDDK
#define DbgBreakMsg(_m)     debug_msg(NULL, FATAL, __FILE__, __LINE__, _m); \
                            debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, WIDEN(#_m##) );\
                            __debugbreak()
// WINDDK
#define DbgBreak()          debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, L"DbgBreak" );\
                            __debugbreak()
// WINDDK (debug)
#define DbgBreakFastPath()          DbgBreak()
#define DbgBreakIfFastPath(_cond)   DbgBreakIf(_cond)
#define DbgBreakMsgFastPath(_s)     DbgBreakMsg(_s)

// WINDDK
#define dbg_out(_c, _m, _s, _d1) debug_msg(_c, _m, __FILE__, __LINE__, _s, _d1)

#endif //OS architectures

// Error Macros (Currently supports Windows DDK & DOS):
// in debug builds - outputs a debug message and enter condition in case TRUE
// in release builds - enters condition in case TRUE (like debug but without the debug print)

// Macro for checking parameter for NULL value
//    Usage Example:
//       if( CHECK_NULL( ptr ))
//       {
//          return FALSE ;
//       }
#define CHK_NULL(p) ((p==NULL)    ? (dbg_out(NULL, WARN, DBG_ERR_STR" %s is NULL\n",#p), TRUE): FALSE )

// Macros that returns the value of the expression and outputs a debug string in debug versions
//    Usage Example:
//       if( ERR_IF( val < 0 ))
//       {
//          return FALSE ;
//       }
#define ERR_IF(cond)(((cond)==TRUE) ? (dbg_out(NULL, WARN, DBG_ERR_STR" ErrIf failed %s\n",#cond), TRUE): FALSE )
#define WRN_IF(cond)(((cond)==TRUE) ? (dbg_out(NULL, WARN, DBG_WRN_STR" WrnIf failed %s\n",#cond), TRUE): FALSE )

#else // !DBG
#define STATIC static

#define DbgMessage(_c, _m, _s)
#define DbgMessage1(_c, _m, _s, _d1)
#define DbgMessage2(_c, _m, _s, _d1, _d2)
#define DbgMessage3(_c, _m, _s, _d1, _d2, _d3)
#define DbgMessage4(_c, _m, _s, _d1, _d2, _d3, _d4)
#define DbgMessage5(_c, _m, _s, _d1, _d2, _d3, _d4, _d5)
#define DbgMessage6(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6)
#define DbgMessage7(_c, _m, _s, _d1, _d2, _d3, _d4, _d5, _d6, _d7)

#if ! (defined(WIN_DIAG) || defined(__LINUX) || defined(USER_LINUX) || defined(__SunOS))
// WINDDK DbgBreak (retail) and logging an event
#define DbgBreak()                       debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, L"DbgBreak" )
#define DbgBreakIf(_cond)     if(_cond){ debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, WIDEN(#_cond##) ); }
#define DbgBreakMsg(_s)                  debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, WIDEN(#_s##) )

// WINDDK DbgBreak (retail) without logging an event
#define DbgBreakNoLog()
#define DbgBreakIfNoLog(_cond)
#define DbgBreakMsgNoLog(_s)

// WINDDK DbgBreak FastPath (retail)
#define DbgBreakFastPath()         DbgBreakNoLog()
#define DbgBreakIfFastPath(_cond)  DbgBreakIfNoLog(_cond)
#define DbgBreakMsgFastPath(_s)    DbgBreakMsgNoLog(_s)

#else // WIN_DIAG and Linux and Solaris
#define DbgBreak()
#define DbgBreakIf(_cond)
#define DbgBreakMsg(_s)
#define DbgBreakFastPath()
#define DbgBreakIfFastPath(_cond)
#define DbgBreakMsgFastPath(_s)
#endif // !WIN_DIAG

#define CHK_NULL(p) (p==NULL) 
#define ERR_IF(cond)(cond==TRUE)
#define WRN_IF(cond)(cond==TRUE)

#endif // !DBG

#if defined(DOS) || defined(__USER_MODE_DEBUG) || defined(UEFI)
#define DbgBreakIfAll(_cond) do {\
    if(_cond) \
    { \
        printf("DEBUG BREAK! Condition failed: if("#_cond##")\n"); \
        fflush(stdout); \
        assert(0); \
    } \
} while(0)

#define EXECUTE_LOGGERS(_s)        do {\
        printf(_s);      \
        fflush(stdout);  \
} while(0)

#elif  defined(_VBD_CMD_)
#include <assert.h>
#include <string.h>
#define DbgBreakIfAll(_cond) do {\
    if(_cond) \
    { \
        DbgMessage(NULL, FATAL, "DEBUG BREAK! Condition failed: if("#_cond##")\n"); \
        assert(0); \
    } \
} while(0)


#define EXECUTE_LOGGERS(_s)                  DbgMessage(NULL, FATAL, _s);

#elif  defined(__LINUX) || defined(USER_LINUX)

#define DbgBreakIfAll(_cond) do {\
    if(_cond) \
    { \
        DbgMessage1(NULL, FATAL, "DEBUG BREAK! Condition failed: if(%s)\n", #_cond); \
        debug_break(); \
    } \
} while(0)

#define EXECUTE_LOGGERS(_s)                  DbgMessage(NULL, FATAL, _s);

#elif defined(__SunOS)

#if defined(__SunOS_MDB)

#define DbgBreakIfAll(_c)      \
    do {                       \
        if (_c)                \
        {                      \
            /* nop in MDB */ ; \
        }                      \
    } while (0)

#else /* !__SunOS_MDB */ 

#define DbgBreakIfAll(_c)                                                     \
    do {                                                                      \
        if (_c)                                                               \
        {                                                                     \
            cmn_err(CE_PANIC, "<%d> %s(%4d): Condition Failed! - if ("#_c")", \
                    ((FATAL) & LV_MASK),                                      \
                    __FILE_STRIPPED__,                                        \
                    __LINE__);                                                \
        }                                                                     \
    } while (0)

#endif /* __SunOS_MDB */

#define EXECUTE_LOGGERS(_s)                  

#else // Windows

#include <ntddk.h>
#include <wchar.h>
#define DbgBreakIfAll(_cond) do {\
    if(_cond) \
    { \
        debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, WIDEN(#_cond##) ); \
        DbgPrint("DEBUG BREAK! Condition failed: if("#_cond##")\n"); \
        DbgBreakPoint(); \
    } \
} while(0)

#define EXECUTE_LOGGERS(_s)                  debug_execute_loggers( __WFILE_STRIPPED__ , __LINE__, WIDEN(#_s##) )

#endif

#endif /* _DEBUG_H_ */
