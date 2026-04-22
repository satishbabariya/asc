import lit.formats
import os
import shutil
import subprocess

config.name = "asc"
config.test_format = lit.formats.ShTest(True)
config.suffixes = ['.ts']
config.test_source_root = os.path.dirname(__file__)

# Find the asc binary
exec_root = config.test_exec_root or os.path.join(os.path.dirname(__file__), '..', 'build')
config.substitutions.append(('%asc', os.path.join(exec_root, 'tools', 'asc', 'asc')))

# Make FileCheck and `not` discoverable for negative tests. Homebrew LLVM 18
# is the project's required toolchain (see CLAUDE.md).
_llvm_bin = '/opt/homebrew/opt/llvm@18/bin'
if os.path.isdir(_llvm_bin):
    config.environment['PATH'] = _llvm_bin + os.pathsep + config.environment.get('PATH', os.environ.get('PATH', ''))

# Detect a wasmtime new enough to run WASI threads. Version 14+ supports the
# `-S threads=y` invocation used by task_spawn_wasm_run.ts. Environments
# without a wasmtime (or with an older one) will skip that test cleanly via
# the `REQUIRES: wasmtime-threads` directive.
wasmtime_path = shutil.which("wasmtime")
if wasmtime_path:
    try:
        out = subprocess.check_output(
            [wasmtime_path, "--version"], text=True, timeout=2)
        # e.g. "wasmtime 43.0.1 (cd4b6ed9b 2026-04-09)"
        parts = out.strip().split()
        if len(parts) >= 2:
            major = int(parts[1].split(".")[0])
            if major >= 14:
                config.available_features.add("wasmtime-threads")
    except Exception:
        pass
