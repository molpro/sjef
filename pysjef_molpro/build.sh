# This script will install pysjef_molpro

echo '__version__ = "'$(git describe --tags --abbrev=0)'"' > pysjef_molpro/_version.py
python -m pip install --no-deps --force-reinstall --verbose . 

