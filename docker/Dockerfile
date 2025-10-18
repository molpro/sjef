FROM continuumio/miniconda3

RUN . /opt/conda/etc/profile.d/conda.sh && conda tos accept --override-channels --channel https://repo.anaconda.com/pkgs/main \
 && conda tos accept --override-channels --channel https://repo.anaconda.com/pkgs/r

RUN  . /opt/conda/etc/profile.d/conda.sh && conda install -c conda-forge -y python 'numpy>=1.17' 'boost-cpp>=1.71' 'pugixml>=1.10' 'cython>=3.0' 'lxml>=4.0' pip regex c-compiler cxx-compiler ghostscript 'pytest>=7' 'cmake>=3.27' 'make>=4' scipy

