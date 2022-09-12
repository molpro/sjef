:gitlab_url: https://gitlab.com/molpro/pysjef

==================
Overview of pysjef
==================

This section is a work in progress, thank you for you patience.

Outline of the main ideas and most important files.

Glossary of key terms
---------------------

.. glossary::

    project bundle
        Object for managing a single execution of a program. 
        It is mapped to the filesystem with input, output and
        any intermediate files stored locally and synced with
        the relevant backend server. At python it is implemented
        in :class:`pysjef.Project`, which parses structured output
        file.

    global tree
        Collections, projects, and structured outputs are all nodes
        in a global tree.
