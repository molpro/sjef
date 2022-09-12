mkdir -p cmake-build
cd cmake-build 


cmake -LAH  -DCMAKE_CXX_FLAGS=-D_HAS_AUTO_PTR_ETC=1 -DCMAKE_BUILD_TYPE=Release  -DBUILD_TESTS=OFF  -DBUILD_PROGRAM=OFF  -DCMAKE_PREFIX_PATH="$PREFIX"  -DCMAKE_INSTALL_PREFIX="$PREFIX"  -DCMAKE_INSTALL_RPATH="$PREFIX/lib"  -DCMAKE_INSTALL_NAME_DIR="$PREFIX/lib"  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON  -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON  ..

cmake --build . --target install

cd ..
echo '__version__ = "1.22.1"' > pysjef/_version.py
python -m pip install --no-deps --force-reinstall --verbose .

