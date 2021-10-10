#! /usr/bin/ksh
#
# vgrind
# Copyright (c) 1999-2000 by Sun Microsystems, Inc.
# All rights reserved.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
#       This is a rewrite in ksh of the command originally written in
#       csh whose last incarnation was:
#       vgrind.csh 1.16 96/10/14 SMI; from UCB 5.3 (Berkeley) 11/13/85
#


# Definitions the user can override

troff=${TROFF:-/usr/bin/troff}
vfontedpr=${VFONTEDPR:-/usr/lib/vfontedpr}
macros=${TMAC_VGRIND:-/usr/share/lib/tmac/tmac.vgrind}
lp=${LP:-/usr/bin/lp}

# Internal processing of options

dpost=/usr/lib/lp/postscript/dpost

args=""
dpostopts="-e 2"
files=""
lpopts=""
troffopts="-t"

filter=0
uselp=1
usedpost=1
stdoutisatty=0

pspec=0
tspec=0
twospec=0

printer=""

if [ -t 1 ] ; then
  stdoutisatty=1
fi

# Process command line options

while getopts ":2d:fh:l:no:P:s:tT:wWx" opt ; do
  case "$opt" in
    +*)
      /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: bad option %s'`\n" "+$opt" >&2
      exit 1
      ;;
    "?")
      /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: bad option %s'`\n" "-$OPTARG" >&2
      exit 1
      ;;
    2)
      dpostopts="$dpostopts -p l"
      usedpost=1
      args="$args -2"
      twospec=1
      ;;
    d)
      args="$args -d $OPTARG"
      if ! [ -r $OPTARG ] ; then
        /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: -%s %s: file not readable'`\n" "$opt" "$OPTARG" >&2
        exit 1
      fi
      ;;
    f)
      filter=1
      args="$args -f"
      ;;
    h)
      args="$args -h '$OPTARG'"
      ;;
    l)
      args="$args -l$OPTARG"
      ;;
    n)
      args="$args -$opt"
      ;;
    o)
      troffopts="$troffopts -o$OPTARG"
      ;;
    P)
      uselp=1
      usedpost=1
      printer=$OPTARG
      pspec=1
      ;;
    s)
      args="$args -s$OPTARG"
      ;;
    T)
      troffopts="$troffopts -T$OPTARG"
      ;;
    t)
      uselp=0
      usedpost=0
      tspec=1
      ;;
    W)
      # Do nothing with this switch
      ;;
    w)
      args="$args -t"
      ;;
    x)
      args="$args -x"
      ;;
    *)
      troffopts="$troffopts -$opt"
      ;;
  esac

  if [ "$opt" = ":" ] ; then
    /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: missing argument to option %s'` \n" "-$OPTARG" >&2
    exit 1
  fi
done

shift $((OPTIND - 1))

for x in "$@" ; do
  args="$args '$x'"
  files="$files '$x'"
done

shift $#

if [ $filter -eq 1 -a \( $twospec -eq 1 -o $pspec -eq 1 \) ] ; then
  /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: option -f is incompatible with -2 and -P'`\n" >&2
  exit 1
fi

if [ $filter -eq 1 -a $tspec -eq 1 ] ; then
  /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: option -f is incompatible with -t'`\n" >&2
  exit 1
fi

if [ $tspec -eq 1 -a \( $twospec -eq 1 -o $pspec -eq 1 \) ] ; then
  /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: option -t is incompatible with -2 and -P'`\n" >&2
  exit 1
fi

# Do some reasoning about whether to print

if [ $uselp -eq 1 ] ; then
  # If we want to print

  if [ -z "$printer" ] ; then
    # If "-P" was not specified

    defaultprinter=`LC_ALL=C /usr/bin/lpstat -d | /usr/bin/sed -e "s/no system default destination//" 2>/dev/null`

    if [ -n "$defaultprinter" ] ; then
      defaultprinter=`echo $defaultprinter | \
        /usr/bin/sed -e "s/system default destination: //" 2>/dev/null`
    fi

    if [ $stdoutisatty -eq 1 ] ; then
      # If stdout is not re-directed

      if [ -z "$defaultprinter" ] ; then
        uselp=0
      else
        printer=$defaultprinter
      fi
    else
      # stdout is redirected - assume it is for further processing of
      # postscript output.

      uselp=0
    fi
  fi
fi

if [ $uselp -eq 1 ] ; then
  case $(/usr/bin/basename $lp) in
   lp)
     lpopts="$lpopts -d$printer"
     ;;
   lpr)
     lpopts="$lpopts -P$printer"
     ;;
   *)
     /usr/bin/printf "`/usr/bin/gettext TEXT_DOMAIN 'vgrind: unknown print program %s'`\n" $lp >&2
     exit 1
     ;;
  esac
fi

# Implementation note:  In the following, we use "eval" to execute the
# command in order to preserve spaces which may appear in the -h option
# argument (and possibly in file names).

if [ $filter -eq 1 ] ; then
  eval "$vfontedpr $args | /usr/bin/cat $macros -"
else
  cmd="$vfontedpr $args"

  if [ -r index ] ; then
    # Removes any entries from the index that come from the files we are
    # processing.

    echo > nindex

    for i in "$files" ; do
      echo "? $i ?d" | /usr/bin/sed -e "s:/:\\\/:g" -e "s:?:/:g" >> nindex
    done

    /usr/bin/sed -f nindex index > xindex

    # Now process the input.
    # (! [$filter -eq 1])

    trap "rm -f xindex nindex; exit 1" INT QUIT

    cmd="$cmd | $troff -rx1 $troffopts -i $macros - 2>> xindex"

    if [ $usedpost -eq 1 ] ; then
      cmd="$cmd | $dpost $dpostopts"
    fi

    if [ $uselp -eq 1 ] ; then
      cmd="$cmd | $lp $lpopts"
    fi

    eval $cmd
    trap - INT QUIT
    /usr/bin/sort -dfu +0 -2 xindex > index
    /usr/bin/rm nindex xindex
  else
    # (! [ -r index ])

    cmd="$cmd | $troff -i $troffopts $macros -"

    if [ $usedpost -eq 1 ] ; then
      cmd="$cmd | $dpost $dpostopts"
    fi

    if [ $uselp -eq 1 ] ; then
        cmd="$cmd | $lp $lpopts"
    fi

    eval $cmd
  fi
fi
