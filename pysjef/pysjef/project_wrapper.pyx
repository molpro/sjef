#distutils: language = c++
from cython.operator cimport dereference as deref
from libcpp cimport nullptr, bool
from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.map cimport map
from libcpp.memory cimport unique_ptr, make_unique

from libcpp cimport nullptr, bool
from .project_wrapper cimport status, Project

import os
from pathlib import Path

cdef str_status(status stat):
    if stat == 0:
        return "unknown"
    elif stat == 1:
        return "running"
    elif stat == 2:
        return "waiting"
    elif stat == 3:
        return "completed"
    elif stat == 4:
        return "unevaluated"
    elif stat == 5:
        return "killed"
    elif stat == 6:
        return "failed"
    else:
        raise ValueError("Unknown status {}".format(stat))

cdef class ProjectWrapper:
    # pointer to the equivalent class in cmolpro
    cdef unique_ptr[Project] c_project
    cdef public object name
    cdef public object location
    cdef public object construct
    cdef public object suffix
    cdef public object file_suffix
    cdef public object _input_variables
    cdef public object __property_name_prefix

    def __init__(self, str name="", location=None, bool construct=True, suffix='',
                 file_suffixes=None):
        """
        Create a new Project bundle called *name* at the specified *location*.

        Note
        ----
        Class should be able to initalize without arguments, but only to allow implementation
        of methods like ``Node.copy_node``. Without specifying at least the ``name`` of the
        project it's in undefined state.
        """
        if file_suffixes is None:
            file_suffixes = {}
        if name:
            self._create(name, location, construct, suffix, file_suffixes)

    def _create(self, str name, location, bool construct, suffix, file_suffixes):
        self.__property_name_prefix = ""
        self.name = name
        self.construct = construct
        cdef string csuffix = str(suffix).encode('utf-8')
        cdef map[string, string] cfile_suffixes
        cdef string ckey, cval
        if file_suffixes:
            try:
                self.default_suffixes = file_suffixes
                for key, val in file_suffixes:
                    ckey = str(key).encode('utf-8')
                    cval = str(val).encode('utf-8')
                    cfile_suffixes[ckey] = cval
            except:
                raise RuntimeError('failed when processing file_suffixes')
        if location is None:
            location = Path.cwd()
        else:
            location = Path(location)
        if not location.is_dir():
            # this isn't a directory!
            raise ValueError(f"The chosen location doesn't seem to be a directory ({location})")
        self._input_variables = [name, location, construct, suffix, file_suffixes]
        location = location / name
        cdef string fname = str(location).encode('utf-8')
        if file_suffixes:
            self.c_project = make_unique[Project](fname, construct, csuffix, cfile_suffixes)
        else:
            self.c_project = make_unique[Project](fname, construct, csuffix, cfile_suffixes)
        self.location = Path(self.filename())
        self.suffix = self.location.suffix

    def __getstate__(self):
        """
        Required for pickling

        :return : representation of the state of this instance.
        """
        return self._input_variables

    def __setstate__(self, state):
        self._create(*state)

    def list_files(self):
        """
        Return a list of files in the project directory
        """
        return os.listdir(self.location)

    def filename(self, suffix="", name="", run=-1):
        """
        Get the file name of the bundle, or a primary file of particular type, or a general file in the bundle

        :param suffix: If present without ``name``, look for a primary file with that type. If absent, the file name of the bundle is instead selected
        :param name: If present,  look for a file of this name, appended with ``.suffix`` if that is non-blank
        :param run
            - 0: the currently focussed run directory
            - other: the specified run directory
        :return: the fully-qualified name of the file
        """
        suffix = str(suffix).encode('utf-8')
        name = str(name).encode('utf-8')
        return deref(self.c_project).filename_string(suffix, name, run).decode('utf-8')

    def import_file(self, fname, bool overwrite=True):
        """Import a file of location *filename* into the bundle"""
        fname = Path(fname)
        cdef string cfname = str(fname).encode('utf-8')
        deref(self.c_project).import_file(cfname, overwrite)

    def export_file(self, str filename, location, bool overwrite=True):
        """Export a file of name *filename* to *location*"""
        location = Path(location)
        if not location.is_dir():
            raise ValueError(f"The chosen location does not seem to be a directory ({location})")
        cdef string fname = str(location / filename).encode('utf-8')
        deref(self.c_project).export_file(fname, overwrite)

    def copy(self, str name, location=None, bool force=False, bool keep_hash=False, int keep_run_directories=1000000):
        """
        Make a copy of the project and its directory to a new path.

        :param name: name of the new project
        :param location: path to the parent of the project,
                         by default parent directory stays the same
        :param force: whether to first remove anything already existing at the new location
        :param keep_hash: whether to clone the project_hash, or allow a fresh one to be generated
        :param keep_run_directories: how many run directories to retain in the copy
        :return: copied project
        """
        if not location:
            location = self.location.parent
        fname = str(Path(location) / name)
        deref(self.c_project).copy(str(fname).encode('utf-8'), force, keep_hash, False, keep_run_directories)
        return ProjectWrapper(name, location, suffix=self.suffix)

    #TODO confirm what happens with project name
    def move(self, name=None, location=None, force=False):
        """
        Move the bundle to a new location

        :param force: whether to first remove anything already existing at the new location
        """
        # convert location to pathlib.Path for cross platform compatibility
        if location is None: location = self.location.parent
        location = Path(location)
        # check that location is a directory
        if not location.is_dir():
            raise ValueError(
                "Export destination must be a directory, given: {}"
                .format(location))
        # check that name is a string
        if name is None:
            name = self.name
        else:
            self.name = name
        if not isinstance(name, str):
            raise TypeError("New bundle name must be a string, not {}"
                            .format(type(name)))
        cdef string fname = str(location / name).encode('utf-8')
        deref(self.c_project).move(fname, force)
        self.location = Path(self.filename())
        # self.name = deref(self.c_project).name().decode('utf-8')

    def trash(self):
        """
        Move the bundle to the trash

        """
        deref(self.c_project).trash()

    def run(self, backend=None, verbosity=0, bool force=False, bool wait=False, options=""):
        cdef int v = verbosity
        cdef string bend
        if backend is None:
            return deref(self.c_project).run(v, force, wait, options.encode('utf-8'))
        else:
            bend = backend.encode('utf-8')
            return deref(self.c_project).run(bend, v, force, wait, options.encode('utf-8'))

    def run_needed(self, int verbosity = 0):
        return deref(self.c_project).run_needed(verbosity)

    def status(self):
        """
        Checks job status
        """
        return str_status(deref(self.c_project).status())

    def clean(self, int keep_run_directories = 1):
        """Removed run directories from the project"""
        deref(self.c_project).clean(keep_run_directories)

    def kill(self):
        """Kill the job started by ``run``"""
        deref(self.c_project).kill()

    def wait(self, max_microseconds = None):
        """Wait for completion of the job started by ``run``"""
        cdef unsigned int max_time
        if max_microseconds:
            max_time = max_microseconds
            deref(self.c_project).wait(max_time)
        else:
            deref(self.c_project).wait()

    def file_contents(self, str extension, str name=None):
        """Return file contents"""
        cdef string cextension = extension.encode('utf-8')
        if name is None:
            return deref(self.c_project).file_contents(cextension).decode(
                'utf-8')
        else:
            return deref(self.c_project).file_contents(cextension, name.encode(
                'utf-8')).decode('utf-8')

    def xml(self):
        """Return the xml output file contents"""
        return deref(self.c_project).xml().decode('utf-8')

    def input_from_output(self):
        """Return the input constructed from the output"""
        return deref(self.c_project).input_from_output().decode('utf-8')

    def erase(self, project_path):
        cdef string cpath = str(project_path).encode('utf-8')
        deref(self.c_project).erase(cpath)

    def project_hash(self):
        """
        Return a globally-unique hash to identify the project.
        The hash is generated on first call to this function, and
        should be relied on to never change, including after calling
        move(). copy() normally results in a new hash string, but
        this can be overridden if an exact clone is really wanted.
        """
        cdef size_t hash = deref(self.c_project).project_hash()
        cdef int long_hash = hash
        return hash

    def input_hash(self):
        """
        Construct a hash that is unique to the contents of the input
        file and anything it references.
        """
        cdef size_t hash = deref(self.c_project).input_hash()
        cdef int long_hash = hash
        return hash

    def property_set(self, props):
        """
        Set the value of a project property.
        """
        cdef map[string, string] cprops
        for key, val in props.items():
            ckey = str(self.__property_name_prefix + key).encode('utf-8')
            cval = str(val).encode('utf-8')
            cprops[ckey] = cval
        deref(self.c_project).property_set(cprops)

    def property_delete(self, props):
        """
        Delete a project property
        @note Only properties defined via property_set() can be deleted
        """
        cdef vector[string] cprops
        for x in props:
            cprops.push_back(str(self.__property_name_prefix + x).encode('utf-8'))
        deref(self.c_project).property_delete(cprops)

    def property_get(self, properties):
        """
        Return the value of a project property

        Variables set by users are protected with a prefix in their name.
        We search twice, first without prefix and then with it.
        Union of results is returned.
        """

        cdef vector[string] cprops
        cdef vector[string] cprops_with_prefix
        for prop in properties:
            cprops.push_back(prop.encode('utf-8'))
            cprops_with_prefix.push_back(str(self.__property_name_prefix + prop).encode('utf-8'))
        cres = deref(self.c_project).property_get(cprops)
        cres_with_prefix = deref(self.c_project).property_get(cprops_with_prefix)
        res = {}
        for i in range(cres.size()):
            prop = properties[i]
            v1 = cres[cprops[i]].decode('utf-8')
            v2 = cres_with_prefix[cprops_with_prefix[i]].decode('utf-8')
            res[prop] = None
            if v2 is not "":
                res[prop] = v2
            elif v1 is not "":
                res[prop] = v1
        return res

    def property_names(self):
        """
        Return names of all assigned properties
        """
        cprops = deref(self.c_project).property_names()
        props = []
        for x in cprops:
            props.append(x.decode('utf-8'))
        return props

    def recent_find(self, location):
        """
        Look for a project by name in the user-global recent project list

        Returns int representing the rank of the project where 1 is most
        recent, or FileNotFoundError if project is not found
        """
        location = Path(location)
        rank = deref(self.c_project).recent_find(str(location).encode('utf-8'))
        if rank == 0:
            raise FileNotFoundError("cmolpro couldn't find '{}'. "
                                    "Ensure the whole file path is specified"
                                    .format(location))
        else:
            return rank

    def backend_get(self, backend, key):
        cdef string cresult = deref(self.c_project).backend_get(str(backend).encode('utf8'), str(key).encode('utf8'))
        return cresult.decode('utf-8')

    def backend_names(self):
        cdef vector[string] cresult = deref(self.c_project).backend_names()
        return [n.decode('utf-8') for n in cresult]

    def backend_parameters(self, backend, bool doc = False):
        """
         Get all of the parameters referenced in the run_command of a backend

         :param backend: The name of the backend
         :param doc: Whether to return documentation instead of default values
         :return: A dictionary where the keys are the parameter names, and the values the defaults.
        """
        cbackend = str(backend).encode('utf-8')
        cdef map[string, string] cresult = deref(self.c_project).backend_parameters(cbackend, doc)
        result = dict()
        for (key, value) in cresult:
            result[key.decode('utf-8')] = value.decode('utf-8')
        return result

    def backend_parameter_set(self, backend, param, value):
        """
        Sets backend parameter in property file. The parameter will be used by the backend
        when interacting with the current jobs.

        :param backend: name of the backend
        :param param: name of parameter
        :param value: parameter value to set
        """
        cdef string cbackend = str(backend).encode('utf-8')
        cdef string cparam = str(param).encode('utf-8')
        cdef string cvalue = str(value).encode('utf-8')
        deref(self.c_project).backend_parameter_set(cbackend, cparam, cvalue)
        return

    def backend_parameter_delete(self, backend, param):
        """
        Delete backend parameter from property file.

        :param backend: name of the backend
        :param param: name of parameter
        """
        cdef string cbackend = str(backend).encode('utf-8')
        cdef string cparam = str(param).encode('utf-8')
        deref(self.c_project).backend_parameter_delete(cbackend, cparam)

    def backend_parameter_get(self, backend, param):
        """
        Gets backend parameter from property file.

        :param backend: name of the backend
        :param param: name of parameter
        """
        cdef string cbackend = str(backend).encode('utf-8')
        cdef string cparam = str(param).encode('utf-8')
        cdef string cvalue = deref(self.c_project).backend_parameter_get(cbackend, cparam)
        return cvalue.decode('utf-8')

    def backend_parameter_documentation(self, backend, param):
        """
        Returns documentation for backend parameter

        :param backend: name of the backend
        :param param: name of parameter
        """
        cdef string cbackend = str(backend).encode('utf-8')
        cdef string cparam = str(param).encode('utf-8')
        cdef string cvalue = deref(self.c_project).backend_parameter_documentation(cbackend,
                                                                                   cparam)
        return cvalue.decode('utf-8')

    def backend_parameter_default(self, backend, param):
        """
        Returns default value for backend parameter

        :param backend: name of the backend
        :param param: name of parameter
        """
        cdef string cbackend = str(backend).encode('utf-8')
        cdef string cparam = str(param).encode('utf-8')
        cdef string cvalue = deref(self.c_project).backend_parameter_default(cbackend, cparam)
        return cvalue.decode('utf-8')

    def xpath_search(self, xpath_query, attribute, int run):
        cdef string cxpath_query = str(xpath_query).encode('utf-8')
        cdef string cattribute = str(attribute).encode('utf-8')
        cdef vector[string] cresult = deref(self.c_project).xpath_search(cxpath_query, cattribute, run)
        result = []
        for value in cresult:
            result.append(value.decode('utf-8'))
        return result

    @staticmethod
    def recent(suffix, number=1):
        """
        Look for a project by rank in the user-global recent project list

        :param suffix: project suffix
        :param rank: the rank of the project (1 is newest)
        :return: the filename of the project, or "" if not found
        """
        cdef string csuffix = str(suffix).encode('utf-8')
        cdef string cvalue = Project.recent(csuffix, number)
        return cvalue.decode('utf-8')
