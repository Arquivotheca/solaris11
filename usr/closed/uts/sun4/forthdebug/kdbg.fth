\
\ Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
\ Use is subject to license terms.
\
\ CDDL HEADER START
\
\ The contents of this file are subject to the terms of the
\ Common Development and Distribution License (the "License").
\ You may not use this file except in compliance with the License.
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

h# 7ff constant v9bias
h# modules-val-here value modules-val
h# primaries-v-here value primaries-val

0 value exit-behavior

\ enable forthdebug when entering interpreter
' kdbg-words is debugger-vocabulary-hook

: next-word ( alf voc-acf -- false | alf' true )
   over  if  drop  else  nip >threads  then
   another-link?  if  >link true  else  false  then
;

\ another? that allows nesting
: another? ( alf voc-acf -- false | alf' voc-acf anf true )
   dup >r next-word  if         ( alf' ) ( r: voc-acf )
      r> over l>name true       ( alf' voc-acf anf true )
   else                         ( ) ( r: voc-acf )
      r> drop false             ( false )
   then
;


create err-no-sym ," symbol not found"

\ guard against bad symbols
: $symbol ( adr,len -- x )
   $handle-literal? 0= if  err-no-sym throw  then
;

\ Compile the value of the symbol if known,
\ otherwise arrange to look it up later at run time.
: symbol ( -- n ) \ symbol-name
   parse-word 2dup 2>r $handle-literal?  if   ( r: sym$ )
      2r> 2drop
   else
      +level
      2r> compile (") ", compile $symbol
      -level
   then
; immediate


\ print in octal
: .o ( n -- ) base @ >r octal . r> base ! ;

\ redefine type, cstrlen & cscount to support 64 bit addresses
: type ( adr len -- ) bounds ?do i c@ emit loop ;

: cstrlen ( cstr -- len )
   dup begin
      dup c@
   while
      char+
   repeat swap -
;

: cscount ( cstr -- adr,len )  dup cstrlen  ;


\ add carriage return if found linefeed
: lf-type ( adr len -- )
   bounds ?do
      i c@
      dup linefeed = if
         cr drop
      else
         emit
      then
   loop
;

\ print at most cnt characters of a string
: .nstr ( str cnt -- )
   over if
      over cscount nip min
      bounds ?do i c@ dup 20 80 within if emit else drop then loop
   else
      ." NULL " 2drop
   then
;

\ print string
: .str ( str -- )
   ?dup  if
      cscount type
   else
      ." NULL"
   then
;

\ new actions
: print 2 perform-action ;
: index 3 perform-action ;
: sizeof 1 perform-action ;

\ indent control
-8 value plevel
: +plevel ( -- ) plevel 8 + to plevel ;
: -plevel ( -- ) plevel 8 - to plevel ;
: 0plevel ( -- ) -8 to plevel ;

defer print-offset
' noop is print-offset

: show-offset ( apf -- apf ) ." [ " dup @ . ." ] " ;
: offset-on ['] show-offset is print-offset ;
: offset-off ['] noop is print-offset ;

\ new print words
: name-print ( addr apf -- addr apf )
   print-offset
   plevel spaces dup body>
   .name ." = "
;

: voc-print ( addr acf -- )
   ??cr +plevel
   0 swap                         ( addr 0 acf )
   begin  another?  while         ( addr alf acf anf )
      3 pick swap name> print     ( addr alf acf )
      exit?  if                   ( addr alf acf )
         0plevel true throw       ( )
      then                        ( addr alf acf )
   repeat                         ( addr )
   drop -plevel                   ( )
;


3 actions ( offset print-acf )
action: ( addr apf -- x )       @ + x@ ;        \ get
action: ( addr x apf -- )       @ rot + x! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + x@ swap         ( x apf )
   na1+ @ execute cr ;                          \ print

: ext-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- l )       @ + l@ ;        \ get
action: ( addr l apf -- )       @ rot + l! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + l@ swap          ( l apf )
   na1+ @ execute cr ;                          \ print

: long-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- w )       @ + w@ ;        \ get
action: ( addr w apf -- )       @ rot + w! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + w@ swap          ( w apf )
   na1+ @ execute cr ;                          \ print

: short-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- c )       @ + c@ ;        \ get
action: ( addr c apf -- )       @ rot + c! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + c@ swap          ( c apf )
   na1+ @ execute cr ;                          \ print

: byte-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- ptr )     @ + p@ ;        \ get
action: ( addr l apf -- )       @ rot + p! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + p@ ?dup  if     ( apf ptr )
      swap na1+ @ execute      ( )
   else                        ( apf )
      drop ." NULL"            ( )
   then                        ( )
   cr ;                                         \ print
 
: ptr-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- saddr )   @ + ;           \ get
action: ( -- )                  quit ;          \ error
action: ( addr apf -- )
   name-print
   dup @ rot + swap             ( saddr apf )
   na1+ @ execute ??cr ;                       \ print
 
: struct-field ( acf offset -- ) create , , use-actions ;


4 actions ( offset inc limit print-acf fetch-acf )
action: ( addr apf -- araddr )  @ + ;           \ get
action: ( -- )                  quit ;          \ set
action: ( addr apf -- )
   name-print
   dup @ rot + swap         ( base apf )
   na1+ dup @ -rot          ( inc base apf' )
   na1+ dup @ swap          ( inc base limit apf' )
   na1+ dup @ swap          ( inc base limit p-acf apf' )
   na1+ @ 2swap             ( inc p-acf f-acf base limit )
   bounds  do               ( inc p-acf f-acf )
      3dup                  ( inc p-acf f-acf inc p-acf f-acf )
      i swap execute        ( inc f-acf p-acf inc p-acf n )
      swap execute          ( inc f-acf p-acf inc )
   +loop                    ( inc f-acf p-acf )
   3drop ??cr ;                                 \ print
action: ( addr index apf -- ith-item )
   rot swap                 ( index addr apf )
   dup @ rot + swap         ( index base apf )
   na1+ dup @ 3 roll *      ( base apf' ioff )
   rot + swap 3 na+ @       ( iaddr f-acf )
   execute ;                                    \ index

: array-field ( f-acf p-acf limit inc offset -- ) create , , , , , use-actions ;


3 actions ( offset mask shift print-acf )
action: ( addr apf -- bits )
   dup @ rot + l@ swap         ( b-word apf )
   na1+ dup @ rot and swap     ( b-masked apf' )
   na1+ @ >> ;                               \ get
action: ( addr bits apf -- )
   rot over @ + dup l@ 2swap   ( b-addr b-word nbits apf )
   na1+ dup @ -rot             ( b-addr b-word mask nbits apf' )
   na1+ @ << over and          ( b-addr b-word mask nb-masked )
   -rot invert and or swap l! ;              \ set
action: ( addr apf -- )
   name-print
   dup @ rot + l@ swap         ( b-word apf )
   na1+ dup @ rot and swap     ( b-mask apf' )
   na1+ dup @ rot swap >> swap ( bits apf' )
   na1+ @ execute cr ;                       \ print

: bits-field ( acf shift mask offset -- ) create , , , , use-actions ;


2 actions ( voc-acf size )
action: ( apf -- )              @ voc-print ;   \ print vocabulary
action: ( apf -- size )         na1+ @ ;        \ sizeof

: c-struct ( size acf -- ) create , , use-actions ;

: c-enum ( {str value}+ n-values -- )
   create   ( n-values {value str}+ )
      dup 2* 1+ 0  do  ,  loop
   does>    ( enum apf -- )
      dup @ 0  do                     ( enum apf' )
         na1+ 2dup @ =  if            ( enum apf' )
            na1+ @ .str               ( enum )
            drop unloop exit          ( )
         then                         ( enum apf' )
         na1+                         ( enum apf' )
      loop                            ( enum apf' )
      drop .d cr                      ( )
;

\ end kdbg section

