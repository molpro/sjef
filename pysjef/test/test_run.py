import os
import pysjef
import pytest


@pytest.fixture
def project(tmp_path):
    suffix = 'testing'
    import pathlib
    config_dir = pathlib.Path.home() / '.sjef' / suffix
    config_dir.mkdir(parents=True, exist_ok=True)
    with open(config_dir / 'backends.xml', 'w') as f:
        f.write("""<?xml version="1.0"?>
    <backends>
      <backend name="local"/>
      <backend name="empty"/>
      <backend name="true" run_command="true"/>
      <backend name="ssh" host="127.0.0.1" run_command="echo"/>
      <backend name="ssh_bad" host="127.0.0.128" run_command="echo"/>
    </backends>
    """)
    p = pysjef.project.Project("TestProject", suffix=suffix,
                                     location=tmp_path)
    p.write_input('hello')
    yield p
    p.erase()
    os.remove(os.path.expanduser('~/.sjef/' + suffix + '/backends.xml'))


def test_project_exists(project):
    assert project.filename() is not None


def test_run_local(project):
    project.run(backend='true', wait=True)
    assert project.status == 'completed'


def test_run_non_existent_backend(project):
    try:
        project.run(backend='does-not-exist', wait=True)
        assert False
    except Exception as e:
        assert str(e) == 'Backend does-not-exist is not registered'


def test_run_ssh(project):
    try:
        project.run(backend='ssh', wait=True)
        assert project.status == 'completed'
    except Exception as e:
        assert False
def test_run_ssh_bad(project):
    try:
        project.run(backend='ssh_bad', wait=True)
        assert False
    except Exception as e:
        # print('exception caught,', e)
        assert 'failed' in str(e)
