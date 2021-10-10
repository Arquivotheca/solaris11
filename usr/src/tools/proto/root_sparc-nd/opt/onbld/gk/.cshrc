#
# 
#
#
# Copyright (c) 1993-1998 by Sun Microsystems, Inc.
# All rights reserved.
# 
#ident	"%Z%%M%	%I%	%E% SMI"
#
# example .cshrc for root build user (often 'gk').

set filec
set history=100
set noclobber
set ignoreeof
set notify

unset nse1
unset nse2

umask 002

# if ($?USER == 0 || $?prompt == 0) exit

alias ls "ls -aF"

if ( ! $?HOSTNAME ) then
	setenv HOSTNAME `uname -n`
endif

if ( ! $?TERM ) then
	setenv TERM sun
endif

set prompt="{${USER}:${HOSTNAME}:\!} "
