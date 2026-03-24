# RFC-0001 — Project Overview

| Field | Value |
|---|---|
| Status | Accepted |
| Authors | Core compiler team |
| Created | 2025 |
| Depends on | None — foundational RFC |
| Supersedes | None |

## Summary

This RFC establishes the vision, goals, non-goals, and high-level architecture for a new
AssemblyScript compiler built on LLVM, using MLIR as the intermediate representation layer,
with Clang and Flang as primary reference implementations. The compiler targets WebAssembly
as its primary output but supports all LLVM targets natively.

## Motivation

The existing AssemblyScript compiler targets WebAssembly through a custom Binaryen-based
backend, limiting optimization opportunities and making native compilation impossible. By
building on LLVM, the compiler gains decades of optimization infrastructure, a portable IR,
and the ability to support every platform LLVM supports — including x86-64, ARM64, RISC-V,
and GPU targets — with no additional backend work.

Additionally, the existing compiler relies on a runtime garbage collector for memory
management. This RFC defines a compiler that eliminates the GC entirely through a
Rust-inspired ownership and borrow model enforced at the MLIR HIR level, resulting in
deterministic memory management with zero runtime overhead.

## Goals

- Build a production-quality AssemblyScript compiler using LLVM as the backend
- Use MLIR as the HIR layer to encode ownership, borrowing, and concurrency semantics
- Eliminate garbage collection through compile-time ownership enforcement
- Support Rust-style fearless concurrency via an ownership-based Send/Sync model
- Target WebAssembly (`wasm32-wasi-threads`) as the primary output
- Support all LLVM targets (x86-64, ARM64, RISC-V, NVPTX) as secondary targets
- Maintain TypeScript-compatible surface syntax with minimal ownership annotations
- Reference Clang (frontend, CodeGen) and Flang (MLIR/FIR dialect design) throughout

## Non-Goals

- Full TypeScript compatibility — only the subset expressible without dynamic types
- A runtime GC as a fallback — this compiler is GC-free by design
- Binaryen integration — the LLVM Wasm backend is used directly
- A JavaScript interop layer — that is a separate tooling concern
- Supporting every TypeScript library — the standard library is purpose-built

## Architecture Overview

The compiler is structured in four major layers:

```
AssemblyScript source
        │
        ▼
┌─────────────────────────────────────┐
│  Frontend                           │
│  Lexer → Parser → AST → Sema        │
│  (Clang-style)                      │
└─────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────┐
│  HIR — MLIR dialect stack           │
│  own dialect + task dialect         │
│  Borrow checker (4-pass analysis)   │
│  (Flang FIR-style)                  │
└─────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────┐
│  LLVM IR (GC-free)                  │
│  Ownership lowering                 │
│  Concurrency lowering               │
│  LLVM PassManager (O2/Oz)           │
└─────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────┐
│  LLVM TargetMachine                 │
│  wasm32-wasi-threads (primary)      │
│  x86-64, ARM64, RISC-V (secondary)  │
│  NVPTX, AMDGPU (experimental)       │
└─────────────────────────────────────┘
```

## RFC Index

| RFC | Title | Status |
|---|---|---|
| RFC-0001 | Project overview (this document) | Accepted |
| RFC-0002 | Surface syntax — TypeScript-compatible ownership annotations | Accepted |
| RFC-0003 | Compiler pipeline — frontend through LLVM IR | Accepted |
| RFC-0004 | Target support — LLVM target matrix and ABI | Accepted |
| RFC-0005 | Ownership and borrow model — MLIR dialect definitions | Accepted |
| RFC-0006 | Borrow checker — four-pass MLIR analysis | Accepted |
| RFC-0007 | Concurrency model — task and chan dialects | Accepted |
| RFC-0008 | Memory model — linear memory layout, arenas, drop insertion | Accepted |
| RFC-0009 | Panic and unwind — Wasm EH, deterministic drops | Accepted |
| RFC-0010 | Toolchain and developer experience — CLI, diagnostics, debug info | Accepted |

## Key Design Decisions

Decisions that were explicitly discussed and resolved are recorded in `rfcs/decisions/`:

- `001-no-binaryen.md` — Why Binaryen was removed in favour of the LLVM Wasm backend
- `002-no-gc.md` — Why GC was removed in favour of compile-time ownership
- `003-mlir-hir.md` — Why MLIR was chosen as the HIR layer over a custom IR
- `004-ts-syntax.md` — Why TypeScript-compatible syntax was chosen with hybrid annotations
