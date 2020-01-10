#!/usr/bin/env bash

###################
#
### Checks that dependencies are not modified and their hash is same as stored
### before committing the main project
#
###################

# directory of the script file
dep_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
cd ${dep_dir}


for fname in *_SHA1
do
    rep_name="${fname%%_SHA1}"
    stored_hash=$(cat $fname)
    if [ ! -d "${rep_name}" ]
    then
        continue 
    fi
    cd "$rep_name"
# 1)
    out=`git diff-index HEAD --`
    if ! [ -z "$out" ]
    then
        echo "Error ($rep_name): uncommitted changes"
#        echo "out = $out"
        exit 1
    fi
# 2)
    current_hash=$(git rev-parse HEAD)
    if [ "$stored_hash" != "$current_hash" ]
    then
        echo "Error ($rep_name): hash stored in ${rep_name}_SHA1 ${stored_hash}"
        echo "                   is different from current hash ${current_hash}"
        echo "                   run update_SHA1 or checkout "
        exit 1
    fi
    cd ../
done

exit 0
