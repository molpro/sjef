from ._version import __version__
from .node_xml import RootXml
from .select import select
from .tree_view import tree_view
from .node import Node
from .container import DirectoryNode
from .settings import Settings
from .project import xpath

import logging

logger = logging.getLogger(__name__)
logger.addHandler(logging.NullHandler())
try:
    from .project_factory import Project
    from .project import all_completed
    from .project import recent_project
except ImportError:
    import warnings

    warnings.simplefilter('default', ImportWarning)
    warnings.warn("could not import Project - possibly not compiled. "
                  "Project functionality will not be available",
                  category=ImportWarning)

