import pathlib
import shutil

import yaml
import regex as re

config_file_name = pathlib.Path.home() / '.sjef' / 'molpro' / 'backends.yaml'
save_config_file_name = pathlib.Path.home() / '.sjef' / 'molpro' / 'backends-save.yaml'
if save_config_file_name.exists():
    print('First remove the old backup',save_config_file_name)
    exit(1)
shutil.copyfile(config_file_name, save_config_file_name)
with open(config_file_name, 'r') as t:
    configs = yaml.safe_load(t)
for name, backend in configs.items():
    # print(backend)
    if re.match("^[a-zA-Z0-9_/'.-]*/molpro.*$", backend['run_command']) or backend['run_command'].startswith('molpro'):
        # print('backend ',name,':',backend)
        backend['run_jobnumber'] = '([0-9]+)'
    else:
        # print('backend probably batch ', name, ':', backend)
        pass
    backend['run_command'] = backend['run_command'].replace('\n', '')

with open(config_file_name, 'w') as t:
    yaml.safe_dump(configs, t)
