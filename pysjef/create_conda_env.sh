if [[ $# -ne 1 ]]; then
    echo "Must pass name of environment as arguement"
    exit 2
fi

conda config --add channels conda-forge
conda create --name $1 --file requirements.txt
