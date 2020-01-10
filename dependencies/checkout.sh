#!/usr/bin/env bash

###################
#
### Update step for CMake's FetchContent (through ExternalProject_add).
# 
### If working with DEVELOPMENT or DEBUG version:
###     x) Print warnings, and exit without error
### Otherwise, do not checkout if:
###    1) dependency has not being cloned yet
###    2) dependency has uncommited changes 
###    3) current hash == stored hash
#
###################

# directory of the script file
dep_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

rep_name=$1
build_type=$2

# --- Safety check
if [ -z "$rep_name" ]; then
        echo "Usage: checkout rep_name [build_type]" 1>&2
	echo "Repository name was not specified" >&2
	exit 1
fi

cd $dep_dir

fname="${rep_name}_SHA1"
stored_hash=$(cat $fname)
if [ ! -d "${rep_name}" ]
then
    exit 0
fi
cd "$rep_name"


# 1)
if ! $(git diff-index --quiet HEAD --)
then
    if [ "${build_type^^}" == "DEVELOPMENT" -o "${build_type^^}" == "DEBUG" ]
    then
        echo "Warning ($rep_name): Uncommitted changes. Nothing checked out." 1>&2
        exit 0
    else
        echo "Error ($rep_name): Uncommitted changes. Either build a development version," 1>&2
        echo "                   or commit and update." 1>&2
        exit 1
    fi
fi


# 2)
current_hash=$(git rev-parse HEAD)
if [ "$stored_hash" != "$current_hash" ]
then
    if [ "${build_type^^}" == "DEVELOPMENT" -o "${build_type^^}" == "DEBUG" ]
    then
        echo "Warning ($rep_name): Stored hash is different from current. Nothing checked out." 1>&2
        exit 0
    else
        if ! $(git checkout ${stored_hash})
        then
            echo "Error ($rep_name): Stored hash is different from current. Either build a development version," 1>&2
            echo "                   or commit and update." 1>&2
            exit 1
        fi
    fi
fi


exit 0
