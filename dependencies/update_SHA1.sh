#!/usr/bin/env bash

###################
#
### For each dependency, update the commit's hash number
### and git add the new hash
#
###################

dep_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

cd $dep_dir

for fname in *_SHA1
do
    rep_name="${fname%%_SHA1}"
    if [ ! -d "${rep_name}" ]
    then
        continue 
    fi
    cd "${rep_name}"
    sha1=$(git rev-parse HEAD)
    cd ../
    echo "$sha1" > $fname
    $(git add $fname)
done
