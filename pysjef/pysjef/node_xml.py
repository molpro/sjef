import regex as re
from lxml import etree
from .node import Node
from .settings import Settings

import logging

logger = logging.getLogger(__name__)


def tag_to_name(tag):
    """
    Takes XML tag with namespace, outputs raw node name.
    """
    return tag


def is_number(value):
    try:
        float(value)
        return True
    except ValueError:
        return False


def to_numerical(value):
    """
    Convert string to int or float, or list of ints/floats for numeral strings
    separated by empty space.
    """
    if not isinstance(value, str):
        return value
    values = value.split()
    if any(not is_number(v) for v in values):
        return value
    if len(values) == 0:
        return value
    elif len(values) == 1:
        v = values[0]
        try:
            return int(v)
        except ValueError:
            try:
                return float(v)
            except ValueError:
                return v
    else:
        return [to_numerical(v) for v in values]


class RootXml(Node):
    """
    Parses xml output file and reproduces its tree structure in Python.
    This is the root of the xml tree.

    There is a GenericXml class which stores attributes of any xml node as string,
    int/float or list of ints/floats for numeric values.
    For some programs a more specialised python representation is necessary.
    It must be implemented in a class inheriting from RootXml.

    When parsing xml, node names are checked against ``SPECIAL_NODES`` under
    current program suffix, if node name is in the corresponding dictionary than the
    class stored as a value is used.

    For example, parsing output of Molpro we can have a special implementation for
    node ``property``.

    class PropertyXml(RootXml):
        # special parsing

    We add it to SPECIAL_NODES to make it available during parsing

    RootXml.SPECIAL_NODES = {'molpro': {'property': PropertyXml}}

    Now, when parsing output of ``molpro`` backend any node called ``property``
    is stored in ``PropertyXml``
    """
    parser = etree.XMLParser(remove_blank_text=True, remove_comments=True)
    SPECIAL_NODES = {}
    TAG_TO_NAME = {'sjef': tag_to_name}
    adjustable_methods = {"to_numerical": to_numerical}

    def __init__(self, filename=None, xml=None, parent=None, suffix=None, **options):
        """
        Note
        ----
        Only one of ``filename`` or ``xml`` should be passed, with filename
        taking precedent.

        :param filename: filename for Molpro's xml output
        :param xml: an Element in xml tree
        :param parent: instance of parent node (NOT Element of lxml)
        :param suffix: suffix specifying the program
        """
        super().__init__()
        self.parent = parent
        if filename is not None:
            xml = etree.parse(filename, self.parser).getroot()
        elif xml is None:
            return
        try:
            special_nodes = self.SPECIAL_NODES[suffix]
        except KeyError:
            special_nodes = {}
        self.nodename = self.tag_to_name(xml, suffix=suffix)
        self._generate_attributes(xml, **options)
        for child in xml:
            child_name = self.tag_to_name(child, suffix=suffix)
            try:
                child_class = special_nodes[child_name]
            except KeyError:
                child_class = GenericXml
            child = child_class(xml=child, parent=self, suffix=suffix, **options)
            self.children.append(child)
        for child in self.children:
            child.finalise(**options)

    def tag_to_name(self, xml, suffix=None):
        try:
            return self.TAG_TO_NAME[suffix](xml.tag)
        except KeyError:
            return self.TAG_TO_NAME[Settings.get('project_default_suffix')](xml.tag)

    def finalise(self, **options):
        """
        Method to run after __init__.

        Children may have special use cases for this, but not the default node
        """
        pass

    def _generate_attributes(self, xml, **options):
        """
        Generate attribute dictionary from XML attributes
        """
        content = xml.text
        self.attributes.update(xml.attrib)
        if content is not None:
            self.attributes.update({".": self.adjustable_methods['to_numerical'](content.strip())})
        for name, value in xml.attrib.items():
            self.attributes[name] = self.adjustable_methods['to_numerical'](value)

    def _rename_attribute(self, current_name, new_name, **options):
        """
        Rename an attribute
        :return: value of attribute
        """
        val = self.attributes.pop(current_name)
        self.attributes[new_name] = val
        return val


class GenericXml(RootXml):
    """
    Generic container for unknown or trivial XML data.

    Unless a specific class has been defined for a given type of node, it will
    default to GenericXml.
    Specific classes do not need to be created unless there is specific behaviour
    needed for a given node type. Otherwise, GenericXml is fine.
    """
    pass
