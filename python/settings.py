from collections import OrderedDict
import sys

INTERACTIVE = sys.flags.interactive
try:
    if sys.ps1:
        INTERACTIVE = True
except AttributeError:
    pass


class Settings:
    """
    Global settings
    """
    __settings = OrderedDict([
        ("project_default_suffix", "sjef"),
        ("filter.select.return_option", "node"),
        ("filter.search_node_tree.infinite_depth", False),
        ("filter.search_node_tree.all_valid_nodes", False)
    ])
    __allowed_values = OrderedDict([
        ("filter.select.return_option", ["node", "project", "view"]),
        ("filter.search_node_tree.infinite_depth", [True, False]),
        ("filter.search_node_tree.all_valid_nodes",
         [True, False])
    ])

    @classmethod
    def set(cls, key, value):
        if isinstance(value, str):
            value = value.lower()
        if key in cls.__allowed_values:
            allowed = cls.__allowed_values[key]
            if value not in allowed:
                raise RuntimeError(
                    "for key = {} allowed values are {}".format(key, allowed))
        cls.__settings[key.lower()] = value

    @classmethod
    def get(cls, key):
        return cls.__settings[key.lower()]
