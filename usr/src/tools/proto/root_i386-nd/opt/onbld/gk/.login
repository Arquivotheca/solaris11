#
# 
#
#
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
# 
# example .login for root build user (often 'gk').
# sets up for potential dmake use in parallel-make mode

unset ignoreeof
umask 002
stty erase  werase  kill  intr 

setenv NIS_PATH 'org_dir.$:$'
setenv EDITOR /usr/bin/vi
setenv MACH `uname -p`

set noglob; eval `/usr/ucb/tset -Q -s -e -k - -m dialup:vt102`; unset noglob
setenv MANPATH /usr/man:/usr/local/man:/usr/local/doctools/man:/opt/onbld/man
setenv DMAKE_MODE parallel
set hostname=`uname -n`
if ( ! -f ~/.make.machines ) then
	set maxjobs=4
else
	set maxjobs="`grep $hostname ~/.make.machines | tail -1 | awk -F= '{print $ 2;}'`"
	if ( "$maxjobs" == "" ) then
		set maxjobs=4
	endif
endif
setenv DMAKE_MAX_JOBS $maxjobs


set path=( \
	/opt/onbld/bin \
	/opt/onbld/bin/${MACH} \
	/opt/SUNWspro/bin \
	/opt/teamware/bin \
	/usr/proc/bin \
	/usr/openwin/bin \
	/bin \
	/usr/bin \
	/usr/sbin \
	/sbin \
	/usr/local/bin \
	/usr/ucb \
	/etc \
)
