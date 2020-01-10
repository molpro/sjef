###################
#
### Pulls each dependency and updates hashes
#
###################

cd $(dirname $0) || exit 1
for fname in *_SHA1
do
    rep_name="${fname%%_SHA1}"
    if [ ! -d "${rep_name}" ]
    then
        continue 
    fi
    echo "Pulling ${rep_name}"
    cd $rep_name
    # do not pull if in detached HEAD state
    if branch=$(git symbolic-ref --short -q HEAD)
    then
        git pull
    else
        echo "Deatached HEAD state. No pull."
    fi
    cd ../
done
. update_SHA1.sh
