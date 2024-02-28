conda install -y -c conda-forge --file requirements.txt m2-bash
rmdir /S /Q cmake-build
mkdir cmake-build
cd cmake-build
cmake -DCMAKE_INSTALL_PREFIX=%CONDA_PREFIX% ..\..
echo cmake done
cmake --build . -t install
echo build-install done
cmake --build . -t _version.py
move _version.py ..\pysjef
cd ..
python -m pip install -e .