# RFC-0004 — Target Support

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0001, RFC-0003 |
| Primary target | `wasm32-wasi-threads` |
| Clang reference | `clang/lib/CodeGen/TargetInfo.cpp` → `WebAssemblyABIInfo` |
| LLVM reference | `llvm/lib/Target/WebAssembly/` |

## Summary

Because the compiler emits LLVM IR, all LLVM targets are supported by selecting the
appropriate `TargetMachine` at compile time. This RFC defines the target support matrix,
primary vs secondary targets, Wasm proposals required, concurrency lowering differences per
target, and the LLVM optimization pass configuration.

## Target Matrix

| Target triple | Tier | Thread primitive | Notes |
|---|---|---|---|
| `wasm32-wasi-threads` | Primary | `wasi_thread_start` | Threads + atomics + bulk-memory proposals required |
| `wasm32-unknown-unknown` | Primary | N/A (single-threaded) | No concurrency; ownership model unchanged |
| `x86_64-unknown-linux-gnu` | Secondary | `pthread_create` | System V ABI; DWARF debug |
| `x86_64-apple-macosx` | Secondary | `pthread_create` | macOS ABI |
| `x86_64-pc-windows-msvc` | Secondary | `CreateThread` | Win64 ABI |
| `aarch64-apple-macosx` | Secondary | `pthread_create` | Apple Silicon |
| `aarch64-unknown-linux-gnu` | Secondary | `pthread_create` | Linux ARM64 |
| `riscv64-unknown-linux-gnu` | Secondary | `pthread_create` | LP64D ABI |
| `nvptx64-nvidia-cuda` | Experimental | CUDA thread model | No `task.spawn`; `own` dialect only |
| `amdgcn-amd-amdhsa` | Experimental | HSA thread model | No `task.spawn`; `own` dialect only |

**Tier definitions:**

- **Primary** — fully supported, tested in CI, all features available
- **Secondary** — supported, tested on release, `task`/`chan` dialects available via pthreads
- **Experimental** — builds and links; concurrency dialect unsupported on GPU memory model

## Required Wasm Proposals (Primary Target)

| Proposal | Why required |
|---|---|
| Threads + shared memory | `task.spawn` via `wasi_thread_start`; shared linear memory between threads |
| Atomics | `mutex.lock`, `chan.send` tail update (`i32.atomic.rmw.add`), `i32.atomic.wait` |
| Bulk memory | `memory.copy` for `own.move` of aggregates; `memory.fill` for zero-init |
| Tail calls | Zero-cost recursive ownership patterns; continuation-passing in async lowering |
| Exception handling | Wasm EH proposal — required for deterministic drops on panic (RFC-0009) |

Minimum Wasm runtime versions:

- Wasmtime ≥ 14.0
- WasmEdge ≥ 0.13
- Node.js ≥ 21 (V8 Wasm EH stable)
- Browser: Chrome ≥ 119, Firefox ≥ 121, Safari ≥ 17.4

## LLVM Optimization Passes

Configured via `llvm::PassBuilder`. Pass selection per optimization level:

### `-O2` (default)

```
always-inline          — inline trivial functions before other passes
mem2reg                — alloca → SSA (prerequisite for everything)
SROA                   — scalar replacement of aggregates
early-cse              — early common subexpression elimination
inlining               — aggressive; Wasm call overhead > native
GVN                    — global value numbering
instcombine            — peephole; cleans up lowering artifacts
simplifycfg            — dead drop-flag branch elimination
loop-unroll            — bounded loops; common in numeric kernels
licm                   — loop-invariant code motion
stack-coloring         — (Wasm) reuse stack slots across disjoint lifetimes
```

### `-Oz` (size-optimized, `--opt-size`)

All `O2` passes plus:

```
global-dce             — dead global elimination
mergefunc              — merge identical functions
strip-dead-prototypes  — remove unused imports from Wasm module
```

### `-O0` (debug builds)

No optimization passes. `mem2reg` is still run to simplify DWARF variable tracking.

## Concurrency Lowering Per Target

The concurrency lowering pass (RFC-0007) switches on the target triple to select platform
thread primitives:

### `wasm32-wasi-threads`

```
task.spawn  →  wasi_thread_start(fn_ptr, closure_ptr)
                 (imported WASI function)
task.join   →  i32.atomic.wait + memory.copy result
mutex.lock  →  i32.atomic.rmw.cmpxchg + i32.atomic.wait
chan.send    →  memory.copy + i32.atomic.rmw.add + memory.atomic.notify
chan.recv    →  i32.atomic.wait + memory.copy + i32.atomic.rmw.add
```

Thread stacks are allocated from the linear memory arena (RFC-0008). Stack size is
determined statically by call-graph analysis before thread creation.

### `x86_64-*`, `aarch64-*`, `riscv64-*` (POSIX)

```
task.spawn  →  pthread_create(thread, NULL, fn_ptr, closure_ptr)
task.join   →  pthread_join(thread, &result_ptr)
mutex.lock  →  pthread_mutex_lock / pthread_mutex_unlock
chan.send    →  C11 atomic_fetch_add + futex_wait (Linux) / sem_wait (macOS)
chan.recv    →  same
```

### `x86_64-pc-windows-msvc`

```
task.spawn  →  CreateThread(NULL, stack_size, fn_ptr, closure_ptr, 0, NULL)
task.join   →  WaitForSingleObject + GetExitCodeThread
mutex.lock  →  AcquireSRWLockExclusive / ReleaseSRWLockExclusive
```

### GPU targets (`nvptx64-*`, `amdgcn-*`)

`task.spawn`, `task.join`, `chan.make`, `chan.send`, `chan.recv`, `mutex.lock` are all
**compile errors** on GPU targets. The `own` dialect and all memory lifecycle ops are fully
supported. GPU-specific parallelism must be expressed using intrinsics directly.

## ABI Notes

### Wasm32 ABI (`WebAssemblyABIInfo`)

- No register arguments — all arguments are on the Wasm value stack
- Multi-value returns supported (Wasm MVP multi-value proposal)
- Structs passed by pointer (pointer into linear memory)
- Alignment: all types aligned to their natural alignment; channel headers to 8 bytes

### Native ABIs

Follow the platform ABI for the target triple exactly as Clang does. The ownership model
does not change the calling convention — `own<T>` lowers to the same calling convention
as `T*` (pointer). The ABI difference is only in who is responsible for freeing the memory
(enforced at compile time, invisible at the machine code level).

## Target Selection CLI

```sh
asc build main.ts                                    # default: wasm32-wasi-threads
asc build main.ts --target wasm32-unknown-unknown    # single-threaded Wasm
asc build main.ts --target x86_64-unknown-linux-gnu  # native Linux x86-64
asc build main.ts --target aarch64-apple-macosx      # Apple Silicon
asc build main.ts --opt-size                         # Oz for current target
```
