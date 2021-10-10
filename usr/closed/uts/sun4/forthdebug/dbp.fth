
\ Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
\ Use is subject to license terms.
\
\ CDDL HEADER START
\
\ The contents of this file are subject to the terms of the
\ Common Development and Distribution License, Version 1.0 only
\ (the "License").  You may not use this file except in compliance
\ with the License.
\
\ You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
\ or http://www.opensolaris.org/os/licensing.
\ See the License for the specific language governing permissions
\ and limitations under the License.
\
\ When distributing Covered Code, include this CDDL HEADER in each
\ file and include the License file at usr/src/OPENSOLARIS.LICENSE.
\ If applicable, add the following below this CDDL HEADER, with the
\ fields enclosed by brackets "[]" replaced with your own identifying
\ information: Portions Copyright [yyyy] [name of copyright owner]
\
\ CDDL HEADER END
\
\ #pragma ident	"%Z%%M%	%I%	%E% SMI"

\ start dbp section

20 constant max#defer-bp
0 value #defer-bp
d# 80 value /defer-bp           \ up to 80 chars for each defer bp symbol
max#defer-bp /defer-bp * buffer: >defer-bp-sym
max#defer-bp /n* buffer: >defer-bp-len
max#defer-bp /n* buffer: >defer-bp-off
max#defer-bp /n* buffer: >defer-bp-adr

: 'dbp-len ( index -- adr ) >defer-bp-len swap na+ ;
: 'dbp-off ( index -- adr ) >defer-bp-off swap na+ ;
: 'dbp-adr ( index -- adr ) >defer-bp-adr swap na+ ;
: 'dbp-sym ( index -- adr ) /defer-bp * >defer-bp-sym + ;

: dbp-len@ ( index -- len ) 'dbp-len @ ;
: dbp-off@ ( index -- off ) 'dbp-off @ ;
: dbp-adr@ ( index -- adr ) 'dbp-adr @ ;

: dbp-len! ( val index -- ) 'dbp-len ! ;
: dbp-off! ( val index -- ) 'dbp-off ! ;
: dbp-adr! ( val index -- ) 'dbp-adr ! ;

: fill0 ( adr len -- ) dup if bounds do 0 i c! loop then ;

: defer-sym>buf ( sym$ -- )
   tuck #defer-bp 'dbp-sym      ( len sym$ buf-addr )
   dup >r swap move r>          ( len buf-addr )
   over + swap /defer-bp swap - ( buf-addr+len 80-len )
   fill0
;

\ overwrite "index" entry with "index+1" entry content
: dbp-shift ( index -- )
   dup 1+ dbp-len@ over dbp-len!  ( index )
   dup 1+ dbp-off@ over dbp-off!  ( index )
   dup 1+ dbp-adr@ over dbp-adr!  ( index )
   'dbp-sym dup /defer-bp +  ( dst-adr src-adr )
   swap /defer-bp move
;

\ try to add break point if the symbol is already there
: dbp-+bp? ( off sym$ -- off sym$ adr+off|0 )
   2dup 2>r                     ( off sym$ ) ( R: sym$ )
   $handle-literal?             ( off adr true | off false )
   if                           ( off adr )
      over + dup +bp            ( off adr+off )
   else                         ( off )
      0                         ( off 0 )
   then
   2r> rot                      ( off sym$ adr+off ) ( R: )
;

: +dbp ( off sym$ -- )
   #defer-bp max#defer-bp >= if
      ." too many defer break points" cr
      2drop drop exit
   then

   dup /defer-bp > if                     ( off sym$ )
      ." symbol too long" cr
      2drop drop exit
   then

   \ add breakpoint for resolvable symbols
   dbp-+bp? #defer-bp dbp-adr!            ( off sym$ )
   rot      #defer-bp dbp-off!            ( sym$ )

   \ copy symbol to buffer
   tuck defer-sym>buf                     ( len )
   #defer-bp dbp-len!                     ( )

   \ adjust array index
   #defer-bp 1+ to #defer-bp              ( off sym$ )
;

: .dbp ( -- )
   #defer-bp dup .d ." defer break points" cr
   ."  "#  address  off len symbol" cr
   0 ?do
      i 2 .r ."  "
      i dbp-adr@ 8 u.r ."  "
      i dbp-off@ 4  .r ."  "
      i dbp-len@ dup 3 .r ."  "           ( len )
      i 'dbp-sym swap type cr
   loop
;

: dbp-scrub-last ( -- )
   #defer-bp 0> if
      #defer-bp 1- dup to #defer-bp       ( #defer-bp-1 )
      'dbp-sym /defer-bp fill0
      0 #defer-bp dbp-len!
      0 #defer-bp dbp-off!
      0 #defer-bp dbp-adr!
   then
;

: --dbp ( -- )
   #defer-bp 0> if
      #defer-bp 1- dbp-adr@ -bp
      dbp-scrub-last
   then
;

: -dbp  ( index -- )
   dup #defer-bp >= if                    ( index )
      ." bad break point index " . exit
   then                                   ( index )
   dup dbp-adr@ -bp                       ( index )
   #defer-bp 1- swap ?do                  ( #defer-bp-1 index )
      i dbp-shift
   loop
   dbp-scrub-last
;

: valid-sym?  ( index -- adr true | false )
   dup 'dbp-sym swap dbp-len@ sym>value
   if true else 2drop false then
;

: dbp-enable  ( bp-virt index -- ) over swap dbp-adr! +bp ;
: dbp-disable ( index -- ) dup dbp-adr@ -bp 0 swap dbp-adr! ;

: dbp-refresh ( -- )
   #defer-bp 0 ?do
      i valid-sym? if        ( adr )
         i dbp-adr@ if drop  ( )
         else                ( adr )      \ found newly resolved symbol
            i dbp-off@ +     ( adr+off )
            dup reloc? if    ( adr+off )  \ has the module been relocated?
               i dbp-enable  ( )          \ set new break point
            else             ( adr+off )
               drop          ( )
            then             ( )
         then                ( )
      else                   ( )
         i dbp-adr@ if       ( )          \ old symbol now becomes invalid
            i dbp-disable    ( )          \ clear break point
         then
      then
   loop
;

\ frontend interface for +dbp
: dbp   \ symbol+off   ( -- )
   parse-word			( symbol+off$ )
   ascii + left-parse-string	( off$ symbol$ )
   2swap $number if 0 then	( symbol$ off|0 )
   dup 3 and 0<> if		( symbol$ off|0 )
      ." warning: offset not aligned" cr
   then
   -rot +dbp			( )
;

\ resolves potential new breakpoints from the new module or deletes
\ breakpoints from theunloaded module and returns.
: dbp-kobj-hook ( -- ) dbp-refresh go ;

\ : dbp-install ( -- )
\    ['] noop 0 ['] ' 0    ['] +bpx (patch)
\    symbol fdbp_hook ['] dbp-kobj-hook +bpx	\ +bpx ( bp-adr bp-action -- )
\    \ test obp cpu sync kernel return fix
\    \ symbol kobj_notify_load ['] dbp-kobj-hook +bpx
\    ['] ' 0    ['] noop 0 ['] +bpx (patch)
\ ;

\ fdbp_hook: a kernel null function called upon each module load/unload.
: dbp-install " symbol fdbp_hook +bpx dbp-kobj-hook" eval ;

: dbp?	( -- )
   ." dbp?			- help, display defer break point words"    cr
   ." dbp pci_attach+4	- add defer break point at pci_attach + 4"  cr
   ." dbp pcisch:pci_attach+4	- same as above, except from pcisch module" cr
   ." .dbp			- display all requested defer break points" cr
   ." 3 -dbp			- remove defer break point #3 (from .dbp)"  cr
   ." --dbp			- remove the very last defer break point"   cr
;

\ end dbp section

