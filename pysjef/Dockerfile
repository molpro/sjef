FROM continuumio/miniconda3

SHELL ["/bin/bash", "-c"]

RUN conda update -n base -c defaults conda
RUN conda config --add channels conda-forge
RUN conda create --name pysjef-ci-dev 'python>=3.8' 'numpy>=1.17' pugixml=1.10 'cython>=3.0' 'lxml>=4.0' 'pugixml>=1.10' pip regex pytest cmake c-compiler cxx-compiler ghostscript scipy
RUN conda install --name pysjef-ci-dev sphinx sphinx_rtd_theme
ENV PATH /opt/conda/envs/pysjef-ci-dev/bin:$PATH
RUN apt-get update
RUN apt-get install -y make rsync
