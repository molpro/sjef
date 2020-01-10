sjef
====

A command-line interface to the [sjef library](../README.md), allowing the manipulation of  project bundles.
  A bundle is a directory that represents one instance of running the target program, and contains all input and output files, plus a registry of properties.
  The general syntax is

`sjef` *[flags]* *operation* *[flags]* *project* *additional...*

Generally applicable flags include

- `-s` | `--suffix` *suffix* Specify the filename extension (without the leading `.`) of the project. This forces `sjef` to work only with projects of type *suffix*.
- `-v` | `--verbose` Increase verbosity. Can be specified more than once to increase the amount of information shown.

*project* is the file name of the bundle. If it has no extension and the `-s` flag has not been used, the extension `.sjef` is appended.
If `-s` has been used, and the extension is absent or different to that specified, the `-s` extension is appended.

*operation* is one of

`new`: Make a completely new project bundle

`edit`: Edit the Molpro input file

`browse`: Browse the Molpro output file

`run`: Launch a job either on the local machine or on a preconfigured [backend](../lib/backends.md). Valid flags that can then be specified are
- `-b` | `--backend` *backend*: specify the name of the backend to be used (default is last used, or `local`)
- `-p` | `--parameter` *key*=*value*: specify a parameter value for substitution in the template for `run_command` defined in the [backend](../lib//backends.md)
- `-w` | `--wait`
- `-f` | `--force`

`status`: Report the status of the job launched by run

`kill`: Kill the job launched by run

`erase`: Erase the project

`copy`: Make a copy of the project bundle. The additional argument gives the destination, which, if already existing, will not be overwritten unless the `-f` flag is given.

`move`:  Move the project bundle. The additional argument gives the destination, which, if already existing, will not be overwritten unless the `-f` flag is given.

`import`: Copy files into the project bundle. For each additional argument, the file specified is copied into the project.

`export`: Copy files out of the project bundle. For each additional argument, its base name specifies which file in the project, and full name gives the destination to be copied to.

Backends are defined in per-user and per-system configuration files; see [further details](../lib/backends.md).

`sjef` can be localised onto a single software package using the options described above with the help of a shell alias, for example
````
alias cmolpro='sjef -s molpro'
````

[//]: # ( @page sjef About sjef)
