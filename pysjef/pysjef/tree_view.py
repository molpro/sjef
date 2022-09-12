from collections import OrderedDict


def root_is_orphan(node):
    try:
        return node.parent is None
    except AttributeError:
        return False


def root_is_project(node):
    try:
        return node.is_project
    except AttributeError:
        return False


def _done(root_validator, view):
    if all(root_validator(node) for node in view):
        return True
    elif len(view) == 1 and root_is_orphan(view[0]):
        return True
    else:
        return False


def tree_view(nodelist, root="orphan"):
    """
    Using nodelist as a base recreates a view of the tree structure.
    Only children leading to the nodelist are included,
    otherwise each copy of original node in the view is unchanged.
    This allows for further filtering of the view.

    **Choice of root**

    Root nodes at the top of the view are specified using ``root``.
    It can be a string to select one of the builtin choices, or
    a callable object.

    If ``root`` is callable, than root node is any node that
    returns true.

    .. note::

        Exception handling should be done by provided callable object.

    **Special values of root**

    .. glossary::

         parentless
            Default, root is any node without a parent

         project
            root is any node with instance attribute 'is_project' set to True


    :param nodelist: list of nodes to start building the view of a tree
    :param root: specification of root node
    :return: list of tree views
    """
    if root.lower() == "orphan":
        root_validator = root_is_orphan
    elif root.lower() == "project":
        root_validator = root_is_project
    else:
        raise RuntimeError("unknown root request")
    # bottom up recreation of the view until all nodes are root
    view = [node.copy_node() for node in nodelist]
    while not _done(root_validator, view):
        # get all unique parents from the nodelist
        parents = OrderedDict()  # mapping from old parents to their view copy
        # ordered by first parent
        for node in view:
            if root_validator(node):
                parents[id(node)] = node
                continue
            new_parent = parents.get(id(node.parent), None)
            if new_parent is None:
                new_parent = node.parent.copy_node()
                new_parent.children.clear()
                parents[id(node.parent)] = new_parent
            node.parent = new_parent
            new_parent.children.append(node)
        view = list(parents.values())
    return view
