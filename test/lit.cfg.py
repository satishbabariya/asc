import lit.formats
import os

config.name = "asc"
config.test_format = lit.formats.ShTest(True)
config.suffixes = ['.ts']
config.test_source_root = os.path.dirname(__file__)

# Find the asc binary
exec_root = config.test_exec_root or os.path.join(os.path.dirname(__file__), '..', 'build')
config.substitutions.append(('%asc', os.path.join(exec_root, 'tools', 'asc', 'asc')))
