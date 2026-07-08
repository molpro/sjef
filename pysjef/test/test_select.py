import pytest
from math import isclose
import pysjef
from pysjef.select import validate_by_nodename
from pysjef.select import validate_by_attribute_name
from pysjef.select import validate_by_func_filter
from pysjef.select import validate_by_builtin_comparison
from pysjef.select import validate_by_filterset
from pysjef.select import selection_string_to_validators
from pysjef.select import search_node_tree
from pysjef.select import select_old, select
from functools import partial


class MockNode:
    max_depth = 2

    def __init__(self, attr1="a", attr2=2, attr3=3.0, depth=0, attr4=" 4 "):
        self.nodename = "mock" + str(depth)
        self.attributes = {'attr1': attr1, 'attr2': attr2, 'attr3': attr3,
                           'attr4': attr4}
        self.attribute_names = ["attr1", "attr2", "attr3", 'attr4']
        self.depth = depth
        self.children = []
        if self.depth < self.max_depth:
            self.children.extend([
                MockNode(attr1 * 2, attr2 * 10, attr3 * 10,
                         depth=self.depth + 1),
                MockNode(attr1 * 3, attr2 * 100, attr3 * 100,
                         depth=self.depth + 1)])


def test_validate_by_nodename():
    node = MockNode()
    validator_pass = validate_by_nodename(node.nodename)
    validator_fail = validate_by_nodename("xxx")
    assert validator_pass(node)
    assert not validator_fail(node)


def test_validate_by_attribute_name():
    node = MockNode()
    validators_pass = [validate_by_attribute_name(name) for name in
                       node.attribute_names]
    validators_fail = [validate_by_attribute_name("xxx") for name in
                       node.attribute_names]
    assert all([validate(node) for validate in validators_pass])
    assert all(not validate(node) for validate in validators_fail)


def test_validate_by_func_filter():
    node = MockNode()
    func_filter_pass = " func ( attr1, attr2, attr3 )"
    func_filter_fails = ["func attr1, attr2, attr3)",
                         "func (attr1, attr2, attr3 ",
                         "func (attr1,  ",
                         "func attr1,attr2  "]
    for func_filter in func_filter_fails:
        with pytest.raises(SyntaxError):
            validate_by_func_filter(func_filter)
    with pytest.raises(TypeError):
        validate_by_func_filter(func_filter_pass, func=None)
    validator_pass, attribute_list = validate_by_func_filter(
        func_filter_pass,
        func=lambda a1, a2, a3: a1 == "a" and a2 == 2 and isclose(a3, 3.0),
        func2=None)
    assert attribute_list == ["attr1", "attr2", "attr3"]
    assert validator_pass(node)
    validator_fail, attribute_list = validate_by_func_filter(
        func_filter_pass,
        func=lambda a1, a2, a3: False)
    assert not validator_fail(node)


def test_validate_by_builtin():
    node = MockNode()
    builtin_filter_fail = ["attr1", "attr1==a", "attr1<a", ""]
    for builtin_filter in builtin_filter_fail:
        with pytest.raises(SyntaxError):
            validate_by_builtin_comparison(builtin_filter)
    validator, attribute = validate_by_builtin_comparison("attr1=a")
    assert attribute == ["attr1"]
    assert validator(node)
    validator, attribute = validate_by_builtin_comparison(" attr1 = a ")
    assert attribute == ["attr1"]
    assert validator(node)
    validator, attribute = validate_by_builtin_comparison(" attr1 = a ")
    assert attribute == ["attr1"]
    assert validator(node)
    validator, attribute = validate_by_builtin_comparison(' attr4 = " 4 " ')
    assert attribute == ["attr4"]
    assert validator(node)


def test_validate_by_filterset():
    node = MockNode()
    filterset_pass = "attr1, attr2, attr3, check_attr2(attr2)" \
                     ", check_attr3(attr3), attr1=a" \
                     ",check_all(attr1,attr2,attr3)"
    check_attr2 = lambda a2: a2 == 2
    check_attr3 = lambda a3: isclose(a3, 3.)
    check_all = lambda a1, a2, a3: (a1 == "a" and check_attr2(a2)
                                    and check_attr3(a3))
    validators = validate_by_filterset(filterset_pass, check_attr2=check_attr2,
                                       check_attr3=check_attr3,
                                       check_all=check_all)
    flags = [validate(node) for validate in validators]
    assert all(flags), "all validators should pass"
    filterset_one_fail = "attr1, attr2, attr3, attr1=a, check_attr2(attr2)" \
                         ", FAIL , check_attr3(attr3)"
    validators_one_fail = validate_by_filterset(filterset_one_fail,
                                                check_attr2=lambda a2: a2 == 2,
                                                check_attr3=lambda a3: isclose(
                                                    a3, 3.))
    flags = [validate(node) for validate in validators_one_fail]
    assert False in flags
    assert flags.count(False) == 1, "one validator should fail"


def test_selection_string_to_validators():
    node = MockNode()
    check_attr3 = lambda a3: isclose(a3, 3.0)
    selection = "  mock0[attr1, attr2, check_attr3(attr3)].attr1  "
    validators, return_method = \
        selection_string_to_validators(selection, check_attr3=check_attr3)
    assert len(validators) != 0 and return_method is not None
    flags = [validate(node) for validate in validators]
    assert all(flags), "all validators should pass"
    attribute_values = return_method([node])
    assert attribute_values == ["a"]
    # TODO check all other combinations.


class MockNodeTree:
    """
    Mock class for testing ``search_node_tree``.

    Validators will be based on tag.
    This is its structure at max_depth = 3
     depth                          tag number for each node
     --------------------------------------------------------
     root                               0
                          |->                         <-|
     level 1              1                             100
                   |->          <-|         |->                   <-|
     level 2       2               101      101                      200
              |->    <-|       |->    <-| |->    <-|            |->    <-|
     level 3  3        102     102    201 102    201            201    300

    """

    def __init__(self, tag, parent, depth, max_depth):
        self.nodename = "mock{}".format(depth)
        self.tag = tag
        self.parent = parent
        self.depth = depth
        self.children = []
        self.attributes = {'text': "text", 'tag': self.tag,
                           'strtag': str(self.tag)}
        if self.depth == 2:
            self.attributes['depth_2'] = True
        if self.depth < max_depth:
            self.children.extend([
                MockNodeTree(self.tag + 1, self, self.depth + 1, max_depth),
                MockNodeTree(self.tag + 100, self, self.depth + 1, max_depth)])


def _has_value(value, node):
    return node.tag == value


def _nodes_at_depth(root, depth, n_nodes):
    validator = lambda node: node.depth == depth
    nodes = search_node_tree([root], [validator], infinite_depth=True,
                             same_level=True)
    assert len(nodes) == n_nodes
    return nodes


# TODO more extensive testing of different options
def test_search_node_tree():
    root = MockNodeTree(0, None, 0, 3)
    allnodes = []
    for depth, n in zip([0, 1, 2, 3], [1, 2, 4, 8]):
        nodes = _nodes_at_depth(root, depth, n)
        allnodes.extend(nodes)
    validator = partial(_has_value, 101)
    nodelist = search_node_tree([root], [validator], infinite_depth=True,
                                same_level=True)
    location = [allnodes.index(node) for node in nodelist]
    assert len(nodelist) == 2
    assert all(node.depth == 2 for node in nodelist)
    validator = lambda node: node.tag % 2 == 0
    nodelist = search_node_tree([root], [validator], infinite_depth=True,
                                same_level=False, all_valid_nodes=True)
    assert len(nodelist) == 8


# TODO more thorough testing
def test_select():
    node = MockNode()
    nodelist = select_old([node], '*', "mock1[attr1=aa]", "mock2[attr1 = aaaa]")
    nodelist_new = select([node], "/mock1[attr1=aa] / mock2[attr1 = aaaa]")
    assert nodelist == nodelist_new
    assert len(nodelist) == 1
    nodelist2 = select_old([node], '*', "*", "mock2[attr1 = aaaa]")
    nodelist2_new = select([node], '/ /mock2[attr1 = aaaa]')
    assert nodelist2 == nodelist2_new
    assert nodelist == nodelist2
    nodelist3 = select_old([node], "***", "mock2[attr1 = aaaa]")
    nodelist3_new = select([node], "//mock2[attr1 = aaaa]")
    assert nodelist3 == nodelist3_new
    assert nodelist == nodelist3
