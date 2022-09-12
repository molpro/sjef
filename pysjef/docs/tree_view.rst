:gitlab_url: https://gitlab.com/molpro/pysjef

.. _tree_view:

===================================
Creating tree views and tree slices
===================================

Tree view
---------

Given a node list we can create a new tree by  calling :func:`pysjef.tree_view`.
It recursively follows the parents until a desired root, and returns corresponding
list of roots.

This effectively creates views of the global tree which contain specified nodes.

.. autofunction:: pysjef.tree_view

Tree slicing
------------

Slices of the tree can be obtained by using :func:`pysjef.select`
to select a set of nodes and using :func:`pysjef.tree_view` to
recreate the tree.

This mechanism is already part of :func:`pysjef.select`
and can be used by specifying ``return_option`` argument.

