"""select.py

Provides the MolproFilter class, used to filter a selection of Parsed objects.

Filters such as Parsed.jobs() and Parsed.molecules() will be callable objects
of type MolproFilter.
"""

import regex as re
from collections import OrderedDict
from .tree_view import tree_view
from .settings import Settings
import logging

logger = logging.getLogger(__name__)


def validate_by_nodename(nodename):
    """
    Creates a validator that passes if the nodename matches.
    Note: node names are lower-case

    :param nodename: name of the node
    :return:
    """

    if nodename in ["*"]:
        return None

    def validator(node):
        return node.nodename.lower() == nodename.lower()

    return validator


def validate_by_attribute_name(attribute_name):
    """
    checks that attribute exists in the node
    Note: attribute names are lower-case

    :param attribute_name:
    :return:
    """
    if attribute_name in ["*"]:
        return None

    def validator(node):
        for attr in node.attributes.keys():
            if attribute_name.lower() == attr.lower():
                return True
        return False

    return validator


def validate_by_func_filter(func_filter, **callables):
    """
    Given a function signature with attributes as parameters, extracts the
    attribute names and creates a validator which applies the function

    :param func_filter: string with functor name and attributes as a signature
    :param callables: dictionary of callable validators
    """
    pattern = re.compile(
        r"""
            \s*(?P<func_name>\w*)
            \s*\(\s*
            (?:(?P<arguments>\w+)\s*,?\s*)*
            \s*\)\s*
        """, re.IGNORECASE | re.VERBOSE)
    match = pattern.match(func_filter)
    if not match:
        raise SyntaxError("could not parse function: '{}'".format(func_filter))
    func_name = match.group("func_name")
    arguments = match.captures("arguments")
    if func_name in [None, ""]:
        raise SyntaxError("invalid function name: '{}'".format(func_filter))
    try:
        func = callables[func_name]
    except KeyError:
        raise KeyError("function '{}' is not provided".format(func_name))
    if not callable(func):
        raise TypeError("function '{}' is not callable".format(func_name))

    def validator(node):
        args = [node.attributes[arg] for arg in arguments]
        return func(*args)

    return validator, arguments


def validate_by_builtin_comparison(builtin_filter):
    """
    Validate by comparing attribute value to specified value.
    Specified value must be a string.
       builtin_filter = "attr=value"
    Special values:
        - "attr" is "*" applies comparison to all attributes

    For anything else (including exact float match), filter by callable
    """
    if builtin_filter.strip() == "":
        raise SyntaxError("using an empty built-in filter")

    builtin = builtin_filter.strip().split("=")
    if len(builtin) != 2:
        raise SyntaxError("more than one = when filtering by attribute value")
    attribute = builtin[0].strip()
    value = builtin[1].strip()

    if value == "":
        raise SyntaxError("Using an empty value for comparison. "
                          "To compare to an empty string, enclose it in"
                          """ brackets, i.e. '' or "" """)
    value = value.strip('"' + "'")
    if attribute in ["*"]:
        def validator(node):
            for val in node.attributes.keys():
                if not isinstance(val, str):
                    continue
                if value.lower() == val.lower():
                    return True
            return False
    else:
        def validator(node):
            val = node.attributes[attribute]
            if isinstance(val, str):
                return value.lower() == val.lower()
            return False
    return validator, [attribute]


def validate_by_filterset(filterset, **callables):
    """
    Generates validators for a node from a comma seperated list of filter strings

    Example of a filter set
        "name=Energy, method=RHF, func(value), value < 0., func2(value)"
    More generally
        "name=attributeName, method=methodName, func(attr1, attr2, attr3), attr1 < 0., func2(attr2)"

    There are two types of filters:
        1. built-in, special syntax understood by validate_by_builtin
        2. callable supplied by the user

    Order of validators:
        1) validate by attribute presence
        2) validate by attribute value
        3) validate by callable

    :param filterset:
    :param callables:
    :return:
    """
    # "name=attributeName, method=methodName, func(attr1, attr2, attr3), attr1<0., func2(attr2),"
    pattern = re.compile(
        r"""
            ^(?:\s*(?:
              (?P<function>\w+(?:\s*\(.*?\)))
              |
              (?P<builtin_comparison>\w+\s*[=]\s*[^,]+)
              |
              (?P<builtin_attribute>[\*]|[\w]+) 
            )\s*,?\s*)*$
        """, re.IGNORECASE | re.VERBOSE)
    match = pattern.match(filterset)
    if not match:
        raise SyntaxError("invalid filterset: '{}'".format(filterset))

    all_validators = []
    all_attributes = []

    for attribute in match.captures('builtin_attribute'):
        all_attributes.append(attribute)
    for builtin in match.captures('builtin_comparison'):
        new_validator, new_attributes = validate_by_builtin_comparison(builtin)
        all_validators.append(new_validator)
        all_attributes.extend(new_attributes)
    for function in match.captures('function'):
        new_validator, new_attributes = validate_by_func_filter(function,
                                                                **callables)
        all_validators.append(new_validator)
        all_attributes.extend(new_attributes)

    # hack of OrderedDict to generate an ordered set
    all_attributes = \
        OrderedDict((attr, None) for attr in all_attributes).keys()
    attribute_validators = \
        [validate_by_attribute_name(name) for name in all_attributes]
    attribute_validators = [v for v in attribute_validators if v is not None]
    return attribute_validators + all_validators


def selection_string_to_validators(selection, **callables):
    """
    SMM attempting at writing a minimal selection translator.

    My goal is to unwrap a complicated input like this into a list of callable
    validators which can be used to search_node_tree
    E.g.
        parsed.select("jobsteps[command=HF-SCF]",
        "atoms[z > 0., y > 0., within_unit_sphere(x, y, z)]",
        ".element_type",
        within_unit_sphere=lambda x, y, z: (x ** 2 + y ** 2 + z ** 2) < 1.0)
    Each argument is a collection of filters.
    Types of filters include (args refers to list of arguments in command above)
    1. by node name (i.e. 'jobsteps' in args[0] above, 'atoms' in args[1])
    2. by attribute presence (i.e. 'command' in args[0], 'x, y, z' in args[1],
    the whole args[3])
    3. by callable
        3.1 either built in (i.e. 'command=HF-SCF' in args[0], or 'z > 0.' in
        args[1], which would be translated into 'lambda z: z > 0.'
        3.2 or passed as kwarg (i.e. 'within_unit_sphere(x, y, z)' in args[1]);
            the signature has to be included in selection;
            the arguments must be names of attributes
        Note: this implies selection by attribute presence first.

    Filters are broken down by nodes, each argument is parsed separately.
    This function does the parsing.
    It's given a string like,
      "jobsteps[command=HF-SCF]"
      "atoms[z > 0., y > 0., within_unit_sphere(x, y, z)]"
      ".element_type"
      "atoms[z > 0., y > 0., within_unit_sphere(x, y, z)].element_type"
    The most general string I can think of is:
        "property[name=Energy, method=RHF, value < 0., some_func(value)].attribute"
    The structure is
        selection: nodename[filterset].attribute
        filterset: [filter1, filter2,, ...]
        filter: either built-in callable pattern, "attribute operator() value"
                or user supplied callable, "func(attribute1, attribute2, ...)
    Split selection into 3 groups ensuring correct order (nodename, filterset,
    attribute)
    Only one match is allowed, no duplicates
    Then process them separately

    :param selection: command specifying selection criteria in a user friendly syntax
    :param callables: kwargs with callable objects that can be used for selection
    :return: list of callable objects to be used for selection a return_method
    which should be applied to selected nodes and returned
    """
    pattern = re.compile(
        r"""
            ^\s*
            (?P<nodename>\w+|\*)?         # nodename can be "*" or a word
            \s*
            (?:\[(?P<filterset>.*)\])?    # filterset is wrapped in brackets
            \s*
            (?:\.\s*(?P<attribute>[\w]+|[.]))?  # attribute whose value to return
            \s*$
        """, re.IGNORECASE | re.VERBOSE)
    match = pattern.match(selection)
    if not match:
        raise SyntaxError("could not parse selection string")
    all_validators = []
    nodename = match.group("nodename")
    if nodename is not None:
        all_validators.append(validate_by_nodename(nodename))
    filterset = match.group("filterset")
    if filterset is not None:
        all_validators.extend(validate_by_filterset(filterset, **callables))
    attribute = match.group("attribute")
    if attribute is not None:
        all_validators.append(validate_by_attribute_name(attribute))

        def return_method(node_list):
            attributes = []
            for node in node_list:
                for attr, val in node.attributes.items():
                    if attr.lower() == attribute.lower():
                        attributes.append(val)
                        break  # Attributes should be case-insensitive unique
            return attributes
    else:
        return_method = None
    all_validators = [x for x in all_validators if x is not None]
    return all_validators, return_method


def apply_validators(node, validators):
    for val in validators:
        if not val(node):
            return False
    return True


def _next_queue(nodelist):
    """
    Returns children of nodelist
    """
    return [child for node in nodelist for child in node.children]


def _search_single_level(nodelist, validators):
    validated_nodes = [node for node in nodelist if
                       apply_validators(node, validators)]
    return validated_nodes


def _search_recursive_same_level(nodelist, validators):
    validated_nodes = _search_single_level(nodelist, validators)
    if len(validated_nodes) != 0:
        return validated_nodes
    return _search_recursive_same_level(_next_queue(nodelist), validators)


def _search_recursive_any_level(nodelist, validators, all_valid_nodes):
    """
    Note
    ----
    The search is not breadth first, it searches down each branch first.
    """
    all_validated_nodes = []
    for node in nodelist:
        if apply_validators(node, validators):
            all_validated_nodes.append(node)
            if not all_valid_nodes:
                continue
        validated_nodes = _search_recursive_any_level(_next_queue([node]),
                                                      validators,
                                                      all_valid_nodes)
        all_validated_nodes.extend(validated_nodes)
    return all_validated_nodes


# TODO should we guarantee a particular order when (infinite_depth and not same_level)?
def search_node_tree(nodelist, validators, same_level=True,
                     infinite_depth=Settings.get(
                         "filter.search_node_tree.infinite_depth"),
                     all_valid_nodes=Settings.get(
                         "filter.search_node_tree.all_valid_nodes")):
    """
    Search for valid nodes from a list.

    Note
    ----
    When searching recursively without same-level restriction, there is no
    guarantee of the order for returned nodes.

    :param nodelist: list of nodes to search
    :param validators: callable methods by which a node is deemed valid,
                must return True for the node to be kept in the selection
    :param infinite_depth: If True, recursively search the tree to the bottom,
                the search continues to lower level if no nodes are selected.
                If False
    :param same_level: requires ``infinite_depth``==True.
                If true, returned valid nodes are from the same depth level,
                 otherwise not.
    :param all_valid_nodes: requires ``infinite_depth``==True and
                ``same_level``==False.
                Searches children of valid nodes to generate a list of
                all valid nodes
    :return: list of selected nodes
    """
    if not infinite_depth:
        return _search_single_level(nodelist, validators)
    elif infinite_depth and same_level:
        return _search_recursive_same_level(nodelist, validators)
    else:
        return _search_recursive_any_level(nodelist, validators,
                                           all_valid_nodes)


def _check_return_option(return_option):
    if return_option not in ["node", "project", "view"]:
        raise RuntimeError("undefined return_option, {}".format(return_option))


def _check_selections(selections):
    for selection in selections:
        if not isinstance(selection, str):
            raise TypeError("selection must be a string, given: {}".format(
                type(selection)))


def _check_callables(callables):
    for name, func in callables.items():
        if not callable(func):
            raise TypeError(
                "Queries must be callable, given: {}".format(type(func)))


def _infinite_depth_signature(selection):
    if selection.strip() == "***":
        return True


def select_old(nodelist, *selections, same_level=True,
               return_option=Settings.get("filter.select.return_option"),
               **callables):
    """
    A general filtering method that accepts a string as syntax.

    Current syntax:

    Select takes string arguments each argument specifies a filter
    for a single node. Filters are ordered descending down the tree.
    Filtering is done recursively with each filter being applied on the list of
    nodes selected by the previous filters.
    Syntax for node filter:
      1) "nodename"
              selects all nodes with given name
      2) "nodename[attr]"
              apply 1), then select nodes with attribute "attr".
              Multiple attributes may be specified,
              e.g. "property[name, method]"
      3) "nodename[attr=string_value]"
              apply 2), then select nodes whose attribute value
              equals string_value. Any string_value with space in it must
              be enclosed in "" or ''.
              Multiple attributes may be specified,
              e.g. "property[name=Energy, method=RHF]"
              NOTE: comparison fails for non-string values
      4) "nodename.attr"
              apply filter 2) and return a list of attribute values
              instead of nodes.
              This can be joined with 2) and 3),
              e.g. "property[name=Energy, method=RHF].value"
              will return a list of RHF energies
              NOTE: this ends the selection process, even if more filter
                    arguments follow. It also has higher priority than any
                    return options (see below).
      5) "nodename[functor(attr1, attr2)]"
              apply 1), then select nodes with attributes named "attr1" and
              "attr2". Finally, apply a callable object called "functor"
              which must be passed on as a kwarg by the user.
              e.g. select(nodes,
                          property[name='Dipole moment', only_up(value)",
                          only_up = lambda value: value[2]> 0)
              NOTE: some of the kwargs are reserved (see below)
      Options 2-5 can be mixed in one argument.
      Node names, attribute names and string_values are not case sensitive.

    Special values:
      "*" - any node or attribute name passes when used in their place.
            This does not work when used as attribute name with selector 5).
            In this case "*" is taken as literal attribute name.
            e.g select(nodes, "job", "*", "property[name='Dipole moment']")
      "***" - apply next filter to arbitrary depth
            e.g select(nodes, "***", "property[name='Dipole moment']")

    Options:
      1) same_level=True/False
              when "***" is specified select() searches to arbitrary depth.
              If same_level is True, than on the first level where at least one
              node is selected, infinite depth search ends and the next filter
              is applied.
              If same_level is False, than each node is searched recursively
              until it is selected or end of tree is reached, regardless
              of the depth level for selected nodes.

    Return Options:
      Output is governed by "return_option" keyword
      1) return_option="node"
              return a list of nodes selected by the filter arguments.
              When syntax 4) is used, list of attribute values is returned
              instead.
              This is the default behaviour.
      2) return_option="project"
              return list of Projects whose node-trees pass the filters.
              instead.
      3) return_option="view"
              return a view of the tree where only branches leading to
              the selected nodes are preserved. The tree is effectively
              reconstructed bottom-up with new nodes in the tree only
              containing children that lead to the selected nodes if traversed
              downwards. Returns a root node
              NOTE: each node is copied and only its children and parent
              modified  leaving its attributes unchanged.
      NOTE: syntax 4) takes priority over the return_option value and
            forces return of a list of attribute values.

    :param nodelist: list of nodes (node is a MolproXML instance)
    :type nodelist: list[MolproXml]
    :param selections: strings specifying filtering procedure
    :type selections: list[str]
    :param callables: kwargs of callable objects that are used in selection
                      process
    :type callables: dict[str, function]
    :return:
    """
    if len(nodelist) == 0:
        return nodelist
    _check_return_option(return_option)
    _check_selections(selections)
    _check_callables(callables)
    next_queue = nodelist
    infinite_depth = False
    selected_nodes = []
    for selection in selections:
        if _infinite_depth_signature(selection):
            infinite_depth = True
            continue
        all_validators, return_method = selection_string_to_validators(
            selection, **callables)
        selected_nodes = search_node_tree(next_queue,
                                          all_validators, same_level,
                                          infinite_depth)
        infinite_depth = False
        if return_method is not None:
            return return_method(selected_nodes)
        next_queue = _next_queue(selected_nodes)
    if return_option.lower() == "node":
        return selected_nodes
    elif return_option.lower() == "project":
        return tree_view(selected_nodes, root="project")
    elif return_option.lower() == "view":
        return tree_view(selected_nodes, root="orphan")


def to_old_selections(selection_string):
    old_selections = []
    s_split1 = selection_string.strip().split("//")
    if not s_split1[0]:
        old_selections.append('***')
        s_split1.pop(0)
    for s in s_split1:
        old_selections.extend(s.split('/'))
        old_selections.append("***")
    old_selections.pop()
    return old_selections


def select(nodelist, selection_string, same_level=False,
           return_option=Settings.get("filter.select.return_option"),
           **callables):
    """
    Applies selection filters on a list of nodes.

    :param nodelist: starting list of nodes for selection procedure
    :param selection_string: specification of which nodes in the tree are to be selected,
                            see documentation for the syntax
    :param same_level: during search to any depth ('//') return valid nodes of
                  the same depth relative to the nodelist
    :param return_option: if 'node' return the selected nodelist
                     if 'project' return the view of selected nodelist up to the Project level
                     if 'orphan' return the view of selected nodelist up to the root node
    :param callables: kwargs with user-specified callable validators
    """
    if not selection_string:
        return nodelist
    old_selections = to_old_selections(selection_string)
    return select_old(nodelist, *old_selections, same_level=same_level,
                      return_option=return_option, **callables)
