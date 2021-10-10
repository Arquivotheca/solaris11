/*
 *
 * Copyright 1999 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 *
 * Comments:
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "lber.h"
#include "ldap.h"
#include "ldap-private.h"
#include "ldap-int.h"

static int count_tokens(char *s);
static int isattrdescchar(char c);
static int read_next_token(char **s, LDAPsortkey **key);


int ldap_create_sort_control(LDAP *ld, LDAPsortkey **sortKeyList,
	const char ctl_iscritical, LDAPControl **ctrlp)
{
	BerElement *ber;
	int i, rc;

	if (NULL == ld) {
		return (LDAP_PARAM_ERROR);
	}

	if (sortKeyList == NULL || ctrlp == NULL) {
		ld->ld_errno = LDAP_PARAM_ERROR;
		return (LDAP_PARAM_ERROR);
	}

	/* create a ber package to hold the controlValue */
	if ((ber = alloc_ber_with_options(ld)) == NULLBER) {
		ld->ld_errno = LDAP_NO_MEMORY;
		return (LDAP_NO_MEMORY);
	}

	/* encode the start of the sequence of sequences into the ber */
	if (ber_printf(ber, "{") == -1) {
		goto encoding_error_exit;
	}

	/*
	 * the sort control value will be encoded as a sequence of sequences
	 * which are each encoded as one of the following: {s} or {sts} or
	 * {stb} or {ststb} since the orderingRule and reverseOrder flag are
	 * both optional
	 */
	for (i = 0; sortKeyList[i] != NULL; i++) {

		/* encode the attributeType into the ber */
		if (ber_printf(ber, "{s", (sortKeyList[i])->sk_attrtype)
		    == -1) {
			goto encoding_error_exit;
		}

		/* encode the optional orderingRule into the ber */
		if ((sortKeyList[i])->sk_matchruleoid != NULL) {
			if (ber_printf(ber, "ts", LDAP_TAG_SK_MATCHRULE,
			    (sortKeyList[i])->sk_matchruleoid)
			    == -1) {
				goto encoding_error_exit;
			}
		}

		/* Encode the optional reverseOrder flag into the ber. */
		/* If the flag is false, it should be absent. */
		if ((sortKeyList[i])->sk_reverseorder) {
			if (ber_printf(ber, "tb}", LDAP_TAG_SK_REVERSE,
			    (sortKeyList[i])->sk_reverseorder) == -1) {
				goto encoding_error_exit;
			}
		} else {
			if (ber_printf(ber, "}") == -1) {
				goto encoding_error_exit;
			}
		}
	}

	/* encode the end of the sequence of sequences into the ber */
	if (ber_printf(ber, "}") == -1) {
		goto encoding_error_exit;
	}

	rc = ldap_build_control(LDAP_CONTROL_SORTREQUEST, ber, 1,
	    ctl_iscritical, ctrlp);
	ld->ld_errno = rc;
	return (rc);

encoding_error_exit:
	ld->ld_errno = LDAP_ENCODING_ERROR;
	ber_free(ber, 1);
	return (LDAP_ENCODING_ERROR);
}


int ldap_parse_sort_control(LDAP *ld, LDAPControl **ctrlp,
	unsigned long *result, char **attribute)
{
	BerElement *ber;
	int i, foundSortControl;
	LDAPControl *sortCtrlp;
	unsigned int len;
	char *attr;
	int tag;

	if ((NULL == ld) || (result == NULL) ||
		(attribute == NULL)) {
	    return (LDAP_PARAM_ERROR);
	}


	/* find the sortControl in the list of controls if it exists */
	if (ctrlp == NULL) {
		ld->ld_errno = LDAP_CONTROL_NOT_FOUND;
		return (LDAP_CONTROL_NOT_FOUND);
	}
	foundSortControl = 0;
	for (i = 0; ((ctrlp[i] != NULL) && (!foundSortControl)); i++) {
		foundSortControl = !(strcmp(ctrlp[i]->ldctl_oid,
			LDAP_CONTROL_SORTRESPONSE));
	}
	if (!foundSortControl) {
		ld->ld_errno = LDAP_CONTROL_NOT_FOUND;
		return (LDAP_CONTROL_NOT_FOUND);
	} else {
		/* let local var point to the sortControl */
		sortCtrlp = ctrlp[i-1];
	}

	/*
	 * allocate a Ber element with the contents of the sort_control's
	 * struct berval
	 */
	if ((ber = ber_init(&sortCtrlp->ldctl_value)) == NULL) {
		ld->ld_errno = LDAP_NO_MEMORY;
		return (LDAP_NO_MEMORY);
	}

	/* decode the result from the Berelement */
	if (ber_scanf(ber, "{i", result) == LBER_ERROR) {
		ld->ld_errno = LDAP_DECODING_ERROR;
		ber_free(ber, 1);
		return (LDAP_DECODING_ERROR);
	}

	/*
	 * if the server returned one, decode the attribute from the Ber element
	 */
	if (ber_peek_tag(ber, &len) == LDAP_TAG_SR_ATTRTYPE) {
		if (ber_scanf(ber, "ta", &tag, &attr) == LBER_ERROR) {
			ld->ld_errno = LDAP_DECODING_ERROR;
			ber_free(ber, 1);
			return (LDAP_DECODING_ERROR);
		}
		*attribute = attr;
	} else {
		*attribute = NULL;
	}

	if (ber_scanf(ber, "}") == LBER_ERROR) {
		ld->ld_errno = LDAP_DECODING_ERROR;
		ber_free(ber, 1);
		return (LDAP_DECODING_ERROR);
	}

	/* the ber encoding is no longer needed */
	ber_free(ber, 1);
	return (LDAP_SUCCESS);
}


/*
 * Routines for the manipulation of string-representations of sort control
 * keylists
 */
static int count_tokens(char *s)
{
	int count = 0;
	char *p = s;
	int whitespace = 1;
	/*
	 * Loop along the string counting the number of times we see the
	 * beginning of non-whitespace. This tells us
	 * the number of tokens in the string
	 */
	while (*p != '\0') {
		if (whitespace) {
			if (!isspace(*p)) {
				whitespace = 0;
				count++;
			}
		} else {
			if (isspace(*p)) {
				whitespace = 1;
			}
		}
		p++;
	}
	return (count);
}


/* Is this character a valid attribute description character ? */
static int isattrdescchar(char c)
{
	/* Alphanumeric chars are in */
	if (isalnum(c)) {
		return (1);
	}
	/* As is ';' */
	if (';' == c) {
		return (1);
	}
	/* Everything else is out */
	return (0);
}


static int read_next_token(char **s, LDAPsortkey **key)
{
	char c = 0;
	char *pos = *s;
	int retval = 0;
	LDAPsortkey *new_key = NULL;

	char *matchrule_source = NULL;
	int matchrule_size = 0;
	char *attrdesc_source = NULL;
	int attrdesc_size = 0;
	int reverse = 0;

	int state = 0;

	while (((c = *pos++) != '\0') && (state != 4)) {
		switch (state) {
		case 0:
		/* case where we've not seen the beginning of the attr yet */
			/* If we still see whitespace, nothing to do */
			if (!isspace(c)) {
				/* Otherwise, something to look at */
				/* Is it a minus sign ? */
				if ('-' == c) {
					reverse = 1;
				} else {
					attrdesc_source = pos - 1;
					state = 1;
				}
			}
			break;
		case 1:
		/*
		 * case where we've seen the beginning of the attr, but not
		 * the end
		 */
			/* Is this char either whitespace or a ':' ? */
			if (isspace(c) || (':' == c)) {
				attrdesc_size = (pos - attrdesc_source) - 1;
				if (':' == c) {
					state = 2;
				} else {
					state = 4;
				}
			}
			break;
		case 2:
		/*
		 * case where we've seen the end of the attr and want the
		 * beginning of match rule
		 */
			if (!isspace(c)) {
				matchrule_source = pos - 1;
				state = 3;
			} else {
				state = 4;
			}
			break;
		case 3:
		/*
		 * case where we've seen the beginning of match rule and
		 * want to find the end
		 */
			if (isspace(c)) {
				matchrule_size = (pos - matchrule_source) - 1;
				state = 4;
			}
			break;
		default:
			break;
		}
	}

	if (3 == state) {
		/*
		 * means we fell off the end of the string looking for the
		 * end of the marching rule
		 */
		matchrule_size = (pos - matchrule_source) - 1;
	}

	if (1 == state) {
		/*
		 * means we fell of the end of the string looking for the
		 * end of the attribute
		 */
		attrdesc_size = (pos - attrdesc_source) - 1;
	}

	if (NULL == attrdesc_source)  {
		/* Didn't find anything */
		return (-1);
	}

	new_key = (LDAPsortkey*)malloc(sizeof (LDAPsortkey));
	if (0 == new_key) {
		return (LDAP_NO_MEMORY);
	}

	/* Allocate the strings */
	new_key->sk_attrtype = (char *)malloc(attrdesc_size + 1);
	if (NULL != matchrule_source) {
		new_key->sk_matchruleoid = (char *)malloc(matchrule_size + 1);
	} else {
		new_key->sk_matchruleoid = NULL;
	}
	/* Copy over the strings */
	memcpy(new_key->sk_attrtype, attrdesc_source, attrdesc_size);
	*(new_key->sk_attrtype + attrdesc_size) = '\0';
	if (NULL != matchrule_source) {
		memcpy(new_key->sk_matchruleoid, matchrule_source,
			matchrule_size);
		*(new_key->sk_matchruleoid + matchrule_size) = '\0';
	}

	new_key->sk_reverseorder = reverse;

	*s = pos - 1;
	*key = new_key;
	return (retval);
}


int
ldap_create_sort_keylist(LDAPsortkey ***sortKeyList, char *string_rep)
{
	int count = 0;
	LDAPsortkey **pointer_array = NULL;
	char *current_position = NULL;
	char *s = NULL;
	int retval = 0;
	int i = 0;

	/* Figure out how many there are */
	if (NULL == string_rep) {
		return (LDAP_PARAM_ERROR);
	}
	if (NULL == sortKeyList) {
		return (LDAP_PARAM_ERROR);
	}
	count = count_tokens(string_rep);
	if (0 == count) {
		*sortKeyList = NULL;
		return (LDAP_PARAM_ERROR);
	}
	/* Allocate enough memory for the pointers */
	pointer_array = (LDAPsortkey**)malloc(sizeof (LDAPsortkey*)
	    * (count + 1));
	if (NULL == pointer_array) {
		return (LDAP_NO_MEMORY);
	}
	/*
	 * Now walk along the string, allocating and filling in the
	 * LDAPsearchkey structure
	 */
	current_position = string_rep;

	for (i = 0; i < count; i++) {
		if (0 != (retval = read_next_token(&current_position,
			&(pointer_array[i])))) {
			pointer_array[count] = NULL;
			ldap_free_sort_keylist(pointer_array);
			*sortKeyList = NULL;
			return (retval);
		}
	}
	pointer_array[count] = NULL;
	*sortKeyList = pointer_array;
	return (LDAP_SUCCESS);
}


void
ldap_free_sort_keylist(LDAPsortkey **sortKeyList)
{
	LDAPsortkey *this_one = NULL;
	int i = 0;

	if (NULL == sortKeyList) {
		return;
	}

	/* Walk down the list freeing the LDAPsortkey structures */
	for (this_one = sortKeyList[0]; this_one;
		this_one = sortKeyList[++i]) {
		/* Free the strings, if present */
		if (NULL != this_one->sk_attrtype) {
			free(this_one->sk_attrtype);
		}
		if (NULL != this_one->sk_matchruleoid) {
			free(this_one->sk_matchruleoid);
		}
		free(this_one);
	}
	/* Free the pointer list */
	free(sortKeyList);
}
