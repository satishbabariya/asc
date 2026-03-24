# RFC-0003 — Compiler Pipeline

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0001, RFC-0002 |
| Clang reference | `clang/lib/Lex`, `clang/lib/Parse`, `clang/lib/Sema`, `clang/lib/CodeGen` |
| Flang reference | `flang/lib/Lower`, `flang/lib/Optimizer` |

## Summary

This RFC defines the complete compiler pipeline from source text to LLVM IR. The pipeline
has six stages: Lexer, Parser, AST construction, Semantic analysis (Sema), HIR construction
(MLIR dialect emission), and LLVM IR codegen. Each stage is modelled after Clang or Flang
equivalents as noted.

## Pipeline Overview

```
Source text (.ts)
      │
      ▼
┌─────────────┐
│    Lexer    │  Token stream + source locations
└─────────────┘  ref: clang/lib/Lex/Lexer.cpp
      │
      ▼
┌─────────────┐
│   Parser    │  Recursive descent, no backtracking
└─────────────┘  ref: clang/lib/Parse/
      │
      ▼
┌─────────────┐
│     AST     │  Decl / Stmt / Expr hierarchies
└─────────────┘  ref: clang/include/clang/AST/
      │
      ▼
┌─────────────┐
│    Sema     │  Name resolution, type checking, ownership inference
└─────────────┘  ref: clang/lib/Sema/ + flang/lib/Evaluate/
      │
      ▼
┌─────────────────────────────┐
│  HIR (MLIR dialect stack)   │  own + task dialect emission
│  ├─ Borrow checker (5 passes)│  RFC-0006
│  └─ Drop insertion pass      │  RFC-0008
└─────────────────────────────┘  ref: flang/lib/Lower/
      │
      ▼
┌─────────────────────────────┐
│  Ownership lowering pass    │  own.alloc → alloca/malloc
│  Concurrency lowering pass  │  task.spawn → thread primitive
│  MLIR → LLVM conversion     │  standard mlir LLVMConversionTarget
└─────────────────────────────┘
      │
      ▼
┌─────────────────────────────┐
│  LLVM PassManager           │  mem2reg, SROA, inlining, GVN, etc.
└─────────────────────────────┘  ref: llvm/lib/Passes/PassBuilder.cpp
      │
      ▼
┌─────────────────────────────┐
│  LLVM TargetMachine         │  wasm32-wasi-threads or native triple
└─────────────────────────────┘
      │
      ▼
  .wasm / .o / .ll / .mlir
```

## Stage 1 — Lexer

The lexer is a hand-written, non-backtracking tokenizer modelled directly on
`clang/lib/Lex/Lexer.cpp`. It produces a flat token stream with a `SourceLocation` attached
to every token. There is no preprocessor stage — AssemblyScript has no macro system.

Key design points:

- Unicode identifiers per the TypeScript specification (UTF-16 internally, re-encoded to
  UTF-8 for string literals)
- Raw string literals and template literals supported
- All whitespace and comment tokens discarded immediately (not stored in the token stream)
- Source location is a (file-id, offset) pair; file-id indexes into a `SourceManager`
  (identical to Clang's `SourceManager`)

## Stage 2 — Parser

A recursive descent parser modelled on `clang/lib/Parse/`. It consumes the token stream and
calls into the AST builder. The parser never calls into Sema.

Error recovery strategy (Clang-style):

1. On unexpected token, emit a diagnostic
2. Skip forward to the next synchronisation point: `;`, `}`, `{`, or a declaration keyword
3. Continue parsing from that point
4. Multiple errors may be reported per file before giving up

The parser produces an untyped AST. All type information is attached by Sema.

## Stage 3 — AST Construction

The AST mirrors Clang's three-hierarchy design:

```
ASTContext  (bump allocator — owns all nodes)
├── Decl hierarchy
│   ├── FunctionDecl
│   ├── VarDecl        (includes ownership annotation if present)
│   ├── TypeDecl
│   └── FieldDecl
├── Stmt hierarchy
│   ├── CompoundStmt
│   ├── IfStmt
│   ├── ReturnStmt
│   └── ...
└── Expr hierarchy
    ├── CallExpr       (ownership of arguments inferred by Sema)
    ├── DeclRefExpr
    ├── BinaryExpr
    └── ...
```

`ASTContext` owns all nodes via a bump allocator. Nodes are never individually freed. The
entire AST is discarded after HIR construction completes.

## Stage 4 — Semantic Analysis (Sema)

Sema performs name resolution, type checking, overload resolution, and ownership annotation
inference. Modelled on `clang/lib/Sema/` and `flang/lib/Evaluate/` for constant folding.

### Responsibilities

| Task | Reference |
|---|---|
| Name resolution through lexical scope chains | `clang/lib/Sema/SemaDecl.cpp` |
| Infer and attach ownership types to all AST nodes | RFC-0002 inference rules |
| Resolve overloaded functions (TypeScript-compatible order) | `clang/lib/Sema/SemaOverload.cpp` |
| Fold compile-time constants | `flang/lib/Evaluate/` |
| Emit structured diagnostics with fix-it hints | `clang/lib/Sema/SemaDiagnostic.cpp` |
| Validate `@send` / `@sync` / `@copy` type attributes | RFC-0002 |
| Check `task.spawn` captures are `Send` | RFC-0007 |

Sema does **not** verify borrow correctness — that is the borrow checker's job (RFC-0006).
Sema only checks that ownership annotations are syntactically and type-level valid.

### Constant Folding

Following Flang's `Evaluate` library design, constant folding happens during Sema on a
typed expression tree. Results are stored as `ConstantExpr` nodes in the AST and are
available to the HIR builder for immediate operands.

## Stage 5 — HIR Construction (MLIR)

After Sema, an HIR builder walks the typed AST and emits MLIR operations into the `own`
and `task` dialects (RFC-0005, RFC-0007). Modelled on `flang/lib/Lower/`.

### HIR builder rules

- Each function body → an `mlir::func::FuncOp` with a region of `own`/`task` dialect ops
- Each `new T()` or local declaration → `own.alloc<T>` op
- Each function call that consumes an argument → `own.move` before the call
- Each function call that borrows an argument → `borrow.ref` or `borrow.mut` scoped to the call
- Each `return` → `own.move` of the return value; `own.drop` for all other live values
- Each `task.spawn` → `task.spawn` op with captured `own.val` operands listed explicitly
- Each `chan<T>(n)` → `chan.make` op; returns `!chan.tx<T>` and `!chan.rx<T>` SSA values

### Pass sequence on HIR

After HIR construction, passes run in this order before any lowering:

```
1. Borrow checker — Pass 1: Liveness analysis       (RFC-0006)
2. Borrow checker — Pass 2: Region inference         (RFC-0006)
3. Borrow checker — Pass 3: Aliasing constraint check (RFC-0006)
4. Borrow checker — Pass 4: Move validity check      (RFC-0006)
5. Borrow checker — Pass 5: Send/Sync check          (RFC-0006)
6. Drop insertion transform                           (RFC-0008)
7. Panic scope wrapping                              (RFC-0009)
```

If any pass in steps 1–5 emits a diagnostic, compilation halts. Steps 6–7 are transforms
that modify the HIR.

## Stage 6 — LLVM IR Codegen

The verified HIR is lowered to LLVM IR through a sequence of MLIR conversion passes:

### Ownership lowering pass

| HIR op | LLVM IR |
|---|---|
| `own.alloc<T>` (stack) | `llvm.alloca sizeof(T)` |
| `own.alloc<T>` (heap) | `llvm.call @malloc(sizeof(T))` |
| `own.drop<T>` | destructor call (if any) + `llvm.call @free(ptr)` (heap only) |
| `own.move<T>` (scalar) | direct SSA value forwarding |
| `own.move<T>` (aggregate) | `llvm.memcpy dst src sizeof(T)` |
| `borrow.ref<T>` | `llvm.getelementptr` → raw pointer |
| `borrow.mut<T>` | `llvm.getelementptr` → raw mutable pointer |

### Concurrency lowering pass

Switches on target triple (see RFC-0004):

| HIR op | Wasm lowering | Native lowering |
|---|---|---|
| `task.spawn` | closure struct + `wasi_thread_start` | closure struct + `pthread_create` |
| `task.join` | `i32.atomic.wait` + `memory.copy` | `pthread_join` + `memcpy` |
| `chan.send` | `memory.copy` + `i32.atomic.rmw.add` + `memory.atomic.notify` | same ops via C11 atomics |
| `chan.recv` | `i32.atomic.wait` + `memory.copy` + `i32.atomic.rmw.add` | same ops via C11 atomics |
| `mutex.lock` | `i32.atomic.rmw.cmpxchg` + `i32.atomic.wait` | `pthread_mutex_lock` |

### Standard MLIR → LLVM conversion

After the two custom lowering passes, the standard `mlir::LLVMConversionTarget` converts all
remaining MLIR ops (arithmetic, memory, control flow) to LLVM dialect ops, which are then
exported to `llvm::Module` via `mlir::translateModuleToLLVMIR`.

## LLVM Pass Pipeline

Configured via `llvm::PassBuilder` with `OptimizationLevel::O2` (default) or `Oz`
(`--opt-size`). Key passes:

```
mem2reg       — promotes allocas to SSA; prerequisite for everything else
SROA          — scalar replacement of aggregates
inlining      — aggressive for Wasm (higher call overhead than native)
GVN           — eliminates redundant loads post-ownership-lowering
instcombine   — cleans up lowering artifacts
simplifycfg   — eliminates dead drop-flag branches
stack-coloring — Wasm: reuses stack slots across disjoint lifetimes
```

## Reference Mapping

| Stage | Primary reference |
|---|---|
| Lexer | `clang/lib/Lex/Lexer.cpp` |
| Parser | `clang/lib/Parse/ParseDecl.cpp`, `ParseExpr.cpp` |
| AST | `clang/include/clang/AST/Stmt.h`, `Decl.h`, `Expr.h` |
| Sema | `clang/lib/Sema/SemaDecl.cpp`, `SemaExpr.cpp` |
| Constant folding | `flang/lib/Evaluate/` |
| HIR builder | `flang/lib/Lower/` (FIR emission pattern) |
| LLVM IR codegen | `clang/lib/CodeGen/CGExpr.cpp`, `CGDecl.cpp`, `CGCall.cpp` |
| Wasm ABI | `clang/lib/CodeGen/TargetInfo.cpp` → `WebAssemblyABIInfo` |
| Pass pipeline | `llvm/lib/Passes/PassBuilder.cpp` |
| Wasm backend | `llvm/lib/Target/WebAssembly/` |
