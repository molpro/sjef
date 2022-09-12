from .node import Node
from .project_factory import Project
from .settings import Settings

from collections import OrderedDict
from pathlib import Path


class DirectoryNode(Node):
    """
    Node mapping to the file system.

    Each DirectoryNode must map to a valid directory at initialization.
    If the directory does not exist, it will be created.

    Example
    -------
    Consider directory '/path/to/root' with many project subdirectories.
    Here is how to initialize the whole tree:
    >>> tree_paths = paths_to_projects("/path/to/root", suffix = 'sjef')
    >>> tree = DirectoryNode(path="/path/to/root", tree_paths=tree_paths)

    Attributes
    ----------
    location - absolute path to the directory
    name - name of the directory
    """

    def __init__(self, location=None, parent=None, tree_paths=None, suffixes=None):
        """
        :param tree_paths: paths to subdirectories for constructing the tree.
            They can be relative to current node or absolute and must contain
            path to the current node as one of the parents.
        :param suffixes: list of suffixes which correspond to Projects
        """
        super().__init__()
        if suffixes is None:
            suffixes = []
        self.nodename = "container"
        self.parent = parent
        if location is not None:
            self._create_node(location, tree_paths, suffixes)

    def _create_node(self, location, tree_paths, suffixes):
        path = Path(location).absolute()
        if not path.exists():
            path.mkdir()
        if not path.is_dir():
            raise ValueError("path must be a valid directory")
        self.location = path
        self.attributes["location"] = path
        self.attributes["name"] = path.name
        if tree_paths is not None:
            self._create_tree(tree_paths, suffixes)

    def _create_tree(self, tree_paths, suffixes):
        """
        Each path in tree_path can be absolute or relative.
        If it is absolute, than it must be a subdirectory of this node at some level.
        If it is relative, than it is assumed to be relative to this node's path
        """
        child_paths = OrderedDict()
        for tpath in tree_paths:
            tpath = Path(tpath)
            if not tpath.is_absolute():
                tpath = self.location / tpath
            if not self.is_subdir(tpath):
                raise ValueError(f"tree_path must be a subdirectory, path={tpath}")
            if self.location == tpath:
                continue
            diff = tpath.relative_to(self.location)
            child_path = self.location / diff.parts[0]
            if child_path not in child_paths:
                child_paths[child_path] = []
            child_paths[child_path].append(tpath)
        for child_path, subtree_paths in child_paths.items():
            self.add_child(child_path, suffixes=suffixes, tree_paths=subtree_paths)

    def copy_node(self):
        other = Node.copy_node(self)
        other.location = self.location
        return other

    def add_child(self, child_path, suffix=None, suffixes=None, tree_paths=None, replace=False):
        """
        Add container pointed to by path as a child.
        Currently, it is either another DirectoryNode or Project

        :param child_path: path to the child. If path is absolute than it must
             be a direct subdirectory. Otherwise, it is assumed to be relative
             to path of this node.
        :param suffix: child is a Project with given suffix
        :param suffixes: if a child path's suffix is in the list of suffixes, than it is a Project
        :param tree_paths: same as in __init__, but relative to child_path
        :param replace: if True and a child with specified path already exists, replace
            it with the new one. Otherwise, do nothing
        :return: child node
        """
        child_path = Path(child_path)
        if suffix:
            new_suffix = child_path.suffix + f'.{suffix}'
            child_path = child_path.with_suffix(new_suffix)
        if not child_path.is_absolute():
            child_path = self.location / child_path
        if not self.is_subdir(child_path):
            raise ValueError(f"child_path must be a subdirectory, child_path={child_path}")
        level = len(child_path.relative_to(self.location).parts)
        if level != 1:
            raise ValueError(
                f"absolute path must be a direct subdirectory, level={level}, child_path={child_path}")
        # TODO Project's path should be called path instead of location for consistency
        pos = -1
        for i, child in enumerate(self.children):
            path = getattr(child, 'location', None)
            if path is None:
                continue
            elif path == child_path:
                if not replace:
                    return child
                pos = i
                break
        if suffix:
            child = Project(name=child_path.stem, suffix=suffix, location=child_path.parent,
                            parent=self)
        elif suffixes and child_path.suffix.replace('.', '') in suffixes:
            child = Project(name=child_path.stem, suffix=child_path.suffix.replace('.', ''),
                            location=child_path.parent, parent=self)
        else:
            child = DirectoryNode(location=child_path, parent=self, tree_paths=tree_paths)
        if replace and pos != -1:
            self.children[pos] = child
        else:
            self.children.append(child)
        return child

    def has_child(self, name):
        """
        :return: True if a node by that name is among children
        """
        if self.select(f"[name={name}"):
            return True
        return False

    def add_child_node(self, child, replace=False):
        """
        Add a node to children

        :param child: child node to be added
        :param replace:  if True replaces first existing child with the same location with ``child``,
        otherwise appends ``child`` to the end of ``children`` list
        :return: True if a child was replaced, False otherwise
        """
        if replace:
            for i, p in enumerate(self.children):
                try:
                    if p.attributes['location'] == child.attributes['location']:
                        self.children[i] = child
                        return True
                except KeyError:
                    continue
        self.children.append(child)
        return False

    def is_subdir(self, path):
        """
        :return: True if path is a subdirectory
        """
        path = Path(path)
        try:
            path.relative_to(self.location)
            return True
        except ValueError:
            return False

    def erase(self, force=False, recursive=False):
        """
        Erases directory if it is empty.
        :todo:

        :param force: if True, erase the directory with all of its contents, including other
        directories. This can invalidate other nodes
        :param recursive: recursively call erase on all children nodes
        :return: True if directory was removed, False otherwise
        """
        pass


def paths_to_projects(root, suffix=Settings.get('project_default_suffix')):
    """
    Given a path to root directory, find paths to all projects witha a given suffix

    :param root: path to root node
    :param suffix: suffix of projects to search for
    :return: list of absolute paths to projects under root
    """
    root = Path(root)
    paths = sorted(root.rglob(f"*.{suffix}"))
    return [p.absolute() for p in paths]
