:gitlab_url: https://gitlab.com/molpro/pysjef

.. _select:

===================================
Selecting nodes and extracting data
===================================

This document details the selection syntax used in the :func:`pysjef.select`

.. autofunction:: pysjef.select

Selection Filters Syntax
------------------------

Selection filters are specified in a single string. Each filter is applied on
the list of nodes selected by the previous filter. That way the whole tree
of nodes can be traversed.

Single selection filter
-----------------------

Selection is done by processing a filter string and generating validator functions.
The validators are applied to potential nodes returning True or False.
The nodes must be of similar structure to :class:`pysjef.Node`, in particular,
they should contain attributes ``nodename``, ``parent``, ``attributes``, and ``children``
to use all functionality of :func:`pysjef.select`.

General structure of filter string is as follows::

    nodename[attr1, attr2 <builtin-operator> value, callable_func(attr1, ..., attrn)]

- specifying ``nodename`` selects all nodes for which ``node.nodename == nodename``
  (see :func:`pysjef.select.validate_by_nodename`)
- if no ``nodename`` is specified, or ``nodename == "*"`` than validator based on
  ``nodename`` is not generated

Following in square brackets are attribute filters:

- ``attr#`` is the name of an attribute stored as key in ``node.attributes`` dictionary

- specifying ``attr1`` selects all nodes for which ``attr1 in node.attributes``
  (see :func:`pysjef.select.validate_by_attribute_name`)

- ``<builin-operator>`` is one of the builtin operators for selection filters, currently
   only ``=`` to check for string equality is implemented

- specifying ``attr2 = value`` selects all nodes that conaint ``attr2`` and ``attr2 == value``
  (see :func:`pysjef.select.validate_by_builtin_comparison`)

- ``callable_func`` is name of a callable function returning bool which expects attributes
   from ``node.attributes`` with name ``attr1``, ..., ``attrn``

- last specification selects nodes that have attributes ``attr1``, and ..., and ``attrn``
  than passes values of ``attr1`` etc. to ``callable_func`` and checks if it's True.
  The ``callable_func`` must be passed as a kwarg.
  (see :func:`pysjef.select.validate_by_func_filter`)

.. note::

    ``nodename`` and attribute names (``attr#``) are not case-sensitive.

Any number of attribute filters can be specified within square brackets, but
the corresponding validators are not guaranteed to be in the same order.
Generally, validators by attribute name come first, followed by validators by
builtin operators, followed by validators by callables.

For example, all of the following selections return the same node

>>> from pysjef import Node, select
>>> import math
>>> point = Node()
>>> point.nodename = "coord"
>>> point.attributes.update([('Cartesian', 'true'), ('3D','false'),('x', 0.0)),('y', 0.0)])
>>> select([point], 'coord[cartesian = True, 3D = false]')
>>> select([point], 'coord[x, y]')
>>> select([point], '[3D = false, is_centre(x,y)]', is_centre= lambda x,y: x**2+y**2 < 1.0e-15)

.. _return_by_value:

Return by value
---------------

Instead of returning a list of nodes, one can return a list of attribute values
by ending filter string with ``.attribute_name``

For example, all of the following return ``x`` by value

>>> select([point], 'coord.x')
>>> select([point], '[3D = True].x')
>>> select([point], '.x')

Separators
----------

Selection filters are separated by slashes '/'.

.. glossary::

    '/'
        single slash indicates the following filter applies to
        direct children

    '//'
        double slash indicates the following filter applies to any
        descendant at any depth

For example, the following takes a collection of graphs and returns all color nodes
that are red

>>> select(graphs, 'graph/coord/colour[red=True]')
>>> select(graphs, 'graph//colour[red=True]')

The first example ensure the hierarchy ``graph <- coord <- color``,
while the second only restricts the first and last nodes and will search any node
in-between until the end of the tree.


Prefix
------

Selection string can either start with a filter or a separator

.. glossary::

    'filter'
        applies the filter on the provided nodelist

    '/filter'
        applies the filter on the children

    '//filter'
        applies the filter on the provided nodelist and descendants at any depth

Return Option
-------------
.. glossary::

    by value
        using special syntax :ref:`return_by_value`

    node
        return list of nodes (default)

    project
        select the nodes and return a tree view up to Project nodes (see :func:`pysjef.tree_view`)

    root
        select the nodes and return a tree view to root nodes (see :func:`pysjef.tree_view`)



Full Example
------------

:todo:

..
    >>> select(plots, 'plot/point/coords[3D = True]')

