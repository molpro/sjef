from .project_wrapper import ProjectWrapper
from .node import Node
from .settings import INTERACTIVE
from .node_xml import RootXml

import os
import shutil
from pathlib import Path
from subprocess import call
import time
from lxml import etree


def view_file(filename):
    if INTERACTIVE:
        editor = os.environ.get('VISUAL')
        if not editor:
            editor = os.environ.get('EDITOR', 'vi')
        call([editor, filename])


def all_completed(projects):
    """
    Checks that all projects have completed

    :param projects: list of project nodes
    :return: True/False whether all projects completed
    """
    return all(p.status == 'completed' for p in projects)


def recent_project(suffix, rank=1):
    """
    Look for a project by rank in the user-global recent project list

    :param suffix: project suffix
    :param rank: the rank of the project (1 is newest)
    :return: the filename of the project, or "" if not found
    """
    return ProjectWrapper.recent(suffix, rank)

class Project(Node):
    """
    Project is a node with parsed output as the only child.
    """

    def __init__(self, name="", location=None, construct=True, parent=None, suffix="",
                 # file_suffixes={"inp":"inp","out":"out","xml":"xml"},
                 file_suffixes=None):
        Node.__init__(self)
        self._project_wrapper = \
            ProjectWrapper(name=name, location=location, construct=construct, suffix=suffix,
                           file_suffixes=file_suffixes)
        self.nodename = "project"
        self.parent = parent
        self.suffix = suffix
        self.is_project = True
        if name:
            self._set_attributes()

    def _set_attributes(self):
        self.attributes["project_hash"] = self._project_wrapper.project_hash()
        self.attributes["name"] = self.name
        self.attributes["location"] = self.location
        self.attributes["suffix"] = self._project_wrapper.suffix

    def copy_node(self):
        other = Node.copy_node(self)
        state = self._project_wrapper.__getstate__()
        other._project_wrapper = ProjectWrapper()
        other._project_wrapper.__setstate__(state)
        other._set_attributes()
        return other

    def select(self, selection_string, **options_and_callables):
        """
        Specialization of :func:`pysjef.select` which applies selection
        to the parsed output

        :return: list of nodes that pass selection criteria
        """
        self.parse()
        return Node.select(self, selection_string, **options_and_callables)


    def xpath(self, query):
        """
        Run xpath search on the job xml, with support for default namespace
        :param query:
        :return: list of etree.Element objects
        """
        from lxml import etree
        from io import StringIO
        tree = etree.parse(StringIO(self.xml), etree.XMLParser())
        root = tree.getroot()
        default_ns_name = '__default__'
        ns = {k if k is not None else default_ns_name: v for k, v in root.nsmap.items()}
        import re
        queryns = re.sub(r'(::|/|^)([_a-zA-Z][-._a-zA-Z0-9]*)(/|$|@|\[)', r'\1' + default_ns_name + r':\2\3', query)
        return tree.xpath(queryns, namespaces=ns)

    def completed(self):
        '''
        :return: True if status is `completed`
        '''
        return self.status == 'completed'

    # TODO name, location and status be stored in attributes to allow their use
    # for searching
    @property
    def name(self):
        """ Name of the project """
        return self._project_wrapper.name

    @property
    def location(self):
        """
        Local path to the project directory
        """
        return self._project_wrapper.location

    @property
    def status(self):
        """
        String status of the project. Can be one of: ``unknown``, ``running``, ``waiting``,
        ``completed``, ``unevaluated``, ``killed``, or ``failed``.
        """
        return self._project_wrapper.status()

    def import_file(self, location, overwrite=True):
        """
        Import file at path ``location`` into the project.

        :param location: path to file
        :param overwrite: if corresponding file already exists in the project, flags that it should
            be overwritten
        """
        self._project_wrapper.import_file(location, overwrite)

    def write_file(self, filename, content, overwrite=True):
        """
        Creates a file in the project with specified content

        :param filename: name of the file to create in the project
        :param content: string content to write in the file
        :param overwrite: if corresponding file already exists in the project, flags that it should be overwritten
        :return: True if file was created or overwritten, otherwise False
        """
        location = self.location / Path(filename)
        if not overwrite and location.exists():
            return False
        with open(location, 'w') as f:
            f.write(content)
        return True

    def export_file(self, filename, location, overwrite):
        """
        Export one or more files from the project.

        :param filename: name of the file in the project
        :param location: The relative or absolute path to the destination directory
        :param overwrite: Whether to overwrite an existing file.
        """
        self._project_wrapper.export_file(filename, location, overwrite)

    def copy(self, name, location=None, force=False, keep_hash=False):
        """
        Make a copy of the project and its directory to a new path.

        :param name: name of the new project
        :param location: path to the parent of the project,
                         by default parent directory stays the same
        :param force: whether to first remove anything already existing at the new location
        :param keep_hash: whether to clone the project_hash, or allow a fresh one to be generated
        :return: copied project
        """
        new_project_wrapper = self._project_wrapper.copy(name, location=location, force=force,
                                                         keep_hash=keep_hash)
        new_project = self.copy_node()
        new_project._project_wrapper = new_project_wrapper
        new_project._set_attributes()
        return new_project

    def move(self, name=None, location=None, force=False):
        """
        Move the project bundle to a new location

        :param name: new name of the project
        :param location: path to the directory where project should be moved.
                         Defaults to same directory as current project.
        :param force: whether to first remove anything already existing at the new location
        """
        self._project_wrapper.move(name, location, force=force)

    def run(self, backend=None, verbosity=0, force=False, wait=False):
        """
        Start a sjef job
        :param backend: name of the backend
        :param verbosity: If >0, show underlying processing
        :param force: whether to force submission of job even if run_needed() reports that it's unnecessary
        :param wait: whether to wait until the job completes instead of returning after launchin it
        """
        self.children = []
        self._project_wrapper.run(backend, verbosity, force, wait=False)
        if wait:
            self.wait()

    def run_needed(self, verbosity=0):
        """
        Check weather the job has changed since the previous run and needs to be rerun
        :return: True/False
        """
        return self._project_wrapper.run_needed(verbosity)

    def wait(self, max_epoch=.005):
        """
        Wait unconditionally for status() to return neither 'waiting' nor 'running'
        :param max_epoch: maximum time to wait between checking status (seconds)
        """
        epoch = 0.001
        while self.status in ["running", "waiting"]:
            time.sleep(epoch)
            epoch = min(epoch * 2, max_epoch)

    def synchronize(self, verbosity=0):
        """
        Synchronize the project with a cached copy belonging to a backend.
        name.inp, name.xyz, Info.plist, and any files brought in with import(),
        will be pushed from the master copy to the backend, and all other
        files will be pulled from the backend.

        :param verbosity: If >0, show underlying processing
        :return:
        """
        return self._project_wrapper.synchronize(verbosity)

    def kill(self):
        """Kill the job started by ``run()``"""
        self._project_wrapper.kill()

    def clean(self, old_output=True, output=False, unused=False):
        """
        Remove potentially unwanted files from the project
        """
        self._project_wrapper.clean(old_output, output, unused)

    def erase(self, path=None):
        """
        Delete the project under path with its directory and all files.

        :param path: path to the project, defaults to current project
        :todo: what should happend to attributes and children?
        """
        if path is None:
            path = self._project_wrapper.filename()
        self._project_wrapper.erase(path)

    @property
    def output(self):
        """
        :return: node with Molpro's output
        """
        return self.parse()

    def parse(self, force=False, **options):
        """
        Parse the output file and save resultant node as a child
        :param force: reparse the output
        """
        if not force and len(self.children) == 1:
            return self.children[0]
        xml_string = self._project_wrapper.xml()
        xml_tree = etree.fromstring(xml_string, parser=RootXml.parser)
        output = RootXml(xml=xml_tree, parent=self, suffix=self.suffix, **options)
        self.children = [output]
        return output

    def findall(self, search_pattern):
        tree = etree.fromstring(self._project_wrapper.xml())
        return tree.findall(search_pattern, namespace=tree.getroot().nsmap.items())

    def import_input(self, fpath):
        """
        Copy file into the project as input file, or write string to input file
        :param fpath: path to input file
        """
        fpath = Path(fpath)
        if fpath.is_file():
            shutil.copy(fpath, self.location / self.input_file_path)

    def write_input(self, content):
        """
        Create an input file with content specified as a string
        :param content: string with content for the input file
        """
        self.write_file(self.name + '.inp', content)

    @property
    def input_file_path(self):
        # return Path(self.location / (self.name + ".inp"))
        return filename("inp")

    @property
    def output_file_path(self):
        # return Path(self.location / (self.name + ".out"))
        return filename("out")

    @property
    def out(self):
        from pathlib import Path
        return Path(self.filename("out")).read_text()

    @property
    def xml(self):
        from pathlib import Path
        return Path(self.filename("xml")).read_text()

    def view_input(self):
        """
        Opens the input file in editor as specified by '$EDITOR' environment
        variable.
        Should by called from an interactive session
        """
        view_file(self.input_file_path.absolute().as_posix())

    def view_output(self):
        """
        Opens the input file in editor as specified by '$EDITOR' environment
        variable.
        Should by called from an interactive session
        """
        view_file(self.output_file_path.absolute().as_posix())

    def property_set(self, **kwargs):
        """
        Set the value of a variable
        """
        self._project_wrapper.property_set(kwargs)

    def property_delete(self, *properties):
        """Delete properties"""
        self._project_wrapper.property_delete(properties)

    def property_get(self, *properties):
        """
        Return a dictionary of property, value pairs
        """
        return self._project_wrapper.property_get(properties)

    def property_names(self):
        """
        Return names of all assigned properties
        """
        return self._project_wrapper.property_names()

    def recent_find(self, location):
        return self._project_wrapper.recent_find(location)


    def backend_parameters(self, backend, doc=False):
        """
         Get all of the parameters referenced in the run_command of a backend

         :param backend: The name of the backend
         :param doc: Whether to return documentation instead of default values
         :return: A dictionary where the keys are the parameter names, and the values the defaults.
        """
        return self._project_wrapper.backend_parameters(backend, doc)

    def backend_parameter_set(self, backend, param, value):
        """
        Sets backend parameter in property file. The parameter will be used by the backend
        when interacting with the current jobs.

        :param backend: name of the backend
        :param param: name of parameter
        :param value: parameter value to set
        """
        return self._project_wrapper.backend_parameter_set(backend, param, value)

    def backend_parameter_delete(self, backend, param):
        """
        Delete backend parameter from property file.

        :param backend: name of the backend
        :param param: name of parameter
        """
        return self._project_wrapper.backend_parameter_delete(backend, param)

    def backend_parameter_get(self, backend, param):
        """
        Gets backend parameter from property file.

        :param backend: name of the backend
        :param param: name of parameter
        """
        return self._project_wrapper.backend_parameter_get(backend, param)

    def backend_parameter_documentation(self, backend, param):
        """
        Returns documentation for backend parameter

        :param backend: name of the backend
        :param param: name of parameter
        """
        return self._project_wrapper.backend_parameter_documentation(backend, param)

    def backend_parameter_default(self, backend, param):
        """
        Returns default value for backend parameter

        :param backend: name of the backend
        :param param: name of parameter
        """
        return self._project_wrapper.backend_parameter_default(backend, param)

    def xpath_search(self, xpath_query, attribute="", run=0):
        """
        Simple XPath search on the xml document. For each matching node found, return a string that
        contains the value of a specified attribute, or if the attribute is omitted, the node contents.

        :param xpath_query:
        :param attribute:
        :param run:
        """
        return self._project_wrapper.xpath_search(xpath_query, attribute, run)

    def list_files(self):
        """
        Return a list of files in the project directory
        """
        return self._project_wrapper.list_files()

    def filename(self, suffix="", name="", run=0):
        """
        Get the file name of the bundle, or a primary file of particular type, or a general file in the bundle

        :param suffix: If present without ``name``, look for a primary file with that type.
            If absent, the file name of the bundle is instead selected
        :param name: If present,  look for a file of this name, appended with ``.suffix``
            if that is non-blank
        :param run
            - 0: the currently focussed run directory
            - other: the specified run directory
        :return: the fully-qualified name of the file
        """
        return self._project_wrapper.filename(suffix, name, run)


