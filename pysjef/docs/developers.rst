:gitlab_url: https://gitlab.com/molpro/pysjef

.. _developers:

==============
For Developers
==============

Preferred way to develop pysjef is within a conda environment,
without any external libraries.
This makes it easier to than distribute the package on conda-forge.

The main pysjef repository is hosted on gitlab https://gitlab.com/molpro/pysjef.
Developers should fork their own copy and add their contribution through a merge request.

.. _developer_install:

Installing pymolpro
-------------------

Clone the ``pysjef`` repository::

    git clone https://gitlab.com/molpro/pysjef.git
    cd pysjef

Run a script to create a conda environment for developing pysjef::

    bash create_conda_env.sh pysjef-dev
    conda activate pysjef-dev

Run a script to build the project and install it into conda::

    bash build.sh

It will build ``sjef`` c++ library in ``cmake-build/`` and compile
a cython wrapper of `sjef.project <https://molpro.gitlab.io/sjef/master/classsjef_1_1_project.html>`__


Testing
-------

For testing, pytest and mocker will need to be installed::

    pip install pytest pytest-mock

Tests are automatically run in CI.

Test files are kept in ``test/``. Any data files required for tests
are kept in ``test/samples/``.

Documenting
-----------

To build the documentation, Sphinx will need to be installed::

    pip install sphinx sphinx_rtd_theme

Then to generate the documentation itself, run Sphinx from ``docs/``::

    make  clean && make  html

Documentation is primarily generated from docstrings in the source code using
sphinx-autodoc. For that, pysjef module must be importable.
