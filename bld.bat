
set PREFIX=%CONDA_PREFIX%
cmake -LAH -DBUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=%PREFIX% -DCMAKE_INSTALL_INCLUDEDIR=%PREFIX%\include  -DCMAKE_INSTALL_LIBDIR=%PREFIX%\Library\lib -B cmake-build .
cmake --build cmake-build --config Release --target install

cd pysjef
set SJEF_VERSION=%1
if "%SJEF_VERSION%"=="" (set SJEF_VERSION=0.0.0)
echo __version__ = "%SJEF_VERSION%" > pysjef/_version.py
python -m pip install --no-deps --force-reinstall --verbose .
