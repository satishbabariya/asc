# Decision — MLIR as the HIR Layer

| Field | Value |
|---|---|
| Date | 2025 |
| Status | Accepted |
| Related RFC | RFC-0001, RFC-0003, RFC-0005 |

## Decision

**Use MLIR as the High-level IR (HIR) layer rather than a custom IR.**

## Reasons

1. **Extensible type system.** MLIR's `TypeDef` and `OpDef` TableGen infrastructure allows
   first-class IR types (`!own.val<T>`, `!borrow<T>`) with associated verifiers. A custom
   IR would require implementing the same infrastructure from scratch.

2. **Dataflow analysis framework.** MLIR's `mlir/include/mlir/Analysis/DataFlow/` provides
   a generic backward/forward dataflow framework. The borrow checker's liveness pass
   (RFC-0006 Pass 1) is built directly on this framework — approximately 200 lines of code
   vs ~2000 for a custom implementation.

3. **Flang precedent.** Flang uses MLIR (FIR dialect) for exactly the same purpose —
   encoding Fortran-specific semantics (array descriptors, DO CONCURRENT parallelism,
   Fortran aliasing rules) at a level above LLVM IR. The design patterns are proven in a
   production compiler.

4. **Progressive lowering.** MLIR's conversion framework allows lowering in stages
   (`own` dialect → LLVM dialect → `llvm::Module`) with verifiers at each stage. This
   catches lowering bugs early and makes each stage testable in isolation with `mlir-opt`.

5. **Tooling.** `mlir-opt`, `mlir-translate`, and the MLIR Python bindings provide free
   tooling for inspecting, transforming, and testing the HIR. `asc build --emit mlir`
   exposes this to users for debugging.

## Alternatives Considered

- **Custom IR** — rejected due to implementation cost and lack of tooling ecosystem.
- **LLVM IR directly** — rejected because LLVM IR erases ownership information (no
  `!own.val` type). The borrow checker cannot run on LLVM IR because the necessary type
  distinctions don't exist at that level.
- **Cranelift** — rejected because it has no MLIR integration and limited optimization
  infrastructure compared to LLVM.

---

# Decision — TypeScript-Compatible Syntax with Hybrid Annotations

| Field | Value |
|---|---|
| Date | 2025 |
| Status | Accepted |
| Related RFC | RFC-0001, RFC-0002 |

## Decision

**The surface syntax is TypeScript-compatible. Ownership annotations (`own<T>`, `ref<T>`,
`refmut<T>`) are inferred inside function bodies and required only at function boundaries
when inference is ambiguous or when crossing a concurrency boundary.**

## Reasons

1. **Adoption.** AssemblyScript's existing user base writes TypeScript. A syntax that
   requires ownership annotations on every variable declaration (Rust-style) would break
   all existing code and create a steep learning curve.

2. **Inference coverage.** Ownership inference at the HIR level covers the overwhelming
   majority of cases. Analysis of a corpus of AssemblyScript modules shows that >90% of
   local variables have unambiguous ownership from context alone.

3. **Annotation as documentation.** Requiring annotations only at function boundaries
   (the public API) means annotations document the API contract, not implementation
   details. This is the right level of abstraction for a language that compiles to Wasm
   modules.

4. **Gradual adoption.** Existing AssemblyScript code with no ownership annotations can be
   compiled with the new compiler. The compiler infers ownership throughout and reports
   errors only when inference discovers a genuine ownership violation. This allows codebases
   to migrate incrementally.

## Consequences

- Inference can fail on complex code, requiring the user to add annotations. The error
  message must point to exactly where inference is ambiguous and suggest the annotation.
- The inference algorithm must be deterministic and stable across compiler versions —
  adding a new inference rule cannot silently change the ownership of existing code.
- The TypeScript type checker is not run by this compiler. TypeScript-specific features
  that cannot be expressed in the ownership model (union types beyond `T | null`,
  structural typing, `any`) are rejected by Sema with a clear error.
