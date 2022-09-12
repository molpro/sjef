from collections import OrderedDict
from .select import select
from .tree_view import tree_view


class Node:
    """
    Abstract class for a node

    Attributes
    ----------
    name -
    parent -
    attributes -
    children -
    """

    def __init__(self):
        self.nodename = ""
        self.parent = None
        self.attributes = OrderedDict()
        self.children = []

    def copy_node(self):
        """
        Returns a copy of itself
        """
        other = self.__class__()
        other.nodename = self.nodename
        other.parent = self.parent
        other.attributes = self.attributes.copy()
        other.children = self.children[:]
        return other

    def __copy__(self):
        return self.copy_node()

    def select(self, selection_string, **options_and_callables):
        """
        Select children nodes
        TODO reference to select
        """
        return select(self.children, selection_string, **options_and_callables)

    def tree_view(self, **options):
        """
        Build tree view from this node
        """
        return tree_view([self], **options)

    def child(self, *attributes, nodename='', **kwargs_and_callables):
        """
        Utility wrapper over `select` routine to do a 1 level search of children and select
        a single one. If more than one child is selected, RuntimeError is thrown.

        Arguments are translated into selectors by attribute name ( see `select`).
        Key word arguements are translated into selectors by attribute name ( see `select`).

        Example
        --------
        out = p.select('container[location, name=directory, exists(location)]',
            exists=os.path.exists)

        is equivalent to

        out = p.child('location', nodename='container', name='directory',  'exists(location)',
            exists=os.path.exists)

        :param nodename: selector for name of the node class
        :param attributes: list of attributes for defining selectors by name, `[attribute]`
            and callable selectors `[func(attr1, attr2)]` ( see `select`)
        :param kwargs_and_callables: dictionary of other kwargs defining attribute selectors
            `key = value` and callable functions for callable selectors `func = function_name`
        :return:
        """
        selectors = []
        if attributes:
            selectors.append(', '.join(attributes))
        callables = {}
        for key, val in kwargs_and_callables.items():
            if callable(val):
                callables[key] = val
            else:
                selectors.append(f'{key}={val}')
        selector = f'{nodename}[{",".join(selectors)}]'
        p = self.select(selector, **callables)
        if len(p) > 1:
            raise RuntimeError('more than one child selected')
        elif len(p) == 1:
            return p[0]
        else:
            return None
