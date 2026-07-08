from pysjef import tree_view
from pysjef import select
from tree_3lvl import Root, tree


def _setup(tree):
    nodes_with_z = select([tree], "//[has_z(id)]", has_z=lambda id: "z" in id)
    tree_with_z = Root(idsA=["1", "2", "3"], idsB=["a", "b", "c"],
                       idsLeaf=["z"])
    return nodes_with_z, tree_with_z


def test_view_orphan(tree):
    nodes_with_z, tree_with_z = _setup(tree)
    view = tree_view(nodes_with_z, root="orphan")
    assert (len(view) == 1)
    assert (tree_with_z == view[0])


def test_view_project(tree):
    nodes_with_z, tree_with_z = _setup(tree)
    projects_with_z = tree_with_z.children
    view = tree_view(nodes_with_z, root="project")
    assert (len(view) == 3)
    assert (projects_with_z == view)
