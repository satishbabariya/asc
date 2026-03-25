# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**asc** is an AssemblyScript compiler built on LLVM, using MLIR as the HIR layer, with a Rust-inspired ownership model. No garbage collector. All LLVM targets supported. Primary target: `wasm32-wasi-threads`.

**Status:** RFC phase — no implementation code exists yet. RFCs in `rfcs/` are the source of truth for all design decisions.

## Repository Structure

- `rfcs/RFC-*.md` — Design RFCs (accepted, authoritative)
- `rfcs/decisions/` — Recorded design decisions with rationale
- `README.md` — Project overview and quick links
- Planned implementation in C++ using LLVM/MLIR libraries directly, organized in `lib/` and `include/` (Lexer, Parser, AST, Sema, HIR, CodeGen)

## Architecture

The compiler has a six-stage pipeline modelled after Clang (frontend) and Flang (MLIR/FIR):

1. **Lexer** — hand-written tokenizer (Clang-style), no preprocessor
2. **Parser** — recursive descent, no backtracking, Clang-style error recovery
3. **AST** — three-hierarchy design (Decl/Stmt/Expr) with bump allocator (ASTContext)
4. **Sema** — name resolution, type checking, ownership inference (does NOT verify borrow correctness)
5. **HIR (MLIR)** — emits into `own` dialect + `task` dialect, then runs 5-pass borrow checker + drop insertion + panic scope wrapping
6. **LLVM IR Codegen** — ownership lowering, concurrency lowering, MLIR→LLVM conversion, then LLVM PassManager

Key MLIR types: `!own.val<T>` (owned), `!borrow<T>` (shared borrow), `!borrow.mut<T>` (exclusive mutable borrow).

## Key Design Decisions

- **No Binaryen** — uses LLVM Wasm backend directly (decision 001)
- **No GC** — compile-time ownership only (decision 002)
- **MLIR as HIR** — custom dialects, not custom IR (decision 003)
- **TypeScript-compatible syntax** — ownership annotations only at boundaries (`own<T>`, `ref<T>`, `refmut<T>`); inferred inside function bodies
- **Concurrency** — ownership-based Send/Sync, channels, no shared mutable state

## RFC Map

When working on a specific area, consult these RFCs:

| Area | RFC |
|---|---|
| Syntax & grammar | RFC-0002, RFC-0015 (complete EBNF) |
| Compiler pipeline | RFC-0003 |
| Target/ABI (Wasm, native) | RFC-0004 |
| Ownership MLIR dialect | RFC-0005 |
| Borrow checker (5-pass) | RFC-0006 |
| Concurrency (task/chan) | RFC-0007 |
| Memory layout, arenas, drop | RFC-0008 |
| Panic/unwind (Wasm EH) | RFC-0009 |
| CLI, diagnostics, LSP | RFC-0010 |
| Core traits (Display, Drop, etc.) | RFC-0011 |
| std::memory module | RFC-0012 |
| Collections & String | RFC-0013, RFC-0017 |
| Concurrency & IO std | RFC-0014 |
| Encoding & Crypto | RFC-0018 |
| Path & Config | RFC-0019 |
| Async runtime | RFC-0020 |
| JSON | RFC-0016 |

## Working with RFCs

- New RFCs follow the template in README.md (frontmatter table + Summary/Motivation/Design/Alternatives/Unresolved)
- Check for contradictions between RFCs before proposing changes — several RFCs cross-reference each other heavily
- Decision records in `rfcs/decisions/` document why alternatives were rejected
- RFC-0015 supersedes RFC-0002 for syntax but RFC-0002 remains authoritative for ownership semantics

## CLI Design (from RFC-0010)

The planned `asc` CLI commands:
- `asc build <file>` — compile (outputs `.wasm` by default)
- `asc check <file>` — frontend + borrow checker only, no codegen
- `asc fmt <file>` — format source
- `asc doc <file>` — extract documentation
- `asc lsp` — start LSP server

Key flags: `--target <triple>`, `--emit <wasm|wat|llvmir|mlir|obj|asm>`, `--opt <0|1|2|3|s|z>`, `--debug`

## LLVM/MLIR Reference Points

When implementing, these upstream files are the primary references:
- Lexer: `clang/lib/Lex/Lexer.cpp`
- Parser: `clang/lib/Parse/`
- AST: `clang/include/clang/AST/`
- Sema: `clang/lib/Sema/`
- HIR builder: `flang/lib/Lower/` (FIR emission pattern)
- MLIR dialect ops: `flang/lib/Optimizer/Dialect/FIROps.cpp`
- Codegen: `clang/lib/CodeGen/CGExpr.cpp`, `CGDecl.cpp`
- Pass pipeline: `llvm/lib/Passes/PassBuilder.cpp`
