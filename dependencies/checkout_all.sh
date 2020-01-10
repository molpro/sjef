#!/usr/bin/env bash

###################
#
### Updates all dependencies using `checkout` on each
#
###################

# Optionally can specify build_type. See `checkout` for details
build_type=$2

# directory of the script file
dep_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

cd $dep_dir


for fname in *_SHA1
do
    rep_name="${f%%_SHA1}"
    if [ ! -d "${rep_name}" ]
    then
        continue 
    fi
    if ! (./checkout.sh "${rep_name}" "${build_type}")
    then 
        exit 1
    fi
done

exit 0
