from .project import Project as SjefProject
from .settings import Settings
import pathlib

# Any special projects have to be stored here with key = backend name
PROJECT_FACTORY = {"sjef": SjefProject}


def Project(name='', suffix=None, **kwargs):
    """
    This is a project factory. Based on filename suffix it decides which Project to create.

    Extensions of pysjef which define a new project should add theirs to the ``PROJECT_FACTORY``.
    The key is the suffix specifying the backend.

    Project('a') # creates ``a.sjef``
    Project('a.molpro') # creates ``a.molpro`` and sets suffix to ``.molpro``
    Project('a.sjef`, suffix='molpro') # creates ``a.sjef.molpro`` and sets suffix to ``.molpro``

    :param name: name of the file
    :param suffix: suffix indicating which program to use
    :return:
    """
    if suffix is not None:
        try:
            return PROJECT_FACTORY[suffix](name=name, suffix=suffix, **kwargs)
        except KeyError:
            raise KeyError(f'Project with suffix = {suffix} is not implemented')
    suffix = pathlib.Path(name).suffix.replace('.', '')
    if not suffix:
        suffix = Settings.get('project_default_suffix')
    else:
        name = name.replace(f'.{suffix}', '')
    return PROJECT_FACTORY[suffix](name=name, suffix=suffix, **kwargs)
