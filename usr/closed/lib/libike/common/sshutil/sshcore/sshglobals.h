/*
 * Author: Tero Kivinen <kivinen@iki.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 */
/*
 *        Program: sshutil
 *
 *        Creation          : 14:04 Dec 19 2001 kivinen
 *        Last Modification : 17:45 Dec 19 2001 kivinen
 *        
 *
 *        Description       : Support for global variables
 */

#ifndef SSHGLOBALS_H
#define SSHGLOBALS_H

/* To use global variables in code you have to do following:

   old code                     new code

   // Declaration of global variable

   extern int foobar;           SSH_GLOBAL_DECLARE(int, foobar);
                                #define foobar SSH_GLOBAL_USE(foobar)

   // Definiation of global variable
   int foobar;                  SSH_GLOBAL_DEFINE(int, foobar);

   // Initialization of global variable (this must be inside the
   // init function or similar, all global variables are initialized to
   // zero at the beginning). If SSH_GLOBAL_INIT is not called then
   // first use of variable might print out warning about use of
   // uninitialized global variable (if warnings are enabled).
   // Warning might also be printed out if the SSH_GLOBAL_INIT is called
   // multiple times without calls to ssh_global_reset or ssh_global_uninit +
   // init.

   int foobar = 1;              // this is not allowed

   foobar = 1;                  SSH_GLOBAL_INIT(foobar,1);

   // Using the global variable

   foobar = 1;                  foobar = 1; // i.e no changes
   foobar = foobar++;           foobar = foobar++;

*/
#ifndef SSH_GLOBALS_EMULATION

# define SSH_GLOBAL_USE(var) ssh_global_ ## var
# define SSH_GLOBAL_DECLARE(type,var) extern type ssh_global_ ## var
# define SSH_GLOBAL_DEFINE(type,var) type ssh_global_ ## var
# define SSH_GLOBAL_INIT(var,value) (ssh_global_ ## var) = (value)

#else /* SSH_GLOBALS_EMULATION */

# define SSH_GLOBAL_TYPE(var) ssh_global_ ## var ## _type
# define SSH_GLOBAL_USE(var) \
   (*((SSH_GLOBAL_TYPE(var) *) \
      ssh_global_get(#var, sizeof(SSH_GLOBAL_TYPE(var)))))
# define SSH_GLOBAL_DECLARE(type,var) typedef type SSH_GLOBAL_TYPE(var)
# define SSH_GLOBAL_DEFINE(type,var) \
typedef enum { SSH_GLOBAL_NOT_EMPTY_ ## var } SshGlobalNotEmpty_ ## var
# define SSH_GLOBAL_INIT(var,value) \
   (ssh_global_init_variable(#var, sizeof(ssh_global_ ## var ## _type)), \
    (var = value))
#endif /* SSH_GLOBALS_EMULATION */

/* Example code:

  SSH_GLOBAL_DECLARE(int, foobar);
  #define foobar SSH_GLOBAL_USE(foobar)

  SSH_GLOBAL_DEFINE(int, foobar);

  void test(void)
    {
      foobar = 1;
      SSH_GLOBAL_INIT(foobar,2);
      foobar++;
    }

   The code above expands to this code when SSH_GLOBALS_EMULATION is undefined:

   extern int ssh_global_foobar;
   int ssh_global_foobar;

   void test(void)
     {
       ssh_global_foobar = 1;
       (ssh_global_foobar) = ( 2 ) ;
       ssh_global_foobar++;
     }

   And if SSH_GLOBALS_EMULATION is defined then it expands to
   following code:

   typedef int ssh_global_foobar_type;

   ;

   void test(void)
     {
       (*((ssh_global_foobar_type *)
          ssh_global_get("foobar", sizeof(ssh_global_foobar_type)))) = 1;
       (ssh_global_init_variable("foobar", sizeof(ssh_global_foobar_type)),
        (*((ssh_global_foobar_type *)
           ssh_global_get("foobar", sizeof(ssh_global_foobar_type)))) = ( 2 ));
       (*((ssh_global_foobar_type *)
          ssh_global_get("foobar", sizeof(ssh_global_foobar_type))))++;
     }

*/

/* Function that returns pointer to the global variable based on the name of
   the global variable. If the variable is used before it is initialized (i.e
   the ssh_global_init_variable is not called before the first use of the
   ssh_global_get), then ssh_global_get might print out warning, and the value
   of the variable will be all zeros. Note, str is assumed to be constant
   string whose lifetime is unlimited. */
void *ssh_global_get(const char *str, size_t variable_size);

/* Initialize variable to have value of all zeros. This makes the variable to
   be known to the system, and ssh_global_get will assume not print out
   warnings about use of uninitialized variables. Call this function twice
   will print out warning. This returns always returns 0. Note, str is assumed
   to be constant string whose lifetime is unlimited.*/
int ssh_global_init_variable(const char *str, size_t variable_size);

/* Initialize global variables system. Calling this will reset all
   global variables to uninitialized state. One UNIX platforms it not
   neccessary to call this routine at all. */
void ssh_global_init(void);

/* Uninitialize global variables system. Calling this will reset all global
   variables to uninitialized state, and free all state allocated for the
   global variables. */
void ssh_global_uninit(void);

#endif /* SSHGLOBALS_H */
