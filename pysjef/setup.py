####################
### Instructions ###
#
# Here are a few ways to build molpro-project
#
# A) Via bash scripts into conda environment
#    >> bash create_conda_env.sh pysjef-dev
#    >> conda activate pysjef-dev
#    >> bash build.sh
#
# B) conda build:
#     >> conda create -n new_env_name conda-build
#     >> conda install conda-build
#     >> conda build .
#     >> conda install -c $CONDA_PREFIX/conda-build pysjef
#
#
####################

from setuptools import setup, find_packages
from setuptools.extension import Extension
from Cython.Build import cythonize
import os

def read_version():
    version = "none-0.0.0"
    with open("pysjef/_version.py", "r") as f:
        for line in f.readlines():
            if "__version__" in line:
                version = line.split("=")[-1].strip().strip(' "').strip("'")
    return version

PREFIX = os.environ["PREFIX"]

# Path to libsjef.a
LIB_DIRS = [PREFIX + "/lib"]
# Path to sjef.h
INCL_DIRS = [PREFIX + "/include"]

with open("README.rst", 'r') as f:
  long_description=f.read()

import platform
if platform.system() == "Darwin":
    extra_args = ['-std=c++17', "-mmacosx-version-min=10.15.0"]
else:
    extra_args = ['-std=c++17']

ext = Extension(
    name="pysjef.project_wrapper",
    sources=["pysjef/project_wrapper.pyx"],
    language="c++",
    libraries=["sjef", "pugixml", "boost_system", "boost_filesystem"],
    library_dirs=LIB_DIRS,
    include_dirs=INCL_DIRS,
    extra_compile_args=extra_args
)

setup(
    name="pysjef",
    version=read_version(),
    long_description=long_description,
    long_description_content_type="text/x-rst",
    packages=find_packages(),
    license="MIT",
    install_requires=["lxml>=4.0", "numpy>=1.12", "regex"],
    ext_modules=cythonize([ext], compiler_directives={"language_level": 3})
)
