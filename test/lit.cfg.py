import lit.formats
import os

config.name = "asc"
config.test_format = lit.formats.ShTest(True)
config.suffixes = ['.ts']
config.test_source_root = os.path.dirname(__file__)

# Find the asc binary
config.substitutions.append(('%asc', os.path.join(config.test_exec_root, '..', 'tools', 'asc', 'asc')))
