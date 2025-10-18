# This script will compile and install molpro-project in the
# currently active conda environment.
#
#
# 1) Load correct conda enviroment with necessary dependencies
#    found in conda-recipe/meta.yaml:requirements
#
#   > conda activate $your_env_name
#
# 2) Run this script
# 3) Add current directory to the PYTHONPATH


cd $(dirname $0)
#PREFIX=${PREFIX:-${CONDA_PREFIX}}
BUILD=cmake-build-$(uname)-$(uname -m)
#echo PREFIX=${PREFIX}
#echo CONDA_PREFIX=${CONDA_PREFIX}
#conda list

#if [ -x "$(command -v $CONDA_PREFIX/bin/cc)" ]
#then
#    CC=$CONDA_PREFIX/bin/cc
#fi

#if [ -x "$(command -v $CONDA_PREFIX/bin/cpp)" ]
#then
#    CXX=$CONDA_PREFIX/bin/c++
#fi
#
#rm -rf build/*$(uname)*
#rm -rf pysjef/*$(uname)*.so

#mkdir -p $BUILD
#cd $BUILD
#if [ -f "install_manifest.txt" ]
#then
#    xargs rm -f < "install_manifest.txt"
#fi
#if [ -f "CMakeCache.txt" ]
#then
#    rm CMakeCache.txt
#fi
cmake -S .. -B $BUILD -DCMAKE_INSTALL_PREFIX=$CONDA_PREFIX -DBUILD_TESTS=OFF -DBUILD_PROGRAM=OFF || { echo 'cmake build failed' ; exit 1; }
cmake --build $BUILD -t install || { echo 'make install failed' ; exit 1; }
#cd ../

#grep CMAKE_PROJECT_VERSION: $BUILD/CMakeCache.txt | sed -e 's/.*=/__version__ = "/' -e 's/$/"/' > pysjef/_version.py
#
#PREFIX=$PREFIX python _setup.py build_ext --inplace
#PREFIX=$PREFIX python -m pip install --no-deps --force-reinstall --verbose . 
#PREFIX=$PREFIX python -m pip install --force-reinstall .
export CXXFLAGS='-std=c++17'
if uname | grep -q Darwin; then
    export CXXFLAGS="$CXXFLAGS -mmacosx-version-min=13"
fi
cd ..
PREFIX=$CONDA_PREFIX python -m pip install -vv ./pysjef