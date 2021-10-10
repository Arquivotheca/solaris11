/*
  sshasn1-oid.h

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  OID routines for the Asn.1 compiler.
*/

#ifndef SSHASN1_OID_H
#define SSHASN1_OID_H

#include "sshadt.h"

/* In this library a OID is a object that holds information related to
   a particular ASN.1 OID. It always has an associated ASN.1 OID, but
   only in the explicit sense (e.g. the explicit string "1.2.3" is a
   OID, but without the meaning that ASN.1 would give it).

   This implementation may be considered as a database for any strings
   such as "1.2.3" that give hierarchial structure (necessarily). The
   name for such strings is OID in this interface (and library) and by
   abuse of language we mean also the context by this term (as above
   indicated).
*/

/* Initialize and free GLOBAL OID database. Initialization should be
   called before applications can use the database. If not called, the
   first access will initialize the database. To avoid memory leaks,
   free needs to be called at application exit. */
Boolean ssh_asn1_oiddb_init(void);
void ssh_asn1_oiddb_free(void);

/* The type definition for the OID. */
typedef struct SshAsn1OidRec *SshAsn1Oid;


/***** Converting OID's to text */

/* Get the OID as char * -pointer, which needs to be freed by the caller. */
char *ssh_asn1_oiddb_get_oid_text(SshAsn1Oid oid);

/* Get a default name of an OID. */
char *ssh_asn1_oiddb_get_oid_default_name(SshAsn1Oid oid);

/***** Obtaining (and forgetting) OID's. */

/* These functions are the only ones that obtain fresh pointers to
   OID information. Each obtained OID should be "forgotten" by the
   "ssh_asn1_oiddb_forget" function. */


/* The "ref_oid" may be set to NULL to indicate the root level. Otherwise,
   the "ref_oid" will be used as the starting point. */
SshAsn1Oid
ssh_asn1_oiddb_find_by_oid(SshAsn1Oid ref_oid, const char *oid);
SshAsn1Oid
ssh_asn1_oiddb_find_by_name(const char *name);

/* Find an OID from the DB that matches best the postfix oid from the
   given reference position. This can be effectively used to test
   whether the OID DB contains any information related to the given
   OID (but not necessarily about the given OID). */
SshAsn1Oid
ssh_asn1_oiddb_find_longest_prefix(SshAsn1Oid ref_oid,
                                   const char *postfix_oid);

/* This function creates a fresh OID or finds an OID of the given
   value. The internal implementation makes it sure that application
   cannot create several instances of a given OID  */
SshAsn1Oid
ssh_asn1_oiddb_create_oid(SshAsn1Oid ref_oid,
                          const char *postfix_oid);

/* Forget the obtained reference to an OID object. This function
   should be used for all OID's obtained from the OID DB. */
void ssh_asn1_oiddb_forget_ref(SshAsn1Oid ref_oid);

/* Returns the parent OID of the reference OID. This can be used to
   individually traverse the OID tree towards root. */
SshAsn1Oid
ssh_asn1_oiddb_get_oid_parent(SshAsn1Oid ref_oid);

/***** Manipulation of OID's and their parameters. */

/* The following function returns the string presentation of the OID,
   such as "1.2.3.4", relative to another reference OID. That is, if
   "ref_root" points to "1.2" and "ref_oid" to "1.2.3.4" then the
   function returns "3.4". If "ref_root" is NULL then the full encoded
   OID is returned.

   The oid returned by this function should be freed by the caller. */
char *
ssh_asn1_oiddb_get_relative_oid(SshAsn1Oid ref_root,
                                      SshAsn1Oid ref_oid);

/* Get the component of the OID, e.g. the number of the current OID
   relative to its parent. That is, if the OID is in encoded form
   "1.2.3" then this returns "3".
   The string is to be freed by the caller. */
char *
ssh_asn1_oiddb_get_oid_component(SshAsn1Oid ref_oid);

/* Add a name that links to the reference OID. The name must be freed
   by the caller. */
Boolean
ssh_asn1_oiddb_add_name(SshAsn1Oid ref_oid, const char *name);

/* Add <tag,data> pair to the OID. It is intended that the database
   keeps this information for the application. It is not allowed to
   have more than one value by a given tag. The caller must free the
   input "tag" and "data", as the library takes copies of those. */
Boolean
ssh_asn1_oiddb_add_param(SshAsn1Oid ref_oid,
                         const char *tag, const char *data);

/* This function looks for the parameters tagged by "tag" within the
   parameter list of the reference OID. The "data" may be NULL in
   which case this function can be used to verify whether there exists
   such a tag or not for the given OID. The returned data should not
   be freed by the caller, and the "data" becomes invalid as soon as
   another call to the OID DB library is made, but not sooner. */
Boolean
ssh_asn1_oiddb_find_param(SshAsn1Oid ref_oid,
                          const char *tag, const char **data);


/***** Scripting and linearization of the OID DB. */

/* The format in which the OID DB can be currently updates is described
   next:

   OIDDB :=  STRING-OR-LIST "=" OID-OR-LIST ( ":" PARAM-LIST | ) ";"

   STRING-OR-LIST := ( STRING | "(" STRING-LIST ")" )
   OID-OR-LIST    := ( ( STRING | OID ) | "(" ( STRING | OID ) OID-LIST ")" )

   OID-LIST    := OID ( OID-LIST | )
   STRING-LIST := STRING ( STRING-LIST | )

   STRING := < US ASCII string with escaping and quoting alike that in
               C language >
   OID    := < A string consisting of, exclusively, digits and "." >

   There is also possibility for using comments with "#" sign, which means
   that the rest of the line (upto to linefeed) is ignored by the parser.

   We explain the semantics with an example:

   ( ISO ) = 1 : std-body = "iso";
   ( MEMBER-BODY ) = ( ISO 2 ) : std-body = "member-body";

   The initial string list, such as "( ISO )", denotes the list of unique
   names that point to the OID, such as "1". The list within ":" and ";"
   gives the parameters with the <tag, value> pair idea. So the tag
   "std-body" has the value "iso" linked to the OID "1". Similarily with
   the "MEMBER-BODY" example. There you can see that "ISO" can be used in
   the first element of the OID list to denote "1".

*/

/* This function linearizes the whole OID DB into the format described
   above. The string can be written for example to the display or to
   a file. */
unsigned char *
ssh_asn1_oiddb_linearize(size_t *return_length);

/* These definitions are used to control the merging operation. */
typedef enum
{
  /* Allow parameter (i.e. value) overwriting. */
  SSH_ASN1_OIDDB_PARAM_OVERWRITE,
  /* Ignore all parameters, and no parameters are thus written (nor
     added) to the database. */
  SSH_ASN1_OIDDB_PARAM_IGNORE,
  /* Allow name overwriting, which in practice means a name that points
     to a one OID before the merging can point to a different OID after
     the merging operation. */
  SSH_ASN1_OIDDB_NAME_OVERWRITE,
  /* Ignore names and thus no new names are introduces to the database. */
  SSH_ASN1_OIDDB_NAME_IGNORE,
  /* This definition allows addition of new OIDs only, e.g. such OIDs that
     were not in the database before. */
  SSH_ASN1_OIDDB_OID_ONLY_NEW,

  /* End the format list. This must end the vararg list. */
  SSH_ASN1_OIDDB_END
} SshAsn1OidDBFmt;

/* Merge a buffer ("buf") to the current OID database. Use the
   definitions above to control the merging. By default no params nor
   names are overwritten, but all new information is added to the
   database.

   The vararg list must end in SSH_ASN1_OIDDB_END, even if no other
   definitions are used.

   This function reads the above (script) format exclusively. */
void ssh_asn1_oiddb_merge(const unsigned char *buf,
                          size_t buf_length,
                          ...);

/***** Miscellaneous routines. */

/* This function produces a nicely formatted output string which can
   be used to visualize the OID. It uses the format

   { id(component) ... }

   where "id" is taken from the parameter field "std-body" and the
   component is the OID component. This format is not understood by
   this library, so you cannot give such OID as arguments. */
char *
ssh_asn1_oiddb_get_pretty_oid(SshAsn1Oid ref_oid);

/* Verify the OID database from test applications. */
void ssh_asn1_oiddb_verify(void);

/* The handle type used for enumeration */
typedef SshADTHandle SshAsn1OidDBEnumHandle;

/* Enumerate the children of an OID. If the oid is NULL, enum the
   childrens of the root. */
SshAsn1OidDBEnumHandle
ssh_asn1_oiddb_enum_children_start(SshAsn1Oid oid);

/* Enumerate the next oid */
SshAsn1OidDBEnumHandle
ssh_asn1_oiddb_enum_children_next(SshAsn1Oid oid,
                                  SshAsn1OidDBEnumHandle handle);

/* Get the oid based on the handle. */
SshAsn1Oid ssh_asn1_oiddb_enum_get(SshAsn1Oid oid,
                                   SshAsn1OidDBEnumHandle handle);

#endif /* SSHASN1_OID_H  */
