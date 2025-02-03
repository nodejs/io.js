#!/bin/bash
# Copyright 2020-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").
# You may not use this file except in compliance with the License.
# You can obtain a copy in the file LICENSE in the source distribution
# or at https://www.openssl.org/source/license.html
#
# This script is a wrapper around check-format.pl.  It accepts a commit sha
# value as input, and uses it to identify the files and ranges that were
# changed in that commit, filtering check-format.pl output only to lines that
# fall into the commits change ranges.
#


# List of Regexes to use when running check-format.pl.
# Style checks don't apply to any of these
EXCLUDED_FILE_REGEX=("\.pod" \
                     "\.pl"  \
                     "\.pm"  \
                     "\.t"   \
                     "\.yml" \
                     "\.sh")

# Exit code for the script
EXIT_CODE=0

# Global vars

# TEMPDIR is used to hold any files this script creates
# And is cleaned on EXIT with a trap function
TEMPDIR=$(mktemp -d /tmp/checkformat.XXXXXX)

# TOPDIR always points to the root of the git tree we are working in
# used to locate the check-format.pl script
TOPDIR=$(git rev-parse --show-toplevel)


# cleanup handler function, returns us to the root of the git tree
# and erases our temp directory
cleanup() {
    rm -rf $TEMPDIR
    cd $TOPDIR
}

trap cleanup EXIT

# Get the canonical sha256 sum for the commit we are checking
# This lets us pass in symbolic ref names like master/etc and 
# resolve them to sha256 sums easily
COMMIT=$(git rev-parse $1)

# Fail gracefully if git rev-parse doesn't produce a valid
# commit
if [ $? -ne 0 ]
then
    echo "$1 is not a valid revision"
    exit 1
fi

# Create a iteratable list of files to check for a
# given commit. It produces output of the format
# <commit id> <file name> <change start line>, <change line count>
touch $TEMPDIR/ranges.txt
git show $COMMIT | awk -v mycmt=$COMMIT '
    BEGIN {myfile=""} 
    /+{3}/ {
        gsub(/b\//,"",$2);
        myfile=$2
    }
    /@@/ {
        gsub(/+/,"",$3);
        printf mycmt " " myfile " " $3 "\n"
    }' >> $TEMPDIR/ranges.txt || true

# filter out anything that matches on a filter regex
for i in ${EXCLUDED_FILE_REGEX[@]}
do
    touch $TEMPDIR/ranges.filter
    grep -v "$i" $TEMPDIR/ranges.txt >> $TEMPDIR/ranges.filter || true
    REMAINING_FILES=$(wc -l $TEMPDIR/ranges.filter | awk '{print $1}')
    if [ $REMAINING_FILES -eq 0 ]
    then
        echo "This commit has no files that require checking"
        exit 0
    fi
    mv $TEMPDIR/ranges.filter $TEMPDIR/ranges.txt
done

# check out the files from the commit level.
# For each file name in ranges, we show that file at the commit
# level we are checking, and redirect it to the same path, relative
# to $TEMPDIR/check-format.  This give us the full file to run
# check-format.pl on with line numbers matching the ranges in the
# $TEMPDIR/ranges.txt file
for j in $(grep $COMMIT $TEMPDIR/ranges.txt | awk '{print $2}')
do
    FDIR=$(dirname $j)
    mkdir -p $TEMPDIR/check-format/$FDIR
    git show $COMMIT:$j > $TEMPDIR/check-format/$j
done

# Now for each file in $TEMPDIR/check-format run check-format.pl
# Note that we use the %P formatter in the find utilty.  This strips
# off the $TEMPDIR/check-format path prefix, leaving $j with the
# path to the file relative to the root of the source dir, so that 
# output from check-format.pl looks correct, relative to the root
# of the git tree.
for j in $(find $TEMPDIR/check-format -type f -printf "%P\n")
do
    range_start=()
    range_end=()

    # Get the ranges for this file. Create 2 arrays.  range_start contains
    # the start lines for valid ranges from the commit.  the range_end array
    # contains the corresponding end line (note, since diff output gives us
    # a line count for a change, the range_end[k] entry is actually
    # range_start[k]+line count
    for k in $(grep $COMMIT $TEMPDIR/ranges.txt | grep $j | awk '{print $3}')
    do
        RANGE=$k
        RSTART=$(echo $RANGE | awk -F',' '{print $1}')
        RLEN=$(echo $RANGE | awk -F',' '{print $2}')
        let REND=$RSTART+$RLEN
        range_start+=($RSTART)
        range_end+=($REND)
    done

    # Go to our checked out tree
    cd $TEMPDIR/check-format

    # Actually run check-format.pl on the file, capturing the output
    # in a temporary file.  Note the format of check-patch.pl output is
    # <file name>:<line number>:<error text>:<offending line contents>
    $TOPDIR/util/check-format.pl $j > $TEMPDIR/format-results.txt

    # Now we filter the check-format.pl output based on the changed lines
    # captured in the range_start/end arrays
    let maxidx=${#range_start[@]}-1
    for k in $(seq 0 1 $maxidx)
    do
        RSTART=${range_start[$k]}
        REND=${range_end[$k]}

        # field 2 of check-format.pl output is the offending line number
        # Check here if any line in that output falls between any of the 
        # start/end ranges defined in the range_start/range_end array.
        # If it does fall in that range, print the entire line to stdout
        # If anything is printed, have awk exit with a non-zero exit code
        awk -v rstart=$RSTART -v rend=$REND -F':' '
                BEGIN {rc=0}
                /:/ {
                    if (($2 >= rstart) && ($2 <= rend)) {
                        print $0;
                        rc=1
                    }
                }
                END {exit rc;}
            ' $TEMPDIR/format-results.txt

        # If awk exited with a non-zero code, this script will also exit
        # with a non-zero code
        if [ $? -ne 0 ]
        then
            EXIT_CODE=1
        fi
    done
done

# Exit with the recorded exit code above
exit $EXIT_CODE
