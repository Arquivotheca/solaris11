/*

  sshadt.h

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Wed Sep  8 17:01:04 1999.

  */

#ifndef SSHADT_H_INCLUDED
#define SSHADT_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/* The fast macro interface is used unless DEBUG_LIGHT is defined.  */


#ifndef DEBUG_LIGHT
#define SSH_ADT_WITH_MACRO_INTERFACE
#endif /* DEBUG_LIGHT */


#define SSHADT_INSIDE_SSHADT_H

/* Generic header type.  */
typedef struct {
  void *ptr[5];
} SshADTHeaderStruct;

typedef struct SshADTContainerRec *SshADTContainer;

/* Handles for doing iteration, inserting objs etc.  */
typedef void *SshADTHandle;


/*********************************************************** Callback types. */

/* Compare two objects.  */
typedef int (* SshADTCompareFunc)(const void *obj1, const void *obj2,
                                  void *context);

/* Duplicate an object.  'obj' is not constant because it is possible
   that the object is duplicated by e.g. increasing its reference
   count and returning itself.  */
typedef void *(* SshADTDuplicateFunc)(void *obj, void *context);

/* Initialize an abstract object from an another.  */
typedef void (* SshADTCopyFunc)(void *dst, size_t dst_size,
                                const void *src, void *context);

/* Destroy an object.  */
typedef void (* SshADTDestroyFunc)(void *obj, void *context);

/* Initialize an abstract object.  */
typedef void (* SshADTInitFunc)(void *obj, size_t size, void *context);

/* Calculate the hash of an object.  */
typedef SshUInt32 (* SshADTHashFunc)(const void *obj, void *context);

/* Notify a map value that there is a new reference to it.  To be more
   precise, this callback is invoked iff map_attach binds a value to a
   key object in a container.  */
typedef void (* SshADTMapAttachFunc)(void *value, void *context);
                                     
/* Notify a map value that a key referencing it has been deleted.
   More precisely, this callback is invoked iff

    - a value is overwritten by a new value attached to an object by
      map_attach, or

    - if a key is deleted from a container.

   In particular, if a key is only detached but not deleted, MapDetach
   is not called.  */
typedef void (* SshADTMapDetachFunc)(void *value, void *context);

/* Clean up the container context.  This is called when the container
   is destroyed, after all elements have been removed from the
   container.  */
typedef void (* SshADTCleanupFunc)(void *ctx);


/******************************************** Container creation parameters. */

typedef enum {
  /* Argument type identifier           C type of the next argument
     ========================           =========================== */

  /* Set the user-supplied context for all callbacks.  Defaults to
     NULL when not set.  */
  SSH_ADT_CONTEXT,                      /* void * */

  /* Set the compare func.  */
  SSH_ADT_COMPARE,                      /* SshADTCompareFunc */

  /* Set the duplicate func.  */
  SSH_ADT_DUPLICATE,                    /* SshADTDuplicateFunc */

  /* Set the copy func.  */
  SSH_ADT_COPY,                         /* SshADTCopyFunc */

  /* Set the destroy func.  */
  SSH_ADT_DESTROY,                      /* SshADTDestroyFunc */

  /* Set the hash func.  */
  SSH_ADT_HASH,                         /* SshADTHashFunc */

  /* Set the init func.  */
  SSH_ADT_INIT,                         /* SshADTInitFunc */

  /* Set the map insert notification func.  */
  SSH_ADT_MAP_ATTACH,                   /* SshADTMapAttachFunc */

  /* Set the map insert notification func.  */
  SSH_ADT_MAP_DETACH,                   /* SshADTMapDetachFunc */

  /* Set the cleanup func.  */
  SSH_ADT_CLEANUP,                      /* SshADTCleanupFunc */

  /* Tell the ADT library to use a header inside the objects instead
     of allocating auxiliary data outside the objects. The next
     argument must be the offset (size_t) to an SshADTHeader field.  */
  SSH_ADT_HEADER,                       /* SSH_ADT_OFFSET_OF(...) */

  /* Tell the ADT library the default size of the objects. This turns
     automatic object allocation on.  */
  SSH_ADT_SIZE,                         /* size_t */

  /* end of args marker.  */
  SSH_ADT_ARGS_END                      /* None */
} SshADTArgumentType;

#define SSH_ADT_OFFSET_OF(type,field) \
((unsigned char *)(&((type *)0)->field) - (unsigned char *)0)

/* Container types.  */

typedef const void *SshADTContainerType;

/* Invalid handle.  (Never ever redefine this to be something
   different from NULL.  It will never work.)  */
#define SSH_ADT_INVALID   NULL


/******************************************** Handling containers as a whole */

/* Generic container allocation. This can return NULL if memory
   allocation fails in those cases where memory allocation can fail
   (e.g. in kernel code).  */
SshADTContainer ssh_adt_create_generic(SshADTContainerType type,
                                       ...);

/* Destroy a container and all objects contained.  */
void ssh_adt_destroy(SshADTContainer container);

/* Make the container empty: destroy the contained objects but not the
   container itself. This returns the container basically to the state
   just after create.  */
void ssh_adt_clear(SshADTContainer container);

/******************************************* Absolute and relative locations */

typedef long SshADTAbsoluteLocation;

/* The negative integers denote 'special' values.  */
#define SSH_ADT_BEGINNING -1
#define SSH_ADT_END       -2
#define SSH_ADT_DEFAULT   -3

/* Concrete absolute locations (e.g. array indices) are not natural
   numbers but quite.  The following macros transform integers into
   absolute locations and back.  Negative integers are forbidden.  */

/* long -> SshADTAbsoluteLocation */
#define SSH_ADT_INDEX(n) (n)

/* SshADTAbsoluteLocation -> long */
#define SSH_ADT_GET_INDEX(n) (n)

/* SshADTAbsoluteLocation -> Boolean */
#define SSH_ADT_IS_INDEX(n) ((n) >= 0)  /* (No BEGINNING, END, DEFAULT here) */

typedef enum {
  SSH_ADT_BEFORE,
  SSH_ADT_AFTER
} SshADTRelativeLocation;

/******************************************** Creating and inserting objects */

/* 1. concrete objects.  */

/* Insert an object into a container (ie. copy the pointer).  Returns
   a handle to the inserted object.  The object may only be used as
   long as it is clear that the object is still alive in the
   container.  The container is responsible for destroying the object
   with an SshADTDestroyFunc callback.  */

SshADTHandle ssh_adt_insert_at(SshADTContainer container,
                               SshADTRelativeLocation location,
                               SshADTHandle handle,
                               void *object);

SshADTHandle ssh_adt_insert_to(SshADTContainer container,
                               SshADTAbsoluteLocation location,
                               void *object);

SshADTHandle ssh_adt_insert(SshADTContainer container,
                            void *object);

/* Instead of copying the pointer of a concrete object, initialize a
   new concrete object from the original using the SshADTDuplicateFunc
   callback.  */

SshADTHandle ssh_adt_duplicate_at(SshADTContainer container,
                               SshADTRelativeLocation location,
                               SshADTHandle handle,
                               void *object);

SshADTHandle ssh_adt_duplicate_to(SshADTContainer container,
                               SshADTAbsoluteLocation location,
                               void *object);

SshADTHandle ssh_adt_duplicate(SshADTContainer container,
                               void *object);

/* 2. abstract objects.  */

/* Invoke SshADTInitFunc to initialize a new object in the container.  */

SshADTHandle ssh_adt_alloc_n_at(SshADTContainer container,
                                SshADTRelativeLocation location,
                                SshADTHandle handle,
                                size_t size);

SshADTHandle ssh_adt_alloc_n_to(SshADTContainer container,
                                SshADTAbsoluteLocation location,
                                size_t size);

SshADTHandle ssh_adt_alloc_at(SshADTContainer container,
                              SshADTRelativeLocation location,
                              SshADTHandle handle);

SshADTHandle ssh_adt_alloc_to(SshADTContainer container,
                              SshADTAbsoluteLocation location);

SshADTHandle ssh_adt_alloc_n(SshADTContainer container,
                             size_t size);

SshADTHandle ssh_adt_alloc(SshADTContainer container);

/* Invoke the SshADTCopyFunc callback to copy an object provided by
   the user into the container.  After this operation, the user's
   object is not associated to the container in any way and must be
   freed by hand eventually.  */

SshADTHandle ssh_adt_put_n_at(SshADTContainer container,
                              SshADTRelativeLocation location,
                              SshADTHandle handle,
                              size_t size,
                              void *obj);

SshADTHandle ssh_adt_put_n_to(SshADTContainer container,
                              SshADTAbsoluteLocation location,
                              size_t size,
                              void *obj);

SshADTHandle ssh_adt_put_at(SshADTContainer container,
                            SshADTRelativeLocation location,
                            SshADTHandle handle,
                            void *obj);

SshADTHandle ssh_adt_put_to(SshADTContainer container,
                            SshADTAbsoluteLocation location,
                            void *obj);

SshADTHandle ssh_adt_put_n(SshADTContainer container,
                           size_t size,
                           void *obj);

SshADTHandle ssh_adt_put(SshADTContainer container,
                         void *obj);

/********************************************************* Accessing objects */

/* Get the object at the handle.  */
void *ssh_adt_get(SshADTContainer container, SshADTHandle handle);

/* Get the number of objects inside a container.  */
size_t ssh_adt_num_objects(SshADTContainer container);

/*********************************************************** Setting handles */

/* Get a handle to the object. The pointers must match exactly. This
   can be a slow operation with concrete objects and abstract headers,
   because there is only a reference in the wrong direction and the
   container must be searched brute-force.  */
SshADTHandle ssh_adt_get_handle_to(SshADTContainer container,
                                   void *object);

/* Get a handle to any object that is equal to 'object'.  */
SshADTHandle ssh_adt_get_handle_to_equal(SshADTContainer container,
                                         void *object);

/* Get any object that is equal to 'object'.  */
void *ssh_adt_get_object_from_equal(SshADTContainer container,
                                    void *object);

/* get the handle from an absolute location */
SshADTHandle ssh_adt_get_handle_to_location(SshADTContainer container,
                                            SshADTAbsoluteLocation location);

/* get the object from an absolute location */
void *ssh_adt_get_object_from_location(SshADTContainer container,
                                       SshADTAbsoluteLocation location);

/* Moving handles in containers that support some kind of ordering.
   In contrast to enumeration, this only works when there is any
   meaningful order in the container structure.  (Eg., even if a map
   is provided with a compare function it won't allow calls to these
   functions.)  */

/* Move the handle to the next object.  */
SshADTHandle ssh_adt_next(SshADTContainer container, SshADTHandle handle);

/* Move the handle to the previous object.  */
SshADTHandle ssh_adt_previous(SshADTContainer container, SshADTHandle handle);

/***************************************** Removing objects from a container */

/* Detach the object that ssh_adt_get(container, handle) would return
   from the container.  After this the handle is invalidated.  Handle
   must be valid initial to the method call.  */
void *ssh_adt_detach(SshADTContainer container,              
                     SshADTHandle handle);

/* Detach object from a valid location.  */
void *ssh_adt_detach_from(SshADTContainer container,
                          SshADTAbsoluteLocation location);

/* Detach object from container.  It is an error for object not been
   found in the container.  */
void *ssh_adt_detach_object(SshADTContainer container, void *object);

/***************************************** Deleting objects from a container */

/* (This parallels exactly the detach methods.)  */

/* Destroy the object that ssh_adt_get(container, handle) would
   return.  Invalidates the handle.  Handle must be valid initial to
   the method call.  */
void ssh_adt_delete(SshADTContainer container,
                    SshADTHandle handle);

/* Destroy object from a valid location.  */
void ssh_adt_delete_from(SshADTContainer container,
                         SshADTAbsoluteLocation location);

/* Delete object from container.  It is an error for object not been
   found in the container.  */
void ssh_adt_delete_object(SshADTContainer container,
                           void *object);

/************************************************* Resizing abstract objects */

/* Reallocate a object inside a container.  */

void *ssh_adt_realloc(SshADTContainer container, void *object,
                      size_t new_size);


/* Return the default size of abstract objects.  */
size_t ssh_adt_default_size(SshADTContainer container);

/******************************************************* Duplicating objects */

/* Duplicate a concrete object.  */
void *ssh_adt_duplicate_object(SshADTContainer container, void *object);

/********************************************* Generic enumeration functions */

/* Start enumerating a container.  Returns 'SSH_ADT_INVALID' if
   container is empty.  */
SshADTHandle ssh_adt_enumerate_start(SshADTContainer container);

/* Continue enumeration. Returns 'SSH_ADT_INVALID' if all objects have
   been enumerated.  */
SshADTHandle ssh_adt_enumerate_next(SshADTContainer container,
                                    SshADTHandle handle);

/************************************************* Generic mapping functions */

/* Lookup the value of key handle in a mapping container.  */
void *ssh_adt_map_lookup(SshADTContainer container, SshADTHandle handle);

/* Add a new value to a mapping with key handle.  */
void ssh_adt_map_attach(SshADTContainer container,
                        SshADTHandle handle, void *obj);

/************************ If the macro interface is active, include the rest */

#ifdef SSH_ADT_WITH_MACRO_INTERFACE
#include "sshadt_structs.h"
#include "sshadt_impls.h"
#include "sshadt_shortcuts.h"
#endif

#undef SSHADT_INSIDE_SSHADT_H

#ifdef __cplusplus
}
#endif

#endif /* SSHADT_H_INCLUDED */
