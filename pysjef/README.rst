==================
Welcome to pysjef!
==================

Python Simple Job Execution Framework (pysjef), pronounced pie-chef,
is a python extension of `sjef <https://molpro.gitlab.io/sjef/master/>`_.

sjef allows for simple execution of programs on local or remote servers,
and pysjef exposes the following features:

- Management of project bundles.
- Launching and monitoring of jobs
- Editing and viewing of input and output files
- Support for marked-up output (currently only XML, soon to include JSON)
- Maintenance of a recent project list
- Support for multiple software packages

Additional features of pysjef include:

- Parsing of structured output (e.g. XML) and bringing the whole tree into
  class objects
- Grouping of projects in a tree hierarchy allowing for more complicated workflows
- Representation of project collections, individual projects and structured
  output as a single tree
- Processing of the project tree by selecting groups of nodes,
  accessing specific attributes, and creating sub-trees

Documentation
=============

Documentation for master release is hosted here
`https://molpro.gitlab.io/pysjef/ <https://molpro.gitlab.io/pysjef/>`_.

Installation
============

We are in the process of adding pysjef to conda-forge. In the mean time,
see the `developers section <https://molpro.gitlab.io/pysjef/developers.html#developer-install>`_.

List of Contributors
====================

- Marat Sibaev
- Peter J. Knowles
