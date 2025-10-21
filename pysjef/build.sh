cd $(dirname $0)
BUILD=cmake-build-$(uname)-$(uname -m)
cmake -S .. -B $BUILD -DCMAKE_INSTALL_PREFIX=$CONDA_PREFIX -DBUILD_TESTS=OFF -DBUILD_PROGRAM=OFF || { echo 'cmake configuration failed' ; exit 1; }
cmake --build $BUILD -t install || { echo 'cmake install failed' ; exit 1; }

export CXXFLAGS='-std=c++17'
if uname | grep -q Darwin; then
    export CXXFLAGS="$CXXFLAGS -mmacosx-version-min=13"
fi
python -m pip install $* .