Simple Job Execution Framework
==============================

A library that supports the running of any program asynchronously either locally, directly on a remote machine, or via a batch queueing system.  The library has the following features.
- Management of project bundles.  A bundle is a directory that represents one instance of running the target program, and contains all input and output files, plus a registry of properties. The purpose of the project bundle is to keep together everything needed for running and inspecting the job.
- Launching and monitoring of jobs.
  Jobs are placed through the definition via configuration files of one or more
[backends](./lib/backends.md) that implement local or remote execution, possibly via a batch queueing system. Communication with remote hosts is via ssh, and users will normally want to arrange that password-free access to the host is established. A cache copy of the project bundle is maintained on the remote host, and synchronization in both directions is carried out as needed. The library can query the status of a job, and by default prevents the re-running of a job that is incomplete, or for which the input is identical to those of an already completed run.
- Editing and viewing of input and output files.
- Support for programs that produced marked-up output (e.g. XML or JSON).
- Maintenance of a recent project list.
- Support for multiple software packages. The file suffix of the project bundle is interpreted as identifying the target software, and configuration data (backends, recent projects) are stored separately for each suffix.


The library is implemented natively in C++ via the `sjef::Project` class, together with a C binding.
There is additionally a free-standing [sjef](program/sjef-program.md) program that implements most of the library functions through command-line options, as well as the [pysjef](https://gitlab.com/molpro/pysjef) Python bundle that includes in addition support for analysing marked-up output produced in one or more projects.

### Customization
- Code can be added to utilize additional information available in some programs - for example, the reconstruction from an output file of the input that generated it, in order to know whether the input file has since been modified.
- Versions of the sjef program customized to a single software package can be constructed using a wrapper script or shell alias that gives sjef an option to force the suffix.

