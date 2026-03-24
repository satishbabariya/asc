# Decision — No Binaryen

| Field | Value |
|---|---|
| Date | 2025 |
| Status | Accepted |
| Deciders | Core compiler team |
| Related RFC | RFC-0001, RFC-0004 |

## Context

The existing AssemblyScript compiler uses Binaryen as its Wasm backend. Binaryen is a
Wasm-to-Wasm optimizer and code generator that takes an internal IR and emits `.wasm`.

The question was whether to keep Binaryen as a post-processing step after LLVM IR emission,
or remove it entirely and use the LLVM Wasm backend directly.

## Decision

**Remove Binaryen. Use the LLVM Wasm backend (`llvm/lib/Target/WebAssembly/`) directly.**

## Reasons

1. **Redundant optimization layer.** LLVM already runs a full optimization pipeline
   (mem2reg, SROA, inlining, GVN, instcombine) before emitting Wasm. Running Binaryen's
   `wasm-opt` after LLVM adds ~15-30% build time for marginal (~5-10%) additional size
   reduction on already-optimized code.

2. **Pipeline complexity.** Adding Binaryen as a post-LLVM stage introduces a second IR
   (Binaryen IR), a second pass pipeline, and a second binary dependency. This doubles the
   surface area for bugs in the Wasm emission path.

3. **Wasm proposals.** The LLVM Wasm backend has first-class support for the Wasm proposals
   this compiler requires (Threads, Atomics, Bulk Memory, Tail Calls, Exception Handling).
   Binaryen's support for these proposals lags behind and has historically introduced
   correctness bugs when transforming code that uses Wasm EH or shared memory atomics.

4. **DWARF debug info.** The LLVM Wasm backend emits DWARF-in-Wasm sections correctly.
   Running `wasm-opt` after LLVM historically strips or corrupts DWARF sections, making
   source-level debugging impossible in size-optimized builds.

5. **Single-target fallacy.** A major reason to add Binaryen was that the previous compiler
   had no native backend — Binaryen's size passes were the only optimization available. With
   the LLVM backend, the same LLVM pass pipeline optimizes for all targets, and size
   optimization is handled by LLVM's `Oz` level.

## Consequences

- Build pipeline is simpler: LLVM IR → LLVM Wasm backend → `.wasm`. One tool, one IR.
- Binaryen is not a dependency of the compiler. Users who want additional `wasm-opt` passes
  can run it manually as a post-build step.
- Size output may be marginally larger (~5%) than a Binaryen-post-processed build. This is
  accepted as the trade-off for correctness, debuggability, and pipeline simplicity.
