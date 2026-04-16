# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**asc** is an AssemblyScript compiler built on LLVM 18, using MLIR as the HIR layer, with a Rust-inspired ownership model. No garbage collector. All LLVM targets supported. Primary target: `wasm32-wasi-threads`.

**Status:** Implementation complete at ~84% RFC coverage. 250 lit tests at 100%. 75 std library files (34,200+ LOC). 30 Sema-registered traits. Builds on arm64 macOS with Homebrew LLVM 18. Wasm e2e validated on wasmtime.

## Repository Structure

```
lib/
├── Lex/           Lexer.cpp (839 LOC) — hand-written tokenizer
├── Parse/         Parser.cpp, ParseDecl/Expr/Stmt.cpp (2210 LOC) — recursive descent
├── AST/           ASTContext.cpp, Type.cpp — bump-allocated AST nodes
├── Sema/          Sema.cpp, SemaDecl/Expr/Type.cpp, Builtins.cpp (2494 LOC)
├── HIR/           HIRBuilder.cpp (4500+ LOC), OwnDialect, TaskDialect, OwnOps, OwnTypes
├── Analysis/      LivenessAnalysis, RegionInference, AliasCheck, MoveCheck, SendSyncCheck,
│                  DropInsertion, PanicScopeWrap — 7-pass borrow checker + transforms
├── CodeGen/       CodeGen.cpp, OwnershipLowering, ConcurrencyLowering, PanicLowering
├── Runtime/       runtime.c, vec_rt.c, string_rt.c, hashmap_rt.c, channel_rt.c,
│                  sync_rt.c, arc_rt.c, rc_rt.c, atomics.c, wasi_*.c (1227 LOC)
├── Driver/        Driver.cpp (700+ LOC) — CLI, pipeline orchestration, LSP, Wasm linking
include/asc/       Headers for all modules
test/              250 lit tests (e2e, integration, std, Lex, Parse, Sema)
rfcs/              20 accepted RFCs — source of truth for design
docs/superpowers/  Design specs and implementation plans
tools/asc/         main.cpp entry point
```

## Building

```bash
# Prerequisites: LLVM 18 via Homebrew
brew install llvm@18

# Configure
mkdir build && cd build
cmake .. -DLLVM_DIR=/opt/homebrew/opt/llvm@18/lib/cmake/llvm \
         -DMLIR_DIR=/opt/homebrew/opt/llvm@18/lib/cmake/mlir \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_OSX_ARCHITECTURES=arm64

# Build
cmake --build . -j$(sysctl -n hw.ncpu)

# Test
pip3 install --break-system-packages lit
cd .. && lit test/ --no-progress-bar
```

## Architecture

Six-stage pipeline modelled after Clang (frontend) and Flang (MLIR/FIR):

1. **Lexer** — hand-written tokenizer, template literals, raw strings, all TypeScript keywords
2. **Parser** — recursive descent with error recovery. Supports: if let, while let, or-patterns, labelled loops, let-else, closures, generics, traits, impl blocks
3. **AST** — 35 ExprKinds, 8 StmtKinds, 13 DeclKinds, 14 TypeKinds, 10 PatternKinds. BumpPtrAllocator via ASTContext
4. **Sema** — name resolution, type checking, ownership inference (4 rules from RFC-0002), @copy/@send/@sync validation, trait method enforcement, match exhaustiveness warning
5. **HIR (MLIR)** — `own` dialect (alloc/move/drop/copy/borrow_ref/borrow_mut) + `task` dialect (spawn/join/chan). 7-pass analysis: Liveness → RegionInference → AliasCheck → MoveCheck → SendSyncCheck → DropInsertion → PanicScopeWrap
6. **LLVM IR Codegen** — PanicLowering (setjmp/longjmp + drop glue) → OwnershipLowering (alloca/malloc/memcpy/free) → ConcurrencyLowering → Canonicalizer → SCF→CF → Func→LLVM → Arith→LLVM → CF→LLVM → translateModuleToLLVMIR → PassBuilder O2 → TargetMachine

Key MLIR types: `!own.val<T, send, sync>` (owned), `!own.borrow<T>` (shared), `!own.borrow_mut<T>` (exclusive mutable). All store inner type via custom TypeStorage.

## What's Working

### Ownership & Safety
- `own<T>`, `ref<T>`, `refmut<T>` at function signatures, inferred inside bodies
- `@heap`, `@copy`, `@send`, `@sync` declaration/type attributes
- Borrow checker: E001 (shared+mutable conflict), E002 (borrow outlives owner), E003 (move while borrowed), E004 (use after move)
- SSA alias tracing through llvm.load chains (depth 16) for accurate origin tracking
- Cross-block E002/E003 detection
- Match exhaustiveness warning (W003)
- Conditional move → warning (not error), per RFC
- Drop destructor dispatch: `__drop_TypeName` called before `free()`
- Panic drop glue: cleanup blocks run destructors on the panic path via setjmp/longjmp, with drop flag checks for conditionally moved values
- `catch_unwind(fn)` builtin for user-level panic recovery (native targets)
- `--no-panic-unwind` flag traps on panic instead of unwinding (skips scope wrapping)
- OOM in arena allocator triggers panic instead of silent NULL return

### Targets
- **Wasm**: `asc build foo.ts -o foo.wasm` auto-links runtime via wasm-ld. Validated on wasmtime (fib(10)=55)
- **ARM64**: `--target aarch64-apple-darwin` → Mach-O arm64
- **x86-64**: `--target x86_64-unknown-linux-gnu` → ELF x86-64
- **RISC-V**: `--target riscv64-unknown-linux-gnu` → ELF RISC-V
- **Windows**: `--target x86_64-pc-windows-msvc` → COFF
- All 6 opt levels (O0-O3, Os, Oz), `--opt-size` alias, Wasm features: bulk-memory, mutable-globals, sign-ext, tail-call

### Concurrency
- `task.spawn(fn)` → real `pthread_create` with wrapper function
- `task.spawn(fn, arg)` → passes argument through `void *arg`
- `chan<T>(n)` → lock-free SPSC ring buffer via `channel_rt.c`
- `Mutex::new/lock/unlock/try_lock` → atomic spin-lock
- `Semaphore` with atomic permits
- `RwLock::new/read_lock/read_unlock/write_lock/write_unlock`
- `AtomicU64` with load, store, swap, compare_exchange, fetch_add, fetch_sub, fetch_and, fetch_or, fetch_xor
- `AtomicUsize` with load, store, swap, compare_exchange, fetch_add, fetch_sub, fetch_and, fetch_or, fetch_xor

### Standard Library
- **Vec\<T\>**: new, push, pop, get, len, is_empty, clear, truncate, iter, fold, map, filter, sort, reverse, dedup, extend, with_capacity, retain (18 methods)
- **String**: new, from, push_str, len, as_ptr, clear, eq, concat, trim, char_at, split, starts_with, ends_with, contains, to_uppercase, to_lowercase, chars, lines, bytes, into_bytes (20 methods)
- **HashMap\<K,V\>**: new, insert, get, contains, remove, len, keys, values, clear, is_empty, entry, or_insert, or_insert_with, and_modify, values_mut (15 methods)
- **Box\<T\>**: new (malloc-backed)
- **Arc\<T\>**: new, clone, drop, get, strong_count (atomic refcount)
- **Rc\<T\>**: new, clone, drop, get, strong_count (non-atomic)
- **Weak\<T\>**: downgrade, upgrade, drop
- **Option\<T\>**: Some, None, unwrap, is_some, is_none, pattern matching
- **Result\<T,E\>**: Ok, Err, `?` operator desugaring
- **File**: open, close, read, seek (wired to WASI fs)

### Traits (30 registered with method signatures)
Drop, Clone, PartialEq, Eq, Iterator, Display, Debug, Send, Sync, Copy, Default, Add, Sub, Mul, Div, Neg, Index, IndexMut, PartialOrd, Ord, Hash, From, Into, AsRef, AsMut, Deref, DerefMut, IntoIterator, FromIterator, Sized

### Toolchain
- `asc build` — full compile to .wasm/.o with auto-linking
- `asc check` — frontend + borrow checker, no codegen
- `asc fmt` — token-stream formatter
- `asc doc` — Markdown documentation extraction
- `asc lsp` — LSP server with real diagnostics, hover, and completion
- `--error-format human|json|github-actions`
- `--verbose` shows per-stage timing

## Key Design Decisions

- **No Binaryen** — LLVM Wasm backend directly (decision 001)
- **No GC** — compile-time ownership only, zero gc intrinsics in output (decision 002)
- **MLIR as HIR** — custom own/task dialects, not custom IR (decision 003)
- **TypeScript-compatible syntax** — ownership only at boundaries (decision 004)
- **O0 codegen for legacy PM** — new-PM handles optimization, legacy PM at O0 to avoid LLVM 18 crash
- **Channels lowered inline in HIRBuilder** — ConcurrencyLowering only declares runtime symbols
- **PanicScopeWrap at module level** — scope ops placed before the function they wrap, PanicLowering associates with next function

## RFC Coverage

| RFC | Title | Coverage |
|-----|-------|----------|
| 0001 | Project Overview | **97%** |
| 0002 | Surface Syntax | **92%** |
| 0003 | Compiler Pipeline | **93%** |
| 0004 | Target Support | **~86%** |
| 0005 | Ownership Model | **~88%** |
| 0006 | Borrow Checker | **~83%** |
| 0007 | Concurrency | **~48%** |
| 0008 | Memory Model | **~68%** |
| 0009 | Panic/Unwind | **~65%** |
| 0010 | Toolchain/DX | **~80%** |
| 0011 | Core Traits | **~93%** |
| 0012 | Memory Module | **~87%** |
| 0013 | Collections/String | **~90%** |
| 0014 | Concurrency/IO | **~86%** |
| 0015 | Complete Syntax | **~89%** |
| 0016 | JSON | ~35% |
| 0017 | Collections Utils | **~40%** |
| 0018 | Encoding/Crypto | **~75%** |
| 0019 | Path/Config | **~72%** |
| 0020 | Async Utilities | ~55% |

**Overall weighted: ~84%**

## Known Gaps

1. **Closure captures for task.spawn** — spawned tasks can't access parent variables (no env struct)
2. **Drop flags for conditional moves** — MaybeMoved warns but no runtime flag
3. **Wasm EH** — uses setjmp/longjmp, not Wasm exception handling proposal. catch_unwind available on native targets.
4. **MPMC channels** — only SPSC ring buffer
5. **Constant folding** — uses arith.constant at HIR, no Flang-style ConstantExpr
6. **Multi-module linking** — import/export parses but no cross-module IR resolution
7. **RFC-0016 JSON** — derive(Serialize/Deserialize) requires unimplemented macro expansion
8. **RFC-0020 Async** — async/await syntax not supported in compiler (RFC-0015 §21)
9. **SHA-3** — Keccak sponge not implemented (SHA-2 family complete)
10. **AtomicPtr** — AtomicI32/U32/I64/Bool/U64/Usize implemented, AtomicPtr not yet
11. **Scoped threads** — thread::scope API not implemented

## Testing

```bash
# Run all tests
lit test/ --no-progress-bar

# Run specific test
lit test/e2e/hello_i32.ts -v

# Compile and run on Wasm
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" ./build/tools/asc/asc build foo.ts --target wasm32-wasi -o foo.wasm
wasmtime foo.wasm

# Native compilation
./build/tools/asc/asc build foo.ts --target aarch64-apple-darwin --emit obj -o foo.o
```

## LLVM/MLIR Reference Points

- Lexer: `clang/lib/Lex/Lexer.cpp`
- Parser: `clang/lib/Parse/`
- AST: `clang/include/clang/AST/`
- Sema: `clang/lib/Sema/`
- HIR builder: `flang/lib/Lower/` (FIR emission pattern)
- MLIR dialect ops: `flang/lib/Optimizer/Dialect/FIROps.cpp`
- Codegen: `clang/lib/CodeGen/CGExpr.cpp`, `CGDecl.cpp`
- Pass pipeline: `llvm/lib/Passes/PassBuilder.cpp`
