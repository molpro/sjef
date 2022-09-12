if [[ $# -ne 1 ]]; then
    echo "Must pass name of environment as arguement"
    exit 2
fi

conda config --add channels conda-forge
conda create --name $1 'python>=3.10' 'numpy>=1.17' 'boost-cpp>=1.71' 'pugixml>=1.10' 'cython>=0.24' 'lxml>=4.0' pip regex c-compiler cxx-compiler ghostscript
