# Runtime library — compiled to object files for linking.
# These provide allocator, panic handler, _start, and WASI I/O.

# DECISION: Runtime is compiled separately per target and linked
# during the final codegen stage. For now, we build for the host.

if(CMAKE_C_COMPILER)
  add_library(ascRuntime OBJECT
    runtime.c
    wasi_io.c
  )
  target_compile_options(ascRuntime PRIVATE -Wall -Wextra -Wno-unused-parameter)

  # For Wasm cross-compilation (when clang with wasm target is available):
  # add_library(ascRuntimeWasm OBJECT runtime.c wasi_io.c)
  # target_compile_options(ascRuntimeWasm PRIVATE --target=wasm32-wasi -O2)
endif()
