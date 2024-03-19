call conda install -y -c conda-forge --file requirements.txt
set PREFIX=%CONDA_PREFIX%
rmdir /S /Q cmake-build
mkdir cmake-build
cd cmake-build
cmake -DCMAKE_INSTALL_PREFIX=%CONDA_PREFIX% -DCMAKE_BUILD_TYPE=Release ..\..
cmake --build . -t install --config Release
cmake --build . -t _version.py
move _version.py ..\pysjef
cd ..
python -m pip install -e .
