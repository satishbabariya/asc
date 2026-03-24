# RFC-0006 — Borrow Checker

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0005 |
| Algorithm reference | Rust NLL RFC 2094; Polonius datalog engine (design inspiration) |
| MLIR reference | `mlir/include/mlir/Analysis/DataFlow/` (dataflow framework) |

## Summary

The borrow checker is a pipeline of five MLIR analysis passes that run sequentially on the
ownership HIR after construction and before any lowering. All five passes are **read-only
analyses** — they emit diagnostics but do not transform IR. If any diagnostic is emitted,
compilation halts before lowering begins.

## Pass Pipeline

```
HIR (own + task dialect, unverified)
        │
        ▼
Pass 1: Liveness analysis
        │  live-in / live-out sets per block
        ▼
Pass 2: Region inference
        │  concrete region spans for all borrows
        ▼
Pass 3: Aliasing constraint check
        │  Rule A, B, C (see below)
        ▼
Pass 4: Move validity check
        │  linearity + conditional move detection
        ▼
Pass 5: Send / Sync check
        │  task.spawn capture validation
        ▼
HIR (verified — safe to lower)
```

## Pass 1 — Liveness Analysis

Computes **live-in** and **live-out** sets per basic block for all `!own.val` SSA values.
Uses backward dataflow over the CFG.

**Formal definition:**

```
live-out(B) = ⋃ live-in(S)  for all successors S of B
live-in(B)  = use(B) ∪ (live-out(B) − def(B))
```

where:
- `use(B)` = set of `!own.val` SSA values used in B before being defined in B
- `def(B)` = set of `!own.val` SSA values defined in B (by consuming ops)

A value `V` is **live** at a program point `p` if there exists a path from `p` to a
consuming use of `V` that does not pass through another consuming use of `V`.

**Complexity:** O(N × B) where N = number of `!own.val` defs, B = number of basic blocks.

**Output:** A `LivenessInfo` object attached to each function, queried by subsequent passes.

## Pass 2 — Region Inference

Assigns a concrete **region** (a span of program points) to every `borrow.ref` and
`borrow.mut` op. Uses the NLL algorithm from Rust RFC 2094.

**Algorithm:**

1. For each borrow op, compute the initial region: the minimal span from the borrow op
   to its last use (using liveness from Pass 1).
2. Propagate outlives constraints:
   - If borrow `R1` is returned from a block that is in region `R2`, then `R1 ⊇ R2`
   - If borrow `R1` flows into a phi node that feeds region `R2`, then `R1 ⊇ R2`
3. Solve constraints via union-find over block ranges until fixed point.

**Constraint types generated:**

| Constraint | Meaning |
|---|---|
| `R ⊆ live(V)` | Borrow region must not outlive the source `!own.val` |
| `R ∩ drop(V) = ∅` | Borrow region must not cross a block where source is moved/dropped |
| `R_mut ∩ R_any = ∅` | Mutable borrow region must not overlap any other borrow on same source |

**Output:** A `RegionMap` mapping each borrow op to its solved region span.

## Pass 3 — Aliasing Constraint Check

Verifies three aliasing rules at every program point using the regions from Pass 2.

### Rule A — At most one mutable borrow at any point

For any `!own.val V`, at any program point `p`:

```
|{ R : R is borrow.mut region of V, p ∈ R }| ≤ 1
```

### Rule B — Mutable borrow excludes all shared borrows

For any `!own.val V`, at any program point `p`:

```
(∃ R_mut : p ∈ R_mut, R_mut is borrow.mut of V)
    ⟹ (∀ R_ref : R_ref is borrow.ref of V, p ∉ R_ref)
```

### Rule C — No drop while any borrow is live

For any `own.drop(V)` at point `p`:

```
∀ R : R is borrow region of V → p ∉ R
```

**Violation diagnostic format:**

```
error: cannot borrow `<name>` as mutable because it is also borrowed as shared
  --> src/file.ts:<line>:<col>
   |
<line> |   <source excerpt — shared borrow>
   |   ^^^^ shared borrow of `<name>` occurs here
<line> |   <source excerpt — mutable borrow>
   |   ^^^^ mutable borrow occurs here
   |
note: shared borrow is active until line <line>
```

## Pass 4 — Move Validity Check

Verifies that every `!own.val` SSA value is consumed exactly once, and that no consuming
use occurs after the value has already been consumed.

### Simple use-after-move

Detected directly from the SSA use-def chain: if an `!own.val` has more than one
consuming use, this is a use-after-move error.

```
error: use of moved value `<name>`
  --> src/file.ts:<line>:<col>
   |
<line> |   process(data);   // data moved here
   |            ^^^^ value moved here
<line> |   log(data);       // use after move
   |       ^^^^ value used after move
```

### Conditional move detection

If a value `V` is moved in one branch of a conditional but not the other, the compiler
cannot statically guarantee it is alive at the join point. This is a **warning** (not an
error) and triggers drop flag insertion (RFC-0008):

```
warning: value `<name>` may be moved in some branches
  --> src/file.ts:<line>:<col>
note: drop flag inserted at join point <line>:<col>
```

The borrow checker marks these values in the `LivenessInfo` object so the drop insertion
pass (RFC-0008) can emit conditional `own.drop` ops.

## Pass 5 — Send / Sync Check

Runs as a **verifier on the `task.spawn` op** rather than a separate pass. Fires whenever a
`task.spawn` op is constructed in the IR.

For each captured `!own.val<T, send, sync>` operand of `task.spawn`:

- If `send = false` → **compile error** identifying the non-Send type and spawn location

```
error: captured value `<name>` cannot be sent between threads safely
  --> src/file.ts:<line>:<col>
   |
<line> |   task.spawn(() => { use(data); });
   |                         ^^^^ `Buffer` is not `Send`
   |
help: annotate `Buffer` with `@send` if it is safe to move across threads
```

`!borrow<T>` (shared ref) is **not** `Send` unless `T` is `@sync`.
`!borrow.mut<T>` (exclusive ref) is **never** `Send`.

## Error Message Standards

All borrow checker diagnostics follow Clang's structured diagnostic format:

1. **Primary message** — one sentence, names the construct
2. **Source excerpt** — file, line, column, caret
3. **One or more notes** — point to related locations
4. **Optional suggestion** — concrete fix-it when the fix is unambiguous

Diagnostics are emitted via the MLIR `DiagnosticEngine`, which supports both human-readable
and JSON (`--error-format json`) output for LSP integration (RFC-0010).
