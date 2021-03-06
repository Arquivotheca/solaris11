#!/usr/bin/ksh93
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
#
# Description: To decompress the driver package
#              if it's compressed.
#
#              Usage:
#              pkg_check.sh pkg_loc
#              $1 is the package location.

PATH=/usr/bin:/usr/sbin:$PATH; export PATH
LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/usr/ddu/lib; export LD_LIBRARY_PATH
cd $(dirname $0)
typeset -r base_dir=$(pwd)
typeset -r platform=$(uname -p)

typeset    dc_dir=/tmp/dc.$$
typeset    f_name=${dc_dir}/$(basename $1)
typeset -r file_type=("ZIP archive" "bzip2" "gzip" "compressed data" "tar archive")
typeset -r action=("unzip" "bunzip2" "gzip -d" "compress -d" "tar -xvf") 
typeset    pkg_type=SVR4


#
# Check each file in the current directory tree.  If one or more 
# files do not match any filetype in file_type, return 1.  
# Otherwise,  return 0.
#
function f_r_file #find regular file
{
    typeset    flag=0 #0: Not found 1:Found
    typeset    num=0
    typeset    s_str

    while [ ! -z ${file_type[$num]} ]; do
        if [ -z $s_str ]; then    
            s_str=${file_type[$num]}
        else
            s_str=${s_str}"|"${file_type[$num]}
        fi
        num=$(($num + 1))
    done
    for f_i in $(/usr/bin/find . -type f); do
        file -m ${magic_file} $f_i | egrep "$s_str" >/dev/null 2>&1
        if [ $? -ne 0 ]; then
            flag=1
            break
        fi
    done
    return $flag
}

#
# Decompress those files which file type are in the $file_type variable
#
function decompress
{
    typeset  i j
    
    mkdir -p $dc_dir
    cd $dc_dir
    cp $1 .
    i=0
    #
    # Loop trying different kinds of decompression, until the relevant 
    # decompression type is found.  We know relevant decompression type 
    # was found when we start seeing regular files.
    #
    while [ ! -z ${file_type[$i]} ]; do
        file -m ${magic_file} $f_name | grep "${file_type[$i]}" >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            ${action[$i]} $f_name 
            if [ $? -eq 0 ]; then
                if [ -f $f_name ]; then
                    rm -f $f_name 
                fi
                f_r_file
                if [ "$?" == "1" ]; then
                    #
                    # 1 means no compressed file found after first 
                    # decompression.So the decompression finishes.
                    #
                    return 0
                else
                    #
                    # After one round of decompression, only one compressed 
                    # file should exist (for example, file driver.tar.gz).  
                    # After gunzip command, only driver.tar should exist.  
                    # If multiple compressed file exists, then this will confuse 
                    # the program.  (Why is there more than 1 compressed file: 
                    # are there two drivers?)   Thus the program takes >1 
                    # compressed file as a abnormal case.
                    #
                    if [ $(find $dc_dir -type f | wc -l | awk '{print $1}') -ne 1 ]; then
                        #error
                        print -u2 "$0: Error: the number of regular files is " \
                            not equal to 1."
                        return 1
                    fi
                    f_name=$(find $dc_dir -type f)
                    mkdir -p ${dc_dir}_1
                    cd ${dc_dir}_1
                    dc_dir=${dc_dir}_1
                    j=0
                    while [ ! -z ${file_type[$j]} ]; do
                        file -m ${magic_file} $f_name \
                            | grep "${file_type[$i]}" >/dev/null 2>&1
                        if [ $? -eq 0 ]; then
                            ${action[$i]} $f_name 
                            if [ $? -eq 0 ]; then
                                return 0
                            else
                                return 1
                            fi
                        fi
                        j=$(($j + 1))
                    done
                fi
            else
                return 1
            fi
        fi
        i=$(($i + 1))
    done
    #Not a compressed file
    cp $1 .
}

#
# After decompress the file, check for SVR4 package 
# if it's a SVR4 package, output the file name,
# otherwise exit with 1.
#
function pkg_type
{
    #check for svr4
    found=0 #0: Not found 1:Found
    cd $dc_dir
    for i in $(/usr/bin/find $dc_dir 2>/dev/null); do
        if [ -f "$i" ]; then
            pkginfo -d $i >/dev/null 2>&1 
            if [ $? -eq 0 ]; then #pkg datastream

                #pkgchk
                
                if [ ! -f $i ]; then
                    pkgchk -f -d $i >/dev/null 2>&1 <<EOF

EOF
                fi
                if [ $? -eq 0 ]; then
                    found=1
                    print -u1 "$pkg_type|$i"
                fi
            fi
        else
            if [ -d "$i" ]; then
                path=$(dirname $i)
                f_name=$(basename $i)
                pkginfo -d $path $f_name >/dev/null 2>&1 
                if [ $? -eq 0 ]; then
                    #pkgchk
                    pkgchk -f -d $path $f_name >/dev/null 2>&1
                    if [ $? -eq 0 ]; then
                        print -u1 "$pkg_type|$i"
                        found=1
                    fi    
                fi
            fi
                

        fi
    done
    if [ $found -eq 0 ]; then
        exit 1
    fi
}

#######################################################################
# clean_up
#       This function attempts to clean up any resources this script
#       could generate. Depending on where in the script this function
#       is involved some resources may not be there to cleanup, but
#       that will not adversely effect anything.
#
#       This function is not defined using the function keyword
#       to avoid an exit loop.
#
# Input: none
# Returns: none
#
#######################################################################
clean_up ()
{
    {
        if [ -s $dc_dir ] && [ $dc_dir != "/tmp/dc.$$" ]; then
            rm -rf /tmp/dc.$$
        fi
    } >/dev/null 2>&1
}


#Main()

trap 'clean_up;exit 10' KILL INT
trap 'clean_up' EXIT

typeset magic_file r_code

#
# Generate ddu magic file to add more entries
#
cat /etc/magic ddu_magic > ${base_dir}/magic
magic_file=${base_dir}/magic

decompress $1
r_code=$?
if [ $r_code -ne  0 ]; then
    exit $r_code
fi
pkg_type
