
set PREFIX=%CONDA_PREFIX%

cmake -LAH -DBUILD_TESTS=OFF -DCMAKE_PREFIX_PATH="%PREFIX%"  -DCMAKE_INSTALL_PREFIX="%PREFIX%"  -DCMAKE_INSTALL_RPATH="%PREFIX%/lib"  -DCMAKE_INSTALL_NAME_DIR="%PREFIX%/lib"  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON  -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON -B cmake-build .

cmake --build cmake-build --config Release --target install

cd pysjef
echo '__version__ = "%1"' > pysjef/_version.py
python -m pip install --no-deps --force-reinstall --verbose .
