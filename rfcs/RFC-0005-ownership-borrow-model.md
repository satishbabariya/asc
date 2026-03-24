# RFC-0005 — Ownership and Borrow Model

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0001, RFC-0003 |
| MLIR reference | `mlir/include/mlir/IR/OpDefinition.h`; CIRCT handshake dialect (linearity) |
| Flang reference | `flang/lib/Optimizer/Dialect/FIROps.cpp` (verifyOp pattern) |

## Summary

This RFC defines the two core MLIR dialects: the `own` dialect for memory lifecycle
operations, and the `task` dialect for concurrency primitives. Together they encode all
ownership and concurrency semantics in a form that survives intact through the entire HIR
pipeline, allowing the borrow checker (RFC-0006) and lowering passes to operate on
first-class typed IR rather than surface annotations.

## Type System

Three new MLIR types, all defined via TableGen `TypeDef`:

### `!own.val<T, send, sync>`

An owned value of type `T`. `send` and `sync` are boolean attributes encoding
`Send`/`Sync`. Every SSA value of this type must have **exactly one consuming use**
(linearity invariant enforced by verifier).

```tablegen
def OwnValType : TypeDef<OwnDialect, "OwnVal"> {
  let parameters = (ins
    "mlir::Type":$inner,
    "bool":$isSend,
    "bool":$isSync
  );
  let mnemonic = "val";
}
```

### `!borrow<T>`

A shared borrow. Carries a region token `R` that must dominate all uses. Multiple
`!borrow<T>` from the same source may coexist. No consuming-use requirement — borrows
are freely copyable at the IR level (they lower to raw pointers).

### `!borrow.mut<T>`

An exclusive mutable borrow. Carries a region token `R`. The verifier ensures no other
borrow (shared or mutable) from the same source is live within `R`.

## `own` Dialect — Op Definitions

### `own.alloc`

```
() -> !own.val<T>
```

Allocates a value of type `T`. Allocation strategy:
- Default: stack (`llvm.alloca`) — selected when the value does not escape its function
- Heap (`llvm.call @malloc`): selected by escape analysis, or forced with `@heap`

The allocation strategy is determined during ownership lowering, not at HIR construction
time. At the HIR level, `own.alloc` is target-agnostic.

---

### `own.move`

```
(!own.val<T>) -> !own.val<T>
```

Transfers ownership from one binding to another. The source operand is **consumed** — the
SSA value may not appear in any subsequent op. The linearity verifier enforces this.

Lowering:
- Scalar types: direct SSA value forwarding (no memory operation)
- Aggregate types: `llvm.memcpy dst src sizeof(T) align alignof(T)`

---

### `own.drop`

```
(!own.val<T>) -> ()
```

Runs the destructor for `T` (if defined) and frees the allocation. Inserted automatically
by the drop insertion pass (RFC-0008) at every scope exit where an `!own.val` is live.
Destructor signature: `(refmut<T>) -> ()`.

If `T` has no destructor and is stack-allocated, `own.drop` is a no-op and is eliminated
by `simplifycfg` after lowering.

---

### `own.copy`

```
(!own.val<T>) -> (!own.val<T>, !own.val<T>)
```

Produces a deep copy of `T`. `T` must be annotated `@copy`. Lowers to `llvm.memcpy` plus
a new `own.alloc`. This op is **always explicit** — the compiler never inserts implicit
copies. Attempting to use a non-`@copy` type in a position that would require a copy is a
Sema error.

---

### `borrow.ref`

```
(!own.val<T>, region) -> !borrow<T>
```

Creates a shared borrow of an owned value, scoped to `region`. The region token is a block
argument representing the borrow's lifetime. Lowers to a raw pointer (`ptr` in LLVM IR).

The borrow checker (RFC-0006 Pass 2) verifies that the region does not outlive the source
`!own.val`.

---

### `borrow.mut`

```
(!own.val<T>, region) -> !borrow.mut<T>
```

Creates an exclusive mutable borrow, scoped to `region`. The verifier checks that no other
borrow (shared or mutable) from the same source is live within this region. Lowers to a
raw mutable pointer in LLVM IR.

## `task` Dialect — Op Definitions

### `task.spawn`

```
(!own.val<T>...) -> !task.handle
```

Spawns a concurrent task. All captured values must be `!own.val<T, send=true>`. The body
is an MLIR region. The verifier (RFC-0006 Pass 5) rejects captures with `send=false`.

Lowering (Wasm): closure struct materialization → `wasi_thread_start`
Lowering (native): closure struct → `pthread_create`

Full lowering spec: RFC-0007.

---

### `task.join`

```
(!task.handle) -> !own.val<R>
```

Waits for a task to complete and retrieves its result as a new `!own.val<R>`. The caller
takes ownership of the result.

Lowering (Wasm): `i32.atomic.wait` on completion flag + `memory.copy` result
Lowering (native): `pthread_join` + `memcpy`

---

### `chan.make`

```
(capacity: i32) -> (!chan.tx<T>, !chan.rx<T>)
```

Creates a bounded channel of capacity `N`. Allocates a channel header in shared linear
memory. Returns a split pair: `tx` (send end) and `rx` (receive end). The channel is freed
when both `tx` and `rx` are dropped (ref-count reaches zero).

---

### `chan.send`

```
(!chan.tx<T>, !own.val<T>) -> ()
```

Sends a value through the channel. **Consumes** the `!own.val<T>` — the sender loses
ownership. After `chan.send`, no `own.drop` is emitted for the sent value. Ownership has
been transferred physically into the channel slot.

Full lowering spec: RFC-0007.

---

### `chan.recv`

```
(!chan.rx<T>) -> !own.val<T>
```

Receives a value from the channel. Produces a new `!own.val<T>` — the receiver takes
ownership and is responsible for dropping or moving it.

## Linearity Enforcement

The `!own.val<T>` linearity invariant is enforced by a custom MLIR op verifier attached to
every op that produces or consumes an `!own.val`. The verifier walks the use-def chain and
checks:

1. Every `!own.val` SSA result has **exactly one use**
2. That use is a **consuming op**: `own.move`, `own.drop`, `own.copy`, `chan.send`, or a
   `return` terminator

If either check fails, a compile-time error is emitted with the source location of the
offending value and its uses.

This mechanism is identical to the CIRCT handshake dialect's token linearity enforcement.

## Verifier Summary

| Op | Verifier checks |
|---|---|
| `own.alloc` | Result type must be `!own.val<T>` for some `T` |
| `own.move` | Operand must be `!own.val<T>`; result same type; operand has no other uses |
| `own.drop` | Operand must be `!own.val<T>`; this is operand's only use |
| `own.copy` | Operand type `T` must have `@copy` attribute |
| `borrow.ref` | Region token dominates all uses; source `!own.val` still live |
| `borrow.mut` | Same as `borrow.ref` + no overlapping borrow regions on same source |
| `task.spawn` | All captured `!own.val` have `send=true` attribute |
| `chan.send` | Second operand is `!own.val<T>` matching channel type; only use |

## TableGen Dialect Registration

```tablegen
def OwnDialect : Dialect {
  let name = "own";
  let summary = "Ownership and borrow lifecycle dialect";
  let cppNamespace = "::asc::own";
  let dependentDialects = ["mlir::arith::ArithDialect",
                           "mlir::memref::MemRefDialect"];
}

def TaskDialect : Dialect {
  let name = "task";
  let summary = "Concurrency primitives dialect";
  let cppNamespace = "::asc::task";
  let dependentDialects = ["::asc::own::OwnDialect"];
}
```
