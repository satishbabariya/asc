# RFC-0008 — Memory Model

| Field | Value |
|---|---|
| Status | Accepted |
| Depends on | RFC-0005, RFC-0006 |
| LLVM reference | `llvm/lib/CodeGen/StackColoring.cpp`; `llvm/lib/Analysis/EscapeAnalysis.cpp` |

## Summary

This RFC defines how memory is managed at runtime — entirely without a garbage collector.
All allocation is either stack-based (`alloca`), explicit arena, or static. The drop
insertion pass places `own.drop` calls at every scope exit. Drop flags handle conditional
moves. The compiler guarantees at the HIR verifier level that no GC safepoints, write
barriers, or GC roots are ever emitted.

## Linear Memory Layout (Wasm Primary Target)

```
Address range        Region
───────────────      ──────────────────────────────────────────────────
0x0000_0000          Null guard — any load/store here is a bug
0x0000_0001          Static data segment start
  ...                  - Global variables
  ...                  - String literals (UTF-8, null-terminated)
  ...                  - Vtables and type descriptors
  ...                  - Channel headers and slot arrays (chan.make)
[static_end]         Thread stack arena start
  ...                  - Thread stacks (statically sized per task)
  ...                  - Closure structs for task.spawn
[arena_end]          Main thread stack base (grows down)
  ...
[stack_top]          Wasm linear memory top (__memory_base)
```

All addresses are computed at link time by the linker. No heap growth is required — the
ownership model has no unbounded heap. The only dynamic allocation at runtime is:

1. Channel slot arrays — allocated once at `chan.make`, freed when both `tx` and `rx` drop
2. Thread stacks — allocated from the arena at `task.spawn`, freed at `task.join`
3. Explicit `@heap` values — `malloc`/`free` directly

## Stack Allocation

All local `!own.val` values are stack-allocated by default. The ownership lowering pass
emits `llvm.alloca sizeof(T) align alignof(T)` for each `own.alloc`. LLVM then applies:

- **`mem2reg`** — promotes scalar allocas to SSA registers (eliminates most allocas)
- **SROA** — decomposes aggregate allocas into scalar components
- **Stack coloring** — reuses alloca slots for values with disjoint lifetimes

After these passes, the remaining allocas in the output are only those the compiler cannot
prove are register-promotable — typically large structs or values whose address is taken.

## Heap Allocation

Heap allocation is used only in two cases:

1. **Escape analysis indicates a value outlives its declaring function** — LLVM's escape
   analysis (run after ownership lowering) detects this and the compiler replaces the
   `llvm.alloca` with a `malloc`/`free` pair.
2. **Explicit `@heap` attribute** — the user forces heap allocation for a specific binding.

The compiler does **not** use a GC heap, reference-counted heap, or any allocator beyond
direct `malloc`/`free`. There is no allocator abstraction layer.

## Thread Stack Arena

The thread stack arena is a statically-sized region of linear memory reserved at link time.
Its size is the sum of all `static_stack_size` values computed by the stack analysis pass
(RFC-0007) across all `task.spawn` call sites in the module, multiplied by the maximum
concurrent thread count (a compile-time parameter, default 8, configurable via
`--max-threads N`).

Arena operations:

```typescript
// Bump allocator — O(1) alloc, O(1) free (LIFO only)
function arena_alloc(size: u32): u32  // returns pointer
function arena_free(ptr: u32): void   // LIFO: must free in reverse order of alloc
```

Arena corruption is detected by a canary value at the end of each thread stack. If a
stack overflows into the canary, the runtime traps immediately.

## Drop Insertion Pass

The drop insertion pass is a **transform pass** that runs on verified HIR after all borrow
checker passes and before concurrency lowering.

### Algorithm

For each function in the module:

1. Read the `LivenessInfo` computed by Pass 1 of the borrow checker
2. For each **block exit** (return, unconditional branch, conditional branch, unwind edge):
   a. Compute the set of `!own.val` values that are live-in to the block but not live-out
      through this specific exit edge
   b. Insert `own.drop` ops for those values immediately before the exit instruction
   c. Order: **reverse declaration order** (LIFO) — ensures a borrow of a value is dropped
      before the owned value itself
3. For values marked **conditional-move** by Pass 4 of the borrow checker (RFC-0006):
   a. Allocate an `i1` drop flag alongside the value's `own.alloc`
   b. Set the flag to `1` at the consuming op (`own.move`, `chan.send`, etc.)
   c. At every potential drop site, emit: `if drop_flag { own.drop(V) }`

### Drop Ordering Rules

Drops at a scope exit are inserted in reverse declaration order:

```typescript
function example(): void {
  const a = new Foo();  // declared first
  const b = new Bar();  // declared second
  // at scope exit: drop b first, then a
  // → own.drop(b); own.drop(a);
}
```

If `b` borrows from `a`, this ordering ensures `b`'s destructor runs before `a` is freed —
identical to C++ RAII and Rust's drop order.

### Drop Flag Example

```typescript
function conditional(flag: bool, data: own<Buf>): own<Buf> | null {
  if (flag) {
    return process(data);  // data moved here (drop_flag_data = 1)
  }
  // join point: data may or may not have been moved
  // emitted: if (!drop_flag_data) { own.drop(data); }
  return null;
}
```

After lowering, LLVM's `simplifycfg` eliminates the conditional drop if the flag's value
is statically determinable. Genuinely dynamic cases retain the check.

## No-GC Guarantee

The compiler enforces a no-GC guarantee at the HIR verifier level:

- No `llvm.gcroot`, `llvm.gcwrite`, `llvm.gcread` intrinsics are emitted
- No safepoint polls are inserted
- No write barrier stubs are called
- Every pointer in the LLVM IR is derived from one of: `llvm.alloca`, `malloc` call,
  static data address, `borrow.ref`/`borrow.mut` of an `!own.val`

This guarantee is checked by a verifier pass that runs on the final LLVM IR before
`TargetMachine::emitToStream`. If any GC intrinsic is found, the compiler emits an
internal error — this indicates a bug in the lowering passes.

## Memory Safety Properties

The ownership model and drop insertion pass together guarantee:

| Property | Mechanism |
|---|---|
| No use-after-free | Borrow checker Pass 4: no use of `!own.val` after consuming op |
| No double-free | Linearity: every `!own.val` has exactly one `own.drop` |
| No memory leak | Drop insertion: every live `!own.val` gets a drop on every exit path |
| No dangling pointer | Borrow checker Pass 3, Rule C: no drop while borrow is live |
| No data race | `task.spawn` requires `Send`; no shared mutable state without `mutex` |
| No null dereference | `!own.val<T> \| null` tracked separately; null check required before use |
