# asc

AssemblyScript compiler built on LLVM, with MLIR as the HIR layer and a
Rust-inspired ownership model. No garbage collector. All LLVM targets supported.

> **Status:** Implementation in progress — compiler builds and runs end-to-end.  
> RFCs are the source of truth for all design decisions.

## Building

### Prerequisites

| Dependency | Version | Install |
|---|---|---|
| CMake | ≥ 3.20 | `brew install cmake` |
| LLVM + MLIR | 18.x | `brew install llvm@18` |
| Clang | 18.x | bundled with `llvm@18` |
| Ninja (optional) | any | `brew install ninja` |

> **Apple Silicon note:** Homebrew installs LLVM as native `arm64`. If your
> terminal is running under Rosetta (`uname -m` prints `x86_64`), prefix every
> `cmake` and build command with `arch -arm64`.

### Configure

```bash
mkdir build && cd build

cmake .. \
  -DLLVM_DIR=/opt/homebrew/opt/llvm@18/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm@18/lib/cmake/mlir \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@18/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@18/bin/clang++ \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_BUILD_TYPE=Debug
```

Drop `-DCMAKE_OSX_ARCHITECTURES=arm64` on Linux or when running in a native
arm64 shell. For a release build, change `Debug` to `Release`.

### Build

```bash
cmake --build . --parallel 8
```

On Apple Silicon under Rosetta:

```bash
arch -arm64 cmake --build . --parallel 8
```

The `asc` binary is written to `build/tools/asc/asc`.

### Verify

```bash
./tools/asc/asc --help
```

Expected output:

```
Usage: asc <command> [options] <file>

Commands:
  build   Compile to output (default)
  check   Frontend + borrow checker only
  fmt     Format source
  doc     Extract documentation
  lsp     Start LSP server
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

---

## Quick Links

| What | Where |
|---|---|
| Project vision and RFC index | [`rfcs/RFC-0001-project-overview.md`](rfcs/RFC-0001-project-overview.md) |
| Surface syntax | [`rfcs/RFC-0002-surface-syntax.md`](rfcs/RFC-0002-surface-syntax.md) |
| Compiler pipeline | [`rfcs/RFC-0003-compiler-pipeline.md`](rfcs/RFC-0003-compiler-pipeline.md) |
| Target support (Wasm + native) | [`rfcs/RFC-0004-target-support.md`](rfcs/RFC-0004-target-support.md) |
| Ownership + borrow MLIR dialect | [`rfcs/RFC-0005-ownership-borrow-model.md`](rfcs/RFC-0005-ownership-borrow-model.md) |
| Borrow checker (5-pass analysis) | [`rfcs/RFC-0006-borrow-checker.md`](rfcs/RFC-0006-borrow-checker.md) |
| Concurrency model (task + chan) | [`rfcs/RFC-0007-concurrency-model.md`](rfcs/RFC-0007-concurrency-model.md) |
| Memory model (no GC) | [`rfcs/RFC-0008-memory-model.md`](rfcs/RFC-0008-memory-model.md) |
| Panic and unwind | [`rfcs/RFC-0009-panic-and-unwind.md`](rfcs/RFC-0009-panic-and-unwind.md) |
| Toolchain and DX | [`rfcs/RFC-0010-toolchain-and-dx.md`](rfcs/RFC-0010-toolchain-and-dx.md) |
| Design decisions | [`rfcs/decisions/`](rfcs/decisions/) |

## Architecture in One Paragraph

The compiler is a hand-written frontend (Lexer → Parser → AST → Sema, Clang-style) that
emits into a custom MLIR dialect stack (`own` + `task` dialects, Flang-FIR-style). A
five-pass borrow checker runs on the HIR, followed by a drop insertion transform that
places deterministic destructors at every scope exit. The verified HIR is lowered to LLVM
IR through ownership and concurrency lowering passes, optimized by the LLVM pass pipeline,
and emitted by the LLVM `TargetMachine`. The primary target is `wasm32-wasi-threads`;
every other LLVM target works by changing the triple.

## Key Design Decisions

| Decision | Choice | RFC |
|---|---|---|
| Backend | LLVM IR → LLVM Wasm backend (no Binaryen) | RFC-0004, [decisions/001](rfcs/decisions/001-no-binaryen.md) |
| Memory management | Compile-time ownership (no GC) | RFC-0005, [decisions/002](rfcs/decisions/002-no-gc.md) |
| HIR | MLIR custom dialects (not custom IR) | RFC-0003, [decisions/003](rfcs/decisions/003-004-mlir-hir-and-ts-syntax.md) |
| Surface syntax | TypeScript-compatible + hybrid annotations | RFC-0002, [decisions/004](rfcs/decisions/003-004-mlir-hir-and-ts-syntax.md) |
| Concurrency | Ownership-based Send/Sync, channels, no shared mutable state | RFC-0007 |
| Panic | Wasm EH proposal, deterministic drops on unwind | RFC-0009 |

## RFC Process

RFCs in this repository follow a lightweight process:

1. Open a PR adding a new `rfcs/RFC-NNNN-title.md` file using the template below
2. Discussion happens in the PR
3. Merge = **Accepted**; close without merge = **Rejected** (add a note explaining why)
4. Significant changes to an accepted RFC require a new RFC that supersedes it

### RFC Template

```markdown
# RFC-NNNN — Title

| Field | Value |
|---|---|
| Status | Draft |
| Authors | ... |
| Created | YYYY-MM-DD |
| Depends on | RFC-XXXX |
| Supersedes | None |

## Summary
## Motivation
## Design
## Alternatives Considered
## Unresolved Questions
```

## Working with Claude Code

This repository is designed to be edited with [Claude Code](https://claude.ai/code). 
Suggested prompts to get started:

```
# Review all RFCs for consistency
"Read all files in rfcs/ and check for any contradictions between RFCs"

# Start implementing a stage
"Read RFC-0003 and RFC-0005 and scaffold the C++ library structure for the HIR module"

# Deepen a specific RFC
"Read RFC-0006 and expand the region inference algorithm with pseudocode"

# Add a new RFC
"Based on RFC-0005 and RFC-0007, write RFC-0011 covering weak references and cycles"
```

## Repository Layout

```
asc/
├── rfcs/                          # Design documents (20 RFCs)
│   ├── RFC-0001-*.md … RFC-0020-*.md
│   └── decisions/                 # Recorded design decisions with rationale
├── include/asc/                   # C++ headers (33 files)
│   ├── Basic/                     # SourceManager, Diagnostics, TokenKinds
│   ├── Lex/                       # Lexer, Token
│   ├── Parse/                     # Parser
│   ├── AST/                       # Decl/Stmt/Expr nodes, ASTContext, Type
│   ├── Sema/                      # Semantic analysis
│   ├── HIR/                       # MLIR own + task dialect definitions
│   ├── Analysis/                  # Borrow checker passes (7 passes)
│   ├── CodeGen/                   # LLVM IR lowering passes
│   └── Driver/                    # CLI driver
├── lib/                           # C++ implementation (33 files)
│   ├── Basic/ Lex/ Parse/ AST/ Sema/ HIR/ Analysis/ CodeGen/ Driver/
│   └── Runtime/                   # C runtime (vec, string, hashmap, channel,
│                                  #   sync, atomics, clock, random, WASI I/O)
├── std/                           # Standard library (.ts modules, 67 files)
│   ├── core/ collections/ mem/ sync/ thread/ async/ io/ fs/
│   ├── json/ encoding/ crypto/ path/ config/
│   └── prelude.ts
├── tools/asc/                     # CLI entry point (main.cpp)
├── test/
│   ├── e2e/                       # End-to-end tests (96 .ts programs)
│   ├── integration/               # Integration tests (10)
│   ├── Lex/ Parse/ Sema/          # Stage-level lit tests
│   └── lit.cfg.py
├── unittests/                     # GoogleTest unit tests
└── build/                         # CMake build output (git-ignored)
```

## LLVM / MLIR References

These files in the LLVM monorepo are the primary implementation references:

| Topic | Path |
|---|---|
| Lexer | `clang/lib/Lex/Lexer.cpp` |
| Recursive descent parser | `clang/lib/Parse/ParseDecl.cpp` |
| AST design | `clang/include/clang/AST/Stmt.h` |
| Sema | `clang/lib/Sema/SemaDecl.cpp` |
| LLVM IR codegen | `clang/lib/CodeGen/CGExpr.cpp`, `CGDecl.cpp` |
| Wasm ABI | `clang/lib/CodeGen/TargetInfo.cpp` → `WebAssemblyABIInfo` |
| Wasm EH | `clang/lib/CodeGen/CGException.cpp` |
| MLIR dialect ops | `flang/lib/Optimizer/Dialect/FIROps.cpp` |
| MLIR HIR builder | `flang/lib/Lower/` |
| LLVM pass pipeline | `llvm/lib/Passes/PassBuilder.cpp` |
| Wasm backend | `llvm/lib/Target/WebAssembly/` |
| Stack coloring | `llvm/lib/CodeGen/StackColoring.cpp` |
